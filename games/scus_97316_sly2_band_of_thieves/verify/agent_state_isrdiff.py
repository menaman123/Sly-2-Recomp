#!/usr/bin/env python3
"""agent_state_isrdiff.py BIN S END -- the ISR/GS-IRQ trace (NATIVEEE_ISR_TRACE=1) of the uninterrupted run from the
save at S to END, against the restored run's from the load to END; prints the first divergence with context (the
det agent's method, on the savestate route). Game data: none (the trace is tids, syscall numbers, masks)."""
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, S, end = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
ev = os.path.expanduser(os.environ.get("RA_ISRDIFF_EVIDENCE", "~/recomp-agent-data/scus_97316/evidence/isrdiff"))
os.makedirs(ev, exist_ok=True)
state_path = os.path.join(states, "isr_%d.ps2st" % S)


def run(name, load):
    sock = "/tmp/ra-control-%s.sock" % name
    log = os.path.join(root, name + ".log")
    proc = start_game(bin_, name, sock, root, [], env_extra={"NATIVEEE_ISR_TRACE": "1"}, log=log)
    c = connect(sock)
    if load:
        print("%s: load -> %d" % (name, c.ok("state.load", path=state_path)["frame"]))
    else:
        c.ok("run.until", frame=S)
        print("%s: save -> %d" % (name, c.ok("state.save", path=state_path, max_frames=0)["frame"]))
    c.ok("run.until", frame=end)
    c.ok("session.quit")
    proc.wait(60)
    lines = open(log, errors="replace").read().splitlines()
    for d in (name + ".dump", name + ".mc", name):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    os.unlink(log)
    return lines


def strip(l):
    return re.sub(r" p[123]=\d+", "", re.sub(r" t=\d+", "", l))   # t= wall; p1..p3 = GIF receive counts since process start


t0 = time.time()
A = run("iA", False)
B = run("iB", True)
ia = next(i for i, l in enumerate(A) if "state.save: frame" in l)
ib = next(i for i, l in enumerate(B) if "state.load: frame" in l and "restored live" in l)
a = [strip(l) for l in A[ia + 1:] if l.startswith(("isr-trace:", "gs-irq:"))]
b = [strip(l) for l in B[ib + 1:] if l.startswith(("isr-trace:", "gs-irq:"))]
open(os.path.join(ev, "A_after_save.txt"), "w").write("\n".join(a) + "\n")
open(os.path.join(ev, "B_after_load.txt"), "w").write("\n".join(b) + "\n")
n = min(len(a), len(b))
first = next((i for i in range(n) if a[i] != b[i]), None)
print("A %d lines after the save, B %d after the load; first divergence at line %s" % (len(a), len(b), first))
if first is not None:
    lo = max(0, first - 12)
    for i in range(lo, min(n, first + 8)):
        print("%6d %s | %s" % (i, a[i][:110], b[i][:110]))
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
