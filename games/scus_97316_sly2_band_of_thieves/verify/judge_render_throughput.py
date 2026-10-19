#!/usr/bin/env python3
"""judge_render_throughput.py -- the RENDERER THROUGHPUT judge (round P phase B): the GS consumer ALONE.

The input is the per-frame CSV `tools/gs_replay stream` writes while replaying a captured consumer stream
(engine/gs/hle/capture `.gsstream`: a Paris play run's draws + vertices, jobs with upload pixels, frames with
the CRTC snapshot, the EE image's local-memory writes) through DrawIrQueueConsumer -> DrawPipeline -> GlDevice
with no producer, no pacing and no frame skip: `frame,ms,draws,jobs,hash,grabbed,readbacks,copies,draw_calls`,
`ms` = the consumer thread's wall time from the previous frame boundary's return to this one's.

  judge_render_throughput.py hist <frames.csv> [--warmup N]
      the per-frame ms distribution (p10 / p50 / p90 / p99 / max), fps (mean and 1000 / p50 ms), the frames
      over the 12 ms bar, the draws per frame, the backend census per frame.
  judge_render_throughput.py judge <scale1.csv> [<scale2.csv>] [--warmup N]
      The frozen phase-B gates, RED/GREEN each (the first N frames excluded: the state convergence):
        fps scale 1 : 1000 / p50(ms) >= 160            (one 512x448 Paris frame in <= 6.25 ms)
        fps scale 2 : 1000 / p50(ms) >= 120            (with the second CSV: the 2x internal scale replay)
        12 ms bar   : no measured frame > 12 ms        (every CSV given)
      Exit 0 when every gate is GREEN, 1 otherwise. RED on main by construction (~35-40 fps at scale 1).
  judge_render_throughput.py identity <a.csv> <b.csv>
      the frames-lane hash sequence of two replays compared frame by frame (the same capture through two
      binaries, or two scales of one binary are NOT comparable -- the hash is at the replay's scale): prints
      the first differing frame; exit 0 when identical.
  judge_render_throughput.py --selftest
"""
import argparse
import csv
import os
import sys
import tempfile

GATE_FPS_SCALE1 = 160.0
GATE_FPS_SCALE2 = 120.0
GATE_FRAME_BAR_MS = 12.0
DEFAULT_WARMUP = 30


def read_frames(path):
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            rows.append({"frame": int(row["frame"]), "ms": float(row["ms"]), "draws": int(row["draws"]),
                         "hash": row["hash"], "readbacks": int(row.get("readbacks", 0) or 0),
                         "copies": int(row.get("copies", 0) or 0), "draw_calls": int(row.get("draw_calls", 0) or 0)})
    return rows


