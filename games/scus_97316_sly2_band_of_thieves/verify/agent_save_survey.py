#!/usr/bin/env python3
"""agent_save_survey.py BIN FROM TO [SAVE_AT [MAX_FRAMES]] -- which boundaries admit a savestate capture
(state.probe at every frame in [FROM, TO]), then one state.save at SAVE_AT (>= TO; default TO) with
max_frames (default 300): the server defers it to the next admissible boundary. Prints the admissibility
map and the frame actually saved; deletes the savestate and the dumps. Evidence for control.md."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
save_at = int(sys.argv[4]) if len(sys.argv) > 4 else hi
max_frames = int(sys.argv[5]) if len(sys.argv) > 5 else 300
root = os.path.expanduser("~/recomp-agent-data/scus_97316/cache/control")
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
os.makedirs(states, exist_ok=True)
sock = "/tmp/ra-control-survey.sock"
t0 = time.time()
proc = start_game(bin_, "survey", sock, root, [])
c = connect(sock)
info = c.ok("session.info")
print("session.info frame=%d paused=%s features=%d" % (info["frame"], info["paused"], len(info["features"])))
c.ok("run.until", frame=lo)
rows = []
for f in range(lo, hi + 1):
    p = c.ok("state.probe")
    assert p["frame"] == f, (p, f)
    rows.append((f, p["admissible"], p["reason"]))
    if f < hi:
        c.ok("run.step", frames=1)
adm = [f for f, a, _ in rows if a]
ref = [(f, r) for f, a, r in rows if not a]
print("probed %d..%d: admissible %d, refused %d" % (lo, hi, len(adm), len(ref)))
print("admissible frames:", adm)
reasons = {}
for f, r in ref:
    reasons.setdefault(r, []).append(f)
for r, fs in reasons.items():
    print("refused [%s]: %s" % (r, fs))
# One state.save at save_at (deferred by the server when that boundary is refused); the file is deleted.
c.ok("run.until", frame=save_at)
path = os.path.join(states, "survey.ps2st")
t1 = time.time()
r = c.call("state.save", path=path, max_frames=max_frames)
print("state.save at frame %d (max_frames %d) -> %s (%.1f s)" % (save_at, max_frames, {k: v for k, v in r.items() if k != "path"}, time.time() - t1))
print("events:", [(e["event"], e.get("reason"), e.get("frame")) for e in c.drain_events()][-4:])
for p in (path, path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
print("wall %.1f s | %s" % (time.time() - t0, uptime()))
c.close()
proc.terminate()
try:
    proc.wait(10)
except Exception:
    proc.kill()
os.system("rm -rf %s/survey.dump %s/survey.mc" % (root, root))
