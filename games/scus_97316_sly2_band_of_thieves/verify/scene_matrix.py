#!/usr/bin/env python3
"""scene_matrix.py -- the scene matrix of a build: a steady 60 fps in every scene.

One hub is not 'everywhere'. This tool turns a set of play runs (one per scene x video backend, each
written with --harness-out) into a matrix with a RED / GREEN verdict per cell, so a build's frame rate
is judged in every scene we can reach from a savestate (hubs of every episode, the dense Paris spawn
view, a ledge, a mission, the Cairo museum, a briefing cutscene, the cold boot through the FMV) on both
the Metal and the GL arm. The runs themselves are driven by a thin shell wrapper around the play
harness (one process at a time, windowed; see `plan`); this tool reads what they wrote.

  scene_matrix.py plan
      The canonical scene list, one line per scene: name <TAB> state <TAB> pad <TAB> frames <TAB> note
      (state = a savestate name or `none` = cold boot; pad = `default` (the harness's walking script),
      `none`, or a pad-script file name). The driver reads this list.
  scene_matrix.py cell <out_dir> [--json]
      The metrics of ONE run: <out_dir>/run.meta (frames, park, clock frame_hz / logic_hz, gs_draws per
      frame, VU1 worker ms per frame (mtvu worker_ns / frames), GIF drain ms per frame (mtvu drain_ns),
      consumer ms per frame (gs_queue), video backend, restore route), <out_dir>/present_times.csv
      (pictures/s p50 on the output lane with judge_pictures' windows; the pacing gates on the shown
      lane with judge_pacing: doubled-frame share, p99 gap, gaps > 40 ms) and the driver's sidecar
      <out_dir>/scene_cell.env (scene, backend, rc, load averages, wall). Verdict per cell:
        GREEN   pictures/s p50 >= 59 AND doubled frames <= 1 %  (the round's bar)
        RED     otherwise
        DEFECT  the run did not complete: rc != 0, park != none, fewer boundaries than frames, watchdog
  scene_matrix.py table <out_dir>... [--out MATRIX.md] [--title T]
      The matrix: rows = scenes (plan order first, then any others), columns = backends; each cell
      `p50 / doubled % / frame_hz` with its verdict in bold when RED / DEFECT, then the detail table
      (every metric of every cell), then the 'worst scenes' ranking (lowest p50 first, Metal arm).
  scene_matrix.py content <out_dir>... [--backend auto]
      WHICH game content loads WHICH stage, one row per scene (the newest cell of the backend): wall ms per
      frame, producer gs_draws / frame, the consumer's steady-state (windows 6+ of gs_warm) draws, draw
      calls, texture binds, uploads and upload kb per frame, VU1 native bundles per frame (vu1_aot) and
      ns per bundle, the worker's busy / wait ms per frame (mtvu worker_ns = busy/wait/spin), the drain
      and consumer ms per frame, the coalescer's absorbed share, the FINISH kinds per frame.
  scene_matrix.py soak <out_dir> [--window-s 10]
      A long run read per wall window: pictures/s, doubled frames, p99 / max gap per window from the
      lanes, plus the run.meta warmup-census series (clock_game_hz, clock_gs_cpu_ms, clock_faults,
      clock_footprint_kb and its running sum) -- does the rate degrade or spike over time.
  scene_matrix.py --selftest
      Synthetic runs (steady 60, a 54/s floor, a parked run) through cell / table / soak. Runs everywhere.
"""
import argparse
import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import judge_pacing  # noqa: E402
import judge_pictures  # noqa: E402

GATE_P50_PICTURES = 59.0
GATE_DOUBLED_SHARE = 0.01
SKIP_S = 5.0
NOISY_LOAD = 12.0  # a cell measured with the host's 1-min load above this is marked `~load N` (shared host: noise, not a result)
BACKEND_ORDER = ("auto", "metal", "gl", "soft")

# name, state, pad, frames, note. `default` pad = the harness's walking script (sticks + CROSS every
# 150 frames); `none` = no input (cutscenes / the cold boot through the FMV / the dense spawn view).
PLAN = [
    ("hub_ep0", "hub_ep0", "default", 900, "ep0 intro: Cairo museum hall (HUD)"),
    ("hub_ep1", "hub_ep1_fixed", "default", 900, "ep1 nightclub: Paris hub, the reference scene (hub_ep1 is the stale-colour capture)"),
    ("hub_ep2", "hub_ep2", "default", 900, "ep2 palace: jungle ledge (old-build capture: timing valid, colours stale)"),
    ("hub_ep3", "hub_ep3", "default", 900, "ep3 temple: terraces"),
    ("hub_ep4", "hub_ep4", "default", 900, "ep4 prison: the safehouse interior -- close-up skinned Bentley at the ThiefNet laptop, beam-dense room (a 3-D scene, not a 2-D menu)"),
    ("hub_ep5", "hub_ep5", "default", 900, "ep5 castle: roof"),
    ("hub_ep6", "hub_ep6", "default", 900, "ep6 train: ground"),
    ("hub_ep7", "hub_ep7", "default", 900, "ep7 sawmill: binocucom view"),
    ("hub_ep8", "hub_ep8", "default", 900, "ep8 blimp: deck"),
    ("museum", "intro_museum_1471", "default", 900, "Cairo museum (the intro level, frame 1471)"),
    ("paris_spawn", "ep1_paris_spawn", "none", 900, "the dense Paris spawn view (4913 draws), no input"),
    ("ledge", "ep1_ledge_3090", "default", 900, "Paris ledge (frame 3090)"),
    ("job1_started", "ep1_job1_started", "default", 900, "mission: Satellite Sabotage, on the plaza after the briefing"),
    ("job1_dish1", "ep1_job1_dish1", "default", 900, "mission: at dish 1 (the water tower)"),
    ("briefing", "ep1_paris_ledge", "none", 900, "cutscene: the Satellite Sabotage binocucom briefing (runs 0..~2200 on this state)"),
    ("cold_boot", "none", "none", 1500, "cold boot: Sly logo -> intro FMV -> attract / title, no input"),
]


# ---------------------------------------------------------------------------------------------------
# reading a run
def read_meta(path):
    rows = {}
    if not os.path.exists(path):
        return rows
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                rows.setdefault(k, v)
    return rows


def read_env(path):
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                out[k] = v.strip().strip('"')
    return out