def pct(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = int(round(p * (len(sorted_values) - 1)))
    return sorted_values[min(max(k, 0), len(sorted_values) - 1)]


def analyse(rows, warmup=DEFAULT_WARMUP):
    meas = rows[warmup:] if len(rows) > warmup else []
    ms = sorted(r["ms"] for r in meas)
    n = len(ms)
    total = sum(ms)
    a = {"frames": len(rows), "measured": n, "warmup": min(warmup, len(rows))}
    a["p10"], a["p50"], a["p90"], a["p99"] = pct(ms, 0.10), pct(ms, 0.50), pct(ms, 0.90), pct(ms, 0.99)
    a["max"] = ms[-1] if ms else 0.0
    a["fps_mean"] = 1000.0 * n / total if total > 0 else 0.0
    a["fps_p50"] = 1000.0 / a["p50"] if a["p50"] > 0 else 0.0
    a["over_bar"] = sum(1 for m in ms if m > GATE_FRAME_BAR_MS)
    a["draws_per_frame"] = sum(r["draws"] for r in meas) / n if n else 0.0
    a["readbacks_per_frame"] = sum(r["readbacks"] for r in meas) / n if n else 0.0
    a["copies_per_frame"] = sum(r["copies"] for r in meas) / n if n else 0.0
    a["draw_calls_per_frame"] = sum(r["draw_calls"] for r in meas) / n if n else 0.0
    return a


def format_hist(a):
    return (f"frames={a['frames']} warmup={a['warmup']} measured={a['measured']} ms p10/p50/p90/p99/max="
            f"{a['p10']:.2f}/{a['p50']:.2f}/{a['p90']:.2f}/{a['p99']:.2f}/{a['max']:.2f} fps_mean={a['fps_mean']:.1f} "
            f"fps_p50={a['fps_p50']:.1f} over_{GATE_FRAME_BAR_MS:g}ms={a['over_bar']} draws/frame={a['draws_per_frame']:.0f} "
            f"readbacks/frame={a['readbacks_per_frame']:.1f} copies/frame={a['copies_per_frame']:.1f} "
            f"draw_calls/frame={a['draw_calls_per_frame']:.0f}")


def judge(a1, a2=None):
    rows = [("fps scale 1 (1000 / p50 ms >= %g)" % GATE_FPS_SCALE1, "%.1f (p50 %.2f ms)" % (a1["fps_p50"], a1["p50"]),
             a1["fps_p50"] >= GATE_FPS_SCALE1),
            ("12 ms bar scale 1 (frames > %g ms == 0)" % GATE_FRAME_BAR_MS, "%d of %d (max %.2f ms)" % (a1["over_bar"], a1["measured"], a1["max"]),
             a1["over_bar"] == 0 and a1["measured"] > 0)]
    if a2 is not None:
        rows.append(("fps scale 2 (1000 / p50 ms >= %g)" % GATE_FPS_SCALE2, "%.1f (p50 %.2f ms)" % (a2["fps_p50"], a2["p50"]),
                     a2["fps_p50"] >= GATE_FPS_SCALE2))
        rows.append(("12 ms bar scale 2 (frames > %g ms == 0)" % GATE_FRAME_BAR_MS, "%d of %d (max %.2f ms)" % (a2["over_bar"], a2["measured"], a2["max"]),
                     a2["over_bar"] == 0 and a2["measured"] > 0))
    return rows


def print_judge(rows):
    print("| gate | value | verdict |")
    print("|---|---|---|")
    for name, value, green in rows:
        print(f"| {name} | {value} | {'GREEN' if green else 'RED'} |")
    ok = all(g for _, _, g in rows)
    print("ALL GREEN" if ok else "RED")
    return 0 if ok else 1


def identity(rows_a, rows_b):
    n = min(len(rows_a), len(rows_b))
    for i in range(n):
        if rows_a[i]["hash"] != rows_b[i]["hash"]:
            print(f"IDENTITY: frame {i} differs ({rows_a[i]['hash']} vs {rows_b[i]['hash']}); {i} identical before it")
            return 1
    if len(rows_a) != len(rows_b):
        print(f"IDENTITY: frame counts differ ({len(rows_a)} vs {len(rows_b)}); the common {n} frames identical")
        return 1
    print(f"IDENTITY: {n} frames identical")
    return 0


def write_csv(path, ms_list):
    with open(path, "w", newline="") as f:
        f.write("frame,ms,draws,jobs,hash,grabbed,readbacks,copies,draw_calls\n")
        for i, m in enumerate(ms_list):
            f.write(f"{i},{m:.4f},4000,20,{i:016x},1,3,40,4100\n")


def selftest():
    failures = []

    def check(cond, what):
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as d:
        # Main's shape: 29 ms frames -> 34 fps, every frame over the bar -> RED.
        main_csv = os.path.join(d, "main.csv")
        write_csv(main_csv, [40.0] * 30 + [29.0 + (i % 5) * 0.5 for i in range(300)])
        a = analyse(read_frames(main_csv))
        check(a["measured"] == 300 and 33 < a["fps_p50"] < 36, "main-like p50 ~34 fps (%.1f)" % a["fps_p50"])
        check(not all(g for _, _, g in judge(a)), "main-like judge RED")
        # The target: 5.5 ms frames -> 182 fps, none over 12 ms -> GREEN; scale 2 at 7.5 ms -> 133 fps GREEN.
        s1 = os.path.join(d, "s1.csv")
        write_csv(s1, [30.0] * 30 + [5.5 + (i % 7) * 0.1 for i in range(600)])
        s2 = os.path.join(d, "s2.csv")
        write_csv(s2, [30.0] * 30 + [7.5 + (i % 7) * 0.1 for i in range(600)])
        a1, a2 = analyse(read_frames(s1)), analyse(read_frames(s2))
        check(all(g for _, _, g in judge(a1, a2)), "target judge GREEN at both scales")
        check(a1["warmup"] == 30 and a1["over_bar"] == 0, "warmup frames excluded from the bar")
        # One 13 ms spike in the measured window -> the bar gate RED, the fps gate GREEN.
        s3 = os.path.join(d, "s3.csv")
        write_csv(s3, [30.0] * 30 + [5.5] * 299 + [13.0])
        rows = judge(analyse(read_frames(s3)))
        check(rows[0][2] and not rows[1][2], "a 13 ms frame fails the bar alone")
        # Identity: the same CSV twice is identical; a changed hash is reported at its frame.
        check(identity(read_frames(s1), read_frames(s1)) == 0, "identity of a replay with itself")
        rows_b = read_frames(s1)
        rows_b[100]["hash"] = "ffff"
        check(identity(read_frames(s1), rows_b) == 1, "a differing frame 100 is reported")
    print("SELFTEST", "PASS" if not failures else "FAIL (%d)" % len(failures))
    return 0 if not failures else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("cmd", nargs="?", choices=["hist", "judge", "identity"])
    ap.add_argument("inputs", nargs="*")
    ap.add_argument("--warmup", type=int, default=DEFAULT_WARMUP)
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.cmd or not args.inputs:
        ap.print_help()
        return 2
    if args.cmd == "hist":
        for p in args.inputs:
            print(os.path.basename(p) + ": " + format_hist(analyse(read_frames(p), args.warmup)))
        return 0
    if args.cmd == "identity":
        if len(args.inputs) != 2:
            ap.print_help()
            return 2
        return identity(read_frames(args.inputs[0]), read_frames(args.inputs[1]))
    a1 = analyse(read_frames(args.inputs[0]), args.warmup)
    a2 = analyse(read_frames(args.inputs[1]), args.warmup) if len(args.inputs) > 1 else None
    print("scale 1: " + format_hist(a1))
    if a2 is not None:
        print("scale 2: " + format_hist(a2))
    return print_judge(judge(a1, a2))


if __name__ == "__main__":
    sys.exit(main())
