#!/usr/bin/env python3
"""judge_pacing.py -- the presentation pacing judge of a play run (a locked 60, no doubled frames).

Reads the FrameTimeline record a play leg writes with --harness-out (<out>/present_times.csv,
`lane,ordinal,t_ns`; lanes boundary / output / shown) and judges the SHOWN lane: the presenter's
window draw that showed a NEW picture. A doubled frame is a shown gap of two fields (33.3 ms): the
previous picture stood on screen one field too long. judge_pictures.py asks "does the game PRODUCE
60 pictures per second" (the output lane); this judge asks "does the presenter SHOW every picture it
received one field after the previous" -- the presenter's own cost over the production floor.

  judge_pacing.py judge <out_dir> [--lane shown] [--skip-s S] [--meta <run.meta>]
      The frozen gates on ONE run, RED/GREEN each:
        p99 gap    : p99 of the inter-shown gaps <= 20 ms
        doubled    : at most 1 % of the gaps > 20 ms
        tripled    : no gap > 40 ms
      plus the report: the field histogram (gaps binned to the nearest field: 1 = 16.7 ms, 2 = doubled,
      3+ = tripled), the per-second drop table (doubled frames per wall second: the "fine then drops
      hard" clusters), the production FLOOR (fields in the span minus pictures received: the doubled
      frames NO presenter policy can avoid, from the output lane) and the presenter's own cost above it,
      and with --meta the `frame_cap=` census row (admitted / held / superseded / late / slipped).
      Exit 0 when every gate is GREEN, 1 otherwise.
  judge_pacing.py hist <out_dir> [--lane shown|output|boundary] [--skip-s S]
      The histogram and the drop table alone (any lane), no verdict.
  judge_pacing.py --selftest
"""
import argparse
import csv
import os
import sys

LANES = ("boundary", "output", "shown")
FIELD_MS = 1000.0 / 60.0
GATE_P99_GAP_MS = 20.0
GATE_DOUBLED_SHARE = 0.01  # <= 1 % of the gaps > 20 ms
GATE_TRIPLED_MS = 40.0  # no gap > 40 ms
MAX_FIELD_BIN = 6


def read_lanes(out_dir):
    path = os.path.join(out_dir, "present_times.csv")
    lanes = {lane: [] for lane in LANES}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if row["lane"] in lanes:
                lanes[row["lane"]].append(int(row["t_ns"]))
    for v in lanes.values():
        v.sort()
    return lanes