def kv_row(text, sep=":"):
    """`a=1:b=2/3:c=x` -> {'a': '1', 'b': '2/3', 'c': 'x'} (also accepts ',' separated rows)."""
    out = {}
    if text is None:
        return out
    for item in text.replace(",", sep).split(sep):
        if "=" in item:
            k, v = item.split("=", 1)
            out.setdefault(k, v)
    return out


def num(text, default=None):
    if text is None:
        return default
    text = str(text).split("/")[0]
    try:
        return float(text) if "." in text else int(text)
    except ValueError:
        return default


def series(text):
    if not text:
        return []
    out = []
    for t in text.split(","):
        v = num(t)
        if v is not None:
            out.append(v)
    return out


def cell_metrics(out_dir, skip_s=SKIP_S):
    meta = read_meta(os.path.join(out_dir, "run.meta"))
    env = read_env(os.path.join(out_dir, "scene_cell.env"))
    m = {
        "out_dir": out_dir,
        "scene": env.get("scene") or os.path.basename(out_dir.rstrip("/")),
        "backend": env.get("backend") or (meta.get("video_backend", "?").split(" ")[0]),
        "rc": num(env.get("rc"), None),
        "load_start": env.get("load_start", ""),
        "load_end": env.get("load_end", ""),
        "wall_s": num(env.get("wall_s"), None),
        "watchdog": num(env.get("watchdog"), 0),
        "other_games": num(env.get("other_games"), 0),  # other nativeee / gs_replay processes alive at the cell's start
        "frames_req": num(env.get("frames"), None),
        "started": env.get("started", ""),
        "has_meta": bool(meta),
    }
    m["frames"] = num(meta.get("frames"), 0)
    m["boundaries"] = num(meta.get("boundaries"), 0)
    m["wall_ms"] = num(meta.get("wall_ms"), 0)
    m["park"] = meta.get("park", "no-meta")
    m["park_detail"] = meta.get("park_detail", "")
    m["restore_route"] = meta.get("restore_route", "")
    m["video_backend"] = meta.get("video_backend", "")
    m["gl_renderer"] = meta.get("gl_renderer", "")
    clock = kv_row(meta.get("clock"))
    m["frame_hz"] = num(clock.get("frame_hz"), 0.0)
    m["logic_hz"] = num(clock.get("logic_hz"), 0.0)
    m["catchup_ticks"] = num(clock.get("catchup_ticks"), 0)
    frames = max(m["frames"], 1)
    gs_frames = num(meta.get("gs_frames"), 0) or frames
    m["draws_per_frame"] = num(meta.get("gs_draws"), 0) / max(gs_frames, 1)
    mtvu = kv_row(meta.get("mtvu"), ",")
    m["worker_ms"] = num(mtvu.get("worker_ns"), 0) / frames / 1e6
    m["drain_ms"] = num(mtvu.get("drain_ns"), 0) / frames / 1e6
    m["mtvu"] = (meta.get("mtvu") or "").split(",")[0]
    gsq = kv_row(meta.get("gs_queue"))
    m["consumer_ms"] = num(gsq.get("consumer_ms_per_frame"), 0.0)
    m["frames_dropped"] = num(gsq.get("frames_dropped"), 0)
    cap = kv_row(meta.get("frame_cap"))
    m["cap_doubled"] = num(cap.get("doubled"), 0)
    aot = kv_row(meta.get("vu1_aot_codegen"))
    m["pack_rejected"] = num(aot.get("pack_rejected"), 0)
    m["footprint_kb"] = series(meta.get("clock_footprint_kb"))
    m["game_hz"] = series(meta.get("clock_game_hz"))
    m["gs_cpu_ms"] = series(meta.get("clock_gs_cpu_ms"))
    m["ee_cpu_ms"] = series(meta.get("clock_ee_cpu_ms"))
    m["faults"] = series(meta.get("clock_faults"))
    # lanes
    m["pps_p50"] = 0
    m["pps_p10"] = 0
    m["pic_gap_p99"] = 0.0
    m["doubled_share"] = 1.0
    m["doubled"] = 0
    m["gaps"] = 0
    m["shown_gap_p99"] = 0.0
    m["shown_gap_max"] = 0.0
    m["over_40"] = 0
    m["floor_doubled"] = 0
    m["presenter_cost"] = 0
    m["repeated_fields"] = 0      # shown gaps of 2+ fields (a picture really held for an extra field)
    m["repeated_share"] = 0.0     # ... as a share of the shown gaps; `doubled` - this = 20-33 ms gaps = jitter, not a held picture
    m["output_gap_p90"] = 0.0     # the output lane's own cadence (ms): 16.7 tight; ~21 = the game publishes with a +-4 ms wobble
    csv_path = os.path.join(out_dir, "present_times.csv")
    if os.path.exists(csv_path):
        lanes = judge_pacing.read_lanes(out_dir)
        if len(lanes["output"]) > 2:
            p = judge_pictures.analyse(lanes["output"], skip_s, 1.0)
            m["pps_p50"] = p["pps_p50"]
            m["pps_p10"] = p["pps_p10"]
            m["pic_gap_p99"] = p["gap_p99"]
        a = judge_pacing.analyse(lanes, "shown", skip_s)
        if a:
            m["doubled_share"] = a["over_20_share"]
            m["doubled"] = a["over_20"]
            m["gaps"] = a["gaps"]
            m["shown_gap_p99"] = a["gap_p99"]
            m["shown_gap_max"] = a["gap_max"]
            m["over_40"] = a["over_40"]
            m["floor_doubled"] = a["floor_doubled"]
            m["presenter_cost"] = a["presenter_cost"]
            m["repeated_fields"] = a["repeated_fields"]
            m["repeated_share"] = a["repeated_fields"] / a["gaps"] if a["gaps"] else 0.0
        out = lanes["output"]
        if len(out) > 2:
            t0 = out[0] + skip_s * 1e9
            kept = [t for t in out if t >= t0]
            gaps = sorted((kept[i] - kept[i - 1]) / 1e6 for i in range(1, len(kept)))
            if gaps:
                m["output_gap_p90"] = judge_pacing.pct(gaps, 0.90)
    m["verdict"], m["why"] = verdict(m)
    return m


