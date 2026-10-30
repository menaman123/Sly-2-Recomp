#!/usr/bin/env python3
"""agent_lane.py BIN FRAMES OUT [--pad PAD] [--step N] -- the pinned deterministic lane (det_lane.sh's pins) WITH the
agent control plane: the game starts paused at boundary 0 and is advanced with run.step only (one op of FRAMES
frames, or --step N frames per op), until the --frames cap ends the leg. Prints det_lane.sh's summary line so the
hashes compare directly with the socket-unset lane. OUT must live under ~/recomp-agent-data (deleted dumps)."""
import hashlib
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

args = sys.argv[1:]
bin_, frames, out = args[0], int(args[1]), args[2]
pad = None
step = frames
i = 3
while i < len(args):
    if args[i] == "--pad":
        pad = args[i + 1]
        i += 2
    elif args[i] == "--step":
        step = int(args[i + 1])
        i += 2
    else:
        raise SystemExit("unknown arg " + args[i])
home = os.path.expanduser("~/recomp-agent-data")
if not os.path.abspath(out).startswith(home):
    raise SystemExit("OUT must live under ~/recomp-agent-data")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
root, name = os.path.dirname(out), os.path.basename(out) + ".leg"
sock = "/tmp/ra-control-%s.sock" % name
env = {"NATIVEEE_PAD_SCRIPT": pad} if pad else {}
t0 = time.time()
proc = start_game(bin_, name, sock, root, ["--frames", str(frames)], env_extra=env, log=os.path.join(out, "log"))
c = connect(sock)
info = c.ok("session.info")
assert info["frame"] == 0 and info["paused"], info
ops = 0
done = 0
while done < frames:
    n = min(step, frames - done)
    ops += 1
    try:
        r = c.call("run.step", frames=n, timeout=600)
    except RuntimeError:
        break   # the frames cap ended the leg inside this step: the connection closed
    if not r.get("ok"):
        break
    done = r["frame"]
    if done >= frames - 1:
        # The cap ends the leg at boundary FRAMES; one more step lets the game reach it.
        try:
            c.call("run.step", frames=1, timeout=60)
            ops += 1
        except Exception:
            pass
        break
proc.wait(120)
t1 = time.time()
# Move the harness leg into OUT/h like det_lane.sh, delete the dumps.
shutil.move(os.path.join(root, name), os.path.join(out, "h")) if os.path.isdir(os.path.join(root, name)) else None
for d in (name + ".dump", name + ".mc"):
    shutil.rmtree(os.path.join(root, d), ignore_errors=True)
for f in (name + ".ini", name + ".log"):
    p = os.path.join(root, f)
    if os.path.exists(p):
        os.unlink(p)


def h(p):
    p = os.path.join(out, "h", p)
    return hashlib.sha256(open(p, "rb").read()).hexdigest()[:12] if os.path.exists(p) else "none"


def row(k, n=40):
    p = os.path.join(out, "h", "run.meta")
    if not os.path.exists(p):
        return "?"
    for line in open(p):
        if line.startswith(k + "="):
            return line.rstrip("\n").split("=", 1)[1][:n]
    return "?"


lines = sum(1 for _ in open(os.path.join(out, "h", "frames.fhash"))) if os.path.exists(os.path.join(out, "h", "frames.fhash")) else 0
name = os.path.basename(out)
print("%s rc=%s wall_s=%.1f mem=%s audio=%s frames=%s(%d lines) domain_tick=%s syscalls=%s gs_draws=%s boundaries=%s env_hash=%s agent=%s ops=%d | %s" % (
    name, proc.returncode, t1 - t0, h("mem.mhash"), h("audio.ahash"), h("frames.fhash"), lines, row("domain_tick"), row("syscall_count"),
    row("gs_draws"), row("boundaries"), row("env_hash"), row("agent", 200), ops, uptime().split("load")[-1].strip()))
