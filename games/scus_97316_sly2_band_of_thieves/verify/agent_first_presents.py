#!/usr/bin/env python3
"""agent_first_presents.py BIN S END -- dump the presented frames S+1..END of the uninterrupted run (A: boot, save at S,
run on) and of the restored run (B: load S), then compare them pixelwise. The PPMs are game data: they stay under
~/recomp-agent-data/scus_97316/cache/splice and are deleted here; only per-frame statistics (hash, fraction of
differing pixels, mean RGB, bounding box of the difference) are printed."""
import glob
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S, end = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/splice")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
state_path = os.path.join(states, "fp_%d.ps2st" % S)


def read_ppm(path):
    data = open(path, "rb").read()
    # P6\nW H\n255\n
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def stats(a, b, w, h):
    diff = 0
    x0 = y0 = 10**9
    x1 = y1 = -1
    for y in range(h):
        row = y * w * 3
        ra = a[row:row + w * 3]
        rb = b[row:row + w * 3]
        if ra == rb:
            continue
        for x in range(w):
            i = x * 3
            if ra[i:i + 3] != rb[i:i + 3]:
                diff += 1
                x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
    return diff, (x0, y0, x1, y1) if diff else None


def mean_rgb(a):
    n = len(a) // 3
    return tuple(round(sum(a[c::3]) / n, 1) for c in range(3))


def run(name, load):
    sock = "/tmp/ra-splice-%s.sock" % name
    proc = start_game(bin_, name, sock, root, [], env_extra={"NATIVEEE_DUMP_FROM": str(S + 1)})
    c = connect(sock)
    if load:
        print("%s: load -> %d" % (name, c.ok("state.load", path=state_path)["frame"]))
    else:
        c.ok("run.until", frame=S)
        r = c.ok("state.save", path=state_path, max_frames=0)
        print("%s: save -> %d" % (name, r["frame"]))
    c.ok("run.until", frame=end)
    c.ok("session.quit")
    proc.wait(120)
    dump = os.path.join(root, name + ".dump")
    files = {}
    for p in glob.glob(os.path.join(dump, "**", "*.ppm"), recursive=True):
        stem = os.path.splitext(os.path.basename(p))[0]
        digits = "".join(ch for ch in stem if ch.isdigit())
        files[int(digits)] = p
    return files


t0 = time.time()
A = run("fpA", False)
B = run("fpB", True)
print("A frames %s..%s (%d), B frames %s..%s (%d)" % (min(A, default=None), max(A, default=None), len(A), min(B, default=None), max(B, default=None), len(B)))
for f in sorted(set(A) & set(B)):
    wa, ha, a = read_ppm(A[f])
    wb, hb, b = read_ppm(B[f])
    if (wa, ha) != (wb, hb):
        print("%d: size A %dx%d B %dx%d" % (f, wa, ha, wb, hb))
        continue
    d, box = stats(a, b, wa, ha)
    print("%d: %dx%d equal=%s diff_px=%d (%.1f%%) box=%s meanA=%s meanB=%s" % (
        f, wa, ha, a == b, d, 100.0 * d / (wa * ha), box, mean_rgb(a), mean_rgb(b)))
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
for name in ("fpA", "fpB"):
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    for f in (name + ".ini", name + ".log"):
        p = os.path.join(root, f)
        if os.path.exists(p):
            os.unlink(p)
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
