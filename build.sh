#!/usr/bin/env bash
# build.sh -- configure + build NativeEE (the engine submodule) with the Sly 2 title module.
#
#   ./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"   your own disc image (NTSC-U, SCUS_973.16):
#                                   the build extracts the ELF from it (SHA-256 gated against
#                                   games/<title>/TITLE_INPUTS.sha256) and recompiles it
#   ./build.sh --elf /path/to/SCUS_973.16                      the loose ELF instead (faster iteration)
#   ./build.sh                       neither: the engine, tools and component tests only (title skipped)
#
#   --build-dir DIR    where to build (default: $SLY2_BUILD_DIR, else the sibling directory
#                      <this repository's directory name>-build/ next to the checkout, e.g. a clone at
#                      ~/src/Sly-2-Recomp builds in ~/src/Sly-2-Recomp-build; the build dir MUST be
#                      outside the repository -- it holds the extracted ELF and the recompiled code,
#                      which are game-derived)
#   --type TYPE        CMAKE_BUILD_TYPE (default Release)
#   --target T         build target(s) (default: nativeee; repeatable)
#   --configure-only   stop after configure
#   --jobs N           parallel jobs (default: ninja's)
#   NATIVEEE_DISC / NATIVEEE_TITLE_ELF in the environment are honoured when the flags are absent.
#
# The engine is the CMake SOURCE directory (its tree uses CMAKE_SOURCE_DIR for itself); this
# repository's games/ is passed as PS2R_GAMES_DIR so the title is auto-discovered from here.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
engine="$here/engine"
games="$here/games"
build_dir="${SLY2_BUILD_DIR:-$here/../$(basename "$here")-build}"
build_type=Release
disc="${NATIVEEE_DISC:-}"
elf="${NATIVEEE_TITLE_ELF:-}"
targets=()
configure_only=0
jobs=""
while [ $# -gt 0 ]; do
    case "$1" in
        --disc) disc="$2"; shift 2 ;;
        --elf) elf="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --type) build_type="$2"; shift 2 ;;
        --target) targets+=("$2"); shift 2 ;;
        --configure-only) configure_only=1; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ ! -f "$engine/CMakeLists.txt" ]; then
    echo "build.sh: engine submodule missing -- run: git submodule update --init" >&2
    exit 2
fi
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd -P)"
case "$build_dir" in
    "$(cd "$here" && pwd -P)"/*) echo "build.sh: the build dir must be outside the repository (it holds game-derived files): $build_dir" >&2; exit 2 ;;
esac
# PS2R_SIMD=ON: the engine's NEON lane kernels (the VU tiers' clamp / MAC-flag / FMAC lanes, the lazy
# MAC ring, BlockDecode) -- each proven byte-identical to its scalar twin by the engine's tests and, for
# the VU1 tier, by replaying this title's captured VU1 programs; the title runs ~1.8x more VU1 bundles/s with it.
args=(-S "$engine" -B "$build_dir" -G Ninja
      -DCMAKE_BUILD_TYPE="$build_type" -DPS2R_DIAGNOSTICS=OFF -DPS2R_SIMD=ON
      -DPS2R_GAMES_DIR="$games")
if [ -n "$disc" ]; then args+=(-DNATIVEEE_DISC="$disc" -DNATIVEEE_TITLE_ELF=); fi
if [ -n "$elf" ]; then args+=(-DNATIVEEE_TITLE_ELF="$elf"); fi
echo "build.sh: cmake ${args[*]}"
cmake "${args[@]}"
[ "$configure_only" = 1 ] && exit 0
[ ${#targets[@]} -eq 0 ] && targets=(nativeee)
build_args=(--build "$build_dir" --target "${targets[@]}")
[ -n "$jobs" ] && build_args+=(-j "$jobs")
cmake "${build_args[@]}"
echo "build.sh: binary at $build_dir/app/host/nativeee"
