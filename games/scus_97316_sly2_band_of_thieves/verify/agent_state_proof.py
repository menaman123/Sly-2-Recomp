#!/usr/bin/env python3
"""agent_state_proof.py BIN [SAVE_AT=2000] [END=3000] -- THE SAVESTATE PROOF on the pinned lane (det_lane.sh's pins +
the agent socket). Run A: boot, run.until SAVE_AT, state.save at the first admissible boundary S >= SAVE_AT with
S = 59 (mod 60) (the restored run's lanes count from engine boundary S+1: the mem lane, hashed every 60 boundaries,
and the audio lane, every 6, then align with run A's), run.until END, sha256 of all of main RAM through mem.read,
session.quit (the census). Run B: a fresh process, state.load of A's file at frame 0 (-> S), run.until END, the
same RAM hash, session.quit (the --frames cap counts the output SEGMENT's boundaries after a live restore). Then cmp_legs.py compares the
harness lanes (frames.fhash per presented frame, audio.ahash per 6-boundary window, mem.mhash per 60) on absolute
positions over [S, END]. Prints the verdict; deletes the savestate and the dumps (game data), keeps the lanes (hashes)
under ~/recomp-agent-data/scus_97316/evidence/state_proof/ (RA_PROOF_EVIDENCE=<dir> and RA_PROOF_CACHE=<dir> override the
evidence and the run/dump roots; the savestate file is named proof_<SAVE_AT>.ps2st under the data store).
Prints the uninterrupted run's `state.save:` line (frame, engine boundary, yield_remaining) and the restored run's
`state.load:` line (sections, yield_restores) from the game logs: the YLDP section's seat is visible there."""
import base64
import hashlib
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_client import connect, start_game, uptime  # noqa: E402

bin_ = sys.argv[1]
save_at = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
end = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
root = os.path.expanduser(os.environ.get("RA_PROOF_CACHE", "~/recomp-agent-data/scus_97316/cache/control"))
states = os.path.expanduser("~/recomp-agent-data/scus_97316/savestates")
evidence = os.path.expanduser(os.environ.get("RA_PROOF_EVIDENCE", "~/recomp-agent-data/scus_97316/evidence/state_proof"))
os.makedirs(states, exist_ok=True)
shutil.rmtree(evidence, ignore_errors=True)
os.makedirs(evidence)
state_path = os.path.join(states, "proof_%d.ps2st" % save_at)
here = os.path.dirname(os.path.abspath(__file__))
cmp_legs = os.path.expanduser(os.environ.get("RA_CMP_LEGS", os.path.join(here, "cmp_legs.py")))


def ram_sha256(c):
    # 32 MiB in 4 MiB pieces (base64 over the socket); only the digest leaves this process.
    h = hashlib.sha256()
    for off in range(0, 32 << 20, 4 << 20):
        r = c.ok("mem.read", addr=off, len=4 << 20)
        h.update(base64.b64decode(r["data_b64"]))
    return h.hexdigest()


def finish(c, proc, name):
    c.ok("session.quit")
    proc.wait(120)
    c.close()
    # The engine's own save/load lines (frame, engine boundary, sections, the yield budget's seat): derived counts only.
    logp = os.path.join(root, name + ".log")
    if os.path.exists(logp):
        for line in open(logp, errors="replace"):
            if "state.save: frame" in line or "state.load: frame" in line or "relaunch:" in line:
                log("%s log: %s" % (name, line.rstrip()))
    for d in (name + ".dump", name + ".mc"):
        shutil.rmtree(os.path.join(root, d), ignore_errors=True)
    for f in (name + ".ini",):
        p = os.path.join(root, f)
        if os.path.exists(p):
            os.unlink(p)


def log(msg):
    print(msg, flush=True)
    with open(os.path.join(evidence, "proof.txt"), "a") as f:
        f.write(msg + "\n")


log("savestate proof: bin %s save_at %d end %d | %s" % (bin_, save_at, end, uptime()))
cap = []

# ---- run A ----
t0 = time.time()
proc = start_game(bin_, "proofA", "/tmp/ra-control-proofA.sock", root, cap)
c = connect("/tmp/ra-control-proofA.sock")
c.ok("run.until", frame=save_at)
S = None
probes = []
f = save_at + (59 - save_at % 60) % 60
while f <= save_at + 600:
    if f != save_at:
        c.ok("run.until", frame=f)
    p = c.ok("state.probe")
    probes.append((f, p["admissible"], p["reason"]))
    if p["admissible"]:
        S = f
        break
    f += 60
log("A: probes at frames = 59 mod 60 from %d: %s" % (save_at, [(f, a) for f, a, _ in probes]))
if S is None:
    log("A: no admissible 59-mod-60 frame in [%d, %d]; a deferred save from here (the mem/audio lanes may not align)" % (save_at, save_at + 600))
r = c.ok("state.save", path=state_path, max_frames=600)
S = r["frame"]
log("A: state.save -> frame %d (requested %d%s) %s" % (S, r.get("requested_frame", -1), ", deferred_from: " + r["deferred_from"] if "deferred_from" in r else "", os.path.basename(r["path"])))
c.ok("run.until", frame=end)
ram_a = ram_sha256(c)
log("A: frame %d RAM sha256 %s" % (end, ram_a))
finish(c, proc, "proofA")
tA = time.time() - t0
log("A: rc=%s wall %.1f s | %s" % (proc.returncode, tA, uptime()))

# ---- run B ----
t0 = time.time()
proc = start_game(bin_, "proofB", "/tmp/ra-control-proofB.sock", root, cap)
c = connect("/tmp/ra-control-proofB.sock")
info = c.ok("session.info")
r = c.ok("state.load", path=state_path)
log("B: state.load at frame %d -> frame %d" % (info["frame"], r["frame"]))
assert r["frame"] == S, (r, S)
c.ok("run.until", frame=end)
ram_b = ram_sha256(c)
log("B: frame %d RAM sha256 %s" % (end, ram_b))
finish(c, proc, "proofB")
tB = time.time() - t0
log("B: rc=%s wall %.1f s | %s" % (proc.returncode, tB, uptime()))

# ---- compare ----
legA = os.path.join(root, "proofA")
legB = os.path.join(root, "proofB")
segs = sorted(d for d in os.listdir(legB) if d.startswith("output-"))
log("B: output segments %s" % segs)
legB_seg = os.path.join(legB, segs[-1]) if segs else legB
res = subprocess.run([sys.executable, cmp_legs, legA, legB_seg, "--from-frame", str(S), "--to-frame", str(end)], capture_output=True, text=True)
log("cmp_legs %s vs %s over [%d, %d]:\n%s%s" % (legA, legB_seg, S, end, res.stdout, res.stderr))
log("RAM at %d: A %s B %s -> %s" % (end, ram_a[:16], ram_b[:16], "EQUAL" if ram_a == ram_b else "DIFFERENT"))
verdict = "PASS" if res.returncode == 0 and ram_a == ram_b else "FAIL"
log("VERDICT %s (cmp_legs rc %d)" % (verdict, res.returncode))
# Keep the lanes (hashes), drop the game data.
for name in ("proofA", "proofB"):
    shutil.copytree(os.path.join(root, name), os.path.join(evidence, name), dirs_exist_ok=True)
    shutil.rmtree(os.path.join(root, name), ignore_errors=True)
    if os.path.exists(os.path.join(root, name + ".log")):
        shutil.move(os.path.join(root, name + ".log"), os.path.join(evidence, name + ".log"))
for p in (state_path, state_path + ".meta"):
    if os.path.exists(p):
        os.unlink(p)
sys.exit(0 if verdict == "PASS" else 1)
