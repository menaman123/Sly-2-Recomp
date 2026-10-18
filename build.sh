#!/usr/bin/env bash
# build.sh -- configure + build NativeEE (the engine submodule) with the Sly 2 title module.
#
#   ./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"   your own disc image (NTSC-U, SCUS_973.16):
#                                   the build extracts the ELF from it (SHA-256 gated against
#                                   games/<title>/TITLE_INPUTS.sha256) and recompiles it
#   ./build.sh --elf /path/to/SCUS_973.16                      the loose ELF instead (faster iteration)
#   ./build.sh --no-disc             no game file: the engine, the tools, the title's TitleProfile library
#                                   and the component tests only (what CI builds; the title's recompiled
#                                   code and its play composition are skipped). NATIVEEE_DISC /
#                                   NATIVEEE_TITLE_ELF in the environment are ignored in this mode.
#   ./build.sh                       neither flag: like --no-disc unless NATIVEEE_DISC / NATIVEEE_TITLE_ELF
#                                   are set in the environment
#   ./build.sh --disc IMAGE --dist   ... and stage the release folder (see --dist below; what release.yml ships)
#
#   --preset NAME      an ENGINE configure preset (engine/CMakePresets.json: macos-release, linux-release,
#                      linux-release-clang, linux-release-gcc, macos-dev, ...). Default: this host's release
#                      preset (macos-release on Darwin, linux-release on Linux). The preset supplies the
#                      per-platform toolchain (the Linux link-group rule, compilers); its binaryDir is
#                      overridden by --build-dir. `--preset none` configures with plain Ninja + --type.
#   --build-dir DIR    where to build (default: $SLY2_BUILD_DIR, else the sibling directory
#                      <this repository's directory name>-build/ next to the checkout, e.g. a clone at
#                      ~/src/Sly-2-Recomp builds in ~/src/Sly-2-Recomp-build; the build dir MUST be
#                      outside the repository -- it holds the extracted ELF and the recompiled code,
#                      which are game-derived)
#   --type TYPE        CMAKE_BUILD_TYPE (default Release)
#   --target T         build target(s) (default: nativeee; repeatable)
#   --tests            build the whole tree (`all`, ninja -k 0 -- the targets in
#                      .github/ci/known_broken_targets.txt may fail, anything else failing is an error)
#                      and run the component suite: ctest -E <.github/ci/ctest_exclusions.txt>
#   --configure-only   stop after configure
#   --jobs N           parallel jobs (default: ninja's)
#   --dist             after the build, stage the release folder (needs a disc / ELF build: the artefact IS the
#                      recompiled title): .github/ci/stage_dist.sh -> <dist root>/<macos|linux-<arch>>/ +
#                      nativeee-<platform>.{zip,tar.gz}, licence-linted and asset-scanned. The default preset
#                      becomes the engine's self-contained one (macos-dist / linux-dist: SDL2 built from the pinned
#                      release, the LGPL FFmpeg of engine/tools/ci/ffmpeg_static.sh built into
#                      <build dir>/third_party/ffmpeg -- both linked statically; Linux also libstdc++/libgcc)
#   --dist-only        only (re)stage from an existing build in --build-dir
#   --dist-root DIR    where the folder and archive go (default: $SLY2_DIST_DIR, else <build dir>/dist -- outside
#                      the repository, like the build dir: the binary is game-derived)
#
# Acceptance shape (the engine's rule): 0 FAILED, 0 `warning:`, 0 `ld: warning` in the build log;
# the script fails on any.
#
# The engine is the CMake SOURCE directory (its tree uses CMAKE_SOURCE_DIR for itself); this
# repository's games/ is passed as PS2R_GAMES_DIR so the title is auto-discovered from here.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
engine="$here/engine"
games="$here/games"
ci_lists="$here/.github/ci/ci_lists.py"
build_dir="${SLY2_BUILD_DIR:-$here/../$(basename "$here")-build}"
build_type=Release
disc="${NATIVEEE_DISC:-}"
elf="${NATIVEEE_TITLE_ELF:-}"
no_disc=0
disc_flag=0
elf_flag=0
preset=""
targets=()
configure_only=0
run_tests=0
jobs=""
run_dist=0
dist_only=0
dist_root="${SLY2_DIST_DIR:-}"
while [ $# -gt 0 ]; do
    case "$1" in
        --disc) disc="$2"; disc_flag=1; shift 2 ;;
        --elf) elf="$2"; elf_flag=1; shift 2 ;;
        --no-disc) no_disc=1; shift ;;
        --preset) preset="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --type) build_type="$2"; shift 2 ;;
        --target) targets+=("$2"); shift 2 ;;
        --tests) run_tests=1; shift ;;
        --configure-only) configure_only=1; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        --dist) run_dist=1; shift ;;
        --dist-only) run_dist=1; dist_only=1; shift ;;
        --dist-root) dist_root="$2"; shift 2 ;;
        -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
        *) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ ! -f "$engine/CMakeLists.txt" ]; then
    echo "build.sh: engine submodule missing -- run: git submodule update --init" >&2
    exit 2
