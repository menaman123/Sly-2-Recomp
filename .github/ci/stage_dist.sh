#!/usr/bin/env bash
# Stages the macOS / Linux dist folder of this title from a finished disc build (build.sh --dist / --dist-only),
# for the title lanes' self-check and local use. Windows is staged by engine/tools/ci/release_dist.ps1.
#
#   stage_dist.sh --repo <checkout> --build-dir <dir> --dist-root <dir> [--preset <name>]
#
# The layout is the engine's (engine/build.sh "The artifact"), one self-contained folder:
#   <dist-root>/<platform>/nativeee              recompiled title code + the native engine (the engine's
#                                                *-dist presets link SDL2, FFmpeg and -- Linux -- libstdc++
#                                                statically; the folder has no lib/)
#   <dist-root>/<platform>/title/hle/sdk_bindings.tsv   the title tables the binary reads beside itself
#   <dist-root>/<platform>/title/verify/nav/*.json      (addresses / hashes / waypoints: no game bytes)
#   <dist-root>/<platform>/BUILD-ID              what the binary prints with --version
#   <dist-root>/<platform>/README.md             the user half of this repository's README.md
#   <dist-root>/<platform>/LICENSE               this repository's licence (Apache-2.0)
#   <dist-root>/<platform>/LICENSE-NativeEE.txt  the engine's licence (MIT; engine/LICENSE)
#   <dist-root>/<platform>/THIRD_PARTY_NOTICES.md + licenses/   what is linked, how, under what; the texts
#                                                from the engine's committed engine/tools/dist/licenses/
#   <dist-root>/nativeee-<platform>.tar.gz       Linux: the one-file download;  macOS: .zip
# <platform> = macos | linux-<uname -m>. The folder passes the engine's own gates before the archive is
# written: tests/lint/lint_artifact_licenses.py --dist (the licence set, the notices, and the FFmpeg
# configuration string libavcodec EMBEDS in the staged binary == the LGPL recipe of
# engine/tools/ci/ffmpeg_static.sh, no GPL / non-free flag) and tools/ci/asset_scan.py over the folder
# and the archive (names, format magic, ELF byte windows -- SLY2_ASSET_WINDOWS=<json> or
# games/<title>/asset_scan_windows.json when present; SLY2_ASSET_ELF=<extracted ELF> adds the live windows).
# Any finding removes the folder: nothing game-derived but the recompiled code is staged, and the VU1 AOT
# pack is never included.
set -euo pipefail
repo=""; build_dir=""; dist_root=""; preset=""
while [ $# -gt 0 ]; do
    case "$1" in
        --repo) repo="$2"; shift 2 ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --dist-root) dist_root="$2"; shift 2 ;;
        --preset) preset="$2"; shift 2 ;;
        -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "stage_dist.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$repo" ] && [ -n "$build_dir" ] && [ -n "$dist_root" ] || { echo "stage_dist.sh: --repo, --build-dir and --dist-root are required" >&2; exit 2; }
engine="$repo/engine"
games="$repo/games"
case "$(uname -s)" in
    Darwin) platform=macos ;;
    Linux) platform="linux-$(uname -m)" ;;
    *) echo "stage_dist.sh: unsupported host $(uname -s) (Windows: build.ps1 -Dist)" >&2; exit 2 ;;
esac
bin="$build_dir/app/host/nativeee"
[ -x "$bin" ] || { echo "stage_dist.sh: no binary at $bin (build first)" >&2; exit 1; }
# A --no-disc build's nativeee is the stub that reports "supplies no play composition" on any run; the
# real composition refuses an unknown option instead (and never opens a window for one).
if "$bin" --stage-dist-probe 2>&1 </dev/null | grep -q "supplies no play composition"; then
    echo "stage_dist.sh: REFUSING -- $bin supplies no play composition (a --no-disc build); the artefact is the recompiled title" >&2
    exit 1
fi

# The one title in games/ (one with an hle/ subdirectory), or PS2R_TITLE.
title_src=""
if [ -n "${PS2R_TITLE:-}" ]; then
    title_src="$games/$PS2R_TITLE"
