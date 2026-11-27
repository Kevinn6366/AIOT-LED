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
}

async function update(channel, mode) {
  if (pending) return;
  clearTimeout(timer);
  const requestId = ++generation;
  const writing = channel !== undefined;
  if (writing) {
    pending = true;
    rows.querySelectorAll('button').forEach((button) => { button.disabled = true; });
  }
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3500);
  try {
    const options = {cache: 'no-store', signal: controller.signal};
    if (writing) Object.assign(options, {method: 'POST',
      headers: {'Content-Type': 'application/json'}, body: JSON.stringify({mode})});
    const response = await fetch(writing ? '/api/panel/' + channel : '/api/panel', options);
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
