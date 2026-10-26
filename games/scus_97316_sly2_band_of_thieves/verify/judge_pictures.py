#!/usr/bin/env python3
"""judge_pictures.py -- the pictures-per-second judge of a play run (a steady 60).

A "picture" is a unique game-produced frame: the GS consumer's present of a NEW picture (the `output`
lane of the FrameTimeline record a play leg writes with --harness-out: <out>/present_times.csv,
`lane,ordinal,t_ns`; lanes boundary / output / shown). At most 60 per second exist (one per field).

  judge_pictures.py hist <out_dir> [--lane output|boundary|shown] [--skip-s S] [--window-s W]
      pictures/s per wall window (W s, default 1; the first S s skipped: the state load / first
      frames), the window histogram (p10 / p50 / p90 / min / max), the inter-picture gap statistics
      (p50 / p99 / max ms), the gaps > 33 ms, > 50 ms and the THREE-FIELD BEATS (gaps of 50-66.7 ms:
      a picture that stood three fields -- the clock contract's catch-up frame).
  judge_pictures.py judge <out_dir> [--lane output] [--skip-s S] [--clock <run.meta>]
      The frozen round-P gates on ONE run, RED/GREEN each:
        pictures  : p50 of the per-second picture count >= 59
        p99 frame : p99 of the inter-picture gaps <= 20 ms
        beats     : no gap in [50, 66.7) ms (no three-field beat)
        clock     : with --clock <run.meta>: `clock=` row logic_hz within 60.0 +- 0.1, slow_motion == 0
                    (the game ran at real time: the INVARIANT; catchup_ticks is reported, and is 0 only
                    when every frame fitted one field)
      Exit 0 when every gate is GREEN, 1 otherwise. RED on main by construction (23-28 pictures/s).
  judge_pictures.py --selftest
"""
import argparse
import csv
import os
import sys

LANES = ("boundary", "output", "shown")
FIELD_MS = 1000.0 / 60.0
GATE_P50_PICTURES = 59.0
GATE_P99_GAP_MS = 20.0
BEAT_LO_MS = 50.0
BEAT_HI_MS = 4 * FIELD_MS  # 66.7: a picture that stood three (or more, up to four) fields
GATE_LOGIC_HZ = 60.0
GATE_LOGIC_TOL = 0.1


def read_lane(out_dir, lane):
    path = os.path.join(out_dir, "present_times.csv")
    instants = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if row["lane"] == lane:
                instants.append(int(row["t_ns"]))
    instants.sort()
    return instants


