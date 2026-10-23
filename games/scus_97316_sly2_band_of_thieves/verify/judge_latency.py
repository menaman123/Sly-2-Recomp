#!/usr/bin/env python3
"""judge_latency.py -- the INPUT -> PHOTON latency judge of a play run (round 5: "super laggy, heavy to move").

Reads the FrameTimeline record a play leg writes with --harness-out (<out>/present_times.csv,
`lane,ordinal,t_ns,frame`; lanes input / pad / boundary / output / shown, every instant tagged with its EE
frame ordinal) and rebuilds the latency budget of a button press, stage by stage, matched by tag:

  input_to_pad     input(N)  -> pad(N)        the edge's arrival on the host -> the boundary's pad sample that
                                              first carried it (the pad is sampled once per boundary: 0..1 field)
  pad_to_output    pad(N)    -> output(N+1)   the game's tick + the chain behind the EE (worker run-ahead, drain,
                                              consumer, present capture) to the FIRST picture whose boundary
                                              followed the sample
  output_to_shown  output(N) -> shown(N)      the presenter (the FIFO + the 60 Hz admission edge + the draw)
  input_to_shown   input(N)  -> shown(N+1)    the HOST's budget end to end: the edge's wait for the sample, the
                                              game tick to its boundary, the chain behind the EE, the presenter
  total            input_to_shown + GAME_DEPTH_EXTRA_FIELDS: plus the game's own display depth beyond that
                   first picture (the title reacts to pad sample N in the picture tagged N+5: START's pause menu;
                   N+6 for the stick / CROSS -- the lockstep hash-divergence experiment of the latency REPORT,
                   a property of the title the console pays too) -- the fields a player waits from the press to
                   the window swap.

The frozen gates (fields of 16.67 ms) are on the HOST's budget: input_to_shown p50 <= 3 fields, p99 <= 4 fields
(the game's depth is not the host's to cut; it is reported in the total). The report shows every stage in ms and
fields (p50 / p99 / max, matched / missing) so a RED names its stage; with --meta the run.meta `latency=` row
(the in-process census of the same stages: must agree) and the `frame_cap=` row.
No input edges in the record (edges=0: the run had no pulse source and no player) -> NO DATA on the input
stages, the chain stages still reported, exit 2.

  judge_latency.py judge <out_dir> [--skip-s S] [--meta <run.meta>] [--extra-fields K]
  judge_latency.py --selftest
"""
import argparse
import csv
import os
import sys

FIELD_MS = 1000.0 / 60.0
GATE_TOTAL_P50_FIELDS = 3.0
GATE_TOTAL_P99_FIELDS = 4.0
# The game's extra display depth beyond "the first picture whose boundary followed the sample" (tag N+1).
# Pinned by the lockstep hash-divergence experiment of the latency REPORT (Paris hub_ep1_fix, UNPACED +
# vu1_lockstep, frame dumps byte-equal up to the divergence): START at pad stamp S first changes the picture
# tagged S+5 (the pause menu fades in), the left stick / CROSS the picture tagged S+6 -> 4 extra fields (5 for
# movement). Informational (the total row); --extra-fields overrides for another title state.
GAME_DEPTH_EXTRA_FIELDS = 4

LANES = ("input", "pad", "boundary", "output", "shown")
STAGES = (
    # name, from lane, to lane, tag offset
    ("input_to_pad", "input", "pad", 0),
    ("pad_to_output", "pad", "output", 1),
    ("output_to_shown", "output", "shown", 0),
    ("input_to_shown", "input", "shown", 1),
    ("boundary_to_output", "boundary", "output", 0),
    ("boundary_to_shown", "boundary", "shown", 0),
)


def read_marks(out_dir):
    """lane -> list of (t_ns, frame) in file order."""
    path = os.path.join(out_dir, "present_times.csv")
    lanes = {lane: [] for lane in LANES}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            if row["lane"] in lanes:
                lanes[row["lane"]].append((int(row["t_ns"]), int(row.get("frame") or 0)))
    return lanes


def pct(sorted_values, p):
    if not sorted_values:
        return 0.0
    k = int(round(p * (len(sorted_values) - 1)))
    return sorted_values[min(max(k, 0), len(sorted_values) - 1)]


def stage(lanes, src, dst, offset, t0_ns=0):
    """The per-mark latency src(N) -> the FIRST dst mark tagged N + offset (ms), marks at/after t0 only."""
    first = {}
    for t, frame in lanes[dst]:
        if frame and frame not in first:
            first[frame] = t
    d, missing = [], 0
    for t, frame in lanes[src]:
        if not frame or t < t0_ns:
            continue
        at = first.get(frame + offset)
        if at is None or at < t:
            missing += 1
            continue
        d.append((at - t) / 1e6)
    d.sort()
    return {"matched": len(d), "missing": missing, "p50": pct(d, 0.50), "p99": pct(d, 0.99),
            "max": d[-1] if d else 0.0}