else
    for cand in "$games"/*/; do
        [ -d "${cand}hle" ] || continue
        [ -z "$title_src" ] || { echo "stage_dist.sh: several titles in games/; set PS2R_TITLE" >&2; exit 1; }
        title_src="${cand%/}"
    done
fi
[ -d "$title_src" ] || { echo "stage_dist.sh: no title directory under $games" >&2; exit 1; }

dist="$dist_root/$platform"
echo "== stage ($dist)"
rm -rf "$dist"
mkdir -p "$dist/title/hle" "$dist/licenses"
cp "$bin" "$dist/nativeee"
"$bin" --version > "$dist/BUILD-ID"
cat "$dist/BUILD-ID"

# --- the title tables the binary reads beside itself (boot_leg.cpp: <exe dir>/title when the source tree is absent)
cp "$title_src/hle/sdk_bindings.tsv" "$dist/title/hle/sdk_bindings.tsv"
if ls "$title_src"/verify/nav/*.json >/dev/null 2>&1; then
    mkdir -p "$dist/title/verify/nav"
    cp "$title_src"/verify/nav/*.json "$dist/title/verify/nav/"
fi

# --- the user half of README.md: the intro, Status, Running a build, the VU1 AOT pack and License
awk '
    /^## Playing on Windows/ || /^## Building/ || /^## Layout/ || /^## CI and releases/ || /^## How this repository relates/ { skip = 1; next }
    /^## Status/ || /^## Running a build/ || /^## The VU1 AOT pack/ || /^## License/ { skip = 0 }
    !skip { print }
' "$repo/README.md" > "$dist/README.md"
cp "$repo/LICENSE" "$dist/LICENSE"
cp "$engine/LICENSE" "$dist/LICENSE-NativeEE.txt"

# --- self-contained: the *-dist presets link everything but the system's GL / libc statically ---
case "$platform" in
    macos)
        if otool -L "$dist/nativeee" | tail -n +2 | grep -qvE '^\s*(/usr/lib/|/System/Library/)'; then
            echo "stage_dist.sh: NOT self-contained -- a non-system library is dynamic (build with --preset macos-dist):" >&2
            otool -L "$dist/nativeee" | tail -n +2 >&2; rm -rf "$dist"; exit 1
        fi ;;
    linux-*)
        if ldd "$dist/nativeee" | grep -qE "libSDL2|libav(codec|util)|libswscale|libstdc\+\+"; then
            echo "stage_dist.sh: NOT self-contained -- a bundled library is dynamic (build with --preset linux-dist):" >&2
            ldd "$dist/nativeee" >&2; rm -rf "$dist"; exit 1
        fi ;;
esac

# --- FFmpeg, if linked. Sly 2 decodes its movies through the engine's IPU model and references no FFmpeg
#     symbol, so the linker drops FFmpeg entirely. That is checked in the binary (no embedded configuration
#     string, no av_* / sws_* symbol). If FFmpeg is linked, its configuration must be the LGPL recipe of
#     engine/tools/ci/ffmpeg_static.sh, argument for argument. ---
ffmpeg_sh="$engine/tools/ci/ffmpeg_static.sh"
confs="$(strings "$dist/nativeee" | grep -- '--prefix=' | sort -u || true)"
n="$(printf '%s' "$confs" | grep -c . || true)"
ffmpeg_linked=0
if [ "$n" -gt 1 ]; then
    echo "stage_dist.sh: REFUSING -- nativeee embeds $n FFmpeg configuration strings (--prefix=), expected 0 or 1" >&2
    printf '%s\n' "$confs" | sed 's/^/  /' >&2; rm -rf "$dist"; exit 1
fi
if [ "$n" -eq 0 ]; then
    if command -v nm >/dev/null 2>&1; then
        ff_syms="$(nm "$dist/nativeee" 2>/dev/null | grep -cE ' [TtDdBbSsRr] _?(av_|avcodec_|avutil_|sws_|ff_)' || true)"
        if [ "${ff_syms:-0}" -ne 0 ]; then
            echo "stage_dist.sh: REFUSING -- nativeee has $ff_syms FFmpeg symbols but no libavcodec configuration string: a foreign or partial FFmpeg" >&2
            rm -rf "$dist"; exit 1
        fi
        echo "ffmpeg: not linked (0 configuration strings, 0 av_*/sws_* symbols -- the FMV path is the engine's IPU model)"
    else
        echo "ffmpeg: not linked (0 configuration strings; nm unavailable for the symbol check)"
    fi
