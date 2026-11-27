const rows = document.querySelector('#channels');
document.querySelector('#host-tools').hidden = !['127.0.0.1', 'localhost'].includes(location.hostname);
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

const syncButton = document.querySelector('#rgb-sync');
syncButton.addEventListener('click', () => update('sync', {
  on: syncButton.getAttribute('aria-pressed') !== 'true'
}));

function render(sample) {
  const synced = Boolean(sample && sample.rgb_sync);
  syncButton.disabled = !sample || typeof sample.rgb_sync !== 'boolean';
  syncButton.setAttribute('aria-pressed', String(synced));
  syncButton.textContent = synced ? '停止同步' : '开启同步';
  document.querySelector('#sync-status').textContent =
      sample ? (synced ? '双灯同步渐变' : '独立控制') : '等待采样';
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
    picker.disabled = !state || synced;
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
      button.disabled = !state || synced;
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
    document.querySelectorAll('#channels button, #rgb-channels button, #rgb-channels input, #rgb-sync')
        .forEach((control) => { control.disabled = true; });
  }
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3500);
  try {
    const options = {cache: 'no-store', signal: controller.signal};
    let url = '/api/panel';
    if (writing) {
      const rgb = channel === 'sync' || channel === 5 || channel === 6;
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

const restart = document.querySelector('#restart');
const restartResult = document.querySelector('#restart-result');
document.querySelector('#restart-tools').hidden = document.querySelector('#host-tools').hidden;
let bootId = '';
let restarting = '';
restart.addEventListener('click', async () => {
  if (!bootId || !window.confirm('重启开发板并从 SD 卡启动？')) return;
  restarting = bootId;
  restart.disabled = true;
  restartResult.textContent = '等待开发板重新启动…';
  try {
    const response = await fetch('/api/reboot', {method: 'POST',
      headers: {'Content-Type': 'application/json'}, body: JSON.stringify({boot_id: bootId})});
    if (!response.ok) throw new Error();
  } catch {
    restarting = '';
    restartResult.textContent = '未收到重启回执，请核对启动状态。';
  }
});

let gptReady = false;
let generating = false;
const gptButton = document.querySelector('#gpt-form button');
function setGptReady(ready) {
  const status = document.querySelector('#gpt-status');
  if (!generating && (!ready || !gptReady)) {
    status.textContent = ready ? '推理核就绪' : '推理核未上线';
  }
  gptReady = ready;
  gptButton.disabled = generating || !ready;
}

async function pollBoot() {
  const box = document.querySelector('#boot-status');
  try {
    const response = await fetch('/api/system', {cache: 'no-store'});
    if (!response.ok) throw new Error();
    const s = await response.json();
    setGptReady(Boolean(s.gpt));
    bootId = s.boot_id;
    const ready = s.stage === 'ready' && s.ps && s.pl && s.uio && s.card;
    if (restarting && bootId !== restarting && ready) {
      restarting = '';
      restartResult.textContent = '已从 SD 卡重新启动，灯控已恢复。';
    }
    restart.disabled = !ready || Boolean(restarting);
    box.textContent = `PetaLinux ${s.kernel} 已启动 · ${s.uptime} 秒 · SD ${s.card ? '已挂载' : '未挂载'} · UIO ${s.uio ? '已绑定' : '待绑定'} · 灯控 ${s.ps && s.pl ? '已运行' : '待就绪'} · ${s.version}`;
  } catch {
    setGptReady(false);
    restart.disabled = true;
    box.textContent = '未收到开发板响应，启动状态未知。';
  } finally {
    setTimeout(pollBoot, 3000);
  }
}
pollBoot();
document.querySelector('#gpt-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  const prompt = form.elements.prompt.value;
  const seed = Number(form.elements.seed.value);
  const output = document.querySelector('#gpt-output');
  const status = document.querySelector('#gpt-status');
  const button = form.querySelector('button');
  if (!/^[ -~]+$/.test(prompt)) {
    status.textContent = '请输入可打印的英文字符。';
    return;
  }
  generating = true;
  button.disabled = true;
  output.textContent = prompt;
  status.textContent = '等待开发板…';
  const start = performance.now();
  let first, cycles = 0, count = 0, complete = false;
  try {
    const response = await fetch('/api/generate', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({prompt, seed, length: Math.min(64, 128 - prompt.length)})});
    if (!response.ok) throw new Error((await response.json()).error);
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';
    for (;;) {
      const chunk = await reader.read();
      if (chunk.done) break;
      buffer += decoder.decode(chunk.value, {stream: true});
      const lines = buffer.split('\n');
      buffer = lines.pop();
      for (const line of lines) {
        const frame = JSON.parse(line);
        if (frame.error) throw new Error(frame.error);
        if (frame.done) { complete = true; continue; }
        if (!count) first = performance.now() - start;
        output.textContent += frame.char;
        cycles = frame.cycles;
        count++;
      }
    }
    if (!complete || !count || !cycles) throw new Error('未收到完整回执');
    const rate = Math.round((prompt.length + count - 1) * 50000000 / cycles);
    status.textContent = `已生成 ${count} 字符 · PL ${rate} 字符/秒（含提示词） · 首字符 ${first.toFixed(1)} ms`;
  } catch (error) {
    status.textContent = error.message || '推理请求失败';
  } finally {
    generating = false;
    button.disabled = !gptReady;
  }
});

document.querySelector('#chat-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  const button = form.querySelector('button');
  const answer = document.querySelector('#chat-answer');
  const status = document.querySelector('#chat-status');
  button.disabled = true;
  answer.textContent = '';
  status.textContent = '开发板正在读取问题…';
  const start = performance.now();
  let first, done = false;
  try {
    const result = await fetch('/api/chat', {method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({question: form.elements.question.value})});
    if (!result.ok) throw new Error((await result.json()).error);
    const reader = result.body.pipeThrough(new TextDecoderStream()).getReader();
    let buffer = '';
    while (!done) {
      const chunk = await reader.read();
      if (chunk.done) break;
      const lines = (buffer + chunk.value).split('\n');
      buffer = lines.pop();
      for (const line of lines) {
        const frame = JSON.parse(line);
        if (frame.error) throw new Error(frame.error);
        if (frame.text) {
          if (first === undefined) first = (performance.now() - start) / 1000;
          answer.textContent += frame.text;
          status.textContent = '正在回答…';
        }
        if (frame.done) {
          done = true;
          status.textContent = (frame.limited ? '达到回答长度上限' : '回答完成') +
            ` · 首字 ${(first ?? 0).toFixed(1)} 秒 · ${frame.timings.predicted_per_second.toFixed(2)} token/秒`;
        }
      }
    }
    if (!done) throw new Error('回答中断，请重新提问');
  } catch (error) {
    status.textContent = error.message || '未收到板端回答';
  } finally {
    button.disabled = false;
  }
});
