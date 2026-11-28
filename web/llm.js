'use strict';
const $ = (id) => document.getElementById(id);
const modes = {
  chat: {model: 'Qwen2.5 · 0.5B', title: '向千问提问', intro: '板端单轮中文问答，最多输入 96 字。', example: '一盏红灯加两盏绿灯，一共有几盏灯？', placeholder: '输入你的问题…'},
  write: {model: 'ZyboGPT · INT8', title: '续写一段英文台词', intro: '给出英文开头，由 PL 逐字符续写。', example: 'ROMEO:', placeholder: '输入英文开头…'}
};
let mode = 'chat', board = null, busy = false;
const drafts = {chat: '', write: ''};
const readings = {chat: {}, write: {}};
const phases = {chat: '等待输入', write: '等待输入'};
function node(tag, content) {
  const el = document.createElement(tag);
  el.textContent = content;
  return el;
}
function refresh() {
  const chat = mode === 'chat';
  const mixed = chat && board?.qmat && board?.qproj && $('backend').value === 'pl';
  $('backend-label').hidden = !chat || !board?.qmat || !board?.qproj;
  $('backend').disabled = busy;
  const ready = chat ? board?.qwen : board?.gpt;
  $('online').textContent = board ? (ready ? '开发板在线 · 推理就绪' : '开发板在线 · 等待推理核') : '开发板未连接';
  $('online').classList.toggle('live', Boolean(ready));
  $('send').disabled = busy || !ready || !$('prompt').value.trim();
  $('phase').textContent = phases[mode];
  const route = [['浏览器', location.hostname], ['板端服务', chat ? 'Bottle → llama.cpp' : 'Bottle → FIFO → gpt.elf']];
  if (chat) {
    route.push(['权重映射', 'SD → DDR · Q4_0 / Q8_0']);
    route.push(mixed ? ['24 层矩阵与输出头', 'DDR ⇄ AXI HP0 ⇄ PL'] : ['矩阵推理', 'PS · Cortex-A9 × 2']);
  } else route.push(['寄存器读写', 'UIO → AXI-Lite'], ['字符推理核', 'PL · BRAM · 50 MHz']);
  $('route').replaceChildren(...route.map(([name, info]) => {
    const li = node('li', name); li.append(node('small', info)); return li;
  }));
  const data = readings[mode];
  const fields = [['首字符', data.first], [chat ? '生成速度' : 'PL 吞吐（含输入）', data.rate]];
  if (!chat || mixed || data.calls) fields.push(['PL 周期' + (chat ? '（调用窗口）' : ''), data.cycles]);
  if (chat && data.backend) fields.push(['本次后端', data.backend], ['PL 矩阵调用', data.calls], ['已执行层', data.layers]);
  $('metrics').replaceChildren(...fields.map(([name, value]) => {
    const line = document.createElement('div'); line.append(node('dt', name), node('dd', value ?? '—')); return line;
  }));
}
function select(next) {
  drafts[mode] = $('prompt').value;
  mode = next;
  const config = modes[mode];
  for (const key of Object.keys(modes)) {
    $(key).setAttribute('aria-pressed', String(key === mode));
    $(key + '-log').hidden = key !== mode;
  }
  $('model').textContent = config.model;
  $('welcome').textContent = config.title;
  $('intro').textContent = config.intro;
  $('example').textContent = config.example + ' ↗';
  $('die').textContent = mode === 'chat' ? 'Q' : 'PL';
  $('empty').hidden = $(mode + '-log').childElementCount > 0;
  $('prompt').value = drafts[mode];
  $('prompt').placeholder = config.placeholder;
  $('seed-label').hidden = $('seed').disabled = mode === 'chat';
  $('hint').textContent = mode === 'chat' ? 'Enter 发送 · Shift+Enter 换行' : 'Enter 续写';
  refresh();
  $('stream').scrollTop = $('stream').scrollHeight;
}
for (const key of Object.keys(modes)) $(key).onclick = () => select(key);
$('example').onclick = () => { $('prompt').value = modes[mode].example; $('prompt').focus(); refresh(); };
$('prompt').oninput = refresh;
$('backend').onchange = refresh;
$('prompt').onkeydown = (event) => {
  if (event.key === 'Enter' && !event.shiftKey && !event.isComposing && event.keyCode !== 229) {
    event.preventDefault(); if (!$('send').disabled) $('composer').requestSubmit();
  }
};
function message(key, role, title, value) {
  const block = document.createElement('article');
  block.className = 'message ' + role;
  const text = node('span', value);
  block.append(node('b', title), text); $(key + '-log').append(block); return text;
}
$('composer').onsubmit = async (event) => {
  event.preventDefault();
  if (busy || $('send').disabled) return;
  const key = mode, chat = key === 'chat', prompt = $('prompt').value.trim();
  if (!chat && !/^[ -~]+$/.test(prompt)) {
    phases[key] = '续写请输入单行可打印 ASCII 字符。'; refresh(); return;
  }
  busy = true;
  $('empty').hidden = true;
  message(key, 'user', '你', prompt);
  const output = message(key, 'assistant', modes[key].model, '');
  const data = readings[key] = {};
  $('prompt').value = drafts[key] = '';
  phases[key] = '等待板端首字符…'; refresh();
  const start = performance.now();
  let count = 0, done = false, buffer = '';
  try {
    const response = await fetch(chat ? '/api/chat' : '/api/generate', {method: 'POST',
      headers: {'Content-Type': 'application/json'}, body: JSON.stringify(chat ? {question: prompt, backend: board?.qmat && board?.qproj ? $('backend').value : 'ps'} :
        {prompt, seed: Number($('seed').value), length: Math.min(64, 128 - prompt.length)})});
    if (!response.ok) throw new Error((await response.json()).error || '请求失败');
    const reader = response.body.getReader(), decoder = new TextDecoder();
    while (!done) {
      const part = await reader.read();
      if (part.done) break;
      const lines = (buffer + decoder.decode(part.value, {stream: true})).split('\n');
      buffer = lines.pop();
      const pane = $('stream'), follow = pane.scrollHeight - pane.clientHeight - pane.scrollTop < 90;
      for (const line of lines) {
        const frame = JSON.parse(line);
        if (frame.error) throw new Error(frame.error);
        if (frame.done) {
          done = true; phases[key] = frame.limited ? '已达到输出上限' : '生成完成';
          if (chat) {
            data.rate = frame.timings.predicted_per_second.toFixed(2) + ' token/s';
            data.backend = frame.backend === 'pl' ? 'PL' : 'PS';
            data.calls = frame.pl_calls;
            data.cycles = (frame.pl_cycles || 0).toLocaleString();
            data.layers = frame.pl_layers ? frame.pl_layers.filter(n => n > 0).length + ' / 24' : '—';
          }
        } else if (frame.text || frame.char) {
          if (!count) data.first = ((performance.now()-start)/1000).toFixed(2) + ' s';
          count++; output.textContent += frame.text || frame.char;
          phases[key] = '正在生成…';
          if (!chat) {
            data.cycles = frame.cycles.toLocaleString();
            data.rate = Math.round((prompt.length+count-1)*50000000/frame.cycles) + ' 字符/s';
          }
        }
      }
      if (key === mode && follow) pane.scrollTop = pane.scrollHeight;
      refresh();
    }
    if (!done) throw new Error('连接中断，未收到完成回执');
  } catch (error) {
    phases[key] = error.message;
    output.append(node('small', '　' + error.message));
  } finally {
    busy = false; refresh();
  }
};
async function poll() {
  try {
    const response = await fetch('/api/system', {cache:'no-store'});
    if (!response.ok) throw new Error();
    board = await response.json();
  } catch { board = null; }
  refresh(); setTimeout(poll, 3000);
}
select('chat'); poll();