else
    ffmpeg_linked=1
    if printf '%s' "$confs" | grep -qE -- '--enable-gpl|--enable-nonfree|--enable-libx26|--enable-version3'; then
        echo "stage_dist.sh: REFUSING -- the linked FFmpeg is a GPL / non-free / LGPLv3 build: $confs" >&2; rm -rf "$dist"; exit 1
    fi
    linked_args="$(printf '%s\n' "$confs" | tr ' ' '\n' | grep -v -e '^--prefix=' -e '^--disable-x86asm$' -e '^$')"
    recipe_args="$("$ffmpeg_sh" --print-args)"
    if [ "$linked_args" != "$recipe_args" ]; then
        echo "stage_dist.sh: REFUSING -- the linked FFmpeg was not configured with the recipe (engine/tools/ci/ffmpeg_static.sh):" >&2
        diff <(printf '%s\n' "$recipe_args") <(printf '%s\n' "$linked_args") | sed 's/^/  /' >&2 || true
        rm -rf "$dist"; exit 1
    fi
    echo "ffmpeg: linked statically; the embedded configuration == the recipe ($(printf '%s\n' "$recipe_args" | grep -c .) arguments)"
fi
ffmpeg_tag="$(cat "$build_dir/third_party/ffmpeg/PS2R_FFMPEG_TAG" 2>/dev/null || printf '%s' "$confs" | grep -o 'ffmpeg/[0-9.]*' | head -1)"
sdl2_tag="$(grep -o 'release-[0-9.]*' "$engine/cmake/Sdl2Dependency.cmake" | head -1)"
imgui_ver="$(grep -o 'Release: `[^`]*`' "$engine/app/gui/third_party/imgui/PROVENANCE.md" | head -1 | sed 's/Release: //; s/`//g')"
# SDL2 must really be compiled in (the *-dist presets build it from the pinned release and link it statically).
sdl_tag_in_bin="$(strings "$dist/nativeee" | grep -o 'SDL-release-[0-9.]*' | head -1 || true)"
[ -n "$sdl_tag_in_bin" ] || { echo "stage_dist.sh: REFUSING -- no SDL2 release tag string in nativeee (SDL2 not linked statically?)" >&2; rm -rf "$dist"; exit 1; }
echo "sdl2: $sdl_tag_in_bin compiled in"

# --- the licence texts: the engine's committed set (byte-equal), only for what the binary carries ---
lic_src="$engine/tools/dist/licenses"
lic_names=(SDL2-LICENSE.txt imgui-LICENSE.txt imgl3w-UNLICENSE.txt)
[ "$ffmpeg_linked" = 1 ] && lic_names+=(FFmpeg-LICENSE.md FFmpeg-COPYING.LGPLv2.1.txt)
case "$platform" in linux-*) lic_names+=(GCC-COPYING3.txt GCC-RUNTIME-LIBRARY-EXCEPTION.txt) ;; esac
for f in "${lic_names[@]}"; do cp "$lic_src/$f" "$dist/licenses/$f"; done
if [ "$ffmpeg_linked" = 1 ]; then
    {
        echo "FFmpeg $ffmpeg_tag as linked into nativeee (statically)."
        echo
        echo "libavcodec's configuration string embedded in nativeee (avcodec_configuration(); the configure command"
        echo "line the linked FFmpeg was really built with; read by stage_dist.sh with: strings nativeee | grep -- --prefix=):"
        echo "$confs"
        echo
        echo "It equals, argument for argument, the recipe (engine/tools/ci/ffmpeg_static.sh --print-configure; --prefix and the"
        echo "host-only --disable-x86asm aside):"
        "$ffmpeg_sh" --print-configure
    } > "$dist/licenses/FFmpeg-BUILD-CONFIGURATION.txt"
fi

