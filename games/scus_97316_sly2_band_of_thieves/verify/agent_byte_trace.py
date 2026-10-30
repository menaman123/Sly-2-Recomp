#!/usr/bin/env python3
"""agent_byte_trace.py BIN S FROM TO ADDR LEN -- the value of LEN bytes at ADDR at every frame in [FROM, TO] in the
uninterrupted run (A, saved at S) and in the restored run (B, loaded at S). Prints only the frames where the
window changes (small hex windows of a global). Diagnostic for the savestate proof."""
import base64
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S, lo, hi, addr, ln = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5], 0), int(sys.argv[6])
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
state_path = os.path.join(states, "bt_%d.ps2st" % S)


def run(name, load):
    sock = "/tmp/ra-control-%s.sock" % name
    proc = start_game(bin_, name, sock, root, [])
    c = connect(sock)
    if load:
        c.ok("state.load", path=state_path)
    else:
        c.ok("run.until", frame=S)
        c.ok("state.save", path=state_path, max_frames=0)
    out = []
    f = S
    if lo > S:
        c.ok("run.until", frame=lo)
        f = lo
    last = None
    while f <= hi:
        v = base64.b64decode(c.ok("mem.read", addr=addr, len=ln)["data_b64"]).hex()
        if v != last:
            out.append((f, v))
            last = v
        c.ok("run.step", frames=1)
        f += 1
    c.ok("session.quit")
    proc.wait(60)
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    return out


t0 = time.time()
A = run("btA", False)
B = run("btB", True)
print("A changes:", A)
print("B changes:", B)
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
