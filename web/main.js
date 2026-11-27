const buttons = document.querySelectorAll('button');
const statusLine = document.querySelector('[role="status"]');

async function setLed(value) {
  buttons.forEach((button) => { button.disabled = true; });
  statusLine.textContent = '正在发送…';
  try {
    const response = await fetch('/api/led', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({on: value === '1'}),
    });
    const result = await response.json();
    statusLine.textContent = response.ok ?
        (value === '1' ? '开灯指令已写入管道' : '关灯指令已写入管道') :
        result.error;
  } catch {
    statusLine.textContent = '未收到服务响应，请检查连接。';
  } finally {
    buttons.forEach((button) => { button.disabled = false; });
  }
}

buttons.forEach((button) => {
  button.addEventListener('click', () => setLed(button.value));
});
