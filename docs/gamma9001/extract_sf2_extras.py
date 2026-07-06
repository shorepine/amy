#!/usr/bin/env python3
"""Fill the gaps in the Koblo TR-808 bank from AMY's own 808 SoundFont.

The Koblo Tokyo distribution shipped no 808 snare, rimshot, toms or cymbal.
sounds/HS-TR-808-Drums.sf2 (the source of AMY's baked-in ROM kit) has all of
them, so pull the missing sounds from there into samples/tr808/ and append
them to samples/manifest.json. Idempotent: already-present files are skipped.

Run from anywhere: paths are resolved relative to this script / the repo root.
"""
import json
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SF2 = os.path.join(REPO, 'sounds', 'HS-TR-808-Drums.sf2')
MANIFEST = os.path.join(HERE, 'samples', 'manifest.json')

# sf2 sample name -> (slug, display name)
WANTED = {
    '808-SNR 4-D': ('tr-808-snare-1', 'TR-808 Snare 1'),
    '808-SNR 7-D': ('tr-808-snare-2', 'TR-808 Snare 2'),
    '808-SNR 12-D': ('tr-808-snare-3', 'TR-808 Snare 3'),
    '808-RIM   -D': ('tr-808-rimshot', 'TR-808 Rimshot'),
    '808-LTOM M-D': ('tr-808-tom-lo', 'TR-808 Tom Lo'),
    '808-HTOM M-D': ('tr-808-tom-hi', 'TR-808 Tom Hi'),
    '808-CYMBAL-D': ('tr-808-cymbal', 'TR-808 Cymbal'),
}


def find_chunk(data, tag):
    i = data.find(tag)
    while i != -1:
        size = struct.unpack('<I', data[i + 4:i + 8])[0]
        if i + 8 + size <= len(data):
            return i + 8, size
        i = data.find(tag, i + 1)
    raise ValueError(f'chunk {tag} not found')


def sf2_samples(path):
    data = open(path, 'rb').read()
    shdr_off, shdr_size = find_chunk(data, b'shdr')
    smpl_off, smpl_size = find_chunk(data, b'smpl')
    out = {}
    for k in range(shdr_size // 46):
        rec = data[shdr_off + k * 46: shdr_off + (k + 1) * 46]
        name = rec[:20].rstrip(b'\x00').decode('latin1')
        start, end, ls, le, sr = struct.unpack('<IIIII', rec[20:40])
        if name and name != 'EOS':
            pcm = data[smpl_off + start * 2: smpl_off + end * 2]
            out[name] = (pcm, sr)
    return out


def main():
    manifest = json.load(open(MANIFEST))
    have = {m['file'] for m in manifest}
    samples = sf2_samples(SF2)
    added = 0
    for sfname, (slug, display) in WANTED.items():
        relpath = f'tr808/{slug}.wav'
        if relpath in have:
            print(f'skip (present): {relpath}')
            continue
        pcm, sr = samples[sfname]
        dst = os.path.join(HERE, 'samples', relpath)
        w = wave.open(dst, 'wb')
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm)  # sf2 smpl data is already 16-bit LE mono
        w.close()
        frames = len(pcm) // 2
        manifest.append({
            'bank': 'tr808',
            'bank_name': 'Roland TR-808',
            'name': display,
            'file': relpath,
            'sr': sr,
            'frames': frames,
            'seconds': round(frames / sr, 4),
            'stereo_source': False,
        })
        added += 1
        print(f'added: {relpath} ({frames} frames @ {sr} Hz)')
    if added:
        # appended at the end so existing manifest indices (== AMY preset
        # numbers in saved/shared setups) stay stable
        json.dump(manifest, open(MANIFEST, 'w'), indent=1)
    print(f'{added} samples added, manifest now {len(manifest)} entries')


if __name__ == '__main__':
    main()
