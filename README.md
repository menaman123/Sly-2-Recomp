# Sly 2: Band of Thieves -- a NativeEE title module

[![CI](https://github.com/menaman123/Sly-2-Recomp/actions/workflows/ci.yml/badge.svg)](https://github.com/menaman123/Sly-2-Recomp/actions/workflows/ci.yml)

A static-recompilation port of **Sly 2: Band of Thieves** (PlayStation 2, NTSC-U, `SCUS_973.16`) on the
[NativeEE](https://github.com/menaman123/NativeEE) engine. This repository is the *title module*: the facts of
this one game (its ELF identity, boot environment, SDK-library binding table, embedded IOP modules) and the
composition that boots it. The engine (the recompilers, the EE/IOP/GS/VU/DMAC/IPU/SPU2 models, the renderer,
the harness) is the `engine/` git submodule and is title-neutral.

**No game data is in this repository.** The build extracts the ELF from your own disc image, checks its SHA-256
against `games/scus_97316_sly2_band_of_thieves/TITLE_INPUTS.sha256` and recompiles it into a build directory
outside the checkout.

## Playing on Windows

1. Download `sly2-<tag>-windows-x64.zip` from the [Releases](https://github.com/menaman123/Sly-2-Recomp/releases)
   page and unzip it anywhere.
2. Open `sly2-windows-x64\launcher\sly2-launcher.exe`.
3. Choose your own disc image of Sly 2: Band of Thieves (North America, SCUS-97316), choose where the game should
   live, and press Play.

Nothing else needs to be installed. The zip holds the launcher and the recompiled game program; the game's data
(textures, sound, movies, levels) is read from your disc. Windows SmartScreen may ask once (More info > Run
anyway).

macOS and Linux have no prebuilt release yet; build from source (below).

## Status

The game boots through the intro, title screen, New Game, the briefings and into gameplay, with audio, on the
engine's generic GS renderer. The title's IOP modules run recompiled and the sce libraries bind to the engine's
generic bodies (`hle/sdk_bindings.tsv`). There is no per-title HLE (`vu1_library/` and `renderer_profile/` are
placeholders).

Known issues:

- Gameplay runs below 60 fps; the VU1 work is the main cost (see "The VU1 AOT pack").
- Some texture pages that are never uploaded show as strips.
- A 989snd handler-node corruption in the recompiled sound driver is still open.
- Some engine tests fail or are excluded; each exclusion and its reason is listed in
  `.github/ci/ctest_exclusions.txt` and `.github/ci/known_broken_targets.txt`.

## Building

The engine is the CMake source tree and this repository's `games/` is its title directory (`-DPS2R_GAMES_DIR`).
`build.sh` (macOS, Linux) and `build.ps1` (Windows) wrap that and use the engine's `CMakePresets.json` for the
toolchain. The build directory is always outside the checkout (default: the sibling `<checkout>-build/`; override
with `--build-dir` / `-BuildDir` / `SLY2_BUILD_DIR`) because with a disc it holds the extracted ELF and the
recompiled code.

Prerequisites:

- **macOS:** Xcode command line tools, `brew install cmake ninja sdl2 ffmpeg python`.
- **Linux:** Clang 15+ or GCC 12+, CMake 3.21+, Ninja, Python 3, and the packages in
  `engine/tools/ci/linux_packages.txt` (SDL2, FFmpeg, GL, and Mesa + `xvfb` for the GL tests without a display).
- **Windows:** Visual Studio 2022 Build Tools with *C++ Clang tools for Windows* (clang-cl + Ninja), CMake 3.21+,
  Python 3, and a [vcpkg](https://github.com/microsoft/vcpkg) checkout with `VCPKG_ROOT` set (the first configure
  builds SDL2 and FFmpeg from `engine/vcpkg.json`).

### Without a disc

```
git clone --recurse-submodules https://github.com/menaman123/Sly-2-Recomp
cd Sly-2-Recomp
./build.sh --no-disc --tests    # Windows: .\build.ps1 -NoDisc -Tests
```

No game file is needed. The title's recompiled targets are reported as `SKIPPED` and everything else is built:
the engine libraries, the tools, `nativeee` (as a stub), the title's `ps2r_title_hle` and the component tests.
`--tests` builds the whole tree, checks failures against `.github/ci/known_broken_targets.txt`, and runs
`ctest` minus `.github/ci/ctest_exclusions.txt`.

### With your own disc

```
./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"          # or: --elf /path/to/SCUS_973.16
.\build.ps1 -Disc "X:\path\Sly 2 - Band of Thieves.iso"           # Windows
```

The ELF's SHA-256 must be `a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73` (NTSC-U, 2,597,048
bytes); any other revision is refused. The build extracts `SCUS_973.16` and `IOPRP300.IMG`, recompiles the ELF
and the four IOP modules (lgAud, 989snd, spiop, FILEIO), checks the output against the committed
`MANIFEST.sha256` files, and links `<build>/app/host/nativeee`.

On macOS and Linux, `--dist` stages a self-contained folder after the build (`--dist-only` re-stages an existing
build); see `.github/ci/stage_dist.sh`.

## Running a build

```
<build>/app/host/nativeee                     # the first-run screen: drop the disc image, verify, Play
<build>/app/host/nativeee --iso "/path/to/Sly 2 - Band of Thieves.iso"
engine/verify/run_bounded.sh 120 leg.log -- <build>/app/host/nativeee --headless --frames 600 --harness-out <out> --iso "..."
```

`--harness-window` is the playable window with the harness census written to `<out>/run.meta` at the end;
`--frames N` ends the session at the N-th frame. F1 opens the session menu (volume, display, pause); Escape with
the menu closed, or closing the window, quits.

### Keyboard and gamepad

The defaults are `engine/platform/sdl/input/default_bindings.ini`; the launcher's Controls page rebinds them.
Left click captures the mouse for the camera, Escape releases it. An SDL game controller maps to the DualShock 2
layout directly.

| PS2 control | Keyboard / mouse | | PS2 control | Keyboard / mouse |
|---|---|---|---|---|
| Left stick (move) | W A S D | | Right stick (camera) | mouse |
| Cross (jump, advance text) | Space | | Square (attack) | Left Shift |
| Circle (grab, interact) | right mouse button | | Triangle | E |
| L1 / L2 | 1 / 2 | | R1 / R2 | 3 / 4 |
| L3 | 5 | | R3 (Binocucom) | 6 |
| START | Y | | SELECT | Tab |
| D-pad | arrow keys | | | |

### Recording and replaying a session

`NATIVEEE_PAD_RECORD=<file>` records every frame's pad state (buttons and the four stick axes, run-length
encoded). `NATIVEEE_PAD_SCRIPT=<file>` replays it; the file contains no game data, so it can be attached to a
bug report together with the replay's `run.meta` and the command line.

```
NATIVEEE_PAD_RECORD=session.pad <build>/app/host/nativeee --harness-window --harness-out <out> --iso "..."
NATIVEEE_PAD_SCRIPT=session.pad NATIVEEE_DUMP_FROM=3900 NATIVEEE_DUMP_EVERY=50 \
    <build>/app/host/nativeee --headless --frames 4100 --dump-frames <dir> --harness-out <out> --iso "..."
```

Replay headless for a reproducible picture: a windowed run is wall-paced, so a windowed recording and its replay
can be a few frames of animation apart. Always set both `NATIVEEE_DUMP_FROM` and `NATIVEEE_DUMP_EVERY` with
`--dump-frames`; a full dump of a long run is gigabytes.

### Debug knobs

Every `NATIVEEE_*` environment variable the composition reads is parsed once in `hle/leg_knobs.cpp`, with its
meaning in `hle/leg_knobs.h`: frame, audio and GS dumps, SPU / GIF / function traces, VU1 kick capture, the
watchdog, the MTVU and VU1 tier overrides. The engine's own settings (`engine.mtvu`, `engine.vu1_mode`,
`engine.vu1_aot_*`, ...) are set with `--set key=value`.

## The VU1 AOT pack

With `engine.vu1_mode=aot` the engine verifies each VU1 microprogram against captured kicks, generates C++ for
it, compiles it with a C++ compiler found on the host, re-verifies it and runs it natively; results are cached
under the user data directory. On a machine without a compiler, a pre-built pack next to the executable
(`vu1_aot_pack/`, or `engine.vu1_aot_pack=<dir>`) gives the same native tier from the first frame:

```
<build>/tools/vu1_aot_pack/vu1_aot_pack build --corpus "<user_data>/vu1_aot/corpus" --out <build>/app/host/vu1_aot_pack
<build>/tools/vu1_aot_pack/vu1_aot_pack check --corpus "<user_data>/vu1_aot/corpus" --pack <build>/app/host/vu1_aot_pack
```

A pack built for another engine build or platform is ignored, and every library is re-verified before it runs.
The corpus and the pack are generated from the game, so neither is committed or released.

## Layout

```
engine/                                  NativeEE (submodule, branch sly2-upstream)
games/scus_97316_sly2_band_of_thieves/   the title module (the engine's games/<serial>_<slug>/ contract)
  TITLE_INPUTS.sha256                    hashes of the disc files the code is generated from
  hle/                                   TitleProfile, the SDK binding table, and the play composition
                                         (boot_leg.cpp, one class per leg_*.{h,cpp} pair)
  recomp/, iop/                          the EE and IOP recompiles, each with a MANIFEST.sha256 of its output
  verify/                                judges and helpers for the harness output
build.sh / build.ps1                     configure and build wrappers
launcher/title.json                      the launcher's description of this title
tools/ci/                                launcher packaging (release_launcher.sh / .ps1)
.github/workflows/                       ci.yml, windows.yml, launcher.yml, windows-release.yml
.github/ci/                              ctest exclusions, known broken targets, stage_dist.sh
```

## CI and releases

- `ci.yml`: the asset scan and guard, then the no-disc build and tests on macOS, Linux and Windows. Title lanes
  that need a disc run only on self-hosted runners with `NATIVEEE_TITLE_LANES` set.
- `windows.yml`: the full Windows tree and test suite.
- `launcher.yml`: builds, tests and packages the launcher on each platform.
- `windows-release.yml`: on a `v*` or `win-*` tag, builds the game from the two disc files in a private
  repository (secrets `SLY2_GAME_FILES_REPO` and `SLY2_GAME_FILES_TOKEN`), packages it with the launcher, and
  publishes the release. The asset scan fails the release on any disc file or game asset.

## How this repository relates to NativeEE

NativeEE is title-neutral and must stay so: `tests/lint/lint_engine_purity.py` fails on any token from
`games/<title>/lint_denylist.txt` inside `engine/`. It is pinned here on its `sly2-upstream` branch, which carries
the title-neutral fixes this port needed; each is an upstream candidate for NativeEE `main`.

## License

This repository is licensed under the **Apache License 2.0** (`LICENSE`). NativeEE is **MIT** (`engine/LICENSE`).
Sly 2: Band of Thieves is the property of Sony Interactive Entertainment / Sucker Punch Productions. No game data
is in this repository or its releases; you need your own copy of the game.
