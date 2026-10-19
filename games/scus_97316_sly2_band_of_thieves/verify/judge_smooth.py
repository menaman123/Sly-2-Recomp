#!/usr/bin/env python3
"""judge_smooth.py -- the presented-frame STEADINESS judge of a play run (not the p50).

Reads the FrameTimeline record a play leg writes with --harness-out: <out>/present_times.csv
(`lane,ordinal,t_ns`; lanes boundary = the EE frame boundary / game tick, output = the GS consumer's
present of a new picture, shown = the window draw that showed a new picture -- what the player sees).

  judge_smooth.py hist <out_dir> [--lane shown|output|boundary] [--skip N]
      The inter-frame gap histogram of one run: n, p50, p90, p99, max (ms), gaps > 33 ms, > 50 ms,
      the FIELD MIXTURE (gaps binned to 60 Hz fields: 1 / 2 / 3 / 4 / 5+ fields -- the cadence a
      player sees; a steady train is one bin) and the 2 ms bins up to 100 ms (`--skip N` drops the
      first N gaps: the state load / first frames). The mixture is load-independent within a run:
      compare a run's shown lane with its own output lane.
  judge_smooth.py judge --main <out_dir> --after <out_dir> [--cap30 <out_dir>] [--lane shown] [--skip N]
      The frozen round-H gates, RED/GREEN each:
        cap off : p99(after) <= 0.75 * p99(main)  AND  over_50(after) <= 0.5 * over_50(main)
        cap 30  : p99(cap30) <= 36 ms  AND  over_50(cap30) == 0        (only with --cap30)
      Exit 0 when every gate given is GREEN, 1 otherwise.
  judge_smooth.py --selftest
"""
import argparse
import csv
import os
import sys

LANES = ("boundary", "output", "shown")


def read_lanes(out_dir):
    path = os.path.join(out_dir, "present_times.csv")
    lanes = {k: [] for k in LANES}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if row["lane"] in lanes:
                lanes[row["lane"]].append(int(row["t_ns"]))
    for k in lanes:
        lanes[k].sort()
    return lanes


def gaps_ms(instants, skip=0):
    g = [(b - a) / 1e6 for a, b in zip(instants, instants[1:])]
    return g[skip:]


