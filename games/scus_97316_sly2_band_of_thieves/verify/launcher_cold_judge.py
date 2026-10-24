#!/usr/bin/env python3
"""launcher_cold_judge.py -- the COLD-START judge of the prepared-game pipeline (the launcher stage, decision D-A).

A player with nothing (an empty HOME), a disc image in a folder and this release: the launcher (or the
headless CLI that the launcher wraps) must turn the disc into a running game, downloading only the
release's engine bundle + compiler, never anything derived from the game -- and the game it makes must
be the developer build of the same release.

  launcher_cold_judge.py --build <dev build dir> --fixture <dir> --iso <disc image> --out <dir>
                         [--driver cli|launcher] [--launcher <exe>] [--jobs N] [--frames 600]
                         [--state <.ps2st>] [--pad <.pad>] [--release TAG] [--port P] [--keep]
  launcher_cold_judge.py --selftest

Fixture: <dir> holds engine-bundle-<platform>.tar.xz + compiler-<platform>.tar.xz (tools/ci/make_compiler_bundle.py);
the judge writes releases.json + SHA256SUMS over them (tools/ci/make_release_index.py, the GitHub Releases API
shape) and serves the directory with `python3 -m http.server` on 127.0.0.1 -- the offline model of a release.

Drivers (the same contract, so the judge drives either):
  cli       <build>/tools/prepare/nativeee-prepare --index URL --iso ISO --folder DIR --jobs N --json REPORT
  launcher  <launcher exe> --prepare --index URL --iso ISO --folder DIR --jobs N --json REPORT   (the launcher's
            headless mode over ps2r_prepare; it must print the same report and exit 0 when the game is ready)

Gates (RED/GREEN each; exit 0 only when all are GREEN):
  prepared   the driver exits 0, the binary exists, report ok=true, manifest_ok=true, cache_hit=false
  download   bytes_downloaded == the two archives' sizes (nothing else was fetched; the index + SHA256SUMS are
             not counted by the tool), and no file under <folder>/cache/downloads is game-derived (asset_scan names)
  hash gate  (a) every row of games/<title>/recomp/MANIFEST.sha256 and iop/*/MANIFEST.sha256 equals the sha256 of the
                 generated file under <folder>/cache/work/<tag>/gen/ (same tool + same disc = same text)
             (b) `<binary> --version` == `<build>/app/host/nativeee --version` (the BUILD-ID the bundle carried)
             (c) prepared.json vu1_aot_abi_key == <build>/generated/vu1_aot_abi_key.h (the VU1 AOT ABI)
  boot       the prepared binary, headless with <folder>/nativeee.ini, cold boot --frames N within 60 s: run.meta
             title=<serial>, every hosted IOP module loaded, exit 0
  replay     the standing harness: --load-state <state> + NATIVEEE_PAD_SCRIPT=<pad>, --frames N, headless, within
             60 s -- its mem.mhash and audio.ahash BYTE-IDENTICAL to the developer build's run with the same
             arguments (the ABI equality in the only form a headless run can show)
  again      a second driver run for the same disc + release finishes with cache_hit=true in < 20 s and the
             binary's sha256 is unchanged

Records: wall time of the prepare, bytes downloaded, prepare minutes, the phase seconds, `uptime` before and
after, into <out>/verdict.json + <out>/verdict.md. Frozen: the gates do not move; a new gate is a new judge.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TITLE_DIR = os.path.dirname(HERE)
TITLE = os.path.basename(TITLE_DIR)
GAME_DERIVED_NAMES = re.compile(r"(\.iso|\.bin|\.img|\.elf|\.irx|\.pss|\.vag|SCUS_|SLUS_|SLES_|IOPRP)", re.I)


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def uptime():
    try:
        return subprocess.run(["uptime"], capture_output=True, text=True).stdout.strip()
    except Exception:
        return ""


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def engine_root(build):
    cache = os.path.join(build, "CMakeCache.txt")
    if os.path.exists(cache):
        for ln in open(cache):
            if ln.startswith("CMAKE_HOME_DIRECTORY:"):
                return ln.split("=", 1)[1].strip()
    return ""


def host_platform():
    import platform as pl
    s = pl.system().lower()
    m = pl.machine().lower()
    arch = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "x86_64", "amd64": "x86_64"}.get(m, m)
    return {"darwin": "macos", "windows": "windows", "linux": "linux"}.get(s, s) + "-" + arch


def manifest_rows(path):
    rows = []
    for ln in open(path):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        parts = ln.split()
        if len(parts) >= 2 and len(parts[0]) == 64:
            rows.append((parts[0], parts[1].lstrip("*")))
    return rows


def run_bounded(engine, timeout, log, argv, env):
    rb = os.path.join(engine, "verify", "run_bounded.sh")
    t0 = time.time()
    rc = subprocess.run([rb, str(timeout), log, "--"] + argv, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    return rc, time.time() - t0


def meta_rows(path):
    rows = {}
    if os.path.exists(path):
        for ln in open(path, errors="replace"):
            ln = ln.strip()
            if "=" in ln:
                k, v = ln.split("=", 1)
                rows[k.strip()] = v
    return rows


class Verdict:
    def __init__(self):
        self.gates = []
        self.facts = {}

    def gate(self, name, ok, detail):
        self.gates.append({"gate": name, "verdict": "GREEN" if ok else "RED", "detail": detail})
        print("%-10s %s  %s" % (name, "GREEN" if ok else "RED", detail), flush=True)
        return ok

    def all_green(self):
        return all(g["verdict"] == "GREEN" for g in self.gates)


def selftest():
    # the manifest reader + the game-derived name filter + the index shape the judge expects
    with tempfile.TemporaryDirectory() as d:
        m = os.path.join(d, "M.sha256")
        open(m, "w").write("# c\n" + "a" * 64 + "  recompiled.h\n" + "b" * 64 + " *x.cpp\nbad row\n")
        rows = manifest_rows(m)
        assert rows == [("a" * 64, "recompiled.h"), ("b" * 64, "x.cpp")], rows
    assert GAME_DERIVED_NAMES.search("SCUS_973.16") and GAME_DERIVED_NAMES.search("x.iso") and not GAME_DERIVED_NAMES.search("engine-bundle-macos-arm64.tar.xz")
    v = Verdict()
    v.gate("a", True, "x")
    v.gate("b", False, "y")
    assert not v.all_green()
    print("launcher_cold_judge selftest: PASS")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build")
    ap.add_argument("--fixture")
    ap.add_argument("--iso")
    ap.add_argument("--out")
    ap.add_argument("--driver", choices=["cli", "launcher"], default="cli")
    ap.add_argument("--launcher", default="")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--state", default="")
    ap.add_argument("--pad", default="")
    ap.add_argument("--release", default="v0.0.0-judge")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not (a.build and a.fixture and a.iso and a.out):
        ap.error("--build, --fixture, --iso and --out are required")
    build = os.path.realpath(a.build)
    engine = engine_root(build)
    if not engine:
        sys.exit("launcher_cold_judge: %s has no CMakeCache.txt (CMAKE_HOME_DIRECTORY)" % build)
    os.makedirs(a.out, exist_ok=True)
    V = Verdict()
    V.facts["uptime_before"] = uptime()
    V.facts["build"] = build
    V.facts["engine"] = engine
    V.facts["driver"] = a.driver
    platform = host_platform()

    # --- the fixture server ---------------------------------------------------------------------
    port = a.port or free_port()
    base = "http://127.0.0.1:%d" % port
    mri = os.path.join(engine, "tools", "ci", "make_release_index.py")
    if os.path.exists(mri):
        idx = subprocess.run([sys.executable, mri, "--dir", a.fixture, "--tag", a.release, "--base-url", base], capture_output=True, text=True)
        if idx.returncode != 0:
            sys.exit("launcher_cold_judge: make_release_index failed: %s" % idx.stderr)
    else:   # a branch without the engine's script (the RED run): the judge writes the same shape itself
        assets = []
        for n in sorted(os.listdir(a.fixture)):
            p = os.path.join(a.fixture, n)
            if os.path.isfile(p) and n not in ("releases.json", "SHA256SUMS") and not n.endswith(".sha256"):
                assets.append({"name": n, "size": os.path.getsize(p), "browser_download_url": base + "/" + n, "digest": "sha256:" + sha256_file(p)})
        json.dump([{"tag_name": a.release, "draft": False, "prerelease": False, "assets": assets}], open(os.path.join(a.fixture, "releases.json"), "w"), indent=2)
    index = json.load(open(os.path.join(a.fixture, "releases.json")))
    sizes = {x["name"]: x["size"] for x in index[0]["assets"]}
    want = ["engine-bundle-%s.tar.xz" % platform, "compiler-%s.tar.xz" % platform]
    for w in want:
        if w not in sizes:
            sys.exit("launcher_cold_judge: fixture lacks %s" % w)
    server = subprocess.Popen([sys.executable, "-m", "http.server", str(port), "--bind", "127.0.0.1"], cwd=a.fixture,
                              stdout=open(os.path.join(a.out, "fixture_server.log"), "w"), stderr=subprocess.STDOUT)
    time.sleep(0.8)
    # an empty HOME and the player's folder
    home = tempfile.mkdtemp(prefix="cold_home_")
    folder = os.path.join(a.out, "game")
    shutil.rmtree(folder, ignore_errors=True)
    env = dict(os.environ, HOME=home)
    for k in list(env):
        if k.startswith("NATIVEEE_") or k.startswith("PS2R_"):
            del env[k]
    try:
        # --- the driver -----------------------------------------------------------------------------
        report_path = os.path.join(a.out, "prepare_report.json")
        if a.driver == "cli":
            exe = os.path.join(build, "tools", "prepare", "nativeee-prepare")
            drv = [exe]
        else:
            exe = a.launcher
            drv = [exe, "--prepare"]
        if not exe or not os.path.exists(exe):
            V.gate("prepared", False, "no driver executable: %s (the branch has no prepare tool)" % exe)
            V.facts["uptime_after"] = uptime()
            return finish(V, a.out)
        cmd = drv + ["--index", base + "/releases.json", "--iso", a.iso, "--folder", folder, "--jobs", str(a.jobs), "--json", report_path]
        t0 = time.time()
        with open(os.path.join(a.out, "prepare_stderr.log"), "w") as log:
            rc = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=log, text=True).returncode
        wall = time.time() - t0
        rep = json.load(open(report_path)) if os.path.exists(report_path) else {}
        binary = rep.get("binary_path", "")
        V.facts["prepare_wall_s"] = round(wall, 1)
        V.facts["prepare_minutes"] = round(wall / 60.0, 2)
        V.facts["prepare_report"] = rep
        ok = rc == 0 and bool(binary) and os.path.exists(binary) and rep.get("ok") is True and rep.get("manifest_ok") is True and not rep.get("cache_hit")
        V.gate("prepared", ok, "rc=%d wall=%.1fs (%.1f min) binary=%s manifest_ok=%s cache_hit=%s phases=%s" %
               (rc, wall, wall / 60, os.path.basename(binary) if binary else "-", rep.get("manifest_ok"), rep.get("cache_hit"),
                {k: round(v, 1) for k, v in rep.get("phase_seconds", {}).items()}))
        if not ok:
            V.facts["uptime_after"] = uptime()
            return finish(V, a.out)
        tag = rep.get("release_tag", a.release)

        # --- download ------------------------------------------------------------------------------
        expected = sum(sizes[w] for w in want)
        got = rep.get("bytes_downloaded", -1)
        dl_dir = os.path.join(folder, "cache", "downloads")
        bad_names = [n for n in os.listdir(dl_dir) if GAME_DERIVED_NAMES.search(n)] if os.path.isdir(dl_dir) else ["(no downloads dir)"]
        V.facts["bytes_downloaded"] = got
        V.facts["bytes_expected"] = expected
        V.gate("download", abs(got - expected) <= 4096 and not bad_names,
               "bytes_downloaded=%d expected=%d (bundle %d + compiler %d); game-derived names in downloads: %s" %
               (got, expected, sizes[want[0]], sizes[want[1]], bad_names or "none"))

        # --- the hash gate ---------------------------------------------------------------------------
        gen = os.path.join(folder, "cache", "work", tag, "gen")
        mism = []
        rows = 0
        for mf, sub in [(os.path.join(TITLE_DIR, "recomp", "MANIFEST.sha256"), "recomp")] + \
                [(os.path.join(TITLE_DIR, "iop", d, "MANIFEST.sha256"), os.path.join("iop", d))
                 for d in sorted(os.listdir(os.path.join(TITLE_DIR, "iop"))) if os.path.exists(os.path.join(TITLE_DIR, "iop", d, "MANIFEST.sha256"))]:
            for digest, name in manifest_rows(mf):
                rows += 1
                p = os.path.join(gen, sub, name)
                if not os.path.exists(p) or sha256_file(p) != digest:
                    mism.append(os.path.join(sub, name))
        dev_bin = os.path.join(build, "app", "host", "nativeee")
        ver_prep = subprocess.run([binary, "--version"], capture_output=True, text=True).stdout.strip()
        ver_dev = subprocess.run([dev_bin, "--version"], capture_output=True, text=True).stdout.strip()
        abi_dev = ""
        kp = os.path.join(build, "generated", "vu1_aot_abi_key.h")
        if os.path.exists(kp):
            m = re.search(r'"(abi-[0-9a-f]+)"', open(kp).read())
            abi_dev = m.group(1) if m else ""
        stamp = json.load(open(os.path.join(folder, "cache", "prepared.json")))
        V.facts["build_id_prepared"] = ver_prep
        V.facts["build_id_dev"] = ver_dev
        V.gate("hash gate", not mism and rows > 0 and ver_prep == ver_dev and bool(ver_prep) and stamp.get("vu1_aot_abi_key") == abi_dev and bool(abi_dev),
               "%d manifest rows, %d mismatch%s; BUILD-ID prepared=%r dev=%r; abi key %s vs %s" %
               (rows, len(mism), (": " + ", ".join(mism[:5])) if mism else "", ver_prep, ver_dev, stamp.get("vu1_aot_abi_key"), abi_dev))

        # --- boot + replay (one game process at a time, <= 60 s each) ----------------------------------
        ini = os.path.join(folder, "nativeee.ini")
        base_env = dict(env, NATIVEEE_WATCHDOG_S="55", NATIVEEE_MTVU="1", NATIVEEE_VU1_MODE="aot", NATIVEEE_UNPACED="1", NATIVEEE_PAD_SCRIPT="")

        def leg(label, exe_, extra, pad):
            out = os.path.join(a.out, label)
            shutil.rmtree(out, ignore_errors=True)
            e = dict(base_env, NATIVEEE_PAD_SCRIPT=pad or "")
            rc_, w = run_bounded(engine, 60, os.path.join(a.out, label + ".log"),
                                 [exe_, "--headless", "--frames", str(a.frames), "--harness-out", out, "--iso", a.iso, "--config", ini] + extra, e)
            return rc_, w, out

        rc_b, w_b, out_b = leg("boot_prepared", binary, [], "")
        meta = meta_rows(os.path.join(out_b, "run.meta"))
        hosted = meta.get("iop_hosted", "")
        loaded = hosted.count(":loaded:")
        V.facts["boot_wall_s"] = round(w_b, 1)
        V.gate("boot", rc_b == 0 and meta.get("title", "").startswith("S") and loaded >= 1 and "FAIL" not in hosted,
               "rc=%d wall=%.1fs title=%s hosted modules loaded=%d boundaries=%s vu1_aot_codegen=%s" %
               (rc_b, w_b, meta.get("title", "?"), loaded, meta.get("boundaries", "?"), (meta.get("vu1_aot_codegen", "")[:120])))

        if a.state and a.pad and os.path.exists(a.state) and os.path.exists(a.pad):
            rc_p, w_p, out_p = leg("replay_prepared", binary, ["--load-state", a.state], a.pad)
            rc_d, w_d, out_d = leg("replay_dev", dev_bin, ["--load-state", a.state], a.pad)
            same = {}
            for f in ("mem.mhash", "audio.ahash"):
                pp, dd = os.path.join(out_p, f), os.path.join(out_d, f)
                same[f] = os.path.exists(pp) and os.path.exists(dd) and os.path.getsize(pp) > 0 and open(pp, "rb").read() == open(dd, "rb").read()
            V.facts["replay_wall_s"] = {"prepared": round(w_p, 1), "dev": round(w_d, 1)}
            V.gate("replay", rc_p == 0 and rc_d == 0 and all(same.values()),
                   "prepared rc=%d %.1fs, dev rc=%d %.1fs; identical: %s (%d mem rows, %d audio rows)" %
                   (rc_p, w_p, rc_d, w_d, same, sum(1 for _ in open(os.path.join(out_p, "mem.mhash"))) if os.path.exists(os.path.join(out_p, "mem.mhash")) else 0,
                    sum(1 for _ in open(os.path.join(out_p, "audio.ahash"))) if os.path.exists(os.path.join(out_p, "audio.ahash")) else 0))
        else:
            V.gate("replay", False, "no --state/--pad given (the standing harness: states/hub_ep1_fix.ps2st + pads/sm_paris.pad)")

        # --- again: the cache hit ------------------------------------------------------------------------
        sha_before = sha256_file(binary)
        report2 = os.path.join(a.out, "prepare_report_2.json")
        t0 = time.time()
        with open(os.path.join(a.out, "prepare_stderr_2.log"), "w") as log:
            rc2 = subprocess.run(drv + ["--index", base + "/releases.json", "--iso", a.iso, "--folder", folder, "--jobs", str(a.jobs), "--json", report2],
                                 env=env, stdout=subprocess.PIPE, stderr=log, text=True).returncode
        w2 = time.time() - t0
        rep2 = json.load(open(report2)) if os.path.exists(report2) else {}
        V.facts["again_wall_s"] = round(w2, 1)
        V.gate("again", rc2 == 0 and rep2.get("cache_hit") is True and w2 < 20 and sha256_file(binary) == sha_before and rep2.get("bytes_downloaded", 1) == 0,
               "rc=%d wall=%.1fs cache_hit=%s bytes_downloaded=%s binary unchanged=%s" %
               (rc2, w2, rep2.get("cache_hit"), rep2.get("bytes_downloaded"), sha256_file(binary) == sha_before))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except Exception:
            server.kill()
        if not a.keep:
            shutil.rmtree(home, ignore_errors=True)
    V.facts["uptime_after"] = uptime()
    return finish(V, a.out)


def finish(V, out):
    verdict = {"judge": "launcher_cold_judge", "title": TITLE, "all_green": V.all_green(), "gates": V.gates, "facts": V.facts}
    with open(os.path.join(out, "verdict.json"), "w") as f:
        json.dump(verdict, f, indent=2)
        f.write("\n")
    lines = ["# launcher_cold_judge -- %s" % ("GREEN" if V.all_green() else "RED"), "", "| gate | verdict | detail |", "|---|---|---|"]
    for g in V.gates:
        lines.append("| %s | %s | %s |" % (g["gate"], g["verdict"], g["detail"].replace("|", "/")))
    lines += ["", "facts:", "```", json.dumps({k: v for k, v in V.facts.items() if k != "prepare_report"}, indent=2), "```"]
    with open(os.path.join(out, "verdict.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("launcher_cold_judge: %s (%s)" % ("GREEN" if V.all_green() else "RED", os.path.join(out, "verdict.md")))
    return 0 if V.all_green() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