def verdict(m):
    """GREEN / RED / DEFECT with the reason."""
    reasons = []
    if not m["has_meta"]:
        return "DEFECT", "no run.meta (crash before the census?)"
    if m["rc"] not in (None, 0):
        reasons.append(f"rc={m['rc']}")
    if m["park"] != "none":
        reasons.append(f"park={m['park']}:{m['park_detail']}")
    if m["watchdog"]:
        reasons.append("watchdog")
    if m["frames_req"] and m["boundaries"] < m["frames_req"]:
        reasons.append(f"boundaries {m['boundaries']} < {m['frames_req']}")
    if reasons:
        return "DEFECT", " ".join(reasons)
    if m["pps_p50"] < GATE_P50_PICTURES:
        reasons.append(f"p50 {m['pps_p50']} < 59")
    if m["doubled_share"] > GATE_DOUBLED_SHARE:
        reasons.append(f"doubled {100 * m['doubled_share']:.1f} % > 1 %")
    if reasons:
        return "RED", " ".join(reasons)
    return "GREEN", f"p50 {m['pps_p50']}, doubled {100 * m['doubled_share']:.1f} %"


# ---------------------------------------------------------------------------------------------------
# the table
def backend_key(b):
    b = b.lower()
    return (BACKEND_ORDER.index(b) if b in BACKEND_ORDER else len(BACKEND_ORDER), b)


def scene_key(s):
    names = [p[0] for p in PLAN]
    return (names.index(s) if s in names else len(names), s)


def fmt_cell(m):
    if m is None:
        return "--"
    v = m["verdict"]
    core = f"p50 {m['pps_p50']} / dbl {100 * m['doubled_share']:.1f} % / {m['frame_hz']:.1f} Hz"
    l = load1(m["load_start"])
    if l > NOISY_LOAD:
        core += f" ~load {l:.0f}"
    if m["other_games"]:
        core += f" ~games {m['other_games']}"
    if m.get("takes", 1) > 1:
        core += f" ({m['takes']} takes: p50 {'/'.join(str(x) for x in m['takes_p50'])})"
    if v == "GREEN":
        return f"GREEN {core}"
    if v == "DEFECT":
        return f"**DEFECT** {m['why']} ({core})"
    if m.get("repeated_share", 0.0) < m["doubled_share"] / 2 and m["doubled_share"] > GATE_DOUBLED_SHARE:
        core += f" [2f+ {100 * m['repeated_share']:.1f} %: jitter]"
    return f"**RED** {core}"


def load1(text):
    """`load averages: 12.3 10.1 9.0` or `12.3 10.1 9.0` -> 12.3 (0.0 without)."""
    if not text:
        return 0.0
    tail = text.split("load averages:")[-1].split("load average:")[-1]
    for tok in tail.replace(",", " ").split():
        try:
            return float(tok)
        except ValueError:
            continue
    return 0.0


def quietest_takes(cells):
    """Several takes of the same scene x backend -> keep the one taken on the quietest host: the lowest VU1 `ns_per_bundle`
    (pure CPU time per unit of game content: 5-6 on an idle M4, 9-11 beside another track's game process -- the host-state
    normaliser, REPORT section 1), ties by the 1-min load at start. The kept cell carries `takes` = how many were seen and
    `takes_p50` = every take's pictures/s p50 (worst first), so the spread stays visible in the table."""
    groups = {}
    for m in cells:
        groups.setdefault((m["scene"], m["backend"]), []).append(m)
    kept = []
    for _, takes in groups.items():
        def host_cost(m):
            try:
                nsb = float(content_row(m["out_dir"])["ns_per_bundle"])
            except (ValueError, TypeError):
                nsb = 99.0
            return (m["verdict"] == "DEFECT", nsb, load1(m["load_start"]))
        best = min(takes, key=host_cost)
        best = dict(best, takes=len(takes), takes_p50=sorted(t["pps_p50"] for t in takes))
        kept.append(best)
    return kept


