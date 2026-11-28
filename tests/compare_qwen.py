"""Run a paired, hardware-backed Edge benchmark on the deployed Qwen service."""
import json
import os
from pathlib import Path

from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
target = root / 'build/qwen/web-comparison.json'
target.parent.mkdir(parents=True, exist_ok=True)
question = '一盏红灯加两盏绿灯，一共有几盏灯？'
url = 'http://127.0.0.1:8089'
records = []


def ask(page, mode, prompt):
    page.locator('#backend').select_option(mode)
    page.locator('#prompt').fill(prompt)
    with page.expect_response(lambda r: r.url == url + '/api/chat', timeout=720000) as pending:
        page.locator('#send').click()
    response = pending.value
    page.wait_for_function("!document.querySelector('#backend').disabled", timeout=900000)
    assert response.ok, (response.status, response.text())
    frames = [json.loads(line) for line in response.text().splitlines()]
    final = frames[-1]
    assert final.get('done') and final['backend'] == mode, final
    layers = final['pl_layers']
    ops = final['pl_ops']
    assert len(layers) == 24 and len(ops) == 10
    if mode == 'pl':
        assert min(layers) > 0 and min(ops) > 0 and sum(ops) == final['pl_calls']
    else:
        assert sum(layers) == sum(ops) == final['pl_calls'] == final['pl_cycles'] == 0
    first = page.locator('#metrics div').filter(has_text='首字符').locator('dd').inner_text()
    return dict(question=prompt, answer=page.locator('#chat-log .assistant span').last.inner_text(),
                first_s=float(first.split()[0]), **final)


with sync_playwright() as playwright:
    edge = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft/Edge/Application/msedge.exe'
    browser = playwright.chromium.launch(executable_path=str(edge))
    page = browser.new_page(viewport={'width': 1360, 'height': 880})
    page.goto(url + '/llm')
    page.wait_for_function("document.querySelector('#online').classList.contains('live')", timeout=600000)
    state = page.request.get(url + '/api/system').json()
    assert state['qmat'] and state['qproj'], state
    print('start', state['version'], state['boot_id'], flush=True)
    # Touch the CPU copy of the weights once. The board has warmed the PL copy at boot.
    ask(page, 'ps', '只回答数字：一加一等于几？')
    for mode in ('pl', 'ps', 'pl', 'ps'):
        result = ask(page, mode, question)
        assert '三' in result['answer'] or '3' in result['answer'], result
        records.append(result)
        target.write_text(json.dumps(dict(board=state, runs=records), ensure_ascii=False, indent=2), encoding='utf8')
        print(mode, result['answer'], result['first_s'], result['timings'], result['pl_calls'], flush=True)
    assert len({r['timings']['prompt_n'] for r in records}) == 1, 'unequal prompt work'
    assert len({r['timings']['predicted_n'] for r in records}) == 1, 'unequal generation work'
    assert len({r['answer'] for r in records}) == 1, 'outputs differ'
    browser.close()
