#!/usr/bin/env python3
"""nav_hunt.py -- find smoothly-changing float triples (a position vector) across guest RAM dumps.

  nav_hunt.py ram_A.bin ram_B.bin ram_C.bin [ram_D.bin ...] [--still ram_S.bin] [--min V] [--max V] [--top N]

The dumps are the 32 MiB EE RAM images NATIVEEE_NAV=ramdump=<dir>@<stamp>+... writes at equally spaced pad
stamps while the player walks in a straight line; --still is a dump taken while standing (a value that is
identical between the still dump and the first moving dump is NOT the player's position). A candidate word
is a float that is finite, changes between consecutive moving dumps by a near-constant step (|step| in
[min, max], steps agree within 35 %) -- a straight walk at constant speed. Candidates are then grouped into
triples: three such words at consecutive offsets (or with one steady word between them: a vec4 whose w is 1)
score higher. Output: the best triples, then the lone words. No game bytes are written anywhere.
"""
import argparse
import struct
import sys


def load(path):
    with open(path, 'rb') as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dumps', nargs='+')
    ap.add_argument('--still', default=None)
    ap.add_argument('--min', type=float, default=0.02)
    ap.add_argument('--max', type=float, default=50.0)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--lo', type=lambda s: int(s, 0), default=0x100000)
    ap.add_argument('--hi', type=lambda s: int(s, 0), default=0x2000000)
    a = ap.parse_args()
    if len(a.dumps) < 3:
        sys.exit('need at least three moving dumps')
    imgs = [load(p) for p in a.dumps]
    still = load(a.still) if a.still else None
    n = len(imgs)
    cands = {}
    fmt = '<f'
    for off in range(a.lo, min(a.hi, len(imgs[0])) - 4, 4):
        v = []
        ok = True
        for im in imgs:
            x = struct.unpack_from(fmt, im, off)[0]
            if x != x or abs(x) > 1e7:
                ok = False
                break
            v.append(x)
        if not ok:
            continue
        steps = [v[i + 1] - v[i] for i in range(n - 1)]
        if any(abs(s) < a.min or abs(s) > a.max for s in steps):
            continue
        ref = steps[0]
        if any(abs(s - ref) > 0.35 * abs(ref) or s * ref <= 0 for s in steps[1:]):
            continue
        if still is not None:
            s0 = struct.unpack_from(fmt, still, off)[0]
            if s0 == v[0]:
                continue
        cands[off] = (v, ref)
    # group into triples at offsets off, off+4, off+8 (or off, off+4, off+8 within a 16-byte aligned quad)
    triples = []
    for off in sorted(cands):
        members = [o for o in (off, off + 4, off + 8) if o in cands]
        if len(members) >= 2:
            triples.append((len(members), off, members))
    seen = set()
    print('# triples (count, base, values per dump, step)')
    out = 0
    for cnt, off, members in sorted(triples, key=lambda t: (-t[0], t[1])):
        if off in seen:
            continue
        for m in members:
            seen.add(m)
        print('0x%08x  n=%d' % (off, cnt))
        for m in members:
            v, ref = cands[m]
            print('   +%d 0x%08x  %s  step %.4f' % (m - off, m, ' '.join('%.3f' % x for x in v), ref))
        out += 1
        if out >= a.top:
            break
    print('# lone words: %d' % sum(1 for o in cands if o not in seen))
    out = 0
    for off in sorted(cands):
        if off in seen:
            continue
        v, ref = cands[off]
        print('0x%08x  %s  step %.4f' % (off, ' '.join('%.3f' % x for x in v), ref))
        out += 1
        if out >= a.top:
            break


if __name__ == '__main__':
    main()
