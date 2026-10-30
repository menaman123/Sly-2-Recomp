#!/usr/bin/env python3
"""agent_calls_diff.py BIN S F ADDR... -- fn.trace on ADDR... from the save/load at S to frame F in the uninterrupted
run (A) and the restored run (B); prints fn.calls.recent (frame, a0..a3, v0) of each, side by side. Addresses,
args and return values only. Diagnostic for the proof."""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S, F = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
addrs = [int(x, 0) for x in sys.argv[4:]]
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
state_path = os.path.join(states, "cl_%d.ps2st" % S)


def run(name, load):
    sock = "/tmp/ra-control-%s.sock" % name
    proc = start_game(bin_, name, sock, root, [])
    c = connect(sock)
    if load:
        c.ok("state.load", path=state_path)
    else:
        c.ok("run.until", frame=S)
        c.ok("state.save", path=state_path, max_frames=0)
    c.ok("fn.trace", addrs=addrs, on=True)
    c.ok("run.until", frame=F)
    out = {}
    for a in addrs:
        calls = c.ok("fn.calls.recent", addr=a, n=64)["calls"]
        out[a] = [(x["frame"], [int(v, 16) for v in x["args"][:4]], [int(v, 16) for v in x["ret"]], x["caller"]) for x in calls]
    c.ok("session.quit")
    proc.wait(60)
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    return out


t0 = time.time()
A = run("clA", False)
B = run("clB", True)
for a in addrs:
    print("== 0x%08x: A %d calls, B %d calls (frame a0 a1 a2 a3 -> v0 ; caller)" % (a, len(A[a]), len(B[a])))
    n = max(len(A[a]), len(B[a]))
    for i in range(n):
        def fmt(x):
            if x is None:
                return "-"
            f, args, ret, caller = x
            return "%d %s -> %s ; %s" % (f, " ".join("%x" % v for v in args), "%x" % ret[0] if ret else "?", hex(caller) if caller else "-")
        print("   %-50s | %s" % (fmt(A[a][i] if i < len(A[a]) else None), fmt(B[a][i] if i < len(B[a]) else None)))
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
