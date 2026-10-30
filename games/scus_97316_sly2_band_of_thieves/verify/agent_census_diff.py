#!/usr/bin/env python3
"""agent_census_diff.py BIN S F -- the per-function call census of frame F -> F+1 (fn.census reset at F, get at F+1)
in the uninterrupted run (saved at S) and in the restored run (loaded at S); prints the functions whose counts
differ (addresses and counts only). Also prints cpu.stack and the hook census totals. Diagnostic for the proof."""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S, F = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
state_path = os.path.join(states, "cd_%d.ps2st" % S)


def run(name, load):
    sock = "/tmp/ra-control-%s.sock" % name
    proc = start_game(bin_, name, sock, root, [])
    c = connect(sock)
    if load:
        c.ok("state.load", path=state_path)
    else:
        c.ok("run.until", frame=S)
        c.ok("state.save", path=state_path, max_frames=0)
    c.ok("run.until", frame=F)
    c.ok("fn.census", action="reset")
    c.ok("run.step", frames=1)
    calls = c.ok("fn.census", action="get")["calls"]
    c.ok("fn.census", action="off")
    c.ok("session.quit")
    proc.wait(60)
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    return {int(k, 16): v for k, v in calls.items()}


t0 = time.time()
A = run("cdA", False)
B = run("cdB", True)
print("frame %d->%d: A %d functions / %d calls, B %d functions / %d calls" % (F, F + 1, len(A), sum(A.values()), len(B), sum(B.values())))
diff = [(fn, A.get(fn, 0), B.get(fn, 0)) for fn in sorted(set(A) | set(B)) if A.get(fn, 0) != B.get(fn, 0)]
print("%d functions differ (fn: A B):" % len(diff))
for fn, a, b in diff[:80]:
    print("   0x%08x %6d %6d" % (fn, a, b))
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
