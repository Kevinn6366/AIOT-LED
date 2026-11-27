"""Prepare pinned ZyboGPT sources, trained weights and independent test vectors."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'build/zybogpt'
REV = '3410ce3d17360a794b4fe0dc24a58e94d6246149'


def setup():
    if not UPSTREAM.exists():
        subprocess.run(['git', 'clone', 'https://github.com/mpai17/ZyboGPT.git',
                        str(UPSTREAM)], check=True)
        subprocess.run(['git', '-C', str(UPSTREAM), 'checkout', '--detach', REV], check=True)
    if subprocess.check_output(['git', '-C', str(UPSTREAM), 'rev-parse', 'HEAD'],
                               text=True).strip() != REV:
        raise SystemExit('Unexpected upstream revision')
    os.chdir(UPSTREAM)
    sys.path.insert(0, str(UPSTREAM))
    path = Path('hw/src/main/scala/zybogpt/RMSNorm.scala')
    text = path.read_text(encoding='utf-8')
    for old, new in [('Reg(UInt(14 bits))', 'Reg(UInt(15 bits))'),
                     ('asUInt.resize(14 bits)', 'asUInt.resize(15 bits)')]:
        if old not in text and new not in text:
            raise SystemExit('Unsupported RMSNorm source')
        text = text.replace(old, new)
    path.write_bytes(text.encode())


def train_weights():
    import torch
    from python.train.config import ZyboGPTConfig
    from python.train.train import train
    for phase, steps, batch, lr, seed, smooth in [
            ('plain', 3000, 32, .0007, 42, 0), ('quant', 1500, 32, .0003, None, 0)]:
        if seed is not None:
            torch.manual_seed(seed)
        directory = Path('checkpoints') / phase
        previous = 'plain' if phase == 'quant' else None
        if not (directory / 'final.pt').exists():
            cfg = ZyboGPTConfig(hw_mode=bool(previous), max_steps=steps, batch_size=batch,
                                learning_rate=lr, warmup_steps=80)
            train(cfg, device='cuda', save_dir=str(directory), label_smoothing=smooth,
                  resume='checkpoints/%s/best.pt' % previous if previous else None)


def export(checkpoint):
    subprocess.run([sys.executable, '-m', 'scripts.generate_weights',
                    checkpoint, '--output', 'export'], check=True)
    from python.train.reference_inference import load_weights, reference_inference
    weights, config = load_weights('export')
    vectors = []
    for prompt, seed in [('ROMEO:', 0xdeadbeef), ('JULIET:', 9876)]:
        predictions, _ = reference_inference(list(prompt.encode()), weights, config,
                                              max_new_tokens=16, temperature=1.5, seed=seed)
        # The upstream reference returns a prediction for each prompt token as well.
        first = len(prompt) - 1
        text = bytes(predictions[first:first + 16]).decode('ascii')
        vectors.append(dict(prompt=prompt, seed=seed, text=text))
    Path('export/console-vectors.json').write_text(json.dumps(vectors), encoding='ascii')
    Path('export/golden.hex').write_text(
        ''.join('%02x\n' % ord(c) for c in vectors[0]['text']), encoding='ascii')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--train', action='store_true')
    parser.add_argument('--checkpoint', default='checkpoints/quant/best.pt')
    args = parser.parse_args()
    os.environ.setdefault('TORCH_COMPILE_DISABLE', '1')
    os.environ.setdefault('PYTHONUTF8', '1')
    setup()
    if args.train:
        train_weights()
    export(args.checkpoint)
