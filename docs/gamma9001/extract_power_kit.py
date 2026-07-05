#!/usr/bin/env python3
"""Extract the Korg AG-10 "Power Kit" (GS program 16) into a Gamma9001 bank.

Parses the SoundFont preset -> instrument -> zone chain for bank 128 preset 16,
pulls the core kit samples (the gated-reverb power sounds plus hats/cymbals),
trims trailing silence, and appends them to samples/manifest.json as the
'power' bank. Idempotent: already-present files are skipped.

Usage: python3 extract_power_kit.py [path-to-Korg_AG-10.sf2]
"""
import json
import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, 'samples', 'manifest.json')
SF2 = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Downloads/Korg_AG-10.sf2')

# GM note -> (slug, display name): the kit's core voices. Latin percussion is
# skipped — the Koblo 'percussion' bank already covers it.
WANTED = {
    35: ('power-kick-real', 'Real Kick'),
    36: ('power-kick-metal', 'Metal Kick'),
    37: ('power-sidestick', 'Side Stick'),
    38: ('power-snare', 'Power Snare'),
    39: ('power-clap', 'Hand Claps'),
    40: ('power-snare-gated', 'Gated Snare'),
    41: ('power-tom-1', 'Process Tom 1'),
    43: ('power-tom-2', 'Process Tom 2'),
    45: ('power-tom-3', 'Process Tom 3'),
    47: ('power-tom-4', 'Process Tom 4'),
    48: ('power-tom-5', 'Process Tom 5'),
    50: ('power-tom-6', 'Process Tom 6'),
    42: ('power-hh-tight', 'Tight HiHat'),
    44: ('power-hh-pedal', 'Pedal HiHat'),
    46: ('power-hh-open', 'Open HiHat'),
    49: ('power-crash', 'Crash Cymbal'),
    51: ('power-ride', 'Ride Edge'),
    53: ('power-ride-cup', 'Ride Cup'),
    55: ('power-splash', 'Splash Cymbal'),
    56: ('power-cowbell', 'Cowbell'),
}

TRIM_THRESHOLD = 60      # |sample| below this counts as silence (~-54 dBFS)
TRIM_PAD_MS = 50         # keep this much tail after the last loud sample
FADE_MS = 10             # fade the trimmed tail to zero to avoid clicks


def find_chunk(data, tag):
    i = data.find(tag)
    while i != -1:
        size = struct.unpack('<I', data[i + 4:i + 8])[0]
        if i + 8 + size <= len(data):
            return i + 8, size
        i = data.find(tag, i + 1)
    raise ValueError(f'chunk {tag} not found')


def records(data, tag, recsize):
    off, size = find_chunk(data, tag)
    return [data[off + i:off + i + recsize] for i in range(0, size, recsize)]


def power_kit_zones(data):
    """Return {gm_note: (sample_name, pcm_bytes, sr)} for bank 128 preset 16."""
    phdr = records(data, b'phdr', 38)
    pbag = [struct.unpack('<HH', r) for r in records(data, b'pbag', 4)]
    pgen = [struct.unpack('<Hh', r) for r in records(data, b'pgen', 4)]
    inst = [(r[:20].rstrip(b'\x00').decode('latin1'), struct.unpack('<H', r[20:22])[0])
            for r in records(data, b'inst', 22)]
    ibag = [struct.unpack('<HH', r) for r in records(data, b'ibag', 4)]
    igen = [struct.unpack('<Hh', r) for r in records(data, b'igen', 4)]
    shdr = records(data, b'shdr', 46)
    smpl_off, _ = find_chunk(data, b'smpl')

    insts = set()
    for k, rec in enumerate(phdr):
        preset, bank, bagidx = struct.unpack('<HHH', rec[20:26])
        if bank == 128 and preset == 16:
            nextbag = struct.unpack('<H', phdr[k + 1][24:26])[0]
            for b in range(bagidx, nextbag):
                for gi in range(pbag[b][0], pbag[b + 1][0]):
                    oper, amt = pgen[gi]
                    if oper == 41:  # instrument generator
                        insts.add(amt)
            break
    zones = {}
    for iidx in insts:
        for b in range(inst[iidx][1], inst[iidx + 1][1]):
            lo = sid = None
            for gi in range(ibag[b][0], ibag[b + 1][0]):
                oper, amt = igen[gi]
                if oper == 43:  # keyRange
                    lo = amt & 0xff
                if oper == 53:  # sampleID
                    sid = amt & 0xffff
            if sid is None or lo is None:
                continue
            srec = shdr[sid]
            name = srec[:20].rstrip(b'\x00').decode('latin1')
            start, end, ls, le, sr = struct.unpack('<IIIII', srec[20:40])
            stype = struct.unpack('<H', srec[44:46])[0]
            assert stype == 1, f'{name}: expected mono sample, got type {stype}'
            zones[lo] = (name, data[smpl_off + start * 2: smpl_off + end * 2], sr)
    return zones


def trim(pcm, sr):
    """Cut trailing silence and fade the tail so one-shots stay compact."""
    n = len(pcm) // 2
    vals = struct.unpack(f'<{n}h', pcm[:n * 2])
    last = 0
    for i, v in enumerate(vals):
        if abs(v) > TRIM_THRESHOLD:
            last = i
    keep = min(n, last + int(sr * TRIM_PAD_MS / 1000))
    vals = list(vals[:keep])
    fade = min(keep, int(sr * FADE_MS / 1000))
    for i in range(fade):
        vals[keep - fade + i] = int(vals[keep - fade + i] * (1 - (i + 1) / fade))
    return struct.pack(f'<{keep}h', *vals)


def main():
    data = open(SF2, 'rb').read()
    zones = power_kit_zones(data)
    manifest = json.load(open(MANIFEST))
    have = {m['file'] for m in manifest}
    os.makedirs(os.path.join(HERE, 'samples', 'power'), exist_ok=True)
    added = 0
    for note in sorted(WANTED):
        slug, display = WANTED[note]
        relpath = f'power/{slug}.wav'
        if relpath in have:
            print(f'skip (present): {relpath}')
            continue
        sfname, pcm, sr = zones[note]
        pcm = trim(pcm, sr)
        w = wave.open(os.path.join(HERE, 'samples', relpath), 'wb')
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm)
        w.close()
        frames = len(pcm) // 2
        manifest.append({
            'bank': 'power',
            'bank_name': '80s Power Kit',
            'name': display,
            'file': relpath,
            'sr': sr,
            'frames': frames,
            'seconds': round(frames / sr, 4),
            'stereo_source': False,
        })
        added += 1
        print(f'added: {relpath} <- {sfname} ({frames} frames @ {sr} Hz)')
    if added:
        json.dump(manifest, open(MANIFEST, 'w'), indent=1)
    print(f'{added} samples added, manifest now {len(manifest)} entries')


if __name__ == '__main__':
    main()