# --- the notices: what the binary links, how, under which licence, where the text is ---
title_name="$(basename "$title_src")"
{
    echo "# Sly-2-Recomp -- third-party notices -- $(cat "$dist/BUILD-ID")"
    echo
    echo "This folder ships the recompiled game program of Sly 2: Band of Thieves (NTSC-U, SCUS_973.16; the"
    echo "games/$title_name module) and the NativeEE engine. It contains NO game data: no textures, audio, models,"
    echo "FMVs or disc bytes (engine/tools/ci/asset_scan.py runs on every artefact), and no VU1 AOT pack. You must own"
    echo "the game and supply your own disc image at first start; it is only ever read."
    echo
    echo "The licence texts named below are in licenses/ beside this file. The port's own code: LICENSE (Apache-2.0,"
    echo "this repository) and LICENSE-NativeEE.txt (MIT, the engine)."
    echo
    echo "## Compiled into the binary"
    echo
    echo "- **NativeEE** (the engine: recompilers, EE/IOP/GS/VU/SPU2 models, the GL renderer, the harness) -- MIT"
    echo "  licence (LICENSE-NativeEE.txt)."
    echo "- **SDL2 $sdl2_tag** -- zlib licence (licenses/SDL2-LICENSE.txt). Built from the pinned release by the build"
    echo "  (engine/cmake/Sdl2Dependency.cmake) and linked STATICALLY. Its platform back ends (macOS: Cocoa/CoreAudio/"
    echo "  GameController; Linux: X11, Wayland, PulseAudio, ALSA) are the system's own, loaded at run time."
    if [ "$ffmpeg_linked" = 1 ]; then
        echo "- **FFmpeg $ffmpeg_tag** -- libavcodec, libavutil, libswscale: GNU Lesser General Public License v2.1 or later"
        echo "  (licenses/FFmpeg-COPYING.LGPLv2.1.txt; the per-component terms are in licenses/FFmpeg-LICENSE.md)."
        echo "  Built from the pinned tag by engine/tools/ci/ffmpeg_static.sh and linked STATICALLY with ONLY the MPEG-1/MPEG-2"
        echo "  video decoders, the mpegvideo parser and the colour conversion enabled; --disable-gpl --disable-nonfree"
        echo "  --disable-version3: NO GPL or non-free component is compiled in. licenses/FFmpeg-BUILD-CONFIGURATION.txt quotes"
        echo "  the configure line AS EMBEDDED in this binary by libavcodec (what was linked, not the recipe) and the recipe it"
        echo "  was checked against. LGPL section 6: the complete source of this program and the recipe that rebuilds the"
        echo "  bundled FFmpeg are published with this BUILD-ID; ./build.sh --dist relinks the artefact against a modified FFmpeg."
    else
        echo "- **FFmpeg: not in this binary.** The engine's FFmpeg movie decoder (engine/engine/ipu, LGPL v2.1) is built and"
        echo "  named on the link line, but this title plays its FMVs through the engine's IPU hardware model and the game's own"
        echo "  recompiled libmpeg, references no FFmpeg symbol, and the linker dropped every libavcodec / libavutil / libswscale"
        echo "  object: stage_dist.sh verified that nativeee embeds no libavcodec configuration string and exports no av_* / sws_*"
        echo "  symbol. No FFmpeg code or licence text is shipped here; the recipe that WOULD build it is"
        echo "  engine/tools/ci/ffmpeg_static.sh (--disable-gpl, --disable-nonfree)."
    fi
    echo "- **Dear ImGui $imgui_ver** (engine/app/gui/third_party/imgui: the F1 session menu and the first-run launcher)"
    echo "  -- MIT licence (licenses/imgui-LICENSE.txt). Its stb headers (imstb_*.h) carry their own public-domain / MIT"
    echo "  notices inside the files."
    echo "- **OpenGL loader**: the engine uses no loader library -- it links the platform's GL"
    case "$platform" in
        macos) echo "  (OpenGL.framework) and calls the exported entry points directly. The only loader code in the binary is" ;;
        *) echo "  (libGL.so.1, libglvnd / Mesa or the vendor driver) and calls the exported entry points directly. The only loader code in the binary is" ;;
    esac
    echo "  Dear ImGui's embedded gl3w-derived header (imgui_impl_opengl3_loader.h), public domain (licenses/imgl3w-UNLICENSE.txt)."
    case "$platform" in
        linux-*)
            echo "- **libstdc++ and libgcc** (the GCC runtime libraries, linked STATICALLY: -static-libstdc++ -static-libgcc) --"
            echo "  GNU General Public License v3 (licenses/GCC-COPYING3.txt) WITH the GCC Runtime Library Exception 3.1"
            echo "  (licenses/GCC-RUNTIME-LIBRARY-EXCEPTION.txt), which permits their inclusion in this independently licensed program." ;;
    esac
    echo
    echo "## From your system at run time (nothing bundled)"
    echo
    case "$platform" in
        macos) echo "OpenGL.framework, libc++, libSystem (macOS)." ;;
        *) echo "glibc, libGL.so.1 (libglvnd / Mesa or the vendor driver), and whichever of libX11 / libwayland-client /"
           echo "libpulse / libasound SDL2 finds." ;;
    esac
    echo
    echo "## The port's own code"
    echo
    echo "LICENSE (this repository, Apache-2.0) and LICENSE-NativeEE.txt (the engine, MIT)."
} > "$dist/THIRD_PARTY_NOTICES.md"