def analyse(lanes, skip_s=5.0, extra_fields=GAME_DEPTH_EXTRA_FIELDS):
    t_first = min((m[0][0] for m in lanes.values() if m), default=0)
    t0 = t_first + int(skip_s * 1e9)
    a = {name: stage(lanes, s, d, off, t0) for name, s, d, off in STAGES}
    a["edges"] = sum(1 for t, f in lanes["input"] if f and t >= t0)
    e2e = a["input_to_shown"]
    a["total"] = {"matched": e2e["matched"], "missing": e2e["missing"],
                  "p50": e2e["p50"] + extra_fields * FIELD_MS, "p99": e2e["p99"] + extra_fields * FIELD_MS,
                  "max": e2e["max"] + extra_fields * FIELD_MS}
    a["extra_fields"] = extra_fields
    return a


def read_meta_row(meta_path, key):
    if not meta_path or not os.path.exists(meta_path):
        return None
    with open(meta_path) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.strip()[len(key) + 1:]
    return None


def format_stage(name, s):
    return (f"| {name} | {s['matched']}/{s['missing']} | {s['p50']:.1f} / {s['p99']:.1f} / {s['max']:.1f} | "
            f"{s['p50'] / FIELD_MS:.2f} / {s['p99'] / FIELD_MS:.2f} / {s['max'] / FIELD_MS:.2f} |")


def print_report(a):
    print("| stage | matched/missing | ms p50 / p99 / max | fields p50 / p99 / max |")
    print("|---|---|---|---|")
    for name, _, _, _ in STAGES:
        print(format_stage(name, a[name]))
    print(format_stage(f"total with the game's depth (+{a['extra_fields']} fields, informational)", a["total"]))
    print(f"edges={a['edges']}")