def pct(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = int(round(p * (len(sorted_values) - 1)))
    return sorted_values[min(max(k, 0), len(sorted_values) - 1)]


def kept_after_skip(instants_ns, t0_ns):
    return [t for t in instants_ns if t >= t0_ns]


def field_bin(gap_ms):
    """A gap binned to the nearest whole field (1 = one field, 2 = a doubled frame ...), capped."""
    b = int(round(gap_ms / FIELD_MS))
    return min(max(b, 0), MAX_FIELD_BIN)


def analyse(lanes, lane="shown", skip_s=5.0):
    """The shown-lane statistics after the skip, plus the production floor from the output lane."""
    series = lanes[lane]
    if len(series) < 2:
        return None
    t0 = series[0] + int(skip_s * 1e9)
    kept = kept_after_skip(series, t0)
    if len(kept) < 2:
        return None
    gaps = [(b - a) / 1e6 for a, b in zip(kept, kept[1:])]
    gs = sorted(gaps)
    hist = {}
    for g in gaps:
        hist[field_bin(g)] = hist.get(field_bin(g), 0) + 1
    over_20 = sum(1 for g in gaps if g > GATE_P99_GAP_MS)
    over_40 = sum(1 for g in gaps if g > GATE_TRIPLED_MS)
    # Per-second drop table: doubled-or-worse gaps attributed to the wall second they END in.
    span_s = (kept[-1] - kept[0]) / 1e9
    n_sec = int(span_s) + 1
    drops = [0] * n_sec
    shown_per_sec = [0] * n_sec
    for a, b in zip(kept, kept[1:]):
        sec = int((b - kept[0]) / 1e9)
        shown_per_sec[sec] += 1
        if (b - a) / 1e6 > GATE_P99_GAP_MS:
            drops[sec] += 1
    # The production floor: the span holds floor(span / field) fields; the output lane delivered N
    # pictures inside it; no policy can show more distinct pictures than it received, so at least
    # fields - pictures fields repeat one (counted once per field, so a tripled frame counts 2).
    # Pictures that could have filled the span's fields: posted after the first shown picture's own
    # field (a picture posted up to one field before the first draw is the one that draw showed) up to
    # the last draw; the first draw consumes one of them, the remaining F slots take the rest.
    out = kept_after_skip(lanes["output"], kept[0] - int(FIELD_MS * 1e6))
    out = [t for t in out if t <= kept[-1]]
    fields_in_span = int(round(span_s * 60.0))
    pictures = len(out)
    floor_doubled = max(fields_in_span - max(pictures - 1, 0), 0)
    extra_fields = sum((b - 1) * n for b, n in hist.items() if b >= 2)  # fields repeated in the shown lane
    return {
        "lane": lane,
        "n": len(kept),
        "span_s": span_s,
        "gaps": len(gs),
        "gap_p50": pct(gs, 0.50),
        "gap_p99": pct(gs, 0.99),
        "gap_max": gs[-1],
        "over_20": over_20,
        "over_20_share": over_20 / len(gs),
        "over_40": over_40,
        "hist": hist,
        "drops_per_sec": drops,
        "shown_per_sec": shown_per_sec,
        "fields_in_span": fields_in_span,
        "pictures_received": pictures,
        "floor_doubled": floor_doubled,
        "repeated_fields": extra_fields,
        "presenter_cost": extra_fields - floor_doubled,
    }


def read_meta_row(meta_path, key):
    if not meta_path or not os.path.exists(meta_path):
        return None
    with open(meta_path) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.strip()[len(key) + 1:]
    return None


def format_hist(a):
    bins = " ".join(f"{b}f={a['hist'].get(b, 0)}" for b in range(1, MAX_FIELD_BIN + 1) if a["hist"].get(b, 0) or b <= 3)
    return (f"lane={a['lane']} n={a['n']} span={a['span_s']:.1f}s | gaps={a['gaps']} p50/p99/max="
            f"{a['gap_p50']:.1f}/{a['gap_p99']:.1f}/{a['gap_max']:.1f} ms >20={a['over_20']} "
            f"({100 * a['over_20_share']:.1f} %) >40={a['over_40']} | fields {bins}")


def format_floor(a):
    return (f"floor: fields={a['fields_in_span']} pictures_received={a['pictures_received']} -> at least "
            f"{a['floor_doubled']} repeated fields unavoidable; shown repeated {a['repeated_fields']} "
            f"(presenter's own cost {a['presenter_cost']:+d})")


def format_drop_table(a, width=30):
    secs = a["drops_per_sec"]
    rows = ["sec : doubled frames (shown pictures)"]
    for i in range(0, len(secs), width):
        chunk = range(i, min(i + width, len(secs)))
        rows.append(" ".join(f"{s:2d}:{secs[s]:d}({a['shown_per_sec'][s]:d})" for s in chunk))
    return "\n".join(rows)


def judge(a):
    return [
        ("shown p99 gap <= 20 ms", f"{a['gap_p99']:.1f}", a["gap_p99"] <= GATE_P99_GAP_MS),
        ("doubled frames (gaps > 20 ms) <= 1 %", f"{a['over_20']} of {a['gaps']} ({100 * a['over_20_share']:.1f} %)",
         a["over_20_share"] <= GATE_DOUBLED_SHARE),
        ("no gap > 40 ms", f"{a['over_40']} (max {a['gap_max']:.1f})", a["over_40"] == 0),
    ]


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
    field = ns // 60
    # A steady 60 Hz shown train for 10 s with the output lane 2 ms ahead: GREEN, floor 0, cost 0.
    shown = [(i * ns) // 60 for i in range(601)]
    output = [t - 2_000_000 for t in shown]
    lanes = {"boundary": output, "output": output, "shown": shown}
    a = analyse(lanes, "shown", skip_s=0.0)
    check(a is not None and a["hist"].get(1) == 600 and a["over_20"] == 0, f"steady hist {a and a['hist']}")
    check(a["floor_doubled"] == 0 and a["presenter_cost"] == 0, f"steady floor {a['floor_doubled']} {a['presenter_cost']}")
    check(all(g for _, _, g in judge(a)), "steady GREEN")
    # The baseline's shape: every 6th shown gap doubled (16 %), pictures produced at 60 -> RED on the
    # doubled share, the floor 0, the presenter's cost = every doubled frame.
    t, bimodal = 0, [0]
    for i in range(600):
        t += 2 * field if i % 6 == 5 else field
        bimodal.append(t)
    output = [i * field - 2_000_000 for i in range(0, 800)]
    lanes = {"boundary": output, "output": output, "shown": bimodal}
    b = analyse(lanes, "shown", skip_s=0.0)
    rows = judge(b)
    check(b["hist"].get(2) == 100 and b["hist"].get(1) == 500, f"bimodal hist {b['hist']}")
    check(not rows[0][2] and not rows[1][2] and rows[2][2], f"bimodal rows {rows}")
    check(b["floor_doubled"] == 0 and b["presenter_cost"] == 100, f"bimodal floor {b['floor_doubled']} cost {b['presenter_cost']}")
    check(sum(b["drops_per_sec"]) == 100 and len(b["drops_per_sec"]) >= 11, f"drop table {b['drops_per_sec']}")
    # Production at 54/s (every 10th picture missing), the presenter perfect (shows each at the next
    # field): the doubled frames equal the floor, the presenter's cost 0; the gates still RED (10 %).
    pictures = [i * field for i in range(600) if i % 10 != 9]
    lanes = {"boundary": pictures, "output": pictures, "shown": pictures}
    c = analyse(lanes, "shown", skip_s=0.0)
    check(c["hist"].get(2) == 59 and c["floor_doubled"] == 59 and c["presenter_cost"] == 0,
          f"floor case hist {c['hist']} floor {c['floor_doubled']} cost {c['presenter_cost']}")
    check(not judge(c)[1][2], "floor case RED on the share")
    # One 50 ms gap trips the tripled gate alone (0.2 % share is within 1 %, p99 still one field).
    t, one = 0, [0]
    for i in range(600):
        t += 3 * field if i == 300 else field
        one.append(t)
    lanes = {"boundary": one, "output": one, "shown": one}
    d = analyse(lanes, "shown", skip_s=0.0)
    rows = judge(d)
    check(rows[0][2] and rows[1][2] and not rows[2][2], f"tripled rows {rows}")
    # Exactly 1 % doubled (6 of 600) passes the share gate, fails p99 (p99 of 600 gaps is the 594th).
    t, six = 0, [0]
    for i in range(600):
        t += 2 * field if i % 100 == 99 else field
        six.append(t)
    lanes = {"boundary": six, "output": six, "shown": six}
    e = analyse(lanes, "shown", skip_s=0.0)
    rows = judge(e)
    check(rows[1][2] and e["over_20"] == 6, f"one-percent rows {rows} {e['over_20']}")
    # The skip drops the first seconds.
    f_ = analyse({"boundary": shown, "output": shown, "shown": shown}, "shown", skip_s=5.0)
    check(f_["n"] == 301, f"skip n {f_['n']}")
    # field_bin rounds to the nearest field and caps.
    check(field_bin(16.7) == 1 and field_bin(33.3) == 2 and field_bin(24.0) == 1 and field_bin(25.5) == 2 and field_bin(500) == MAX_FIELD_BIN,
          "field_bin")
    # The meta row reader.
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".meta", delete=False) as f:
        f.write("frames=10\nframe_cap=cap=60:admitted=1:held=2:superseded=3:late=4\n")
        path = f.name
    check(read_meta_row(path, "frame_cap") == "cap=60:admitted=1:held=2:superseded=3:late=4", "meta row")
    check(read_meta_row(path, "absent") is None and read_meta_row(None, "x") is None, "meta row absent")
    os.unlink(path)
    print("judge_pacing selftest:", "PASS" if fails == 0 else f"{fails} failure(s)")
    return fails == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    for name in ("hist", "judge"):
        p = sub.add_parser(name)
        p.add_argument("out_dir")
        p.add_argument("--lane", default="shown", choices=LANES)
        p.add_argument("--skip-s", type=float, default=5.0)
        p.add_argument("--meta", default=None, help="run.meta: the frame_cap= census row is reported")
    args = ap.parse_args()
    if args.selftest:
        return 0 if selftest() else 1
    if args.cmd in ("hist", "judge"):
        a = analyse(read_lanes(args.out_dir), args.lane, args.skip_s)
        if a is None:
            print(f"{args.out_dir}: lane {args.lane} too short after the skip")
            return 1
        print(f"{args.out_dir} skip={args.skip_s}s: {format_hist(a)}")
        print(format_floor(a))
        print(format_drop_table(a))
        meta = args.meta or os.path.join(args.out_dir, "run.meta")
        row = read_meta_row(meta, "frame_cap")
        if row:
            print(f"frame_cap={row}")
        if args.cmd == "hist":
            return 0
        return 0 if print_judge(judge(a)) else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