def pct(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = int(round(p * (len(sorted_values) - 1)))
    return sorted_values[min(max(k, 0), len(sorted_values) - 1)]


def windows(instants_ns, skip_s=0.0, window_s=1.0):
    """Pictures per wall window over [t0 + skip, t_last), whole windows only."""
    if len(instants_ns) < 2:
        return []
    t0 = instants_ns[0] + int(skip_s * 1e9)
    span = int(window_s * 1e9)
    n_windows = (instants_ns[-1] - t0) // span
    counts = [0] * max(n_windows, 0)
    for t in instants_ns:
        if t < t0:
            continue
        k = (t - t0) // span
        if k < n_windows:
            counts[k] += 1
    return counts


def gaps_ms(instants_ns, skip_s=0.0):
    t0 = instants_ns[0] + int(skip_s * 1e9) if instants_ns else 0
    kept = [t for t in instants_ns if t >= t0]
    return [(b - a) / 1e6 for a, b in zip(kept, kept[1:])]


def analyse(instants_ns, skip_s=0.0, window_s=1.0):
    w = sorted(windows(instants_ns, skip_s, window_s))
    g = gaps_ms(instants_ns, skip_s)
    gs = sorted(g)
    beats = sum(1 for x in g if BEAT_LO_MS <= x < BEAT_HI_MS)
    return {
        "windows": len(w),
        "pps_p10": pct(w, 0.10),
        "pps_p50": pct(w, 0.50),
        "pps_p90": pct(w, 0.90),
        "pps_min": w[0] if w else 0,
        "pps_max": w[-1] if w else 0,
        "gaps": len(gs),
        "gap_p50": pct(gs, 0.50),
        "gap_p99": pct(gs, 0.99),
        "gap_max": gs[-1] if gs else 0.0,
        "over_33": sum(1 for x in g if x > 33.0),
        "over_50": sum(1 for x in g if x > 50.0),
        "beats": beats,
    }


def read_clock_row(meta_path):
    """The `clock=` row of a run.meta as a dict (logic_hz float, counters int); None without it."""
    with open(meta_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("clock="):
                out = {}
                for item in line[len("clock="):].split(":"):
                    if "=" not in item:
                        continue
                    k, v = item.split("=", 1)
                    try:
                        out[k] = float(v) if "." in v else int(v.split("/")[0])
                    except ValueError:
                        out[k] = v
                return out
    return None


def format_hist(a):
    return (f"windows={a['windows']} pictures/s p10/p50/p90={a['pps_p10']}/{a['pps_p50']}/{a['pps_p90']} "
            f"min/max={a['pps_min']}/{a['pps_max']} | gaps={a['gaps']} p50/p99/max={a['gap_p50']:.1f}/"
            f"{a['gap_p99']:.1f}/{a['gap_max']:.1f} ms >33={a['over_33']} >50={a['over_50']} beats(50-66.7ms)={a['beats']}")


def judge(a, clock=None):
    rows = [
        ("pictures/s p50 >= 59", f"{a['pps_p50']}", a["pps_p50"] >= GATE_P50_PICTURES),
        ("p99 gap <= 20 ms", f"{a['gap_p99']:.1f}", a["gap_p99"] <= GATE_P99_GAP_MS),
        ("no three-field beat (50-66.7 ms)", f"{a['beats']}", a["beats"] == 0),
    ]
    if clock is not None:
        hz = clock.get("logic_hz", 0.0)
        slow = clock.get("slow_motion", 1)
        rows.append((f"clock logic_hz = 60.0 +- {GATE_LOGIC_TOL}", f"{hz:.2f} (catchup_ticks={clock.get('catchup_ticks', '?')})",
                     abs(float(hz) - GATE_LOGIC_HZ) <= GATE_LOGIC_TOL))
        rows.append(("clock slow_motion == 0", f"{slow}", int(slow) == 0))
    return rows


def print_judge(rows):
    print("| gate | value | verdict |")
    print("|---|---|---|")
    ok = True
    for name, value, green in rows:
        print(f"| {name} | {value} | {'GREEN' if green else 'RED'} |")
        ok = ok and green
    print("ALL GREEN" if ok else "RED")
    return ok


def selftest():
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            print("FAIL", what)
            fails += 1

    ns = int(1e9)
    # A steady 60 Hz train for 10 s: 60 pictures per window, 16.7 ms gaps, no beats -> GREEN.
    steady = [i * ns // 60 for i in range(601)]
    a = analyse(steady, skip_s=0.0)
    check(a["windows"] == 10 and a["pps_p50"] == 60, f"steady windows {a}")
    check(abs(a["gap_p99"] - FIELD_MS) < 0.1 and a["beats"] == 0, f"steady gaps {a}")
    check(all(g for _, _, g in judge(a)), "steady judge GREEN")
    # Main's shape: 36 ms frames with a 60 ms frame every 7th -> ~26 pictures/s, beats present -> RED.
    t, mainish = 0, [0]
    for i in range(400):
        t += int((60 if i % 7 == 6 else 36) * 1e6)
        mainish.append(t)
    b = analyse(mainish)
    check(b["pps_p50"] <= 28 and b["beats"] > 0, f"main-like {b}")
    check(not all(g for _, _, g in judge(b)), "main-like judge RED")
    # Exactly 59 per second with one two-field gap per second: p50 59 GREEN, p99 33.3 > 20 RED.
    t, fiftynine = 0, [0]
    for i in range(600):
        t += int((2 * FIELD_MS if i % 60 == 0 else FIELD_MS) * 1e6)
        fiftynine.append(t)
    c = analyse(fiftynine)
    rows = judge(c)
    check(rows[0][2] and not rows[1][2] and rows[2][2], f"59-with-doubles rows {rows} {c}")
    # The skip drops the first seconds.
    d = analyse(steady, skip_s=5.0)
    check(d["windows"] == 5, f"skip windows {d['windows']}")
    # The clock row parse and gates.
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".meta", delete=False) as f:
        f.write("frames=10\nclock=logic_hz=60.03:logic_hz_min=59:logic_hz_max=61:frame_hz=59.8:windows=50/55:catchup_ticks=0:catchup_fields=0:slow_motion=0\n")
        path = f.name
    clock = read_clock_row(path)
    os.unlink(path)
    check(clock is not None and abs(clock["logic_hz"] - 60.03) < 1e-9 and clock["slow_motion"] == 0 and clock["windows"] == 50, f"clock row {clock}")
    rows = judge(a, clock)
    check(len(rows) == 5 and rows[3][2] and rows[4][2], f"clock gates {rows}")
    rows = judge(a, {"logic_hz": 59.5, "slow_motion": 3, "catchup_ticks": 0})
    check(not rows[3][2] and not rows[4][2], "clock gates RED on a slow run")
    print("judge_pictures selftest:", "PASS" if fails == 0 else f"{fails} failure(s)")
    return fails == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    h = sub.add_parser("hist")
    h.add_argument("out_dir")
    h.add_argument("--lane", default="output", choices=LANES)
    h.add_argument("--skip-s", type=float, default=5.0)
    h.add_argument("--window-s", type=float, default=1.0)
    j = sub.add_parser("judge")
    j.add_argument("out_dir")
    j.add_argument("--lane", default="output", choices=LANES)
    j.add_argument("--skip-s", type=float, default=5.0)
    j.add_argument("--clock", default=None, help="run.meta with the clock= row (the invariant gates)")
    args = ap.parse_args()
    if args.selftest:
        return 0 if selftest() else 1
    if args.cmd == "hist":
        a = analyse(read_lane(args.out_dir, args.lane), args.skip_s, args.window_s)
        print(f"{args.out_dir} lane={args.lane} skip={args.skip_s}s: {format_hist(a)}")
        return 0
    if args.cmd == "judge":
        a = analyse(read_lane(args.out_dir, args.lane), args.skip_s, 1.0)
        print(f"{args.out_dir} lane={args.lane} skip={args.skip_s}s: {format_hist(a)}")
        clock = read_clock_row(args.clock) if args.clock else None
        if args.clock and clock is None:
            print(f"no clock= row in {args.clock}")
        return 0 if print_judge(judge(a, clock)) else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