# --- macOS: the staged copy is signed ad hoc (arm64 refuses to load an unsigned, copied image; Gatekeeper
#     still quarantines a downloaded zip -- README "Platforms") ---
case "$platform" in
    macos) codesign --force --sign - "$dist/nativeee" >/dev/null 2>&1 || true
           codesign -dv "$dist/nativeee" 2>&1 | grep -E '^Signature=' || true ;;
esac

# --- the engine's gates over the staged folder ---
if [ "$ffmpeg_linked" = 1 ]; then
    echo "== licences (engine/tests/lint/lint_artifact_licenses.py --dist)"
    if ! python3 "$engine/tests/lint/lint_artifact_licenses.py" --root "$engine" --dist "$dist"; then
        echo "stage_dist.sh: REFUSING -- the licence set in $dist is incomplete or inconsistent" >&2; rm -rf "$dist"; exit 1
    fi
else
    # The engine's --dist mode presumes a linked FFmpeg (it reads libavcodec's configuration string); without
    # one, the committed set + recipes are linted and the staged texts are checked byte-equal here.
    echo "== licences (engine/tests/lint/lint_artifact_licenses.py --root; staged texts byte-equal to the committed set)"
    if ! python3 "$engine/tests/lint/lint_artifact_licenses.py" --root "$engine"; then
        echo "stage_dist.sh: REFUSING -- the engine's committed licence set is inconsistent" >&2; rm -rf "$dist"; exit 1
    fi
    for f in "${lic_names[@]}"; do
        cmp -s "$lic_src/$f" "$dist/licenses/$f" || { echo "stage_dist.sh: REFUSING -- licenses/$f differs from the committed copy" >&2; rm -rf "$dist"; exit 1; }
    done
    for word in SDL2 ImGui "OpenGL loader" licenses/ "not in this binary"; do
        grep -q -- "$word" "$dist/THIRD_PARTY_NOTICES.md" || { echo "stage_dist.sh: REFUSING -- THIRD_PARTY_NOTICES.md does not name $word" >&2; rm -rf "$dist"; exit 1; }
    done
    echo "licences: ${lic_names[*]} byte-equal; notices name SDL2 / ImGui / the GL loader / FFmpeg's absence"
fi
scan_args=()
windows="${SLY2_ASSET_WINDOWS:-$title_src/asset_scan_windows.json}"
[ -f "$windows" ] && scan_args+=(--windows "$windows")
[ -n "${SLY2_ASSET_ELF:-}" ] && [ -f "${SLY2_ASSET_ELF}" ] && scan_args+=(--elf "$SLY2_ASSET_ELF")
echo "== asset scan ($dist${scan_args[*]+ ${scan_args[*]}})"
if ! python3 "$engine/tools/ci/asset_scan.py" "$dist" ${scan_args[@]+"${scan_args[@]}"} --report "$build_dir/asset_scan_dist.md"; then
    echo "stage_dist.sh: REFUSING -- the asset scan found game data in $dist (report: $build_dir/asset_scan_dist.md)" >&2
    rm -rf "$dist"; exit 1
fi

# --- the one-file download, scanned as well ---
case "$platform" in
    linux-*) archive="$dist_root/nativeee-$platform.tar.gz"; rm -f "$archive"
             tar -C "$dist_root" -czf "$archive" "$platform" ;;
    macos)   archive="$dist_root/nativeee-$platform.zip"; rm -f "$archive"
             (cd "$dist_root" && zip -qr -y "$archive" "$platform") ;;
esac
if ! python3 "$engine/tools/ci/asset_scan.py" "$archive" ${scan_args[@]+"${scan_args[@]}"} --report "$build_dir/asset_scan_archive.md"; then
    echo "stage_dist.sh: REFUSING -- the asset scan found game data in $archive" >&2; rm -f "$archive"; rm -rf "$dist"; exit 1
fi

echo "== artefact ($dist)"
du -sh "$dist"; ls -la "$dist"
case "$platform" in
    macos) echo; echo "load check (every line a system library):"; otool -L "$dist/nativeee" | tail -n +2 ;;
    linux-*) echo; echo "load check (glibc / libGL / none):"; ldd "$dist/nativeee" | grep -v linux-vdso || true ;;
esac
echo; ls -la "$archive"; (shasum -a 256 "$archive" 2>/dev/null || sha256sum "$archive")
echo "stage_dist.sh: done -- $dist/nativeee ($(cat "$dist/BUILD-ID"))"
