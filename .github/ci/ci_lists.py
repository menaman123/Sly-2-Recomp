#!/usr/bin/env python3
"""ci_lists.py -- the two CI lists of this repository, read by build.sh, build.ps1 and ci.yml.

  ci_lists.py exclude-regex            print the `ctest -E` regex: ctest_exclusions.txt joined with '|'
  ci_lists.py audit-build <build.log>  the `all` build ran with `ninja -k 0`: every `FAILED:` line of
                                       the log must name a target listed in known_broken_targets.txt,
                                       else exit 1. Exit 0 prints which listed targets did NOT fail
                                       (rows to remove once the engine fixes them).
  ci_lists.py --selftest

Both lists live beside this script (.github/ci/). Lines starting with '#' and blank lines are
comments. No game data is involved anywhere here: the lists name tests and CMake targets.
"""
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXCLUSIONS = os.path.join(HERE, "ctest_exclusions.txt")
KNOWN_BROKEN = os.path.join(HERE, "known_broken_targets.txt")

# ninja >= 1.12: "FAILED: [code=1] app/gui/CMakeFiles/x.dir/a.cpp.o b.o"; older: "FAILED: app/gui/x".
_FAILED = re.compile(r"^FAILED:\s*(?:\[code=\d+\]\s*)?(.*)$")
_OBJ_DIR = re.compile(r"CMakeFiles/([^/]+)\.dir/")


def read_list(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip() if not line.lstrip().startswith("#") else ""
            if line:
                rows.append(line)
    return rows


def exclude_regex(path=EXCLUSIONS):
    rows = read_list(path)
    if not rows:
        sys.exit("ci_lists.py: %s names no test" % path)
    return "|".join(rows)


def target_of(output_path):
    """The CMake target a ninja output path belongs to."""
    p = output_path.replace("\\", "/")
    m = _OBJ_DIR.search(p)
    if m:
        return m.group(1)
    base = p.rsplit("/", 1)[-1]
    for suffix in (".exe", ".a", ".lib", ".dylib", ".so"):
        if base.endswith(suffix):
            base = base[: -len(suffix)]
            if suffix != ".exe" and base.startswith("lib"):
                base = base[3:]           # libps2r_product_clock.a -> ps2r_product_clock
            break
    return base


def failed_targets(log_text):
    found = []
    for line in log_text.splitlines():
        m = _FAILED.match(line.strip())
        if not m:
            continue
        outputs = m.group(1).strip()
        if not outputs:
            continue
        first = outputs.split()[0]
        t = target_of(first)
        if t not in found:
            found.append(t)
    return found


def audit(log_path, known_path=KNOWN_BROKEN):
    with open(log_path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    known = read_list(known_path)
    failed = failed_targets(text)
    unknown = [t for t in failed if t not in known]
    healed = [t for t in known if t not in failed]
    print("ci_lists.py audit-build: %d failed target(s): %s" % (len(failed), ", ".join(failed) or "-"))
    if unknown:
        print("ci_lists.py audit-build: NOT in known_broken_targets.txt: %s" % ", ".join(unknown))
        return 1
    if healed:
        print("ci_lists.py audit-build: listed but built fine (remove the rows): %s" % ", ".join(healed))
    print("ci_lists.py audit-build: OK -- every failure is a known-broken target")
    return 0


def selftest():
    sample = (
        "[1/5] Building CXX object a/b.o\n"
        "FAILED: [code=1] app/gui/CMakeFiles/test_session_restart.dir/__/host/session_restart.cpp.o \n"
        "FAILED: app/gui/CMakeFiles/ps2r_product_clock.dir/__/__/engine/ee/timing/tick_sync.cpp.o\n"
        "FAILED: [code=1] app/gui/test_session_restart_runtime \n"
        "FAILED: [code=1] tests/vu/test_vu_unit.exe\n"
        "ninja: build stopped: cannot make progress due to previous errors.\n"
    )
    got = failed_targets(sample)
    assert got == ["test_session_restart", "ps2r_product_clock", "test_session_restart_runtime", "test_vu_unit"], got
    assert failed_targets("[3/3] Linking CXX executable x\n") == []
    with tempfile.TemporaryDirectory() as d:
        known = os.path.join(d, "known.txt")
        with open(known, "w") as f:
            f.write("# c\ntest_session_restart\nps2r_product_clock\ntest_session_restart_runtime\n")
        log = os.path.join(d, "log")
        with open(log, "w") as f:
            f.write(sample)
        assert audit(log, known) == 1          # test_vu_unit is not listed
        with open(log, "w") as f:
            f.write(sample.replace("FAILED: [code=1] tests/vu/test_vu_unit.exe\n", ""))
        assert audit(log, known) == 0
        with open(log, "w") as f:
            f.write("[1/1] ok\n")
        assert audit(log, known) == 0          # nothing failed: every row is "healed", still OK
        ex = os.path.join(d, "ex.txt")
        with open(ex, "w") as f:
            f.write("# header\n^Root\n\n^Og   # trailing comment\n^Foo$\n")
        assert exclude_regex(ex) == "^Root|^Og|^Foo$", exclude_regex(ex)
    # the committed lists parse and are non-empty
    assert read_list(KNOWN_BROKEN)
    r = exclude_regex()
    re.compile(r)
    assert "^Root" in r
    print("ci_lists.py: selftest OK")
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == "exclude-regex":
        print(exclude_regex())
        return 0
    if len(argv) == 3 and argv[1] == "audit-build":
        return audit(argv[2])
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
