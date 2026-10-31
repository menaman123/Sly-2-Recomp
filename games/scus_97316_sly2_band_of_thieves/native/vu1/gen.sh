#!/usr/bin/env bash
# gen.sh BUILD_DIR [--corpus DIR]... [--record]  -- regenerate Sly 2's native VU1 TUs by hand (what the build does itself) and,
# with --record, re-record MANIFEST.sha256 from the output (after a generator / corpus / images.txt change; say so in the commit).
#
# The build runs the same tool automatically (CMakeLists.txt: vu1_native_gen over NATIVEEE_VU1_CORPUS into
# <build>/generated/games/<title>/native/vu1/). A fresh build dir takes the corpus from the environment:
#     NATIVEEE_VU1_CORPUS="<dir1>;<dir2>" ./build.sh --disc <iso>
# or from the cache afterwards:  cmake -DNATIVEEE_VU1_CORPUS="<dir1>;<dir2>" <build dir>
# The corpus (Vu1Case records = the microprogram images + the kick state) is GAME DATA: keep it under
# ~/recomp-agent-data/scus_97316/corpora/vu1/, never in the repository. Capture more of it with the title's probe:
#     NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>:<count> nativeee ...   (hle/vu1_probe.h; delete the kicks_*.bin / *.txt sidecars)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${1:?gen.sh BUILD_DIR [--corpus DIR]... [--record]}"; shift
title="$(basename "$(cd "$here/../.." && pwd)")"
gen="$build/generated/games/$title/native/vu1"
tool="$build/games/$title/native/vu1/vu1_native_gen"
record=0; corpus=()
while [ $# -gt 0 ]; do case "$1" in --corpus) corpus+=(--corpus "$2"); shift 2;; --record) record=1; shift;; *) echo "gen.sh: unknown $1" >&2; exit 2;; esac; done
if [ ${#corpus[@]} -eq 0 ]; then
    IFS=';' read -r -a dirs <<< "$(grep '^NATIVEEE_VU1_CORPUS:' "$build/CMakeCache.txt" | cut -d= -f2-)"
    for d in "${dirs[@]}"; do [ -n "$d" ] && corpus+=(--corpus "$d"); done
fi
[ ${#corpus[@]} -gt 0 ] || { echo "gen.sh: no corpus (give --corpus DIR or configure NATIVEEE_VU1_CORPUS)" >&2; exit 2; }
[ -x "$tool" ] || cmake --build "$build" --target vu1_native_gen
mkdir -p "$gen"
"$tool" "${corpus[@]}" --out "$gen" --images "$here/images.txt" --chain 1
if [ "$record" = 1 ]; then
    (cd "$gen" && shasum -a 256 vu1_*.cpp vu1_native_images.cpp vu1_native_images.txt) > "$here/MANIFEST.sha256"
    echo "gen.sh: MANIFEST.sha256 re-recorded ($(wc -l < "$here/MANIFEST.sha256" | tr -d ' ') rows)"
fi
