#!/usr/bin/env python3
"""Append AMY's `enum params` to amy/constants.py as PARAM_<NAME>=<value>.

These are the parameter ids a direct-parameter `midi_cc` mapping names
(issue #1175), e.g. amy.send(synth=1, midi_cc='74,1,100,8000,0,%d' % amy.PARAM_FILTER_FREQ).

The enum's values are computed (DUTY=AMP + NUM_COMBO_COEFS, ...) and its
comments are not kept up to date, so the numbers are not parsed out of the
header: the names are, and a throwaway C program built against amy.h
prints the values the compiler actually assigns.  The PARAM_ prefix keeps
names like FREQ, BUS or MODE from shadowing anything else in `amy.`.

Usage: gen_param_constants.py [CC] >> amy/constants.py
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def param_names(header_text):
    m = re.search(r'enum\s+params\s*\{(.*?)\};', header_text, re.S)
    if not m:
        raise RuntimeError('enum params not found in amy.h')
    body = re.sub(r'//[^\n]*', '', m.group(1))
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    names = []
    for item in body.split(','):
        item = item.strip()
        if not item:
            continue
        name = item.split('=')[0].strip()
        if not re.fullmatch(r'[A-Z_][A-Z0-9_]*', name):
            raise RuntimeError('unexpected enum params entry: %r' % item)
        names.append(name)
    return names


def main():
    cc = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('CC', 'cc')
    names = param_names((ROOT / 'src' / 'amy.h').read_text())
    src = ['#include <stdio.h>', '#include "amy.h"', 'int main(void) {']
    src += ['    printf("PARAM_%s=%%d\\n", (int)%s);' % (n, n) for n in names]
    src += ['    return 0;', '}']
    with tempfile.TemporaryDirectory() as tmp:
        c_path = os.path.join(tmp, 'params.c')
        exe = os.path.join(tmp, 'params')
        Path(c_path).write_text('\n'.join(src) + '\n')
        subprocess.run([cc, '-I', str(ROOT / 'src'), c_path, '-o', exe], check=True)
        sys.stdout.write(subprocess.run([exe], check=True, capture_output=True, text=True).stdout)


if __name__ == '__main__':
    main()