def render_table(cells, title="Scene matrix"):
    scenes = sorted({m["scene"] for m in cells}, key=scene_key)
    backends = sorted({m["backend"] for m in cells}, key=backend_key)
    by = {(m["scene"], m["backend"]): m for m in cells}
    notes = {p[0]: p[4] for p in PLAN}
    lines = [f"# {title}", ""]
    n_green = sum(1 for m in cells if m["verdict"] == "GREEN")
    n_red = sum(1 for m in cells if m["verdict"] == "RED")
    n_def = sum(1 for m in cells if m["verdict"] == "DEFECT")
    lines.append(f"Cells: {len(cells)} -- GREEN {n_green}, RED {n_red}, DEFECT {n_def}. Bar per cell: pictures/s p50 >= 59 "
                 f"AND doubled frames (shown gaps > 20 ms) <= 1 % (skip {SKIP_S:.0f} s). p50 = judge_pictures output lane; "
                 f"dbl = judge_pacing shown lane; Hz = run.meta clock frame_hz; `~load N` = the host's 1-min load at the "
                 f"cell's start was above {NOISY_LOAD:.0f}; `~games N` = N other game / replay processes were running at its start "
                 f"(shared host: both are noise, re-run).")
    lines.append("")
    lines.append("| scene | " + " | ".join(backends) + " | note |")
    lines.append("|---|" + "---|" * len(backends) + "---|")
    for s in scenes:
        row = [fmt_cell(by.get((s, b))) for b in backends]
        lines.append(f"| {s} | " + " | ".join(row) + f" | {notes.get(s, '')} |")
    lines.append("")
    lines.append("## Detail (every cell)")
    lines.append("")
    lines.append("| scene | backend | verdict | pictures/s p50 (p10) | doubled | shown p99 / max ms | >40 ms | frame_hz | logic_hz | "
                 "draws/frame | worker ms | drain ms | consumer ms | floor / presenter | 2f+ fields | output gap p90 | frames (bound.) | wall s | park | load start -> end | games | out |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for s in scenes:
        for b in backends:
            m = by.get((s, b))
            if m is None:
                continue
            lines.append(
                f"| {s} | {b} | {m['verdict']} | {m['pps_p50']} ({m['pps_p10']}) | {m['doubled']}/{m['gaps']} "
                f"({100 * m['doubled_share']:.1f} %) | {m['shown_gap_p99']:.1f} / {m['shown_gap_max']:.1f} | {m['over_40']} | "
                f"{m['frame_hz']:.1f} | {m['logic_hz']:.2f} | {m['draws_per_frame']:.0f} | {m['worker_ms']:.2f} | {m['drain_ms']:.2f} | "
                f"{m['consumer_ms']:.2f} | {m['floor_doubled']} / {m['presenter_cost']:+d} | {m['repeated_fields']} ({100 * m['repeated_share']:.1f} %) | "
                f"{m['output_gap_p90']:.1f} | {m['frames']} ({m['boundaries']}) | "
                f"{m['wall_s'] if m['wall_s'] is not None else m['wall_ms'] / 1000:.0f} | {m['park']} | "
                f"{load1(m['load_start']):.0f} -> {load1(m['load_end']):.0f} | {m['other_games']} | {os.path.basename(m['out_dir'].rstrip('/'))} |")
    lines.append("")
    lines.append("## Worst scenes (lowest pictures/s p50 first; DEFECT cells first)")
    lines.append("")
    ranked = sorted(cells, key=lambda m: (m["verdict"] != "DEFECT", m["pps_p50"], -m["doubled_share"]))
    for m in ranked[:8]:
        lines.append(f"- {m['scene']} / {m['backend']}: {m['verdict']} -- {m['why']}; frame_hz {m['frame_hz']:.1f}, "
                     f"draws/frame {m['draws_per_frame']:.0f}, worker {m['worker_ms']:.1f} ms, drain {m['drain_ms']:.1f} ms, "
                     f"consumer {m['consumer_ms']:.1f} ms, load {load1(m['load_start']):.0f}, other games {m['other_games']}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------------------------
# the diff: two sets of cells (two binaries, or the same binary in two host windows) side by side
def _short(m):
    """`GREEN 60/0.0%` -- verdict, pictures/s p50, doubled share -- or `--` for a missing cell."""
    if m is None:
        return "--"
    tag = m["verdict"] if m["verdict"] != "RED" else "**RED**"
    return f"{tag} {m['pps_p50']}/{100 * m['doubled_share']:.1f}%"


def render_diff(before, after, title="before -> after"):
    """One row per scene x backend: verdict / p50 / doubled before -> after, plus the two normalisers (draws/frame says
    whether the two cells saw the same content; worker ms and ns/bundle say whether the host was in the same state --
    see REPORT section 1: ns/bundle 5-6 on a quiet host, 9-10 beside another game process). Cells present on one side
    only show `--`. The closing line counts GREEN before / after and the cells that changed verdict."""
    b_by = {(m["scene"], m["backend"]): m for m in before}
    a_by = {(m["scene"], m["backend"]): m for m in after}
    keys = sorted(set(b_by) | set(a_by), key=lambda k: (scene_key(k[0]), backend_key(k[1])))
    lines = [f"### {title}", "",
             "| scene | backend | before | after | draws/f b -> a | worker ms b -> a | ns/bundle b -> a | changed |",
             "|---|---|---|---|---|---|---|---|"]
    up = down = 0
    for k in keys:
        b, a = b_by.get(k), a_by.get(k)
        cb = content_row(b["out_dir"]) if b else None
        ca = content_row(a["out_dir"]) if a else None
        fmt = lambda m, key, f: (f % m[key]) if m is not None else "--"  # noqa: E731
        changed = ""
        if b and a and b["verdict"] != a["verdict"]:
            changed = f"{b['verdict']} -> {a['verdict']}"
            if a["verdict"] == "GREEN":
                up += 1
            elif b["verdict"] == "GREEN":
                down += 1
        lines.append(f"| {k[0]} | {k[1]} | {_short(b)} | {_short(a)} | {fmt(b, 'draws_per_frame', '%.0f')} -> {fmt(a, 'draws_per_frame', '%.0f')} | "
                     f"{fmt(b, 'worker_ms', '%.1f')} -> {fmt(a, 'worker_ms', '%.1f')} | "
                     f"{cb['ns_per_bundle'] if cb else '--'} -> {ca['ns_per_bundle'] if ca else '--'} | {changed} |")
    gb = sum(1 for m in before if m["verdict"] == "GREEN")
    ga = sum(1 for m in after if m["verdict"] == "GREEN")
    lines += ["", f"GREEN {gb}/{len(before)} before -> {ga}/{len(after)} after; {up} cell(s) turned GREEN, {down} lost it."]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------------------------
# the soak
def soak_windows(lanes, window_s=10.0, skip_s=SKIP_S):
    shown = lanes["shown"]
    output = lanes["output"]
    if len(shown) < 2:
        return []
    t0 = shown[0] + int(skip_s * 1e9)
    span = int(window_s * 1e9)
    kept = [t for t in shown if t >= t0]
    if len(kept) < 2:
        return []
    n = int((kept[-1] - t0) // span)
    rows = []
    for k in range(n):
        lo, hi = t0 + k * span, t0 + (k + 1) * span
        w = [t for t in kept if lo <= t < hi]
        o = [t for t in output if lo <= t < hi]
        gaps = sorted((b - a) / 1e6 for a, b in zip(w, w[1:]))
        rows.append({
            "window": k,
            "from_s": (lo - shown[0]) / 1e9,
            "pictures_per_s": len(o) / window_s,
            "shown": len(w),
            "doubled": sum(1 for g in gaps if g > judge_pacing.GATE_P99_GAP_MS),
            "doubled_share": (sum(1 for g in gaps if g > judge_pacing.GATE_P99_GAP_MS) / len(gaps)) if gaps else 0.0,
            "gap_p99": judge_pacing.pct(gaps, 0.99) if gaps else 0.0,
            "gap_max": gaps[-1] if gaps else 0.0,
            "over_40": sum(1 for g in gaps if g > judge_pacing.GATE_TRIPLED_MS),
        })
    return rows


def render_soak(m, rows, window_s):
    lines = [f"soak {m['out_dir']}: frames={m['frames']} wall={m['wall_ms'] / 1000:.1f}s park={m['park']} "
             f"frame_hz={m['frame_hz']:.1f} p50={m['pps_p50']} doubled={100 * m['doubled_share']:.1f}% verdict={m['verdict']}"]
    lines.append(f"| window ({window_s:.0f} s) | from s | pictures/s | shown | doubled (share) | p99 gap ms | max gap ms | >40 ms | verdict |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        ok = r["pictures_per_s"] >= GATE_P50_PICTURES and r["doubled_share"] <= GATE_DOUBLED_SHARE
        lines.append(f"| {r['window']} | {r['from_s']:.0f} | {r['pictures_per_s']:.1f} | {r['shown']} | {r['doubled']} "
                     f"({100 * r['doubled_share']:.1f} %) | {r['gap_p99']:.1f} | {r['gap_max']:.1f} | {r['over_40']} | "
                     f"{'GREEN' if ok else 'RED'} |")
    if rows:
        pps = [r["pictures_per_s"] for r in rows]
        half = max(len(rows) // 2, 1)
        first, last = sum(pps[:half]) / half, sum(pps[-half:]) / half
        lines.append(f"trend: first-half pictures/s {first:.1f} -> second-half {last:.1f} ({last - first:+.1f}); "
                     f"min window {min(pps):.1f} max {max(pps):.1f}; windows RED: "
                     f"{sum(1 for r in rows if not (r['pictures_per_s'] >= GATE_P50_PICTURES and r['doubled_share'] <= GATE_DOUBLED_SHARE))}/{len(rows)}")
    fp = m["footprint_kb"]
    if fp:
        run, acc = [], 0
        for v in fp:
            acc += v
            run.append(acc)
        lines.append(f"clock_footprint_kb (per census window, {len(fp)}): {','.join(str(v) for v in fp)}")
        lines.append(f"  running sum kb: first {run[0]} -> last {run[-1]} (growth after window 1: {run[-1] - run[0]:+d} kb; "
                     f"max {max(run)})")
    if m["game_hz"]:
        lines.append(f"clock_game_hz: {','.join(str(v) for v in m['game_hz'])}")
    if m["gs_cpu_ms"]:
        lines.append(f"clock_gs_cpu_ms: {','.join(str(v) for v in m['gs_cpu_ms'])}")
    if m["ee_cpu_ms"]:
        lines.append(f"clock_ee_cpu_ms: {','.join(str(v) for v in m['ee_cpu_ms'])}")
    if m["faults"]:
        lines.append(f"clock_faults: {','.join(str(v) for v in m['faults'])}")
    return "\n".join(lines) + "\n"


def content_row(m_dir):
    """The content numbers of one run (what the game asked of each stage per frame)."""
    meta = read_meta(os.path.join(m_dir, "run.meta"))
    env = read_env(os.path.join(m_dir, "scene_cell.env"))
    frames = max(num(meta.get("frames"), 0), 1)
    gs_frames = max(num(meta.get("gs_frames"), 0) or frames, 1)
    wall = num(meta.get("wall_ms"), 0)
    # gs_warm: per 1 s window frames/draws/draw_calls/uploads/upload_kb/readbacks/readback_kb/copies/program_binds/
    # framebuffer_binds/texture_binds; steady state = windows 6+ (the first windows carry the warm-up).
    win = [w.split("/") for w in (meta.get("gs_warm") or "").split(",") if w.count("/") >= 10]
    steady = win[5:] or win
    tot = [sum(num(w[i], 0) for w in steady) for i in range(11)] if steady else [0] * 11
    cf = max(tot[0], 1)
    mtvu = kv_row(meta.get("mtvu"), ",")
    wn = (mtvu.get("worker_ns") or "0/0/0").split("/")
    busy, wait = num(wn[0], 0), num(wn[1], 0) if len(wn) > 1 else 0
    aot_items = (meta.get("vu1_aot") or "").split(":")
    aot = dict(zip(aot_items[0::2], aot_items[1::2]))
    gsq = kv_row(meta.get("gs_queue"))
    co = kv_row(meta.get("gs_coalesce"))
    irq = kv_row(meta.get("gs_irq_kinds"))
    return {
        "scene": env.get("scene") or os.path.basename(m_dir.rstrip("/")),
        "backend": env.get("backend", "?"),
        "load": f"{load1(env.get('load_start', '')):.0f}",
        "ms_per_frame": wall / frames,
        "gs_draws_per_frame": num(meta.get("gs_draws"), 0) / gs_frames,
        "consumer_draws": tot[1] / cf, "draw_calls": tot[2] / cf, "texture_binds": tot[10] / cf,
        "uploads": tot[3] / cf, "upload_kb": tot[4] / cf, "program_binds": tot[8] / cf,
        "bundles_m": num(aot.get("native_bundles"), 0) / frames / 1e6,
        "ns_per_bundle": aot.get("ns_per_bundle", "?"),
        "worker_busy_ms": busy / frames / 1e6, "worker_wait_ms": wait / frames / 1e6,
        "drain_ms": num(mtvu.get("drain_ns"), 0) / frames / 1e6,
        "consumer_ms": num(gsq.get("consumer_ms_per_frame"), 0.0),
        "absorbed_share": (num(co.get("absorbed"), 0) / num(co.get("in"), 1)) if num(co.get("in"), 0) else 0.0,
        "finish": "/".join(f"{num(irq.get(k), 0) / gs_frames:.1f}" for k in ("finish_p1", "finish_p2", "finish_p3")),
    }


def render_content(rows):
    lines = ["| scene | backend | load | ms/frame | gs_draws/f | consumer draws/f | draw calls/f | tex binds/f | uploads/f | "
             "upload kb/f | VU1 Mbundles/f | ns/bundle | worker busy ms | worker wait ms | drain ms | consumer ms | "
             "coalesce absorbed | FINISH p1/p2/p3 per f |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in sorted(rows, key=lambda r: -r["worker_busy_ms"]):
        lines.append(f"| {r['scene']} | {r['backend']} | {r['load']} | {r['ms_per_frame']:.1f} | {r['gs_draws_per_frame']:.0f} | "
                     f"{r['consumer_draws']:.0f} | {r['draw_calls']:.0f} | {r['texture_binds']:.0f} | {r['uploads']:.1f} | "
                     f"{r['upload_kb']:.0f} | {r['bundles_m']:.2f} | {r['ns_per_bundle']} | {r['worker_busy_ms']:.1f} | "
                     f"{r['worker_wait_ms']:.1f} | {r['drain_ms']:.1f} | {r['consumer_ms']:.2f} | {100 * r['absorbed_share']:.0f} % | "
                     f"{r['finish']} |")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------------------------
def write_synthetic_run(d, shown, output, meta_rows, env_rows=None):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "present_times.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["lane", "ordinal", "t_ns"])
        for i, t in enumerate(output):
            w.writerow(["boundary", i, t - 1_000_000])
        for i, t in enumerate(output):
            w.writerow(["output", i, t])
        for i, t in enumerate(shown):
            w.writerow(["shown", i, t])
    with open(os.path.join(d, "run.meta"), "w") as f:
        for k, v in meta_rows.items():
            f.write(f"{k}={v}\n")
    if env_rows:
        with open(os.path.join(d, "scene_cell.env"), "w") as f:
            for k, v in env_rows.items():
                f.write(f"{k}={v}\n")


def selftest():
    import shutil
    import tempfile
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            print("FAIL", what)
            fails += 1

    ns = int(1e9)
    field = ns // 60
    root = tempfile.mkdtemp(prefix="scene_matrix_selftest_")
    try:
        # (a) a steady 60 for 20 s on the Metal arm: GREEN, the metrics read back.
        steady = [i * field for i in range(1201)]
        meta = {
            "frames": 1200, "boundaries": 1200, "wall_ms": 20000, "park": "none", "park_detail": "frames_cap",
            "video_backend": "metal (auto)", "gl_renderer": "Metal Apple M4", "restore_route": "cold",
            "clock": "logic_hz=60.00:logic_hz_min=59:logic_hz_max=61:frame_hz=60.00:windows=20/20:catchup_ticks=0:catchup_fields=0:slow_motion=0",
            "gs_draws": 3340800, "gs_frames": 1200,
            "mtvu": "on,worker=1,drain=149416,drain_thread=on,drain_ns=4320000000/86386,gif_fences=0,worker_ns=7468800000/15998366279/0,worker_segs=1/2",
            "gs_queue": "pipelined:frame_skip=1:frames=1200:frames_dropped=5:consumer_ms_per_frame=4.33",
            "frame_cap": "cap=60:admitted=1194:held=13:superseded=0:late=0:slipped=359:slip_ms=1989:doubled=189",
            "vu1_aot_codegen": "compiler:x:abi:abi-1:opt:2:pack:6:pack_rejected:0",
            "clock_footprint_kb": "26577,8929,5745,-5278,-4239", "clock_game_hz": "60,60,60,60,60",
            "clock_gs_cpu_ms": "335,354,301,236,227", "clock_ee_cpu_ms": "58,58,50,40,37", "clock_faults": "1,2,3,4,5",
        }
        env = {"scene": "hub_ep1", "backend": "auto", "rc": 0, "load_start": "load averages: 8.12 9.00 10.00",
               "load_end": "load averages: 7.50 8.90 9.90", "wall_s": 24, "frames": 1200, "watchdog": 0, "started": "09:00:00"}
        d1 = os.path.join(root, "pipe_sm_hub_ep1_auto")
        write_synthetic_run(d1, steady, [t - 2_000_000 for t in steady], meta, env)
        m = cell_metrics(d1)
        check(m["verdict"] == "GREEN", f"steady verdict {m['verdict']} {m['why']}")
        check(m["pps_p50"] == 60 and m["doubled"] == 0, f"steady p50 {m['pps_p50']} doubled {m['doubled']}")
        check(abs(m["frame_hz"] - 60.0) < 1e-6 and abs(m["logic_hz"] - 60.0) < 1e-6, f"clock {m['frame_hz']} {m['logic_hz']}")
        check(abs(m["draws_per_frame"] - 2784.0) < 1e-6, f"draws/frame {m['draws_per_frame']}")
        check(abs(m["worker_ms"] - 6.224) < 1e-3 and abs(m["drain_ms"] - 3.6) < 1e-3, f"worker {m['worker_ms']} drain {m['drain_ms']}")
        check(abs(m["consumer_ms"] - 4.33) < 1e-6 and m["frames_dropped"] == 5, f"consumer {m['consumer_ms']} dropped {m['frames_dropped']}")
        check(m["backend"] == "auto" and m["scene"] == "hub_ep1" and m["rc"] == 0, f"env {m['backend']} {m['scene']} {m['rc']}")
        check(m["footprint_kb"] == [26577, 8929, 5745, -5278, -4239] and m["game_hz"] == [60] * 5, f"series {m['footprint_kb']}")
        check(abs(load1(m["load_start"]) - 8.12) < 1e-6 and abs(load1("12.3 4 5") - 12.3) < 1e-6 and load1("") == 0.0, "load1")
        # (b) the baseline's shape on the GL arm: 54 pictures/s (every 10th missing), shown = output -> RED on both.
        pictures = [i * field for i in range(1200) if i % 10 != 9]
        meta_b = dict(meta)
        meta_b["clock"] = meta["clock"].replace("frame_hz=60.00", "frame_hz=54.00")
        meta_b["video_backend"] = "gl"
        env_b = dict(env, backend="gl", load_start="load averages: 31.00 20.00 10.00", other_games=2)  # a noisy cell
        d2 = os.path.join(root, "pipe_sm_hub_ep1_gl")
        write_synthetic_run(d2, pictures, pictures, meta_b, env_b)
        mb = cell_metrics(d2)
        check(mb["verdict"] == "RED" and "p50" in mb["why"] and "doubled" in mb["why"], f"floor verdict {mb['verdict']} {mb['why']}")
        check(mb["pps_p50"] == 54 and mb["doubled_share"] > 0.05, f"floor p50 {mb['pps_p50']} share {mb['doubled_share']}")
        check(m["repeated_fields"] == 0 and abs(m["output_gap_p90"] - 1000 / 60) < 0.05, f"steady repeated {m['repeated_fields']} out p90 {m['output_gap_p90']}")
        check(mb["repeated_fields"] > 0 and abs(mb["repeated_share"] - mb["doubled_share"]) < 1e-9 and "[2f+" not in fmt_cell(mb), f"floor repeated {mb['repeated_fields']} {mb['repeated_share']}")
        # a jitter-only cell: 60 pictures/s with every other gap 21 / 12 ms -> doubled by the gate, 0 repeated fields, marked jitter
        jit, t = [0], 0
        for i in range(1200):
            t += 21_000_000 if i % 2 else 12_333_333
            jit.append(t)
        d6 = os.path.join(root, "pipe_sm_paris_spawn_auto")
        write_synthetic_run(d6, jit, jit, meta, dict(env, scene="paris_spawn"))
        mj = cell_metrics(d6)
        check(mj["verdict"] == "RED" and mj["repeated_fields"] == 0 and mj["doubled_share"] > 0.4 and "[2f+ 0.0 %: jitter]" in fmt_cell(mj),
              f"jitter cell {mj['verdict']} {mj['doubled_share']} {mj['repeated_fields']} {fmt_cell(mj)}")
        check(abs(mj["output_gap_p90"] - 21.0) < 0.01, f"jitter out p90 {mj['output_gap_p90']}")
        # (c) a 1 % doubled share with p50 59 stays GREEN; p50 58 is RED on p50 alone.
        t, six = 0, [0]
        for i in range(1200):
            t += 2 * field if i % 120 == 119 else field
            six.append(t)
        d3 = os.path.join(root, "pipe_sm_ledge_auto")
        write_synthetic_run(d3, six, six, meta, dict(env, scene="ledge"))
        mc = cell_metrics(d3)
        check(mc["verdict"] == "GREEN" and mc["pps_p50"] >= 59, f"one-percent {mc['verdict']} {mc['why']} p50 {mc['pps_p50']}")
        # (d) a parked run: DEFECT regardless of the lanes; a non-zero rc: DEFECT; no run.meta: DEFECT.
        d4 = os.path.join(root, "pipe_sm_museum_auto")
        write_synthetic_run(d4, steady, steady, dict(meta, park="watchdog", park_detail="tid3@0x1234", boundaries=600),
                            dict(env, scene="museum", rc=124, watchdog=1))
        md = cell_metrics(d4)
        check(md["verdict"] == "DEFECT" and "park=watchdog" in md["why"] and "rc=124" in md["why"] and "boundaries 600" in md["why"],
              f"defect {md['verdict']} {md['why']}")
        d5 = os.path.join(root, "pipe_sm_cold_boot_gl")
        os.makedirs(d5)
        with open(os.path.join(d5, "scene_cell.env"), "w") as f:
            f.write("scene=cold_boot\nbackend=gl\nrc=139\n")
        me = cell_metrics(d5)
        check(me["verdict"] == "DEFECT" and "no run.meta" in me["why"], f"no-meta {me['verdict']} {me['why']}")
        # (e) the table: plan order, backends auto then gl, RED / DEFECT bold, worst list led by the DEFECT.
        table = render_table([mb, m, mc, md, me], "selftest")
        check("| hub_ep1 | GREEN p50 60" in table and "| **RED** p50 54 / dbl 11.0 % / 54.0 Hz ~load 31 ~games 2 |" in table, "table cells")
        check("GREEN p50 60 / dbl 0.0 % / 60.0 Hz |" in table, "quiet cell has no load mark")
        check(table.index("| hub_ep1 |") < table.index("| museum |") < table.index("| ledge |") < table.index("| cold_boot |"), "plan order")
        check("| scene | auto | gl | note |" in table, f"backend columns")
        check("**DEFECT** rc=124 park=watchdog" in table and "**DEFECT** no run.meta" in table, "defect cells")
        check("GREEN 2, RED 1, DEFECT 2" in table, "counts")
        worst = table.split("## Worst scenes")[1].strip().splitlines()[2:]
        check(worst[0].startswith("- museum / auto: DEFECT") or worst[0].startswith("- cold_boot / gl: DEFECT"), f"worst[0] {worst[0]}")
        check(any(l.startswith("- hub_ep1 / gl: RED") for l in worst[:3]), f"worst has the gl floor {worst[:3]}")
        # (f) the soak windows: 20 s steady -> 1 full 10 s window after the 5 s skip, 60/s; the 54/s floor -> RED windows.
        rows = soak_windows(judge_pacing.read_lanes(d1), 10.0, 5.0)
        check(len(rows) == 1 and abs(rows[0]["pictures_per_s"] - 60.0) < 0.2 and rows[0]["doubled"] == 0, f"soak steady {rows}")
        rows_b = soak_windows(judge_pacing.read_lanes(d2), 5.0, 0.0)
        check(len(rows_b) == 3 and all(abs(r["pictures_per_s"] - 54.0) < 0.3 for r in rows_b) and all(r["doubled"] >= 25 for r in rows_b),
              f"soak floor {rows_b}")
        text = render_soak(m, rows, 10.0)
        check("running sum kb: first 26577 -> last 31734" in text and "trend:" in text, "soak text")
        # (h) content: a run with gs_warm / mtvu worker_ns / vu1_aot rows -> per-frame content numbers; a run without them -> zeros.
        d4 = os.path.join(root, "pipe_sm_synth_auto")
        write_synthetic_run(d4, steady, [t - 2_000_000 for t in steady], {
            "frames": "100", "gs_frames": "100", "wall_ms": "2000", "gs_draws": "300000",
            "gs_warm": ",".join(["50/75000/100000/10/500/150/112000/200/4000/1000/70000"] * 7),
            "mtvu": "on,worker=1,drain_ns=400000000/1000,worker_ns=700000000/800000000/1000",
            "vu1_aot": "programs:6:verified:6:native_kicks:1000:native_bundles:120000000:ns_per_bundle:5.50",
            "gs_queue": "pipelined:frames=100:consumer_ms_per_frame=5.00", "gs_coalesce": "on:in=1000:absorbed=330",
            "gs_irq_kinds": "finish_p1=100:finish_p2=200:finish_p3=0"}, {"scene": "synth", "backend": "auto", "load_start": "3.1 4 5"})
        r = content_row(d4)
        check(abs(r["gs_draws_per_frame"] - 3000) < 1e-6 and abs(r["consumer_draws"] - 1500) < 1e-6 and abs(r["texture_binds"] - 1400) < 1e-6
              and abs(r["bundles_m"] - 1.2) < 1e-9 and r["ns_per_bundle"] == "5.50" and abs(r["worker_busy_ms"] - 7.0) < 1e-9
              and abs(r["worker_wait_ms"] - 8.0) < 1e-9 and abs(r["drain_ms"] - 4.0) < 1e-9 and abs(r["consumer_ms"] - 5.0) < 1e-9
              and abs(r["absorbed_share"] - 0.33) < 1e-9 and r["finish"] == "1.0/2.0/0.0" and r["load"] == "3", f"content row {r}")
        r0 = content_row(d2)
        check(r0["bundles_m"] == 0 and r0["ns_per_bundle"] == "?" and r0["consumer_draws"] == 0 and r0["load"] == "31", f"content row without rows {r0}")
        check("| synth | auto | 3 | 20.0 | 3000 | 1500 |" in render_content([r, r0]), "content table")
        # (i) the diff: the GL floor cell (b) 'before' vs the steady cell (a) relabelled gl 'after' -> one row turned GREEN;
        #     a cell on one side only shows `--`.
        ma = dict(m, backend="gl")
        dtext = render_diff([mb, md], [ma], "t")
        check("| hub_ep1 | gl | **RED** 54/11.0% | GREEN 60/0.0% |" in dtext and "RED -> GREEN" in dtext, f"diff row {dtext}")
        check("| museum | auto | **DEFECT** 60/0.0% | -- |" in dtext.replace("DEFECT 60", "**DEFECT** 60") or "| museum | auto | DEFECT 60/0.0% | -- |" in dtext, "diff one-sided")
        check("GREEN 0/2 before -> 1/1 after; 1 cell(s) turned GREEN, 0 lost it." in dtext, f"diff summary {dtext.splitlines()[-1]}")
        # (j) quietest takes: two takes of hub_ep1/auto -- the synthetic content run (ns/bundle 5.50) wins over the one without
        #     the aot row (99); the kept cell says `2 takes` and lists both p50s.
        m_take2 = dict(m, out_dir=d4, pps_p50=41, verdict="RED")  # the take WITH the aot row (ns/bundle 5.50) wins
        q = quietest_takes([m_take2, dict(m, out_dir=d1), mb])
        kept = {(c["scene"], c["backend"]): c for c in q}
        check(len(q) == 2 and kept[("hub_ep1", "auto")]["takes"] == 2 and kept[("hub_ep1", "gl")]["takes"] == 1, f"quietest groups {[(c['scene'], c['backend'], c['takes']) for c in q]}")
        check(kept[("hub_ep1", "auto")]["out_dir"] == d4 and kept[("hub_ep1", "auto")]["takes_p50"] == [41, 60], f"quietest pick {kept[('hub_ep1', 'auto')]['out_dir']}")
        check("(2 takes: p50 41/60)" in fmt_cell(kept[("hub_ep1", "auto")]), "takes mark")
        # (g) the plan: unique names, every entry well-formed.
        names = [p[0] for p in PLAN]
        check(len(set(names)) == len(names) and all(isinstance(p[3], int) and p[3] > 0 for p in PLAN), "plan")
        check(scene_key("hub_ep0") < scene_key("cold_boot") < scene_key("zzz_unknown"), "scene_key")
        check(backend_key("auto") < backend_key("metal") < backend_key("gl") < backend_key("vulkan"), "backend_key")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("scene_matrix selftest:", "PASS" if fails == 0 else f"{fails} failure(s)")
    return fails == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("plan")
    c = sub.add_parser("cell")
    c.add_argument("out_dir")
    c.add_argument("--json", action="store_true")
    c.add_argument("--skip-s", type=float, default=SKIP_S)
    t = sub.add_parser("table")
    t.add_argument("out_dirs", nargs="+")
    t.add_argument("--out", default=None)
    t.add_argument("--title", default="Scene matrix")
    t.add_argument("--skip-s", type=float, default=SKIP_S)
    t.add_argument("--quietest", action="store_true", help="several takes of one cell: keep the one with the lowest ns/bundle")
    k = sub.add_parser("content")
    k.add_argument("out_dirs", nargs="+")
    df = sub.add_parser("diff")
    df.add_argument("--before", nargs="+", required=True)
    df.add_argument("--after", nargs="+", required=True)
    df.add_argument("--title", default="before -> after")
    df.add_argument("--skip-s", type=float, default=SKIP_S)
    df.add_argument("--quietest", action="store_true", help="several takes of one cell on either side: keep the quietest")
    s = sub.add_parser("soak")
    s.add_argument("out_dir")
    s.add_argument("--window-s", type=float, default=10.0)
    s.add_argument("--skip-s", type=float, default=SKIP_S)
    args = ap.parse_args()
    if args.selftest:
        return 0 if selftest() else 1
    if args.cmd == "plan":
        for name, state, pad, frames, note in PLAN:
            print(f"{name}\t{state}\t{pad}\t{frames}\t{note}")
        return 0
    if args.cmd == "cell":
        m = cell_metrics(args.out_dir, args.skip_s)
        if args.json:
            print(json.dumps(m, indent=1, default=str))
        else:
            print(f"{m['scene']}/{m['backend']}: {m['verdict']} -- {m['why']} | frame_hz {m['frame_hz']:.1f} logic {m['logic_hz']:.2f} | "
                  f"p50 {m['pps_p50']} (p10 {m['pps_p10']}) | doubled {m['doubled']}/{m['gaps']} ({100 * m['doubled_share']:.1f} %) "
                  f"p99 {m['shown_gap_p99']:.1f} max {m['shown_gap_max']:.1f} >40 {m['over_40']} | draws/frame {m['draws_per_frame']:.0f} | "
                  f"worker {m['worker_ms']:.2f} ms drain {m['drain_ms']:.2f} ms consumer {m['consumer_ms']:.2f} ms | "
                  f"frames {m['frames']} ({m['boundaries']}) wall {m['wall_ms'] / 1000:.1f} s park {m['park']} | "
                  f"backend {m['video_backend']} | pack_rejected {m['pack_rejected']} | load {m['load_start']} | other games {m['other_games']}")
        return 0 if m["verdict"] == "GREEN" else 1
    if args.cmd == "table":
        cells = [cell_metrics(d, args.skip_s) for d in args.out_dirs if os.path.isdir(d)]
        if args.quietest:
            cells = quietest_takes(cells)
        text = render_table(cells, args.title)
        if args.out:
            with open(args.out, "w") as f:
                f.write(text)
            print(f"wrote {args.out} ({len(cells)} cells)")
        else:
            sys.stdout.write(text)
        return 0 if all(m["verdict"] == "GREEN" for m in cells) else 1
    if args.cmd == "content":
        rows = [content_row(d) for d in args.out_dirs if os.path.isfile(os.path.join(d, "run.meta"))]
        sys.stdout.write(render_content(rows))
        return 0
    if args.cmd == "diff":
        before = [cell_metrics(d, args.skip_s) for d in args.before if os.path.isdir(d)]
        after = [cell_metrics(d, args.skip_s) for d in args.after if os.path.isdir(d)]
        if args.quietest:
            before, after = quietest_takes(before), quietest_takes(after)
        sys.stdout.write(render_diff(before, after, args.title))
        return 0
    if args.cmd == "soak":
        m = cell_metrics(args.out_dir, args.skip_s)
        lanes = judge_pacing.read_lanes(args.out_dir)
        rows = soak_windows(lanes, args.window_s, args.skip_s)
        sys.stdout.write(render_soak(m, rows, args.window_s))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
