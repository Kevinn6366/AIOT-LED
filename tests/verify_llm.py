"""Exercise both inference paths in Edge and verify hardware-backed feedback."""
import os
from pathlib import Path

from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
base = 'http://127.0.0.1:8089'
with sync_playwright() as playwright:
    edge = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft/Edge/Application/msedge.exe'
    browser = playwright.chromium.launch(executable_path=str(edge))
    page = browser.new_page(viewport={'width': 1360, 'height': 880})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.goto(base)
    page.get_by_role('link', name='模型工作台').click()
    page.locator('#write').click()
    page.locator('#example').click()
    page.locator('#send').click()
    page.wait_for_function("document.querySelector('#phase').textContent === '生成完成'")
    assert len(page.locator('#write-log .assistant span').inner_text()) == 64
    print('PL continuation:', page.locator('#metrics').inner_text(), flush=True)
    page.locator('#chat').click()
    page.locator('#prompt').fill('中国的首都是哪里？请只回答城市名。')
    page.locator('#send').click(timeout=180000)
    page.wait_for_function("document.querySelector('#phase').textContent === '生成完成'", timeout=900000)
    answer = page.locator('#chat-log .assistant span').inner_text()
    assert '北京' in answer, answer
    assert int(page.locator('#metrics div').filter(has_text='PL 矩阵调用').locator('dd').inner_text()) > 0
    print('Qwen:', answer, page.locator('#metrics').inner_text(), flush=True)
    page.screenshot(path=str(root / 'build/qwen/llm-edge.png'))
    page.set_viewport_size({'width': 390, 'height': 844})
    assert page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')
    page.locator('#write').click()
    assert page.locator('#write-log .assistant').is_visible()
    assert not errors, errors
    browser.close()