def pct(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = int(round(p * (len(sorted_values) - 1)))
    return sorted_values[min(max(k, 0), len(sorted_values) - 1)]


def stats(gaps):
    s = sorted(gaps)
    return {
        "n": len(s),
        "p50": pct(s, 0.50),
        "p90": pct(s, 0.90),
        "p99": pct(s, 0.99),
        "max": s[-1] if s else 0.0,
        "over_33": sum(1 for g in s if g > 33.0),
        "over_50": sum(1 for g in s if g > 50.0),
        "mean": (sum(s) / len(s)) if s else 0.0,
    }


def histogram(gaps, bin_ms=2.0, top_ms=100.0):
    bins = {}
    for g in gaps:
        b = min(int(g // bin_ms) * bin_ms, top_ms)
        bins[b] = bins.get(b, 0) + 1
    return sorted(bins.items())


FIELD_MS = 1000.0 / 60.0


def field_mixture(gaps):
    """Counts of gaps by nearest field count (1..4, 5+) -- the cadence mixture."""
    bins = {1: 0, 2: 0, 3: 0, 4: 0, 5: 0}
    for g in gaps:
        f = max(1, int(round(g / FIELD_MS)))
        bins[min(f, 5)] += 1
    return bins


def format_mixture(label, gaps):
    m = field_mixture(gaps)
    n = max(len(gaps), 1)
    return "%-10s fields 1:%d (%.0f%%) 2:%d (%.0f%%) 3:%d (%.0f%%) 4:%d (%.0f%%) 5+:%d (%.0f%%)" % (
        label, m[1], 100.0 * m[1] / n, m[2], 100.0 * m[2] / n, m[3], 100.0 * m[3] / n, m[4], 100.0 * m[4] / n,
        m[5], 100.0 * m[5] / n)


def format_stats(label, st):
    return ("%-10s n=%d p50=%.1f p90=%.1f p99=%.1f max=%.1f mean=%.1f >33ms=%d >50ms=%d"
            % (label, st["n"], st["p50"], st["p90"], st["p99"], st["max"], st["mean"], st["over_33"], st["over_50"]))


def cmd_hist(args):
    lanes = read_lanes(args.out_dir)
    for lane in (LANES if args.lane == "all" else (args.lane,)):
        g = gaps_ms(lanes[lane], args.skip)
        print(format_stats(lane, stats(g)))
        print(format_mixture(lane, g))
        if lane == args.lane:
            total = max(len(g), 1)
            for b, n in histogram(g):
                bar = "#" * min(60, int(60 * n / total + 0.5))
                print("  %5.0f-%3.0f ms %5d %s" % (b, b + 2 if b < 100 else 999, n, bar))
    return 0


def judge(main_st, after_st, cap30_st=None):
    rows = []
    ok_p99 = after_st["p99"] <= 0.75 * main_st["p99"]
    ok_50 = after_st["over_50"] <= 0.5 * main_st["over_50"]
    rows.append(("cap off p99 <= 0.75 x main", "%.1f vs %.1f (limit %.1f)" % (after_st["p99"], main_st["p99"], 0.75 * main_st["p99"]), ok_p99))
    rows.append(("cap off >50 ms <= 0.5 x main", "%d vs %d (limit %d)" % (after_st["over_50"], main_st["over_50"], main_st["over_50"] // 2), ok_50))
    if cap30_st is not None:
        rows.append(("cap 30 p99 <= 36 ms", "%.1f" % cap30_st["p99"], cap30_st["p99"] <= 36.0))
        rows.append(("cap 30 >50 ms == 0", "%d" % cap30_st["over_50"], cap30_st["over_50"] == 0))
    return rows


def cmd_judge(args):
    main_st = stats(gaps_ms(read_lanes(args.main)[args.lane], args.skip))
    after_st = stats(gaps_ms(read_lanes(args.after)[args.lane], args.skip))
    cap30_st = stats(gaps_ms(read_lanes(args.cap30)[args.lane], args.skip)) if args.cap30 else None
    print(format_stats("main", main_st))
    print(format_stats("after", after_st))
    if cap30_st:
        print(format_stats("cap30", cap30_st))
    rows = judge(main_st, after_st, cap30_st)
    all_ok = True
    for name, detail, ok in rows:
        print("%-32s %-40s %s" % (name, detail, "GREEN" if ok else "RED"))
        all_ok = all_ok and ok
    print("VERDICT", "GREEN" if all_ok else "RED")
    return 0 if all_ok else 1


def selftest():
    # A steady 60 Hz train and a hitching one; the gates must separate them.
    steady = [i * 16_666_667 for i in range(601)]
    hitch = []
    t = 0
    for i in range(600):
        hitch.append(t)
        t += 16_666_667 if i % 10 else 90_000_000
    hitch.append(t)
    st_s = stats(gaps_ms(steady))
    st_h = stats(gaps_ms(hitch))
    assert abs(st_s["p50"] - 16.67) < 0.01 and st_s["over_33"] == 0 and st_s["over_50"] == 0, st_s
    assert st_h["over_50"] == 60 and st_h["p99"] >= 89.9, st_h
    rows = judge(st_h, st_s, st_s)
    assert all(ok for _, _, ok in rows), rows
    rows = judge(st_h, st_h, st_h)
    assert not any(ok for _, _, ok in rows), rows
    # A 30 Hz train (every other field) passes the cap-30 gate; one 51 ms gap fails it.
    thirty = [i * 33_333_333 for i in range(301)]
    st_30 = stats(gaps_ms(thirty))
    assert judge(st_h, st_s, st_30)[2][2] and judge(st_h, st_s, st_30)[3][2]
    thirty[150] += 20_000_000
    st_30b = stats(gaps_ms(sorted(thirty)))
    assert not judge(st_h, st_s, st_30b)[3][2]
    # The histogram bins hold every gap once; the field mixture too, 60 Hz = every gap one field,
    # a 30 Hz train every gap two fields, the hitching train 540 one-field + 60 five-plus.
    assert sum(n for _, n in histogram(gaps_ms(hitch))) == 600
    assert field_mixture(gaps_ms(steady)) == {1: 600, 2: 0, 3: 0, 4: 0, 5: 0}
    assert field_mixture(gaps_ms([i * 33_333_333 for i in range(301)])) == {1: 0, 2: 300, 3: 0, 4: 0, 5: 0}
    assert field_mixture(gaps_ms(hitch)) == {1: 540, 2: 0, 3: 0, 4: 0, 5: 60}
    # --skip drops the leading gaps.
    assert len(gaps_ms(hitch, 10)) == 590
    print("judge_smooth selftest: PASS")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hist")
    h.add_argument("out_dir")
    h.add_argument("--lane", default="all", choices=LANES + ("all",))
    h.add_argument("--skip", type=int, default=0)
    j = sub.add_parser("judge")
    j.add_argument("--main", required=True)
    j.add_argument("--after", required=True)
    j.add_argument("--cap30")
    j.add_argument("--lane", default="shown", choices=LANES)
    j.add_argument("--skip", type=int, default=0)
    args = ap.parse_args(argv)
    return cmd_hist(args) if args.cmd == "hist" else cmd_judge(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
