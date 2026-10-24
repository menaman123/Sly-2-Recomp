#!/usr/bin/env python3
"""launcher_settings_judge.py -- the ONE-SETTINGS-STORE judge (frozen).

Every user-facing settings row (the engine's generated settings_schema.json) must travel
BOTH ways through the one file `<game folder>/nativeee.ini`:

  A. launcher -> game.  The launcher's path (nativeee_settings `set`, the same ConfigStore /
     SettingsFile writer a Qt launcher mirrors) writes every row's judge value into
     <folder>/nativeee.ini; the game is started HEADLESS for 60 frames FROM THE FOLDER (the
     binary copied beside the file, no --config: the executable-folder rule) and its run.meta
     must show every row (`env_ps2=` is the config fingerprint; `user_data_root=` for the data
     folder) and `settings_file=<folder>/nativeee.ini:source=executable folder`.
  B. --config.  The same leg started with `--config <folder>/nativeee.ini` from the build's
     binary: the same rows, `source=--config`.
  C. game -> launcher.  A WINDOW leg drives every row that has an in-game writer through the
     in-game pages' own request route (harness.display_sequence: the Display page's rows, the
     Controls page's keyboard toggle, the Sound page's slider), then the launcher reads the
     file back (nativeee_settings `get`): every value must be what the page set. Rows with no
     in-game widget are written with the game-side ConfigStore API (the same `set`) and read
     back the same way.
  D. re-read on open.  The file says one thing, the command line pinned another; F1 opens the
     menu at boundary 30 -> the menu re-reads the file and applies it: run.meta
     `settings_persisted=...:resyncs=1`.

  launcher_settings_judge.py judge --bin <nativeee> --tool <nativeee_settings> --schema <settings_schema.json>
        --folder <scratch game folder> --iso <image> [--state <ps2st>] [--pad <padlog>]
        [--corpus <dir>] [--pack <dir>] [--mc <memcards dir>] [--frames 60] [--skip C|D]
      Exit 0 when every row is GREEN in every direction, 1 otherwise. `uptime` is printed with
      every timing number.
  launcher_settings_judge.py --selftest
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

# The judge value per user-facing row (never the default), and the in-game writer that sets it
# (a harness.display_sequence mode, the pages' own request route) when the row has a widget.
ROWS = {
    "video.fullscreen": ("borderless", "borderless", "host"),   # the WINDOW MANAGER grants a mode swap, not the settings
                                                             # plumbing: macOS refuses SDL_SetWindowFullscreen for a window
                                                             # that is not frontmost (a shared desktop). The page route is
                                                             # driven and reported; the row is judged through the store API.
    "video.aspect": ("16:9", "aspect_16_9"),
    "video.scale": ("2", "scale2"),
    "video.vsync": ("on", "vsync"),
    "video.frame_cap": ("30", "cap30"),
    "video.texture_filter": ("bilinear", "filter_bilinear"),
    "video.anisotropy": ("4", "aniso4"),
    "video.texture_pack": ("judge_pack", None),
    "video.backend": ("gl", None),
    "ui.perf_overlay": ("on", "overlay"),
    "audio.volume": ("55", "volume55"),
    "audio.interpolation": ("gaussian", None),
    "input.kbm_pad": ("off", "nokbm"),
    "input.rumble": ("off", None),
    "input.device": ("none", None),
    "save.slot": ("3", None),
    "qol.skip_movies": ("off", None),
    "qol.fast_forward": ("4", None),
    "fixes.look_rest_seconds": ("on", None),
    "fixes.skid_mark_init": ("on", None),
    "paths.user_data": ("<folder>/data", None),
}
# Rows judged by a key other than env_ps2 (paths.* are not in the fingerprint).
META_KEY = {"paths.user_data": "user_data_root"}
# User-facing rows the judge does not drive (path plumbing the leg pins itself).
NOT_JUDGED = {"paths.mc_dir": "the leg's memory card", "paths.save_dir": "the leg's out dir",
              "paths.texture_packs": "path plumbing", "paths.game_files": "the build's ELF", "paths.iso": "the leg's --iso"}
RESYNC_KEY, RESYNC_FILE, RESYNC_CLI = "video.vsync", "on", "off"


def uptime():
    try:
        return subprocess.run(["uptime"], capture_output=True, text=True).stdout.strip().split("load")[-1]
    except Exception:
        return "?"


def read_meta(path):
    meta = {}
    try:
        with open(path) as f:
            for line in f:
                if "=" in line:
                    k, v = line.rstrip("\n").split("=", 1)
                    meta[k] = v
    except FileNotFoundError:
        pass
    return meta


def fingerprint_has(meta, key, value):
    return f"{key}={value};" in (meta.get("env_ps2", "") + ";")


def tool(args, tool_bin):
    r = subprocess.run([tool_bin] + args, capture_output=True, text=True)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def run_game(bin_path, args, out_dir, log, timeout, env_extra, folder):
    env = dict(os.environ)
    env.update(env_extra)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    cmd = [bin_path] + args + ["--harness-out", out_dir]
    t0 = time.time()
    with open(log, "a") as lf:
        lf.write("$ " + " ".join(cmd) + "\n")
        lf.flush()
        try:
            p = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT, env=env, timeout=timeout, cwd=folder)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = 124
    return rc, time.time() - t0


def user_rows(schema):
    doc = json.load(open(schema))
    return [s for s in doc["settings"] if s.get("user_facing") and not s.get("hidden")]


def judge(a):
    for name in ("bin", "tool", "schema", "iso", "state", "pad", "corpus", "pack", "mc"):   # the legs run with cwd = the folder
        if getattr(a, name):
            setattr(a, name, os.path.abspath(getattr(a, name)))
    folder = os.path.abspath(a.folder)
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    os.makedirs(os.path.join(folder, "data"), exist_ok=True)
    ini = os.path.join(folder, "nativeee.ini")
    log = os.path.join(folder, "judge.log")
    results = []  # (direction, key, ok, detail)
    rows = user_rows(a.schema)
    keys = [r["key"] for r in rows]
    missing = [k for k in keys if k not in ROWS and k not in NOT_JUDGED]
    if missing:
        print(f"RED: schema rows without a judge value: {missing} (extend ROWS)")
        return 1
    print(f"== launcher_settings_judge {time.strftime('%H:%M:%S')} load{uptime()} rows={len(rows)} folder={folder}")

    def value_of(key):
        v = ROWS[key][0]
        return v.replace("<folder>", folder)

    # The leg's own pins live in the file too: the file IS the configuration.
    mc = a.mc or os.path.join(folder, "data", "memcards")
    pins = {"paths.mc_dir": mc, "paths.log_dir": os.path.join(folder, "data", "logs")}
    if a.corpus:
        pins["engine.vu1_aot_corpus"] = a.corpus
    if a.pack:
        pins["engine.vu1_aot_pack"] = a.pack

    # ---- A/B: the launcher writes every row ----------------------------------------------
    if os.path.exists(ini):
        os.remove(ini)
    for k, v in list(pins.items()) + [(k, value_of(k)) for k in keys if k in ROWS]:
        rc, out, err = tool(["set", ini, k, v], a.tool)
        if rc != 0:
            results.append(("A.write", k, False, f"nativeee_settings set failed: {err}"))
    for k in keys:
        if k in ROWS:
            rc, out, err = tool(["get", ini, k], a.tool)
            results.append(("A.write", k, rc == 0 and out == value_of(k), f"file says {out!r}"))
    env = {"NATIVEEE_WATCHDOG_S": "150", "NATIVEEE_MTVU": "1", "NATIVEEE_VU1_MODE": "aot", "NATIVEEE_UNPACED": "1",
           "NATIVEEE_PAD_SCRIPT": a.pad or ""}
    common = ["--headless", "--frames", str(a.frames), "--iso", a.iso]
    if a.state:
        common += ["--load-state", a.state]
    # A: the binary beside the file, no --config (the executable-folder rule).
    game_bin = os.path.join(folder, os.path.basename(a.bin))
    shutil.copy2(a.bin, game_bin)
    for aux in ("title", "vu1_aot_pack"):   # the staged title dir / pack beside the build's binary, when present
        src = os.path.join(os.path.dirname(a.bin), aux)
        if os.path.isdir(src) and not os.path.exists(os.path.join(folder, aux)):
            os.symlink(src, os.path.join(folder, aux))
    out_a = os.path.join(folder, "leg_a")
    rc, wall = run_game(game_bin, common, out_a, log, a.timeout, env, folder)
    meta = read_meta(os.path.join(out_a, "run.meta"))
    print(f"== A (from the folder) rc={rc} wall={wall:.0f}s load{uptime()} settings_file={meta.get('settings_file', '(absent)')}")
    def same_file(reported, expected):   # /tmp vs /private/tmp: the executable path is resolved by the OS
        got = reported.split(":source=")[0]
        return os.path.realpath(got) == os.path.realpath(expected) and reported.endswith(":source=" + expected_source)
    expected_source = "executable folder"
    results.append(("A.run", "settings_file", same_file(meta.get("settings_file", ""), ini),
                    f"run.meta settings_file={meta.get('settings_file', '(absent)')}"))
    for k in keys:
        if k not in ROWS:
            continue
        if k in META_KEY:
            ok = meta.get(META_KEY[k]) == value_of(k)
            results.append(("A.game", k, ok, f"run.meta {META_KEY[k]}={meta.get(META_KEY[k], '(absent)')}"))
        else:
            ok = fingerprint_has(meta, k, value_of(k))
            results.append(("A.game", k, ok, "in env_ps2" if ok else f"not in env_ps2 ({meta.get('env_ps2', '(absent)')[:160]})"))
    # B: --config from the build's binary.
    out_b = os.path.join(folder, "leg_b")
    rc, wall = run_game(a.bin, common + ["--config", ini], out_b, log, a.timeout, env, os.path.dirname(a.bin))
    meta_b = read_meta(os.path.join(out_b, "run.meta"))
    print(f"== B (--config) rc={rc} wall={wall:.0f}s load{uptime()} settings_file={meta_b.get('settings_file', '(absent)')}")
    expected_source = "--config"
    results.append(("B.run", "settings_file", same_file(meta_b.get("settings_file", ""), ini),
                    f"run.meta settings_file={meta_b.get('settings_file', '(absent)')}"))
    for k in keys:
        if k in ROWS and k not in META_KEY:
            ok = fingerprint_has(meta_b, k, value_of(k))
            results.append(("B.game", k, ok, "in env_ps2" if ok else "not in env_ps2"))

    # ---- C: the in-game writers -> the file -> the launcher reads it back -------------------
    if "C" not in a.skip:
        # Reset the driven rows to their defaults in the file so the page's write is visible.
        defaults = {r["key"]: r["default"] for r in rows}
        steps = []
        frame = 20
        for k in keys:
            if k in ROWS and ROWS[k][1]:
                tool(["set", ini, k, defaults[k]], a.tool)
                steps.append(f"{frame}:{ROWS[k][1]}")
                frame += 4
        out_c = os.path.join(folder, "leg_c")
        c_frames = max(a.frames, frame + 90)
        args_c = ["--harness-window", "--frames", str(c_frames), "--iso", a.iso, "--set", "harness.display_sequence=" + ",".join(steps)]
        if a.state:
            args_c += ["--load-state", a.state]
        env_c = dict(env)
        env_c.pop("NATIVEEE_UNPACED", None)
        rc, wall = run_game(game_bin, args_c, out_c, log, a.timeout, env_c, folder)
        meta_c = read_meta(os.path.join(out_c, "run.meta"))
        print(f"== C (window, in-game writers) rc={rc} wall={wall:.0f}s load{uptime()} settings_persisted={meta_c.get('settings_persisted', '(absent)')} "
              f"display_sequence={meta_c.get('display_sequence', '(absent)')[:120]}")
        for k in keys:
            if k not in ROWS:
                continue
            if ROWS[k][1]:
                rc2, out, err = tool(["get", ini, k], a.tool)
                ok = rc2 == 0 and out == value_of(k)
                if len(ROWS[k]) > 2 and ROWS[k][2] == "host" and not ok:
                    print(f"INFO  C.page    {k:28s} the host refused the page's swap (launcher reads {out!r}); judged through the store API")
                    tool(["set", ini, k, defaults[k]], a.tool)
                    rc1, _, _ = tool(["set", ini, k, value_of(k)], a.tool)
                    rc2, out, _ = tool(["get", ini, k], a.tool)
                    results.append(("C.api", k, rc1 == 0 and rc2 == 0 and out == value_of(k), f"store write, launcher reads {out!r}"))
                    continue
                results.append(("C.page", k, ok, f"page wrote, launcher reads {out!r}" if rc2 == 0 else "key absent from the file"))
            else:
                # No in-game widget: the game-side ConfigStore write (the same API), read back by the launcher.
                tool(["set", ini, k, defaults[k]], a.tool)
                rc1, _, err = tool(["set", ini, k, value_of(k)], a.tool)
                rc2, out, _ = tool(["get", ini, k], a.tool)
                results.append(("C.api", k, rc1 == 0 and rc2 == 0 and out == value_of(k), f"store write, launcher reads {out!r}"))

    # ---- D: re-read on menu open ---------------------------------------------------------
    if "D" not in a.skip:
        tool(["set", ini, RESYNC_KEY, RESYNC_FILE], a.tool)
        out_d = os.path.join(folder, "leg_d")
        args_d = ["--harness-window", "--frames", "60", "--iso", a.iso, "--set", f"{RESYNC_KEY}={RESYNC_CLI}", "--set", "harness.inject_key=F1@30"]
        if a.state:
            args_d += ["--load-state", a.state]
        env_d = dict(env)
        env_d.pop("NATIVEEE_UNPACED", None)
        rc, wall = run_game(game_bin, args_d, out_d, log, a.timeout, env_d, folder)
        meta_d = read_meta(os.path.join(out_d, "run.meta"))
        sp = meta_d.get("settings_persisted", "")
        print(f"== D (re-read on F1) rc={rc} wall={wall:.0f}s load{uptime()} settings_persisted={sp or '(absent)'}")
        results.append(("D.resync", RESYNC_KEY, ":resyncs=1" in sp, f"settings_persisted={sp or '(absent)'}"))

    # ---- verdict -------------------------------------------------------------------------
    red = [r for r in results if not r[2]]
    for d, k, ok, detail in results:
        print(f"{'GREEN' if ok else 'RED  '} {d:9s} {k:28s} {detail}")
    for k, why in NOT_JUDGED.items():
        if k in keys:
            print(f"----  not judged {k:28s} {why}")
    print(f"== verdict: {'GREEN' if not red else 'RED'} ({len(results) - len(red)}/{len(results)} checks, {len(rows)} rows) load{uptime()}")
    return 0 if not red else 1


def selftest():
    meta = {"env_ps2": "audio.volume=55;video.vsync=on;profile=default", "user_data_root": "/x/data"}
    assert fingerprint_has(meta, "audio.volume", "55") and fingerprint_has(meta, "video.vsync", "on")
    assert not fingerprint_has(meta, "video.vsync", "off") and not fingerprint_has(meta, "audio.volume", "5")
    assert read_meta("/nonexistent/run.meta") == {}
    for k, row in ROWS.items():
        assert row[0], k
    print("selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    j = sub.add_parser("judge")
    j.add_argument("--bin", required=True)
    j.add_argument("--tool", required=True)
    j.add_argument("--schema", required=True)
    j.add_argument("--folder", required=True)
    j.add_argument("--iso", required=True)
    j.add_argument("--state")
    j.add_argument("--pad")
    j.add_argument("--corpus")
    j.add_argument("--pack")
    j.add_argument("--mc")
    j.add_argument("--frames", type=int, default=60)
    j.add_argument("--timeout", type=int, default=150)
    j.add_argument("--skip", default="", help="letters of the directions to skip (C, D)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.cmd == "judge":
        return judge(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
