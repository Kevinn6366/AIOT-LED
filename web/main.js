const rows = document.querySelector('#channels');
const link = document.querySelector('#link');
const clock = document.querySelector('#clock');
const errorLine = document.querySelector('#error');
const modes = [['switch', '跟随拨杆'], ['on', '开灯'], ['off', '关灯']];
let timer;
let generation = 0;
let pending = false;

for (let i = 0; i < 4; i++) {
  const row = rows.insertRow();
  row.innerHTML = `<th scope="row">LD${i}</th><td><span class="reading">SW${i}<b>—</b></span></td>
      <td><span class="lamp">—</span></td><td><div class="group"></div></td>`;
  for (const [mode, name] of modes) {
    const button = document.createElement('button');
    button.textContent = name;
    button.disabled = true;
    button.dataset.mode = mode;
    button.setAttribute('aria-label', `LD${i} ${name}`);
    button.addEventListener('click', () => update(i, mode));
    row.querySelector('.group').append(button);
  }
}

const rgbRows = document.querySelector('#rgb-channels');
for (const lamp of [5, 6]) {
  const row = rgbRows.insertRow();
  row.innerHTML = `<th scope="row">LD${lamp}</th>
      <td><label class="rgb-color"><input type="color" aria-label="LD${lamp} 颜色" disabled><code>—</code></label></td>
      <td><span class="lamp">未知</span></td>
      <td><div class="group"><button data-on="1" disabled>开灯</button><button data-on="0" disabled>关灯</button></div></td>`;
  const picker = row.querySelector('input');
  picker.addEventListener('change', () => update(lamp, {color: picker.value}));
  row.querySelectorAll('button').forEach((button) => {
    button.setAttribute('aria-label', `LD${lamp} ${button.textContent}`);
    button.addEventListener('click', () => update(lamp, {on: button.dataset.on === '1'}));
  });
}

function render(sample) {
  link.textContent = sample ? 'PL 在线' : 'PL 离线';
  link.classList.toggle('live', Boolean(sample));
  clock.textContent = sample ? '采样 ' + new Date().toLocaleTimeString('zh-CN') : '无有效采样';
  Array.from(rows.rows).forEach((row, i) => {
    const bit = 1 << i;
    const on = sample && Boolean(sample.leds & bit);
    row.querySelector('b').textContent = sample ? Number(Boolean(sample.switches & bit)) : '—';
    const lamp = row.querySelector('.lamp');
    lamp.textContent = sample ? (on ? '亮' : '灭') : '未知';
    lamp.classList.toggle('on', Boolean(on));
    const selected = sample && (sample.remote & bit ? (on ? 'on' : 'off') : 'switch');
    row.querySelectorAll('button').forEach((button) => {
      button.disabled = !sample;
      button.setAttribute('aria-pressed', String(button.dataset.mode === selected));
    });
  });
  Array.from(rgbRows.rows).forEach((row, i) => {
    const state = sample && sample.rgb && sample.rgb[i];
    const picker = row.querySelector('input');
    picker.disabled = !state;
    if (state && picker.dataset.confirmed !== state.color) {
      picker.value = state.color;
      picker.dataset.confirmed = state.color;
    } else if (!state) {
      delete picker.dataset.confirmed;
    }
    row.querySelector('code').textContent = state ? state.color.toUpperCase() : '—';
    const lamp = row.querySelector('.lamp');
    lamp.textContent = state ? (state.on ? '开启' : '关闭') : '未知';
    lamp.classList.toggle('on', Boolean(state && state.on));
    row.querySelectorAll('button').forEach((button) => {
      button.disabled = !state;
      button.setAttribute('aria-pressed', String(Boolean(state && state.on === (button.dataset.on === '1'))));
    });
  });
}

async function update(channel, mode) {
  if (pending) return;
  clearTimeout(timer);
  const requestId = ++generation;
  const writing = channel !== undefined;
  if (writing) {
    pending = true;
    document.querySelectorAll('#channels button, #rgb-channels button, #rgb-channels input')
        .forEach((control) => { control.disabled = true; });
  }
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3500);
  try {
    const options = {cache: 'no-store', signal: controller.signal};
    let url = '/api/panel';
    if (writing) {
      const rgb = channel === 5 || channel === 6;
      url = (rgb ? '/api/rgb/' : '/api/panel/') + channel;
      Object.assign(options, {method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(rgb ? mode : {mode})});
    }
    const response = await fetch(url, options);
    if (!response.ok) throw new Error();
    const data = await response.json();
    if (requestId === generation) { render(data); errorLine.textContent = ''; }
  } catch {
    if (requestId === generation) {
      render(null);
      errorLine.textContent = writing ? '未收到控制回执，正在重新读取。' : '读取失败，正在重连。';
    }
  } finally {
    clearTimeout(timeout);
    if (requestId === generation) { pending = false; timer = setTimeout(update, 500); }
  }
}

const psButtons = document.querySelectorAll('[data-ps]');
psButtons.forEach((button) => button.addEventListener('click', async () => {
  psButtons.forEach((b) => { b.disabled = true; });
  const status = document.querySelector('#ps-result');
  try {
    const on = button.dataset.ps === '1';
    const response = await fetch('/api/led', {method: 'POST',
      headers: {'Content-Type': 'application/json'}, body: JSON.stringify({on})});
    if (!response.ok) throw new Error();
    status.textContent = on ? '开灯指令已发送' : '关灯指令已发送';
  } catch { status.textContent = '发送失败'; }
  finally { psButtons.forEach((b) => { b.disabled = false; }); }
}));
update();
