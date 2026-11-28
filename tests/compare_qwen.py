"""Run the paired Edge benchmark or one PL request for hardware phase sampling."""
import argparse
import json
import os
from pathlib import Path

from playwright.sync_api import sync_playwright

root = Path(__file__).resolve().parents[1]
question = '一盏红灯加两盏绿灯，一共有几盏灯？'
url = 'http://127.0.0.1:8089'


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
    if 'pl_op_cycles' in final:
        assert len(final['pl_op_cycles']) == len(final['pl_last_graph_cycles']) == 10
        assert sum(final['pl_op_cycles']) == final['pl_cycles']
        assert all(0 <= last <= total for last, total in
                   zip(final['pl_last_graph_cycles'], final['pl_op_cycles']))
    first = page.locator('#metrics div').filter(has_text='首字符').locator('dd').inner_text()
    return dict(question=prompt, answer=page.locator('#chat-log .assistant span').last.inner_text(),
                first_s=float(first.split()[0]), **final)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--sample-baseline', action='store_true',
                        help='send one PL request while the board sampler is listening')
    group.add_argument('--explanation', action='store_true',
                       help='test a longer generated answer on both backends')
    options = parser.parse_args()
    profile = options.sample_baseline
    prompt = '请用两句话解释为什么红绿灯能让路口更安全。' if options.explanation else question
    name = 'explanation-comparison.json' if options.explanation else 'web-comparison.json'
    target = root / 'build/qwen' / ('profile-web.json' if profile else name)
    target.parent.mkdir(parents=True, exist_ok=True)
    records = []
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
        for mode in (('pl',) if profile else ('pl', 'ps', 'pl', 'ps')):
            result = ask(page, mode, prompt)
            if options.explanation:
                assert result['timings']['predicted_n'] >= 18, result
            else:
                assert '三' in result['answer'] or '3' in result['answer'], result
            records.append(result)
            target.write_text(json.dumps(dict(board=state, runs=records), ensure_ascii=False, indent=2), encoding='utf8')
            print(mode, result['answer'], result['first_s'], result['timings'], result['pl_calls'], flush=True)
        assert len({r['timings']['prompt_n'] for r in records}) == 1, 'unequal prompt work'
        assert len({r['timings']['predicted_n'] for r in records}) == 1, 'unequal generation work'
        assert len({r['answer'] for r in records}) == 1, 'outputs differ'
        browser.close()


if __name__ == '__main__':
    main()
