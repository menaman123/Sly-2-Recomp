#!/usr/bin/env python3
"""judge_metal_identity.py -- the METAL IDENTITY judge (round 3, track metal): the Metal arm's picture
against the GL arm's TWO blend routes, as a band.

The GS blends in integers; GL's fixed-function arm (GL_ff, the default route) blends in floats the
driver rounds, GL's shader arm (GL_sw, `gs_replay --force-shader-blend`: integer blend on a destination
copy) rounds the other way on the same draw -- and neither is an oracle for the other (one pre-draw Cd
copy counts k self-overlapping faces once; the fixed-function arm accumulates). The Metal arm blends in
the shader on the framebuffer fetch (integer per fragment, accumulating). Its picture is therefore
judged per pixel, per RGB channel, against the BAND the two GL routes span:

    min(GL_ff, GL_sw) - tol  <=  Metal  <=  max(GL_ff, GL_sw) + tol        (tol = 2 / 255)

A pixel OUTSIDE the band on any channel is counted; its excess is the distance to the band. Gates per
dumped frame (RED on any frame): outside <= 0.3 % of the pixels, max excess <= 8. The frames are every
`step`-th frame of each capture (the three Paris / Castle / Cairo consumer streams, engine/gs/hle/capture),
dumped by `tools/gs_replay stream <capture> --dump-ppm <dir> --dump-frames lo-hi:step --backend gl|metal
[--force-shader-blend]` at the same scale.

  judge_metal_identity.py run --gs-replay <bin> --stream <capture> [--stream ...] --artefacts <dir>
                              [--frames 300] [--step 50] [--scale 1] [--tol 2] [--gate-pct 0.3] [--gate-excess 8]
      replays each capture three times (GL_ff, GL_sw, Metal; one process at a time), dumps the frames and
      judges them; prints one row per frame and the verdict; exit 0 = every frame GREEN, 1 = a RED frame,
      77 = gs_replay found no Metal device (SKIP).
  judge_metal_identity.py judge --ff <dir> --sw <dir> --metal <dir> [--tol 2] [--gate-pct 0.3] [--gate-excess 8]
      the same over three existing dump directories (g<frame>.ppm, the frames they share).
  judge_metal_identity.py --selftest
      synthetic frames: inside / outside / excess arithmetic, the gates, the PPM reader, the step range.

Artefacts (--artefacts): <dir>/<capture>/{ff,sw,metal}/g<frame>.ppm, <dir>/summary.json, and for every RED
frame <dir>/<capture>/outside_<frame>.pgm (255 where the pixel lies outside the band).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

try:
    import numpy as np   # the fast path; the pure-Python path below gives the same numbers
except ImportError:   # pragma: no cover
    np = None

GATE_PCT = 0.3
GATE_EXCESS = 8
TOL = 2
SKIP = 77


# --------------------------------------------------------------------------------------------- PPM

def read_ppm(path):
    """Binary P6 -> (w, h, rgb bytes). Header tokens are scanned (the body may begin with whitespace bytes)."""
    data = open(path, "rb").read()
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1
    if tokens[0] != b"P6":
        raise ValueError("not a binary PPM: " + path)
    w, h = int(tokens[1]), int(tokens[2])
    return w, h, data[pos:pos + w * h * 3]


def write_ppm(path, w, h, rgb):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(rgb)


def write_pgm(path, w, h, gray):
    with open(path, "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (w, h))
        f.write(gray)


# -------------------------------------------------------------------------------------------- band

def band_stats(ff, sw, metal, tol=TOL):
    """ff / sw / metal: equal-length RGB byte strings. Returns dict(outside, pixels, pct, max_excess,
    hist[excess 1..9+], worst=(index, ff, sw, metal), mask bytes (255 per outside pixel))."""
    n = len(ff) // 3
    if np is not None:
        a = np.frombuffer(ff, dtype=np.uint8).astype(np.int16).reshape(n, 3)
        b = np.frombuffer(sw, dtype=np.uint8).astype(np.int16).reshape(n, 3)
        m = np.frombuffer(metal, dtype=np.uint8).astype(np.int16).reshape(n, 3)
        lo = np.minimum(a, b) - tol
        hi = np.maximum(a, b) + tol
        exc = np.maximum(np.maximum(lo - m, m - hi), 0).max(axis=1)
        bad = exc > 0
        outside = int(bad.sum())
        max_excess = int(exc.max()) if n else 0
        hist = {}
        if outside:
            h = np.bincount(np.minimum(exc[bad], 9), minlength=10)
            hist = {int(k): int(v) for k, v in enumerate(h) if v and k > 0}
        i = int(exc.argmax()) if outside else -1
        worst = (i, tuple(int(x) for x in a[i]), tuple(int(x) for x in b[i]), tuple(int(x) for x in m[i])) if outside else None
        mask = (bad.astype(np.uint8) * 255).tobytes()
    else:
        outside = 0
        max_excess = 0
        hist = {}
        worst = None
        mask = bytearray(n)
        for i in range(n):
            e = 0
            for c in range(3):
                x, y, z = ff[3 * i + c], sw[3 * i + c], metal[3 * i + c]
                lo = min(x, y) - tol
                hi = max(x, y) + tol
                e = max(e, lo - z, z - hi)
            if e > 0:
                outside += 1
                mask[i] = 255
                hist[min(e, 9)] = hist.get(min(e, 9), 0) + 1
                if e > max_excess:
                    max_excess = e
                    worst = (i, tuple(ff[3 * i:3 * i + 3]), tuple(sw[3 * i:3 * i + 3]), tuple(metal[3 * i:3 * i + 3]))
        mask = bytes(mask)
    return {"outside": outside, "pixels": n, "pct": 100.0 * outside / n if n else 0.0, "max_excess": max_excess,
            "hist": hist, "worst": worst, "mask": mask}


def verdict(st, gate_pct=GATE_PCT, gate_excess=GATE_EXCESS):
    return st["pct"] <= gate_pct and st["max_excess"] <= gate_excess


def shared_frames(dirs):
    sets = []
    for d in dirs:
        fs = set()
        for name in os.listdir(d):
            if name.startswith("g") and name.endswith(".ppm"):
                try:
                    fs.add(int(name[1:-4]))
                except ValueError:
                    pass
        sets.append(fs)
    out = set.intersection(*sets) if sets else set()
    return sorted(out)


def judge_dirs(ff_dir, sw_dir, metal_dir, tol=TOL, gate_pct=GATE_PCT, gate_excess=GATE_EXCESS, artefacts=None,
               label=""):
    frames = shared_frames([ff_dir, sw_dir, metal_dir])
    rows = []
    for fr in frames:
        wf, hf, ff = read_ppm(os.path.join(ff_dir, "g%d.ppm" % fr))
        ws, hs, sw = read_ppm(os.path.join(sw_dir, "g%d.ppm" % fr))
        wm, hm, mt = read_ppm(os.path.join(metal_dir, "g%d.ppm" % fr))
        if (wf, hf) != (ws, hs) or (wf, hf) != (wm, hm):
            rows.append({"frame": fr, "ok": False, "error": "size mismatch ff %dx%d sw %dx%d metal %dx%d" % (wf, hf, ws, hs, wm, hm)})
            continue
        st = band_stats(ff, sw, mt, tol)
        ok = verdict(st, gate_pct, gate_excess)
        row = {"frame": fr, "w": wf, "h": hf, "outside": st["outside"], "pct": round(st["pct"], 4),
               "max_excess": st["max_excess"], "hist": st["hist"], "ok": ok}
        if st["worst"]:
            i, a, b, m = st["worst"]
            row["worst"] = {"x": i % wf, "y": i // wf, "ff": a, "sw": b, "metal": m}
        if artefacts and not ok:
            os.makedirs(artefacts, exist_ok=True)
            write_pgm(os.path.join(artefacts, "outside_%d.pgm" % fr), wf, hf, st["mask"])
        rows.append(row)
    return rows


def print_rows(label, rows, tol, gate_pct, gate_excess):
    print("%s: band tol=%d  gates: outside <= %.2f %%, max excess <= %d" % (label or "frames", tol, gate_pct, gate_excess))
    print("  %6s %9s %9s %8s %10s  %s" % ("frame", "size", "outside", "pct", "max_exc", "verdict / worst"))
    for r in rows:
        if "error" in r:
            print("  %6d %s RED %s" % (r["frame"], " " * 40, r["error"]))
            continue
        w = r.get("worst")
        wtxt = ""
        if w:
            wtxt = " worst=(%d,%d) ff=%s sw=%s metal=%s hist=%s" % (w["x"], w["y"], w["ff"], w["sw"], w["metal"],
                                                                     json.dumps(r["hist"], sort_keys=True))
        print("  %6d %4dx%-4d %9d %7.3f%% %10d  %s%s" % (r["frame"], r["w"], r["h"], r["outside"], r["pct"],
                                                        r["max_excess"], "GREEN" if r["ok"] else "RED", wtxt))


# ---------------------------------------------------------------------------------------------- run

ARMS = (("ff", "gl", []), ("sw", "gl", ["--force-shader-blend"]), ("metal", "metal", []))


def replay(gs_replay, stream, out_dir, backend, extra, frames, step, scale, log):
    os.makedirs(out_dir, exist_ok=True)
    cmd = [gs_replay, "stream", stream, "--backend", backend, "--scale", str(scale), "--quiet", "--warmup", "0",
           "--max-frames", str(frames + 1), "--dump-ppm", out_dir, "--dump-frames", "%d-%d:%d" % (step, frames, step)] + extra
    with open(log, "ab") as lf:
        lf.write(("\n$ " + " ".join(cmd) + "\n").encode())
        lf.flush()
        rc = subprocess.call(cmd, stdout=lf, stderr=subprocess.STDOUT)
    return rc


def run(args):
    os.makedirs(args.artefacts, exist_ok=True)
    summary = {"tol": args.tol, "gate_pct": args.gate_pct, "gate_excess": args.gate_excess, "scale": args.scale,
               "frames": args.frames, "step": args.step, "captures": {}}
    all_ok = True
    for stream in args.stream:
        name = os.path.splitext(os.path.basename(stream))[0]
        cap_dir = os.path.join(args.artefacts, name)
        log = os.path.join(cap_dir, "replay.log")
        os.makedirs(cap_dir, exist_ok=True)
        dirs = {}
        for arm, backend, extra in ARMS:
            d = os.path.join(cap_dir, arm)
            rc = replay(args.gs_replay, stream, d, backend, extra, args.frames, args.step, args.scale, log)
            if rc == SKIP:
                print("%s: gs_replay --backend %s found no device -> SKIP (77)" % (name, backend))
                return SKIP
            if rc != 0:
                print("%s: gs_replay --backend %s %s failed rc=%d (see %s)" % (name, backend, " ".join(extra), rc, log))
                return 1
            dirs[arm] = d
        rows = judge_dirs(dirs["ff"], dirs["sw"], dirs["metal"], args.tol, args.gate_pct, args.gate_excess, cap_dir, name)
        print_rows(name, rows, args.tol, args.gate_pct, args.gate_excess)
        ok = bool(rows) and all(r["ok"] for r in rows)
        if not rows:
            print("  no shared dumped frames (capture shorter than --step?)")
        all_ok = all_ok and ok
        summary["captures"][name] = {"ok": ok, "frames": rows}
    summary["ok"] = all_ok
    with open(os.path.join(args.artefacts, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1, sort_keys=True)
    print("VERDICT: %s" % ("GREEN" if all_ok else "RED"))
    return 0 if all_ok else 1


# ----------------------------------------------------------------------------------------- selftest

def selftest():
    fails = []

    def check(c, msg):
        if not c:
            fails.append(msg)

    w, h = 8, 4
    n = w * h
    base = bytes((i * 7 + c * 50) % 256 for i in range(n) for c in range(3))
    ff = bytearray(base)
    sw = bytearray(base)
    mt = bytearray(base)
    # Identical pictures: nothing outside.
    st = band_stats(bytes(ff), bytes(sw), bytes(mt))
    check(st["outside"] == 0 and st["max_excess"] == 0 and verdict(st), "identical -> 0 outside")
    # The band: ff 100 / sw 110 on one pixel; metal 98, 112 inside (tol 2); 97 / 113 one outside.
    for v, inside in ((98, True), (112, True), (97, False), (113, False), (105, True)):
        ff[0], sw[0], mt[0] = 100, 110, v
        st = band_stats(bytes(ff), bytes(sw), bytes(mt))
        check((st["outside"] == 0) == inside, "band value %d inside=%s" % (v, inside))
        if not inside:
            check(st["max_excess"] == 1 and st["worst"][0] == 0 and st["hist"] == {1: 1}, "excess 1 at pixel 0 (%d)" % v)
            check(st["mask"][0] == 255 and st["mask"][1] == 0, "mask marks the outside pixel only")
    # Excess arithmetic on another channel and the 9+ bucket.
    ff[0], sw[0], mt[0] = base[0], base[0], base[0]
    ff[5], sw[5], mt[5] = 10, 20, 40          # pixel 1, channel 2: hi = 22 -> excess 18
    st = band_stats(bytes(ff), bytes(sw), bytes(mt))
    check(st["outside"] == 1 and st["max_excess"] == 18 and st["hist"] == {9: 1} and st["worst"][0] == 1, "excess 18 -> bucket 9+")
    check(not verdict(st), "max excess 18 is RED")
    # The percentage gate: 1 of 32 pixels = 3.125 % with excess 1 -> RED at 0.3 %, GREEN at 5 %.
    ff[5], sw[5], mt[5] = 10, 20, 23
    st = band_stats(bytes(ff), bytes(sw), bytes(mt))
    check(abs(st["pct"] - 3.125) < 1e-9 and not verdict(st) and verdict(st, 5.0, 8), "pct gate")
    # PPM round trip + judge_dirs over directories with a shared and an unshared frame.
    with tempfile.TemporaryDirectory() as td:
        d = {a: os.path.join(td, a) for a in ("ff", "sw", "metal")}
        for a in d.values():
            os.makedirs(a)
        for fr in (50, 100):
            write_ppm(os.path.join(d["ff"], "g%d.ppm" % fr), w, h, bytes(ff))
            write_ppm(os.path.join(d["sw"], "g%d.ppm" % fr), w, h, bytes(sw))
        write_ppm(os.path.join(d["metal"], "g50.ppm"), w, h, bytes(mt))
        write_ppm(os.path.join(d["metal"], "g150.ppm"), w, h, bytes(mt))
        rw, rh, rgb = read_ppm(os.path.join(d["ff"], "g50.ppm"))
        check((rw, rh) == (w, h) and rgb == bytes(ff), "PPM round trip")
        check(shared_frames(list(d.values())) == [50], "shared frames = intersection")
        art = os.path.join(td, "art")
        rows = judge_dirs(d["ff"], d["sw"], d["metal"], TOL, GATE_PCT, GATE_EXCESS, art, "t")
        check(len(rows) == 1 and rows[0]["frame"] == 50 and not rows[0]["ok"] and rows[0]["worst"] == {"x": 1, "y": 0, "ff": tuple(ff[3:6]), "sw": tuple(sw[3:6]), "metal": tuple(mt[3:6])}, "judge_dirs row")
        check(os.path.exists(os.path.join(art, "outside_50.pgm")), "RED frame writes the outside mask")
        # Size mismatch is RED with an error.
        write_ppm(os.path.join(d["metal"], "g50.ppm"), w, h - 1, bytes(mt[:w * (h - 1) * 3]))
        rows = judge_dirs(d["ff"], d["sw"], d["metal"], TOL, GATE_PCT, GATE_EXCESS, None, "t")
        check(not rows[0]["ok"] and "size mismatch" in rows[0]["error"], "size mismatch is RED")
    # The replay's dump range: lo-hi:step covers lo, lo+step, ... <= hi.
    check([f for f in range(0, 301) if f >= 50 and f <= 300 and (f - 50) % 50 == 0] == [50, 100, 150, 200, 250, 300],
          "step range")
    if fails:
        for f in fails:
            print("FAIL " + f)
        print("judge_metal_identity selftest: %d failures" % len(fails))
        return 1
    print("judge_metal_identity selftest: OK")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    r = sub.add_parser("run")
    r.add_argument("--gs-replay", required=True)
    r.add_argument("--stream", action="append", required=True)
    r.add_argument("--artefacts", required=True)
    r.add_argument("--frames", type=int, default=300)
    r.add_argument("--step", type=int, default=50)
    r.add_argument("--scale", type=int, default=1)
    j = sub.add_parser("judge")
    j.add_argument("--ff", required=True)
    j.add_argument("--sw", required=True)
    j.add_argument("--metal", required=True)
    j.add_argument("--artefacts")
    for p in (r, j):
        p.add_argument("--tol", type=int, default=TOL)
        p.add_argument("--gate-pct", type=float, default=GATE_PCT)
        p.add_argument("--gate-excess", type=int, default=GATE_EXCESS)
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    if args.cmd == "run":
        return run(args)
    if args.cmd == "judge":
        rows = judge_dirs(args.ff, args.sw, args.metal, args.tol, args.gate_pct, args.gate_excess, args.artefacts, "")
        print_rows(args.metal, rows, args.tol, args.gate_pct, args.gate_excess)
        ok = bool(rows) and all(r["ok"] for r in rows)
        print("VERDICT: %s" % ("GREEN" if ok else "RED"))
        return 0 if ok else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
