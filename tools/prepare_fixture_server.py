#!/usr/bin/env python3
"""prepare_fixture_server.py -- the OFFLINE release for the launcher and the judge.

  prepare_fixture_server.py --dir <assets> [--port 8765] [--tag v0.0.0-local]

<assets> holds engine-bundle-<platform>.tar.xz (cmake --build <build> --target engine_bundle, then
tar -cJf) and compiler-<platform>.tar.xz (engine/tools/ci/make_compiler_bundle.py). This writes the
GitHub-Releases-shaped releases.json + SHA256SUMS over them (engine/tools/ci/make_release_index.py)
and serves the directory on 127.0.0.1 -- point the launcher's release index at
http://127.0.0.1:<port>/releases.json. Nothing here is game-derived; the player's disc never leaves
the player's machine.
"""
import argparse
import http.server
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--tag", default="v0.0.0-local")
    a = ap.parse_args(argv)
    mri = os.path.join(ROOT, "engine", "tools", "ci", "make_release_index.py")
    base = "http://127.0.0.1:%d" % a.port
    subprocess.run([sys.executable, mri, "--dir", a.dir, "--tag", a.tag, "--base-url", base], check=True)
    os.chdir(a.dir)
    print("serving %s at %s/releases.json (Ctrl-C to stop)" % (a.dir, base), flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", a.port), http.server.SimpleHTTPRequestHandler).serve_forever()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