fi
if [ "$no_disc" = 1 ]; then
    if [ "$disc_flag" = 1 ] || [ "$elf_flag" = 1 ]; then
        echo "build.sh: --no-disc excludes --disc / --elf" >&2; exit 2
    fi
    disc=""; elf=""
fi
if [ "$run_dist" = 1 ] && [ "$no_disc" = 1 ]; then
    echo "build.sh: --dist needs a disc / ELF build (the artefact is the recompiled title); --no-disc excludes it" >&2; exit 2
fi
if [ -z "$preset" ]; then
    case "$(uname -s)" in
        Darwin) preset=macos-release; [ "$run_dist" = 1 ] && preset=macos-dist ;;
        Linux) preset=linux-release; [ "$run_dist" = 1 ] && preset=linux-dist ;;
        *) preset=none ;;
    esac
fi
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd -P)"
case "$build_dir" in
    "$(cd "$here" && pwd -P)"/*) echo "build.sh: the build dir must be outside the repository (it holds game-derived files): $build_dir" >&2; exit 2 ;;
esac
# PS2R_SIMD=ON: the engine's NEON lane kernels (the VU tiers' clamp / MAC-flag / FMAC lanes, the lazy
# MAC ring, BlockDecode) -- each proven byte-identical to its scalar twin by the engine's tests and, for
# the VU1 tier, by replaying this title's captured VU1 programs; the title runs ~1.8x more VU1 bundles/s with it.
# The sources gate every NEON body on __aarch64__: on x86-64 the flag selects the scalar twins.
args=(-S "$engine" -B "$build_dir")
if [ "$preset" != none ]; then
    # The engine's preset (generator, toolchain file, compilers, the Release shape); -S/-B and the -D
    # values below override its binaryDir and cache variables (CMake >= 3.21 allows this).
    args+=(--preset "$preset")
else
    args+=(-G Ninja)
fi
args+=(-DCMAKE_BUILD_TYPE="$build_type" -DPS2R_DIAGNOSTICS=OFF -DPS2R_SIMD=ON -DPS2R_GAMES_DIR="$games")
if [ -n "$disc" ]; then args+=(-DNATIVEEE_DISC="$disc" -DNATIVEEE_TITLE_ELF=); fi
if [ -n "$elf" ]; then args+=(-DNATIVEEE_TITLE_ELF="$elf"); fi
if [ -z "$disc" ] && [ -z "$elf" ]; then
    # A build dir configured earlier WITH a disc keeps its cache; a --no-disc run must not inherit it.
    args+=(-DNATIVEEE_DISC= -DNATIVEEE_TITLE_ELF=)
fi
[ -z "$dist_root" ] && dist_root="$build_dir/dist"
if [ "$dist_only" = 0 ]; then
    case "$preset" in
        macos-dist|linux-dist)
            # The engine's self-contained presets name a static FFmpeg prefix under THEIR binaryDir
            # (engine/build-<preset>-dist/third_party/ffmpeg); the build dir is ours, so the prefix is built here
            # (engine/tools/ci/ffmpeg_static.sh: cached when its recipe record matches) and the preset's
            # CMAKE_PREFIX_PATH is overridden to it.
            echo "build.sh: ffmpeg static prefix (engine/tools/ci/ffmpeg_static.sh -> $build_dir/third_party/ffmpeg)"
            "$engine/tools/ci/ffmpeg_static.sh" "$build_dir/third_party/ffmpeg"
            args+=(-DCMAKE_PREFIX_PATH="$build_dir/third_party/ffmpeg") ;;
    esac
    echo "build.sh: cmake ${args[*]}"
    cmake "${args[@]}"
    [ "$configure_only" = 1 ] && exit 0
fi

stage_dist() {
    [ "$run_dist" = 1 ] || return 0
    local stage_args=(--repo "$here" --build-dir "$build_dir" --dist-root "$dist_root" --preset "$preset")
    echo "build.sh: .github/ci/stage_dist.sh ${stage_args[*]}"
    bash "$here/.github/ci/stage_dist.sh" "${stage_args[@]}"
}
if [ "$dist_only" = 1 ]; then
    stage_dist
    exit 0
fi

build_log="$build_dir/build.sh.log"
if [ "$run_tests" = 1 ]; then
    # The whole tree, keep going past the known-broken targets, then audit the log: any OTHER failure
    # is an error. nativeee and the title library are part of `all`.
    build_args=(--build "$build_dir")
    [ -n "$jobs" ] && build_args+=(-j "$jobs")
    build_args+=(-- -k 0)
    echo "build.sh: cmake ${build_args[*]}"
    set +e
    cmake "${build_args[@]}" 2>&1 | tee "$build_log"
    build_rc=${PIPESTATUS[0]}
    set -e
    if ! python3 "$ci_lists" audit-build "$build_log"; then
        echo "build.sh: BUILD FAILED (log: $build_log)" >&2; exit 1
    fi
    [ "$build_rc" -ne 0 ] && echo "build.sh: build exit $build_rc -- only known-broken targets failed (see .github/ci/known_broken_targets.txt)"
else
    [ ${#targets[@]} -eq 0 ] && targets=(nativeee)
    build_args=(--build "$build_dir" --target "${targets[@]}")
    [ -n "$jobs" ] && build_args+=(-j "$jobs")
    echo "build.sh: cmake ${build_args[*]}"
    set +e
    cmake "${build_args[@]}" 2>&1 | tee "$build_log"
    build_rc=${PIPESTATUS[0]}
    set -e
    [ "$build_rc" -ne 0 ] && { echo "build.sh: BUILD FAILED (log: $build_log)" >&2; exit 1; }
fi
# The engine's acceptance rule: no compiler or linker warning in the log (the tree is -Werror; this
# catches the linker's and any tool's). "[IOP-RECOMP] ... warnings: 0" lines do not match `warning:`.
n_warn=$(grep -c 'warning:' "$build_log" || true)
n_ldwarn=$(grep -c 'ld: warning' "$build_log" || true)
echo "build.sh: build OK -- warning: $n_warn, ld: warning $n_ldwarn"
if [ "$n_warn" -ne 0 ] || [ "$n_ldwarn" -ne 0 ]; then
    echo "build.sh: REFUSING -- the build log has warnings (log: $build_log)" >&2; exit 1
fi
if [ "$run_tests" = 1 ]; then
    exclude="$(python3 "$ci_lists" exclude-regex)"
    ctest_args=(--test-dir "$build_dir" -E "$exclude" --timeout 900 --output-on-failure)
    [ -n "$jobs" ] && ctest_args+=(-j "$jobs")
    # The GL tests need a display: on a Linux host without one, xvfb-run provides llvmpipe's.
    runner=()
    if [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
        export SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
        runner=(xvfb-run -a -s "-screen 0 1280x1024x24")
    fi
    echo "build.sh: ${runner[*]+${runner[*]} }ctest ${ctest_args[*]}"
    if ! ${runner[@]+"${runner[@]}"} ctest "${ctest_args[@]}"; then
        echo "build.sh: TESTS FAILED (ctest --test-dir $build_dir --rerun-failed --output-on-failure)" >&2; exit 1
    fi
fi
[ -x "$build_dir/app/host/nativeee" ] && echo "build.sh: binary at $build_dir/app/host/nativeee"
stage_dist
exit 0
