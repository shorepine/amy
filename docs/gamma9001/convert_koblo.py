#!/usr/bin/env python3
"""Convert Koblo Tokyo 2.0/2.5 Sound Designer II drum samples to mono WAVs for AMY.

The Koblo sample files are classic Mac SDII: raw 16-bit big-endian PCM in the
data fork, with sample-rate/size/channel counts stored as STR resources in the
resource fork (extracted by `unar -k hidden` as AppleDouble `._name` sidecars).

Usage:
  python3 convert_koblo.py <tokyo20-samples-dir> <tokyo25-samples-dir> [outdir]

Writes <outdir>/<bank>/<slug>.wav (mono, 16-bit LE, native sample rate) and
<outdir>/manifest.json describing every sample for the web app.
"""
import json
import os
import re
import struct
import sys
import wave

import numpy as np


def read_appledouble(sidecar):
    """Return (resource_fork_bytes, finder_info_bytes) from an AppleDouble file."""
    with open(sidecar, 'rb') as f:
        blob = f.read()
    if blob[:4] != b'\x00\x05\x16\x07':
        return None, None
    nent = struct.unpack('>H', blob[24:26])[0]
    rsrc = finder = None
    for i in range(nent):
        eid, off, ln = struct.unpack('>III', blob[26 + i * 12: 38 + i * 12])
        if eid == 2:
            rsrc = blob[off:off + ln]
        elif eid == 9:
            finder = blob[off:off + ln]
    return rsrc, finder


def parse_resources(rsrc):
    """Return {(type, id): bytes} from a classic Mac resource fork."""
    out = {}
    if not rsrc or len(rsrc) < 16:
        return out
    data_off, map_off = struct.unpack('>II', rsrc[0:8])
    typelist_off = struct.unpack('>H', rsrc[map_off + 24: map_off + 26])[0]
    ntypes = struct.unpack('>h', rsrc[map_off + typelist_off: map_off + typelist_off + 2])[0] + 1
    for t in range(ntypes):
        base = map_off + typelist_off + 2 + t * 8
        rtype = rsrc[base:base + 4]
        count = struct.unpack('>h', rsrc[base + 4:base + 6])[0] + 1
        ref_off = struct.unpack('>H', rsrc[base + 6:base + 8])[0]
        for r in range(count):
            rb = map_off + typelist_off + ref_off + r * 12
            rid = struct.unpack('>h', rsrc[rb:rb + 2])[0]
            doff = struct.unpack('>I', b'\x00' + rsrc[rb + 5:rb + 8])[0]
            dlen = struct.unpack('>I', rsrc[data_off + doff:data_off + doff + 4])[0]
            out[(rtype.decode('mac_roman'), rid)] = rsrc[data_off + doff + 4:data_off + doff + 4 + dlen]
    return out


def pstr(b):
    return b[1:1 + b[0]].decode('mac_roman') if b else ''


def sd2_params(path):
    """Return (sample_rate, channels) for an SDII file, or None if not SDII."""
    sidecar = os.path.join(os.path.dirname(path), '._' + os.path.basename(path))
    if not os.path.exists(sidecar):
        return None
    rsrc, finder = read_appledouble(sidecar)
    if finder is None or finder[0:4] != b'Sd2f':
        return None
    res = parse_resources(rsrc)
    try:
        size = int(pstr(res.get(('STR ', 1000))))
        rate = int(float(pstr(res.get(('STR ', 1001)))))
        chans = int(pstr(res.get(('STR ', 1002))))
        assert size == 2
        return rate, chans
    except (ValueError, TypeError, AssertionError):
        # A couple of files (e.g. "Tokyo Deep bd") are missing the STR
        # resources; every one of those is 16-bit mono 44.1k like its siblings.
        return 44100, 1


def slugify(name):
    s = name.lower().replace('#', ' ').replace('&', 'and')
    s = re.sub(r'[^a-z0-9]+', '-', s).strip('-')
    return s


def convert(src, dst, rate, chans):
    raw = open(src, 'rb').read()
    raw = raw[:len(raw) - (len(raw) % (2 * chans))]
    pcm = np.frombuffer(raw, dtype='>i2').astype(np.int32)
    if chans == 2:
        pcm = (pcm[0::2] + pcm[1::2]) // 2
    pcm = pcm.astype('<i2')
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    w = wave.open(dst, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(pcm.tobytes())
    w.close()
    return len(pcm)


# (bank id, bank display name, source root, relative source dir)
# Order matters: earlier banks win when the same sample appears twice.
BANKS_20 = [
    ('tr808', 'Roland TR-808', 'Roland TR-808'),
    ('linn9000', 'Linn 9000', 'Linn9000'),
    ('percussion', 'Percussion', 'Percussion'),
]
BANKS_25 = [
    ('tr909', 'Roland TR-909', 'Drums & Percussion/Drum Machines/ROLAND TR-909'),
    ('mr12', 'Univox Micro Rythmer 12', 'Drums & Percussion/Drum Machines/UNIVOX MICRO RYTHMER 12'),
    ('linn9000', 'Linn 9000', 'Drums & Percussion/Drum Machines/LINN9000'),
    ('synthetics', 'Tokyo Synthetics', 'Drums & Percussion/Tokyo Synthetics'),
    ('acoustic', 'Acoustic Percussion', 'Drums & Percussion/Acoustic Unpitched'),
    ('extras', 'Extras', 'Effect'),
    ('extras', 'Extras', 'Instruments'),
    ('extras', 'Extras', 'Synth'),
]


def walk_bank(root, rel):
    base = os.path.join(root, rel)
    for dirpath, dirnames, filenames in os.walk(base):
        for name in sorted(filenames):
            if name.startswith('._') or name.startswith('.'):
                continue
            yield os.path.join(dirpath, name), name


def main():
    tokyo20, tokyo25 = sys.argv[1], sys.argv[2]
    outdir = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'samples')
    manifest = []
    seen = {}
    for banks, root in ((BANKS_20, tokyo20), (BANKS_25, tokyo25)):
        for bank, bank_name, rel in banks:
            for src, name in walk_bank(root, rel):
                params = sd2_params(src)
                if params is None:
                    print(f"skip (not SDII): {src}")
                    continue
                rate, chans = params
                slug = slugify(name)
                key = (bank, slug)
                if key in seen:
                    print(f"skip (dupe): {bank}/{name}")
                    continue
                seen[key] = True
                dst = os.path.join(outdir, bank, slug + '.wav')
                frames = convert(src, dst, rate, chans)
                manifest.append({
                    'bank': bank,
                    'bank_name': bank_name,
                    'name': name,
                    'file': f'{bank}/{slug}.wav',
                    'sr': rate,
                    'frames': frames,
                    'seconds': round(frames / rate, 4),
                    'stereo_source': chans == 2,
                })
    with open(os.path.join(outdir, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1)
    by_bank = {}
    for m in manifest:
        by_bank.setdefault(m['bank'], []).append(m)
    total = sum(m['frames'] * 2 for m in manifest)
    for bank, items in by_bank.items():
        print(f"{bank:12s} {len(items):3d} samples")
    print(f"total: {len(manifest)} samples, {total/1e6:.2f} MB PCM -> {outdir}")


if __name__ == '__main__':
    main()
