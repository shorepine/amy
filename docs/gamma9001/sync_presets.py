#!/usr/bin/env python3
"""Sync AMY preset numbers into the web app's sample manifest.

sounds/gamma9001/manifest.json is the source of truth for the Gamma9001 banks
baked into AMY (see amy/headers.py generate_gamma9001_headers): tr808 samples
are the baked ROM set (presets 0..18 in manifest order) and everything else
lives in drums.bin at GAMMA9001_PRESET_BASE (256) + index. This script copies
each sample's preset number, root note and 22050 Hz frame count into
docs/gamma9001/samples/manifest.json so the app can trigger the baked presets
directly instead of loading WAVs into memorypcm.

Run after regenerating either manifest.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SOUNDS = os.path.join(REPO, 'sounds', 'gamma9001', 'manifest.json')
DOCS = os.path.join(HERE, 'samples', 'manifest.json')

GAMMA9001_PRESET_BASE = 256
ROM_BANK = 'tr808'

sounds = json.load(open(SOUNDS))
by_file = {}
rom_i = bin_i = 0
for m in sounds:
    if m['bank'] == ROM_BANK:
        preset = rom_i
        rom_i += 1
    else:
        preset = GAMMA9001_PRESET_BASE + bin_i
        bin_i += 1
    by_file[m['file']] = {'preset': preset, 'root': m['root'], 'preset_frames': m['frames']}

docs = json.load(open(DOCS))
missing = []
for m in docs:
    info = by_file.get(m['file'])
    if info is None:
        missing.append(m['file'])
        continue
    m.update(info)
if missing:
    raise SystemExit(f"files missing from sounds manifest: {missing}")

json.dump(docs, open(DOCS, 'w'), indent=1)
print(f"synced presets for {len(docs)} samples ({rom_i} ROM + {bin_i} bin)")
