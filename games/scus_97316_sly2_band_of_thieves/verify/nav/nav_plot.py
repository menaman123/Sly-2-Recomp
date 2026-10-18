#!/usr/bin/env python3
"""nav_plot.py -- plot a nav_trace.csv (hle/nav_probe.h) as a top-down track PNG.

  nav_plot.py TRACE.csv OUT.png [--from STAMP] [--to STAMP] [--mark x,y,label ...] [--title T]

Top-down (x, y) track of the player coloured by stamp, camera position as a faint track, start / end markers,
optional labelled marks (targets, beacons). Also prints a per-100-stamp summary (position, yaw, cam_yaw) and
the valid-sample count. Pure data: no game bytes.
"""
import argparse
import csv
import math
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('out')
    ap.add_argument('--from', dest='lo', type=int, default=0)
    ap.add_argument('--to', dest='hi', type=int, default=1 << 40)
    ap.add_argument('--mark', action='append', default=[])
    ap.add_argument('--title', default='')
    a = ap.parse_args()
    rows = [r for r in csv.DictReader(open(a.trace)) if r['valid'] == '1' and a.lo <= int(r['stamp']) <= a.hi]
    if not rows:
        sys.exit('no valid samples in range')
    st = [int(r['stamp']) for r in rows]
    xs = [float(r['pos_x']) for r in rows]
    ys = [float(r['pos_y']) for r in rows]
    zs = [float(r['pos_z']) for r in rows]
    cx = [float(r['cam_x']) for r in rows if r.get('cam_x')]
    cy = [float(r['cam_y']) for r in rows if r.get('cam_y')]
    print('valid samples %d, stamps %d..%d' % (len(rows), st[0], st[-1]))
    last = None
    for r in rows:
        s = int(r['stamp'])
        if last is None or s - last >= 100:
            last = s
            print('  %5d pos=(%.0f,%.0f,%.0f) yaw=%s cam_yaw=%s' % (s, float(r['pos_x']), float(r['pos_y']), float(r['pos_z']),
                                                                r.get('yaw', ''), r.get('cam_yaw', '')))
    dist = sum(math.hypot(xs[i + 1] - xs[i], ys[i + 1] - ys[i]) for i in range(len(xs) - 1))
    print('path length %.0f units; z range %.0f..%.0f' % (dist, min(zs), max(zs)))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(9, 9))
    if cx:
        ax.plot(cx, cy, '-', color='0.8', lw=0.8, label='camera')
    sc = ax.scatter(xs, ys, c=st, cmap='viridis', s=6)
    ax.plot(xs[0], ys[0], 'go', ms=10, label='start %d' % st[0])
    ax.plot(xs[-1], ys[-1], 'rs', ms=10, label='end %d' % st[-1])
    for m in a.mark:
        p = m.split(',')
        ax.plot(float(p[0]), float(p[1]), 'k*', ms=12)
        ax.annotate(p[2] if len(p) > 2 else '', (float(p[0]), float(p[1])), textcoords='offset points', xytext=(6, 6))
    ax.set_aspect('equal')
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.grid(True, alpha=0.3)
    ax.legend(loc='best')
    plt.colorbar(sc, ax=ax, label='pad stamp')
    ax.set_title(a.title or a.trace)
    fig.savefig(a.out, dpi=110, bbox_inches='tight')
    print('wrote', a.out)


if __name__ == '__main__':
    main()