def judge(a):
    if a["edges"] == 0 or a["input_to_shown"]["matched"] == 0:
        return None
    t = a["input_to_shown"]
    return [
        (f"host input -> shown p50 <= {GATE_TOTAL_P50_FIELDS:.0f} fields", f"{t['p50'] / FIELD_MS:.2f} ({t['p50']:.1f} ms)",
         t["p50"] <= GATE_TOTAL_P50_FIELDS * FIELD_MS),
        (f"host input -> shown p99 <= {GATE_TOTAL_P99_FIELDS:.0f} fields", f"{t['p99'] / FIELD_MS:.2f} ({t['p99']:.1f} ms)",
         t["p99"] <= GATE_TOTAL_P99_FIELDS * FIELD_MS),
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


def synthetic(input_phase_ms, chain_ms, present_ms, frames=600, boundary_jitter=lambda i: 0.0):
    """A 60 Hz run: boundary N at N fields; the pad sample 0.3 ms after; an edge every 7th frame
    input_phase_ms before the sample; output(N) chain_ms after boundary N; shown present_ms after output."""
    ns = int(1e9)
    field = ns // 60
    lanes = {lane: [] for lane in LANES}
    for n in range(1, frames + 1):
        b = n * field + int(boundary_jitter(n) * 1e6)
        lanes["boundary"].append((b, n))
        pad = b + 300_000
        lanes["pad"].append((pad, n))
        if n % 7 == 0:
            lanes["input"].append((pad - int(input_phase_ms * 1e6), n))
        out = b + int(chain_ms * 1e6)
        lanes["output"].append((out, n))
        lanes["shown"].append((out + int(present_ms * 1e6), n))
    return lanes


def selftest():
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            print("FAIL", what)
            fails += 1

    # The round-4 Paris shape: edge 8 ms before the sample, the picture posted 18.9 ms after its boundary
    # (deferred capture), shown 8.9 ms later: host input -> shown = 8 + (16.67 - 0.3) + 18.9 + 8.9 = 52.2 ms
    # = 3.13 fields: RED on p50; the total adds the game's 1 field here = 68.8 ms.
    a = analyse(synthetic(8.0, 18.9, 8.9), skip_s=0.0, extra_fields=1)
    check(abs(a["input_to_pad"]["p50"] - 8.0) < 0.01, f"input_to_pad {a['input_to_pad']}")
    check(abs(a["pad_to_output"]["p50"] - (FIELD_MS - 0.3 + 18.9)) < 0.05, f"pad_to_output {a['pad_to_output']}")
    check(abs(a["output_to_shown"]["p50"] - 8.9) < 0.01, f"output_to_shown {a['output_to_shown']}")
    check(abs(a["total"]["p50"] - 68.83) < 0.1, f"total {a['total']}")
    check(a["edges"] == 85 and a["input_to_shown"]["matched"] == 85, f"edges {a['edges']} {a['input_to_shown']}")
    rows = judge(a)
    check(rows is not None and not rows[0][2] and rows[1][2], f"paris RED on p50 {rows}")
    # The target shape: the same edge phase, the picture posted 5 ms after its boundary (synchronous
    # capture), shown 3 ms later -> host 8 + 16.37 + 5 + 3 = 32.4 ms = 1.94 fields: GREEN on both.
    # (p99 outlier below: 32.4 + 20 = 52.4 ms = 3.14 fields: still <= 4.)
    a = analyse(synthetic(8.0, 5.0, 3.0), skip_s=0.0, extra_fields=1)
    rows = judge(a)
    check(rows is not None and rows[0][2] and rows[1][2], f"target GREEN {rows} {a['total']}")
    # A p99 outlier: every 50th boundary's output lands 40 ms late -> p50 GREEN, p99 = 32.4 + 40 = 72.4 ms > 4 fields: RED.
    lanes = synthetic(8.0, 5.0, 3.0)
    lanes["output"] = [(t + (40_000_000 if n % 50 == 0 else 0), n) for t, n in lanes["output"]]
    lanes["shown"] = [(t + (40_000_000 if n % 50 == 0 else 0), n) for t, n in lanes["shown"]]
    a = analyse(lanes, skip_s=0.0, extra_fields=1)
    rows = judge(a)
    check(rows is not None and rows[0][2] and not rows[1][2], f"outlier p99 RED {rows}")
    # A dropped picture: output(N+1) missing for an edge's frame -> counted missing, never guessed.
    lanes = synthetic(8.0, 5.0, 3.0)
    lanes["output"] = [(t, n) for t, n in lanes["output"] if n != 8]
    lanes["shown"] = [(t, n) for t, n in lanes["shown"] if n != 8]
    a = analyse(lanes, skip_s=0.0, extra_fields=1)
    check(a["input_to_shown"]["missing"] == 1 and a["input_to_shown"]["matched"] == 84, f"missing {a['input_to_shown']}")
    # No edges: no verdict (NO DATA), the chain stages still there.
    lanes = synthetic(8.0, 5.0, 3.0)
    lanes["input"] = []
    a = analyse(lanes, skip_s=0.0)
    check(judge(a) is None and a["boundary_to_output"]["matched"] == 600, "no data")
    # The skip drops the first seconds' marks.
    a = analyse(synthetic(8.0, 5.0, 3.0), skip_s=5.0, extra_fields=1)
    check(298 <= a["boundary_to_output"]["matched"] <= 301, f"skip {a['boundary_to_output']}")
    # The chain-free shape: the edge AT the sample, output at the boundary, shown at once -> host = one field - 0.3 ms.
    a = analyse(synthetic(0.0, 0.0, 0.0), skip_s=0.0, extra_fields=0)
    check(abs(a["total"]["p50"] - (FIELD_MS - 0.3)) < 0.05, f"edge p50 {a['total']}")
    # Exactly at the gate: host p50 = 3 fields is GREEN, a hair above RED.
    a = analyse(synthetic(0.0, 2 * FIELD_MS + 0.3, 0.0), skip_s=0.0, extra_fields=0)
    check(abs(a["input_to_shown"]["p50"] - 3 * FIELD_MS) < 0.05 and judge(a)[0][2], f"at the gate {a['input_to_shown']}")
    a = analyse(synthetic(0.0, 2 * FIELD_MS + 0.5, 0.0), skip_s=0.0, extra_fields=0)
    check(not judge(a)[0][2], "above the gate RED")
    print("judge_latency selftest:", "OK" if fails == 0 else f"{fails} FAIL")
    return fails == 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    j = sub.add_parser("judge")
    j.add_argument("out_dir")
    j.add_argument("--skip-s", type=float, default=5.0)
    j.add_argument("--meta", default=None)
    j.add_argument("--extra-fields", type=int, default=GAME_DEPTH_EXTRA_FIELDS)
    args = ap.parse_args(argv)
    if args.selftest:
        return 0 if selftest() else 1
    if args.cmd != "judge":
        ap.print_help()
        return 2
    lanes = read_marks(args.out_dir)
    a = analyse(lanes, skip_s=args.skip_s, extra_fields=args.extra_fields)
    print_report(a)
    meta = args.meta or os.path.join(args.out_dir, "run.meta")
    for key in ("latency", "frame_cap"):
        row = read_meta_row(meta, key)
        if row:
            print(f"{key}={row}")
    rows = judge(a)
    if rows is None:
        print("NO DATA: no input edges in the record (run with NATIVEEE_LAT_PULSE=<scancode>:<period_ms>:<hold_ms> or play)")
        return 2
    return 0 if print_judge(rows) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
