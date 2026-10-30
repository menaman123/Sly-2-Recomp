#!/usr/bin/env python3
"""agent_state_diff.py BIN S FRAMES... -- where does the RAM of a restored run diverge from the uninterrupted one?
Run A: run.until S, state.save (S must be admissible), then at each frame in FRAMES mem.read all of main RAM into
~/recomp-agent-data/.../cache/control/diff/A_<frame>.bin. Run B: state.load at 0 (-> S), the same frames. Prints,
per frame, the differing byte ranges (address, length, up to 40 ranges) -- addresses and counts only. The dumps are
deleted at the end (game data)."""
import base64
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S = sys.argv[1], int(sys.argv[2])
args = sys.argv[3:]
# --bb: run B twice instead of A then B (is the restored run reproducible by itself?); A saves the state first.
bb = "--bb" in args
frames = [int(x) for x in args if x != "--bb"]
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
dd = os.path.join(root, "diff")
shutil.rmtree(dd, ignore_errors=True)
os.makedirs(dd)
state_path = os.path.join(states, "diff_%d.ps2st" % S)


def dump(c, path):
    with open(path, "wb") as f:
        for off in range(0, 32 << 20, 4 << 20):
            f.write(base64.b64decode(c.ok("mem.read", addr=off, len=4 << 20)["data_b64"]))


def ranges(a, b, cap=40):
    out = []
    i, n = 0, len(a)
    while i < n:
        if a[i] == b[i]:
            i += 1
            continue
        j = i
        while j < n and a[j] != b[j]:
            j += 1
        # merge gaps < 16
        if out and i - (out[-1][0] + out[-1][1]) < 16:
            out[-1] = (out[-1][0], j - out[-1][0])
        else:
            out.append((i, j - i))
        i = j
    total = sum(l for _, l in out)
    return total, out[:cap], len(out)


def run(name, load):
    sock = "/tmp/ra-control-%s.sock" % name
    proc = start_game(bin_, name, sock, root, [])
    c = connect(sock)
    if load:
        r = c.ok("state.load", path=state_path)
        print("%s: loaded -> frame %d" % (name, r["frame"]))
    else:
        c.ok("run.until", frame=S)
        r = c.ok("state.save", path=state_path, max_frames=0)
        print("%s: saved at frame %d" % (name, r["frame"]))
    for f in frames:
        if f > S:
            c.ok("run.until", frame=f)
        dump(c, os.path.join(dd, "%s_%d.bin" % (name, f)))
    c.ok("session.quit")
    proc.wait(60)
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)


t0 = time.time()
run("dA", False)
run("dB", True)
if bb:
    run("dC", True)
for f in frames:
    a = open(os.path.join(dd, "%s_%d.bin" % ("dC" if bb else "dA", f)), "rb").read()
    b = open(os.path.join(dd, "dB_%d.bin" % f), "rb").read()
    total, rs, nr = ranges(a, b)
    print("frame %d (%s): %d differing bytes in %d ranges%s" % (f, "B vs B" if bb else "A vs B", total, nr, "" if total else " (IDENTICAL)"))
    for addr, ln in rs:
        small = " %s | %s" % (a[addr:addr + ln].hex(), b[addr:addr + ln].hex()) if ln <= 8 else ""
        print("   0x%08x +%d%s" % (addr, ln, small))
shutil.rmtree(dd, ignore_errors=True)
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
