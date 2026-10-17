# Sly 2: Band of Thieves -- a NativeEE title module

[![CI](https://github.com/menaman123/Sly-2-Recomp/actions/workflows/ci.yml/badge.svg)](https://github.com/menaman123/Sly-2-Recomp/actions/workflows/ci.yml)
<!-- the badge goes live with the first push of .github/workflows/ci.yml to GitHub -->

A static-recompilation port of **Sly 2: Band of Thieves** (PlayStation 2, NTSC-U, `SCUS_973.16`) on the
[NativeEE](https://github.com/menaman123/NativeEE) engine. This repository is the *title module*:
the facts of this one game (its ELF identity, boot environment, SDK-library binding table) and the
composition that boots it. The engine -- the recompiler, the EE/IOP/GS/VU/DMAC/SPU2 models, the
harness -- is the `engine/` git submodule and knows nothing about Sly.

**Nothing derived from the game is in this repository**, by construction: the build extracts the ELF
from *your* disc image, checks its SHA-256 against `games/<title>/TITLE_INPUTS.sha256` and recompiles
it into the build directory. The build directory lives outside the repository.

## Status (2026-10-16): playable in a window, no per-title HLE

What works, verified on macOS (Apple clang 21, cmake 4.3, ninja 1.13) from a fresh clone:

- `./build.sh --disc <iso>` configures NativeEE as the source tree with this repository's `games/` as
  the title directory (`-DPS2R_GAMES_DIR`), auto-discovers `scus_97316_sly2_band_of_thieves`, extracts
  `SCUS_973.16` and `IOPRP300.IMG` from the disc (SHA-256 gated), pulls the four IOP module images out
  of them (lgAud, 989snd_Library, spiop, FILEIO_service; gated), recompiles the ELF with the in-tree
  recompiler (6574 functions, 265 jump-table sites, 32 shards, 0 unhandled instructions) and the four
  modules with the IOP recompiler, verifies every generated file against the committed
  `MANIFEST.sha256` (strict: 35 + 4x2 files) and links `nativeee` with the title's play composition.
  0 compiler warnings (`-Wall -Wextra -Werror`).
- The game boots through the FMV / legal / logo / memory-card screens and renders through the engine's
  generic GS renderer -- no per-title VU1 programs, renderer profile or IOP HLE: the title's own IOP
  modules run recompiled (`iop/`, `hle/iop_host.cpp`), the sce libraries bind to the engine's generic
  bodies (`hle/sdk_bindings.tsv`: 15 libkernl, 13 libcdvd, 11 libmc rows). The intro cinematic, the
  title screen, New Game, the Binocucom briefings and the first playable area (the Cairo museum) all
  reach the screen; the left stick walks Sly and the camera follows (keyboard WASD / a gamepad).
- `nativeee` with no arguments shows the engine's first-run launcher: drop the disc image, it is
  verified (ISO9660, `SCUS_973.16` present, ELF SHA-256 equal to this build's), extracted under the
  user data root (`~/Library/Application Support/NativeEE` on macOS) and the game starts; later
  starts show the settings screen (Play / Display / Controls / Save states / Data). `--iso` and the
  harness flags bypass it.
- Audio plays through the SDL device in windowed runs (the SPU2 mixer, one quantum per 60 Hz field;
  headless legs stay muted and hash the same lane as before).
- The GS consumer runs pipelined on its own thread (split VRAM image, frame skip) in windowed runs;
  closing the window or pressing Escape shuts down cleanly (guest workers joined, census written,
  exit 0); the `--frames` cap does the same.

Performance: 23-27 fps in gameplay on an M4 (the intro / title screens run at 60) with the engine's
MTVU wired -- the VU1 and the VIF1 stream that feeds it run on their own host thread in windowed runs
(`engine.mtvu`, default on; `--set engine.mtvu=0` or `NATIVEEE_MTVU=0` is the lockstep inline path, 19-24
fps on the same host; headless runs stay lockstep unless `NATIVEEE_MTVU=1`, and within a timing cluster the
worker's frames are byte-identical to lockstep's). Before that the VU1 cached tier was made ~1.8x faster
(fast kinds for every word the title's microprogram uses, inlined clamp / flag / stall kernels, NEON lane
kernels under `PS2R_SIMD=ON` -- which build.sh now passes; every step proven byte-identical by replaying
720 captured VU1 kicks on both tiers). The floor is now the VU1 execution itself: the title waits for the
FINISH at the end of each chain it kicks, so the frame is a ping-pong between the EE and the worker and only
the GIF drain overlaps; ~1.08 M VU1 bundles per frame at ~18-21 ns each is 20-23 ms of the worker's time.
The engine's generic VU1 AOT tier (`engine.vu1_mode=aot` / `NATIVEEE_VU1_MODE=aot`, engine/vu/vu1_aot/) runs
a microprogram image native -- its block table re-resolved to instruction bodies specialised on each slot's
kind and dest mask -- once the image is VERIFIED against the title's VU1 corpus (every captured kick of that
image replays identically to the interpreter: registers, memory, clock, kicked bytes); the corpus is collected
at first run and the verdicts cached under the user data dir (`<user_data>/vu1_aot/`, keyed by image hash and
engine build), anything unverified runs on the block tier. Measured on the gameplay window with the worker on:
block 24.4 fps p50 / 19.1 ns per bundle, aot 24.9 fps p50 / 18.4 ns per bundle on the same host and minute
(the stall model's writeback-ring removal that landed with it took the block and cached tiers down 9-12 % on the bench); the 30 fps target is
not met -- the remaining cost is the per-op glue of the block loop (~40 % of VU1 time), which only generated
per-block code would remove.
Not done: no per-title HLE of any kind (VU1 program library, renderer profile); the 989snd
handler-node corruption noted in the title README remains.

## Layout

```
engine/                                  NativeEE (submodule, branch sly2-upstream) -- title-neutral; its Sly-era fixes are listed below
games/scus_97316_sly2_band_of_thieves/   the title module (the engine's games/<serial>_<slug>/ contract)
  TITLE_INPUTS.sha256                    the disc files the generated code is made from (the ELF, its three embedded
                                         IOP modules, IOPRP300.IMG's FILEIO) -- hashes only, extracted at build time
  hle/title_profile.cpp                  TitleProfile: identity, ELF facts, boot env
  hle/sdk_bindings.tsv                   the SDK-library binding table (39 rows: libkernl, libcdvd, libmc; each with its evidence)
  hle/boot_leg.cpp                       the play composition (TitlePlayMain): the launcher hand-over, headless legs, the SDL
                                         window + audio device + live pad, the pipelined GS queue, the shutdown ladder
  hle/video_stack.*, frame_dump.*        the GS -> GL video stack and the --dump-frames writer
  hle/iop_host.*                         hosts the recompiled IOP modules on the engine's cooperative IOP kernel
  hle/vu1_probe.*                        NATIVEEE_VU1_CAP: VU1 kick capture for tests/vu/test_vu1_capture_replay
  hle/pad_script.*, pad_recorder.*       NATIVEEE_PAD_SCRIPT (scripted / replayed pad) and NATIVEEE_PAD_RECORD (record a
                                         session as a replayable script) -- see "Playing"
  recomp/CMakeLists.txt                  runs the recompiler at build time into <build>/generated/...
  recomp/MANIFEST.sha256, INVOCATION.md  hashes of the generated output + how it was produced
  iop/{fileio,lgaud,snd989,spiop}/       the IOP recompiles (one MANIFEST.sha256 each), iop/iop_modules.h = bases
  vu1_library/, renderer_profile/        STATUS-only stubs (no target: nothing per-title)
  lint_denylist.txt, README.md
build.sh / build.ps1                     configure + build wrappers (the engine is the CMake source dir; --no-disc / -NoDisc
                                         builds without a game file; --tests / -Tests adds the component suite)
CMakePresets.json                        the cache variables build.sh passes, as documentation (CMake reads presets
                                         from the source dir = engine/; see "Building")
.github/workflows/ci.yml                 CI: asset scan + guard, the no-disc build on macOS / Linux / Windows
.github/ci/                              ctest_exclusions.txt, known_broken_targets.txt (one reason per row), ci_lists.py
CONTRIBUTING.md, SECURITY.md, LICENSE    how to contribute (no game data, ever), how to report, Apache-2.0
```

## Building

The engine is the CMake source tree and this repository's `games/` is its title directory
(`-DPS2R_GAMES_DIR`); `build.sh` (macOS, Linux) and `build.ps1` (Windows) spell that out and reuse the
engine's `CMakePresets.json` for the per-platform toolchain (`cmake -S engine -B <build dir> --preset
<engine preset>`). The build directory is always OUTSIDE the checkout (default: the sibling
`<checkout>-build/`; `--build-dir` / `-BuildDir` / `SLY2_BUILD_DIR`) because with a disc it holds the
extracted ELF and the recompiled code.

Prerequisites per OS (the engine's README, "Developers", is the authority):

- **macOS:** Xcode command line tools (Apple clang), `brew install cmake ninja sdl2 ffmpeg python`.
  Verified: Apple clang 21, CMake 4.3, Ninja 1.13, SDL2 2.32, FFmpeg 7 on an M4 (arm64).
- **Linux:** Clang 15+ or GCC 12+, CMake 3.21+, Ninja, Python 3, `libsdl2-dev libavcodec-dev
  libavutil-dev libswscale-dev libgl-dev libglvnd-dev`, Mesa + `xvfb` for the GL tests without a display
  -- `engine/tools/ci/linux_packages.txt` is the exact Ubuntu 24.04 list CI installs. The default preset is
  `linux-release` (the environment's CC/CXX); `--preset linux-release-clang` / `linux-release-gcc` pin one.
  Not yet run on a Linux machine from this repository (the engine's own Linux lanes are green upstream).
- **Windows:** Visual Studio 2022 Build Tools with the *C++ Clang tools for Windows* component (clang-cl +
  Ninja), CMake 3.21+, Python 3, a [vcpkg](https://github.com/microsoft/vcpkg) checkout with `VCPKG_ROOT`
  set (`engine/vcpkg.json` names SDL2 and FFmpeg; the first configure builds them). `build.ps1` drives the
  engine's `windows-release` preset. UNVERIFIED on a Windows machine: the engine's Windows arm itself has not
  run on one yet (see "Known issues").

### Without a disc (what CI does)

```
git clone --recurse-submodules https://github.com/menaman123/Sly-2-Recomp
cd Sly-2-Recomp
git submodule status            # must print the pinned engine commit with no leading '-' or '+'
./build.sh --no-disc --tests    # Windows: .\build.ps1 -NoDisc -Tests
```

No game file is needed: the engine configures, prints one `SKIPPED` line per title target (the recompiled
program, the IOP modules, the play composition) and builds everything else -- every engine library, the
tools, `nativeee` (a stub that reports "supplies no play composition"), the title's `ps2r_title_hle`
(`ActiveTitleProfile()`), the component test suite. `--tests` builds the whole tree with `ninja -k 0`,
audits the log against `.github/ci/known_broken_targets.txt` (anything else failing is an error),
requires 0 `warning:` / 0 `ld: warning`, then runs `ctest -E <.github/ci/ctest_exclusions.txt>`.
On the macOS reference host: 972 ninja steps, ~6 min at `-j 8`; **359 tests run, 359 pass (25 of them
SKIP 77: private fixtures / the frozen original tree), 50 excluded** -- "Known issues" lists every exclusion.

### With your own disc

Your own disc image of Sly 2 NTSC-U (`SCUS_973.16`); nothing from it is ever committed.

```
./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"          # or: --elf /path/to/SCUS_973.16
.\build.ps1 -Disc "X:\path\Sly 2 - Band of Thieves.iso"           # Windows (unverified, see above)
```

`engine/` is pinned to a specific NativeEE commit (`.gitmodules` names the branch `sly2-upstream`); if the
submodule fetch reports `not our ref <sha>`, that commit is not on GitHub yet -- clone NativeEE yourself into
`engine/` and `git checkout <sha>` (the sha `git submodule status` prints with a leading `-`).

The build directory defaults to a sibling of the checkout named after it: `<checkout>-build/` (a clone at
`~/src/Sly-2-Recomp` builds in `~/src/Sly-2-Recomp-build`; override with `--build-dir DIR` or
`SLY2_BUILD_DIR`; build.sh refuses a directory inside the repository). A full build from scratch is
~2 minutes on an M-series Mac (580 ninja steps: the engine, the tools, the disc extraction, the four IOP
module recompiles, the EE recompile into 32 shards, the `MANIFEST.sha256` checks, `nativeee`). The ELF's
SHA-256 must be `a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73` (NTSC-U, 2,597,048
bytes); any other revision is refused with the reason. The binary ends up at `<build>/app/host/nativeee`
(`nativeee --help` lists only `--version`/`--help` and the linked title; the harness flags below are read
by the engine's config loader, `engine/engine/core/config/config_loader.cpp`).

Run the boot leg (always bounded):

```
engine/verify/run_bounded.sh 120 leg.log -- <build>/app/host/nativeee --headless --frames 600 --harness-out <out> \
    --iso "/path/to/Sly 2 - Band of Thieves.iso"
grep -E "^(park|park_pc|park_detail|sdk_rows_hit_list|iop_modules|boundaries|first_syscalls)" <out>/run.meta
```

## Playing

Start the game (the launcher on a plain start; `--iso` skips it for a harness-style run):

```
<build>/app/host/nativeee                                   # first run: drop the disc image on the window, Verify & extract, Play
<build>/app/host/nativeee --extract-disc "/path/to/Sly 2 - Band of Thieves.iso"   # the same extraction from the command line
engine/verify/run_bounded.sh 600 play.log -- <build>/app/host/nativeee --harness-window --frames 6000 --harness-out <out> --iso "..."
```

`--harness-window` is the playable window with the harness census (`<out>/run.meta`) written at the end;
`--frames N` ends the session cleanly at the N-th frame boundary (omit it to play until the window is
closed or Escape is pressed; `run_bounded.sh <seconds>` is the wall-clock bound every example keeps).
From the intro: START skips the cinematic, START at the title screen starts a new game (the game asks to
create a save on the host memory card the first time: square = yes), cross advances the briefing lines.
F1 opens the session menu (volume, display, pause); Escape with the menu closed, or the window's close
button, quits.

### Keyboard and gamepad

The engine's `engine/platform/sdl/input/default_bindings.ini` (baked into the binary; the launcher's
Controls page writes `<user data>/bindings.ini` to rebind). Left click captures the mouse for the
camera, Escape releases it. An SDL game controller uses the DualShock 2 layout directly (cross = A,
circle = B, square = X, triangle = Y, shoulders / triggers = L1 R1 / L2 R2, stick clicks = L3 R3).

| PS2 control | Keyboard / mouse | | PS2 control | Keyboard / mouse |
|---|---|---|---|---|
| Left stick (move) | W A S D | | Right stick (camera) | mouse |
| Cross (jump; advance text) | Space | | Square (attack) | Left Shift |
| Circle (grab / interact) | right mouse button | | Triangle | E |
| L1 / L2 | 1 / 2 | | R1 / R2 | 3 / 4 |
| L3 | 5 | | R3 (Binocucom) | 6 |
| START | Y | | SELECT | Tab |
| D-pad | arrow keys | | | |

### Recording a session

`NATIVEEE_PAD_RECORD=<file>` writes every frame's pad state as it reached the game -- buttons and the
four stick axes, run-length encoded -- in the same grammar `NATIVEEE_PAD_SCRIPT` reads, so the file
replays the session frame for frame. It contains only stamp indices, button bits and stick values (no
game data): safe to attach to a bug report.

```
NATIVEEE_PAD_RECORD=session.pad engine/verify/run_bounded.sh 1800 play.log -- <build>/app/host/nativeee --harness-window \
    --harness-out <out> --iso "/path/to/Sly 2 - Band of Thieves.iso"
```

The file starts with `# nativeee pad record build=<build id> frames=<frames> steps=<lines>` and then one line
per run of one pad state, `<from_stamp>:<buttons_hex>:<hold>:<lx>,<ly>,<rx>,<ry>` (frames with nothing
pressed and centred sticks are implicit). `run.meta` reports it as `pad_record=<file>:frames=:steps=`.

### Replaying headless (bug reports)

`NATIVEEE_PAD_SCRIPT=<file>` replays a recording with no window, no audio device and no pacing; the frame
cap and the dump knobs pick the evidence:

```
NATIVEEE_PAD_SCRIPT=session.pad NATIVEEE_DUMP_FROM=3900 NATIVEEE_DUMP_EVERY=50 engine/verify/run_bounded.sh 900 replay.log -- \
    <build>/app/host/nativeee --headless --frames 4100 --dump-frames <dir> --harness-out <out> --iso "..."
```

- `--frames N`: stop at frame boundary N (the recording's `frames=` or earlier).
- `--dump-frames <dir>`: write presented frames as `<dir>/p<key>.ppm` (`key` = the frame boundary; headless
  also writes `f<key>.ppm`, the display grab). `NATIVEEE_DUMP_FROM=<key>` starts the dumps there and
  `NATIVEEE_DUMP_EVERY=N` keeps every N-th -- always use both: a full dump of a long run is gigabytes.
- The pad stream is reproduced exactly (`run.meta` `pad_served=` of the replay equals the recording's) and
  the game reaches the same state -- the same place, the same events. Pixel-exact frames at a given key
  need the game's own field clock to agree too: two HEADLESS replays agree (0 to 0.2 % of pixels differ
  at the same key), while a windowed run is wall-paced, so a windowed recording and its replay (in either
  mode) sit a few frames of animation / camera phase apart (4-9 % of pixels at the same key, about one
  50-frame motion step); the briefing's voice-timed text crawl differs between any two runs. So: replay
  headless for a reproducible picture; replay with `--harness-window` to watch it (no interaction needed).
  Headless is not faster (a 4100-frame replay: ~200 s windowed, ~350 s headless on an M4).

A bug report wants `session.pad`, the replay's `run.meta` and the command line (see CONTRIBUTING.md).

### Other play knobs

`run.meta` rows a play run writes: `boundary_wall_ms=` (the wall clock at every 100th boundary -- the
frame rate of a window of the run), `pace=` / `tick_profile=` / `pace_long_trace=` (the engine's pacer
census), `audio_device=` (quanta pushed / non-silent, the ring's underruns / padded frames),
`pad_served=` (stamps that carried a press or a stick), `pad_record=` / `pad_script=` (the recorder's file and counts,
the script's steps / end / source), `input_sources=`, `gs_queue=pipelined:frame_skip...`,
`vu1_tier=bundles=...:unit=worker|inline` (VU1 bundles interpreted, and which unit ran them), `mtvu=on,worker=1,...`
(the MTVU census: batches, idle waits, dropped kicks -- always 0 -- and pending bytes at the end). The shutdown
ladder prints `nativeee: leg shutdown: ...` (`mtvu_worker@<ms>` is the worker's join).

MTVU (the VU1 + VIF1 worker thread): `engine.mtvu` (Config, default on) arms it in windowed runs; `--set engine.mtvu=0`
runs the lockstep inline path instead (the VU1 on the EE render thread, ~15 % slower, useful when bisecting a
picture difference). Headless and `--dump-frames` runs are lockstep by default -- the frames lane's determinism;
`NATIVEEE_MTVU=1` asks for the worker there too (within a timing cluster its frames are byte-identical to
lockstep's), and `NATIVEEE_MTVU=0` forces it off in every mode. `engine.mtvu_depth` (default 2) is the worker's
queue depth in frames.

`NATIVEEE_UNPACED=1` runs the window without the 60 Hz field pacer; `NATIVEEE_DUMP_EVERY=N` writes every Nth key;
`NATIVEEE_GS_ROUTES=<key,...>` / `NATIVEEE_GS_VERTS=<key>` write a frame's draw routes (+ `vram_<key>.bin`) /
every DrawIr with its vertices; `NATIVEEE_IPU_OUT_DUMP=<file>` is the fromIPU side of `NATIVEEE_IPU_DUMP`;
`NATIVEEE_AUDIO_DUMP=<file.wav>` writes the DEVICE audio stream (pads included) as 48 kHz stereo int16 (play mode);
`NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>]` records VU1 kicks as `Vu1Case` files + their PATH1 packets
(hle/vu1_probe.h; replay with `tests/vu/test_vu1_capture_replay <case.bin> [--trace] [--cached|--block|--aot]`);
`NATIVEEE_VU1_MODE=interp|cached|block|aot` selects the VU1 tier over Config `engine.vu1_mode` (aot: the generic
recompiler -- `engine.vu1_aot_corpus=<dir>` adds a corpus of captured cases to verify against at start,
`engine.vu1_aot_fallback=block|interp` is where an unverified image runs, `engine.vu1_aot_verify=off` runs every image
native unverified -- a bench knob; run.meta `vu1_aot=programs:..:verified:..:native_kicks:..:ns_per_bundle:..` and
`vu1_tier=...:block_ns_per_bundle=` are the census); `NATIVEEE_PAD_SCRIPT=<text or file>` with `<stamp>:<buttons_hex>:<hold>[:<lx>,<ly>[,<rx>,<ry>]][,...]`
presses buttons / holds the sticks over the live pad (`500:8:10,650:8:10` = START twice: skip the intro, New
Game; `3950:0:120:0,128` = left stick full left for 120 boundaries; a file in the same grammar -- blanks and
newlines separate like commas, `#` comments -- is what `NATIVEEE_PAD_RECORD` writes); `NATIVEEE_KEY_SCRIPT=<stamp>:<sdl_scancode>:<hold>`
holds a KEYBOARD key through the real key route (A = 4, D = 7, S = 22, W = 26, Space = 44). The engine's harness
verbs `--set harness.close_at_frame=N` (a real window close at boundary N) and `--set harness.inject_key=Escape@N`
(a real key press) are honoured by the composition.

`NATIVEEE_WATCHDOG_S` (default 60) ends a leg that reaches no frame boundary with `park=watchdog` and the
spinning pc, so a hang is still a reported result. Boot-triage tooling in the composition (all env-gated,
title-side): `NATIVEEE_FN_TRACE=<hex,...>` (entry/return prints of generated functions), `NATIVEEE_GIF_TRACE=1`,
`NATIVEEE_IPU_DUMP=<file>`, `NATIVEEE_RAM_DUMP=<file>` (the 32 MB RAM image at a watchdog park) and
`NATIVEEE_WATCH=<hex>,<bytes>[;<hex>,<bytes>...][;max=<n>][;val=<hex>...]` (every EE store into the ranges with
the shadow-stack frame; needs a non-NDEBUG engine build, e.g. `--type RelWithDebInfo` with
`-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g"`). The engine's own per-tag DMAC / VIF narration is
`--log-level narrate` (`[DMAC] dmac-vif1: tag@...`, `[DMAC] dmac-gif:`, `[DMAC] dmac-sprfrom:`, `[VIF] vif1: cmd=`).

## Known issues

Performance and picture (the title):

- **24-29 fps in gameplay** on an M4 (intro / title screens at 60; 19-24 with MTVU off, `--set engine.mtvu=0`):
  the bound is the engine's VU1 tier itself on the MTVU worker thread (~1.08 M bundles per frame at ~18-21 ns
  = 20-23 ms; a real VU1 is ~6x faster). The generic AOT tier (`engine.vu1_mode=aot`) and the stall model's
  ring removal bought ~10-13 % per bundle over the block tier of the previous round, not the ~25 % the target
  needs; the title waits for each kicked chain's FINISH before building the next, so the EE and the worker
  alternate rather than overlap. 30 fps needs generated per-block code (the AOT tier's next step) or per-title
  VU1 HLE -- and this port has, by design, no per-title HLE yet (`vu1_library/`, `renderer_profile/` are STATUS-only).
- Picture gaps: strips over never-uploaded texture pages; one GL "zero texture" warning per run.
- The 989snd handler-node corruption noted in the title README (`[IOP-RECOMP] Unresolved JALR` is 0 now).

Build and test (the engine at the pinned commit, `be7386a`; each row is also in
`.github/ci/ctest_exclusions.txt` / `known_broken_targets.txt` with the same reason, and CI reads those files):

- **Three engine targets do not compile in a whole-tree build** (`app/gui/restart.cmake`, `clock.cmake`:
  `test_session_restart`, `ps2r_product_clock`, `test_session_restart_runtime` -- engine sources added by
  path with no include directory for the engine root). `build.sh --tests` builds with `ninja -k 0` and
  accepts exactly these; their tests `SessionRestart`, `SessionRestartRuntime`, `SessionRestoreDeadline`
  are excluded. Upstream fix pending; nothing else depends on them.
- **First-title pins.** The engine's first title left tests that read its own tables and fixture layout
  instead of SKIPping for another title: `RecompDispatchTableLink`, `RecompManifestIntact`,
  `RecompSymbolCensus` (its recompiled TU), `LintSdkBindingRowCount` / `LintSdkBindingNoGameAddress` /
  `SdkBindingManifestResolves` (a 298-row `sdk_bindings.tsv`; this title's has 39), `LibmpegBindings`
  (reads `engine/games/<title>/...`, ignoring `PS2R_GAMES_DIR`), `BootEnvRetailArgv` (its retail argv
  string), `Render_AttractReplaySelftest` (imports a module from its `verify/`), `VuVifGifSectionsAnchors`
  (`verify/restore_mhash_expected.tsv`), `TitleSymbolsTableSelftest` (`symbols/build_symbols.py`), and the
  `Verify*Selftest` / `VerifyAnchorsTable*` rows (`games/<title>/verify/baseline`, `verify/platform_ref/`).
  All excluded; the upstream issue is "parametrise on the title or SKIP 77".
- **Two engine component regressions at `be7386a`**, reproduced on a fresh build (the dev host's stale
  test binaries had hidden them): `Present_DrawScanoutSource` still expects the pre-"one stored-alpha
  convention" values (that commit re-pinned `test_gl_alpha_convention` and `test_blend_ff_map` only),
  `LibsdModule` expects ordinal 16 to return 0. Excluded with that reason; to be fixed in the engine and
  the rows removed. (`Render_BlendEquationPin` was a third: the test uploaded its texel alpha under the
  old x2 GPU rescale, so a stored 0xFF read as alpha 2.0 and both halves of the "As halves the additive
  contribution" check saturated. The engine's MODULATE/As path matched the GS User's Manual; the test's
  texture now carries the stored alpha bytes 0x80/0x40 and passes 35/35.)
- **Windows is unverified.** Neither this repository's `build.ps1` nor the engine's Windows arm has run on
  a Windows machine (engine README: no Windows GL loader yet, POSIX headers in six TUs). The CI lane is
  `continue-on-error`: it configures with the `windows-release` preset (clang-cl, vcpkg) and builds
  `ps2r_core` + `ps2r_title_hle`, then tries the whole tree.
- **Linux is structurally verified only.** The CI lane is the engine's own Linux lane (its package list,
  `linux-release-clang`, `xvfb-run` for the GL tests) pointed at this `games/`; it has not run here yet.
- **Title lanes need the disc.** The recompile, `MANIFEST.sha256` checks and boot legs run only where the
  runner supplies `NATIVEEE_DISC` (repository variable `NATIVEEE_TITLE_LANES`, as in the engine); on the
  public repository that job is skipped.
- `CMakePresets.json` at this root is documentation: CMake reads presets from the SOURCE directory, which
  must be `engine/` (`cmake --preset sly2-elf` here fails with "does not appear to contain CMakeLists.txt").
  `build.sh --preset` names the ENGINE's presets instead.

## How this repository relates to NativeEE

[NativeEE](https://github.com/menaman123/NativeEE) is the engine: the EE recompiler and the IOP
recompiler, the EE/IOP/GS/VU/VIF/GIF/DMAC/IPU/SPU2 models, the generic GS renderer over OpenGL, the SDK
HLE bodies, the harness and the tests. It is title-neutral and must stay so: `tests/lint/lint_engine_purity.py`
fails on any token from `games/<title>/lint_denylist.txt` inside `engine/`. It is pinned here as the
`engine/` submodule on its `sly2-upstream` branch -- the title-neutral fixes this port needed (the list
below), each an upstream candidate for NativeEE `main`.

This repository is one *title module* under the engine's `games/<serial>_<slug>/` contract: the facts of
one game (ELF identity and hashes, boot environment, the SDK-binding table with disassembly evidence per
row, which IOP modules are embedded and recompiled) and the play composition that boots it. No recompiled
output, no extracted file, no screenshot of the game is committed -- the build regenerates everything
from the user's disc, and the engine's asset scan proves the tree clean on every CI run.

## Continuous integration

`.github/workflows/ci.yml` -- `guard` (checkout with submodules; `engine/tools/ci/asset_scan.py --tree
games` and `--tree engine`, the asset guard over both, the tools' and lints' selftests, this workflow's
YAML, actionlint), then `build-nodisc` on macOS arm64 / Ubuntu x86-64 / Windows x64: `./build.sh --no-disc
--tests` (`.\build.ps1 -NoDisc` on Windows, `continue-on-error`). `title-lanes` runs only with the
repository variable `NATIVEEE_TITLE_LANES` set on a runner that exports `NATIVEEE_DISC`. Every ctest
exclusion lives in `.github/ci/ctest_exclusions.txt` with its reason; `.github/ci/ci_lists.py --selftest`
checks the lists parse.

## Changes to the engine for this title (the submodule's `sly2-upstream` branch; upstream candidates)

All title-neutral; none mentions Sly. Committed on NativeEE's `sly2-upstream` branch (10 commits over `main`),
which is what the submodule pins.

- `PS2R_GAMES_DIR` cache variable: title discovery and the five `add_subdirectory` calls from a
  directory outside the engine tree (the parent-repository layout).
- `engine/title/title_play.h` + `app/host`: a title that defines `ps2r_title_play` supplies
  `TitlePlayMain`; `nativeee` calls it (`if(TARGET ...)`, like every other title target).
- `app/host`: `PS2R_IOP_ORACLE=0` on `ps2r_app_host` (the play shape; `iop_arm.h` #errors otherwise).
- `tests/{gs,gif,vu}`: `if(TARGET gs_replay|vu1_replay)` around the rows that name tools the public
  tree does not build (the public engine did not configure without this).
- `tools/recompiler_v2`: a u32-wrap crash on data words near `0xFFFFFFFF`; the jump-table engine-2
  guard scan now handles the `bne x,c,L; sltiu g,x,N` delay-slot idiom; `ctc1/cfc1` printed the
  register index in hex; emitters for 15 R5900 instructions the first title never used (`vadda vadday
  vmaddx vmaddy vmaddax vsuby vsubz vmuly vmulz vmulw vmulay vmulaz padduw prot3w adda.s`); the first
  title's hardcoded stub/diagnostic blocks behind `--legacy-title-stubs` (default off).
- `engine/ee/mem/main_memory.h` (`MainMemory::Phys`): the kernel's uncached-accelerated window
  `0x3xxxxxxx` maps onto RAM (the first title never used it; this one builds its DMA display lists
  through it -- every store there was silently dropped, so the lists CALLed into stale memory). The other
  `& 0x1FFFFFFF` address classifiers (SIF RAM sink, syscall pointer check, cdvd/pad/mc buffer checks,
  `memory::get_pointer`) go through `Phys` too; `tests/ee/test_fastmem.cpp` pins the window.
- `engine/dmac`, `engine/vif`: the per-tag / per-VIFcode traces are `PS2R_LOG(..., Narrate, ...)` lines
  (no `getenv` outside `config_loader.cpp`).
- `engine/sif/hle/sce_sif`: a `sceSifLoadModuleBuffer` row (the module loader is told
  `buffer@<iop addr>` + the args; titles that embed their IRX in the ELF load them this way).
- `engine/dmac/chain_walker` (`Translate`): a channel register address (`Dn_MADR` / `Dn_TADR`) with the
  SPR flag (bit 31) names the scratchpad -- libmpeg drains the IPU's macroblocks there; they landed in RAM
  before, so the title converted an all-zero picture and the intro FMV was a flat green frame.
  `tests/dmac/test_dmac_regs.cpp` pins it.
- `engine/gs/state/gs_priv_regs` (`Display`): the `SetWindow` job's content height follows the CRTC's read
  of the buffer -- `SMODE2` interlaced FRAME mode (INT=1 FFMD=1) scans a half-height field buffer
  line-doubled, so the content is half the raster height (this title renders 512x224 fields shown at 448
  lines; they filled the top half of the window before). `tests/gs/state/test_gs_state.cpp` pins it.
- `engine/title/title_profile.h`: `TitleProfile::Gs::vblank_wait_entry` -- the title's own vsync-wait
  routine when its ELF links no `sceGsSyncV`; the composition installs the engine's `WaitForVblank`
  mirror (the frame boundary) there.
- `engine/vif/vif_regs.h` (`MaskAction`): the STMASK field codes follow the EE User's Manual -- 00 data,
  01 ROW, 10 COL, 11 write-protect (ROW/COL/protect were rotated by one: a masked S-8 alpha overlay with
  `0x3f` protection wrote ROW = 0 into every vertex's RGB, so this title's lit geometry and its text were
  black). `tests/vif/test_vif_unpack_scalar.cpp` pins the table and the V4-5 + masked S-8 idiom;
  `tests/vu/test_vu1_capture_replay.cpp` replays a captured VU1 run (SKIP without a capture).

- `engine/pad/hle/padman_module.{h,cpp}`: the padman RPC face follows the padman the title loads
  (`SetModuleFacts`: .iopmod version + an actuator-DMA sink). A padman >= 3.00 speaks the SDK-3 command
  table (OPEN area at +0x10 / result +0x0C / pad id +0x14, SET_MMODE +0x14, GET_MODVER +0x0C = the version,
  ...; read from padman.irx 4.10's dispatcher); the 2.x set stays for a 2.x padman. Raw libpad 3.x on top of
  the old face failed scePadInit's version check and never registered the pad area. Pinned in
  `tests/pad/test_libpad_bindings.cpp`.
- `engine/pad/hle/pad_protocol.{h,cpp}`: the READY slot carries a DualShock 2's actuator table (+0x30),
  mode table (+0x50 = 4, 7), nrOfModes 2 / modeCurOffs 1 / nrOfActuators 2 (a libpad 3.x title's pad
  state machine reads them through scePadInfoAct / scePadInfoMode). `LibpadBindings::InfoAct` reads the
  same table. Pins in tests/pad updated.
- `engine/mc/hle/libmc_bindings.{h,cpp}`, `mc_command_model.h`, `mc_host_store.{h,cpp}`: `sceMcGetEntSpace`
  row (fno 0x12: free entries of a directory, -4 when missing) and sceMcGetDir's count-only form (maxent
  -1, table may be NULL). `tests/mc/test_mc_save_flow.cpp` pins both.
- `tools/recompiler_v2/recompiler_tool/recompiler.{h,cpp}`: every `func.base_address == 0x...` emission
  override (270 sites keyed on the first title's retail addresses) tests through `legacy_title_addr()`
  (= `--legacy-title-stubs`); six collided with real function starts of this ELF (one returned -1 from the
  game's memory-card open-write-close helper). `recomp/MANIFEST.sha256` re-recorded.
- `tools/iop_recompiler/iop_recompiler.cpp`: the unresolved JR/JALR diagnostics name the site pc.
- `engine/dmac/dmac_deferred_vif1.{h,cpp}`, `dmac.cpp`: a blocking kernel wait (`Dmac::ObserveAtKernelWait`,
  FlushReason::KernelWait) ends the END-slot append race -- the walk it flushes records no tail and earlier
  tail records are dropped. The frame-sync tail chase had resumed a completed VIF1 chain into the display
  list the game was rebuilding in the same buffer (Sly 2's double-buffered list, rebuilt once its FINISH
  arrived), leaving VIF1 mid-UNPACK for the next kick: no FINISH, the render thread's WaitSema never
  returned (the loading-screen stall). Register-access / GIF-kick / frame-sync flushes keep the chase.
  `tests/dmac/test_dmac_deferred_vif1.cpp` TestKernelWaitEndsAppendRace.
- `engine/iop/sysmem_heap.{h,cpp}`: AllocSysMemory rounds to SYSMEM's 256-byte allocation unit (was 16).
  spiop fills a 0x48-byte 989snd handler node into a 0x38-byte block; with 16-byte blocks the next
  allocation overwrote handler slots 10..13 -> `[IOP-RECOMP] Unresolved JALR at 0x33208` once per frame
  (the EE's per-frame fno 0x4d command flush). `tests/iop/test_sysmem_heap.cpp` vector 8.
- `engine/vif/vif.cpp` (`Vif::SubmitDirect`), `engine/gif/gif_parser.{h,cpp}` (`ActivePath()`,
  the receive line): narrate-level diagnostics only -- DIRECT packets from the whole-span parser were
  invisible to `--log-level narrate`; `gif: pathN receive ... in_transfer=` shows the parser's state.
- `engine/gs/hle/ir/alpha_fail_mask.{h,cpp}` (new) + `engine/render/backends/gl/gl_draw_executor`: the TEST
  register's alpha test settled per draw -- ATST=NEVER fails every pixel, so AFAIL is a whole-draw write
  mask (KEEP: no draw; FB_ONLY: no depth write; ZB_ONLY: no colour write; RGB_ONLY: no alpha, no depth),
  composed with FBMSK / ZMSK. The shader had let ZB_ONLY fragments through as colour writes of black: this
  title's Z-clear sprites (atst NEVER, afail ZB_ONLY) erased the sky at the frame start and the finished
  scene at the frame end -- the black level. `tests/gs/ir/test_alpha_fail_mask.cpp`; GL pins in
  `tests/render/test_gl_device_smoke.cpp`.
- `engine/render/backends/gl/blend_ff_map`: ALPHA with A = B is Cv = D exactly (Cs / Cd / 0) for any C; the
  fallback (standard alpha blend) had painted `(0-0)*FIX+Cd` fans with their vertex colour.
  `tests/render/test_blend_ff_map.cpp` (27 arms).
- `engine/render/backends/gl/gl_shader_sources` (PS2 vertex + fragment programs): `gl_FragDepth = z * 2^-32`
  -- the exact window depth (clip z keeps the OLD mapping for clipping only). `-1 + z/2^31` rounds to -1.0
  in float32 for every z < 64, so a GS Z test of a small z against a 0-cleared Z buffer (GREATER passes on
  the console: this title's letterbox bars at z = 16) failed. Shader hashes re-pinned in
  `tests/render/test_gl_shader_library.cpp`; GL pin in test_gl_device_smoke.
- `gl_shader_sources` / `gl_draw_executor` / `gl_shader_library`: the exact 32-bit FBMSK merge
  (`u_fbmsk32_en`): a sub-byte channel mask on a 32-bit frame (0xFFFF80FF = G[6:0]) merges per bit against
  the Cd copy; glColorMask had widened it to the whole channel.
- `engine/gs/hle/ir/halfword_shuffle.{h,cpp}` (new), `gl_draw_executor::DrawHalfwordShuffle`, the
  `kHalfwordShuffleFragSrc` program, `texture_source.h` (`Owner::alt16_serve` / `alt16_main`),
  `texture_resolver.cpp`, `render_target.cpp`, `render/backend.h` (`DrawResult::halfword_shuffle`): the
  texture shuffle -- a 16-bit FRAME over a 32-bit-content target textured from the same layout read as
  16 bpp, full-height 8-wide sprite columns shifted by 8 -- is exactly a per-32-bit-pixel half-word copy
  (CT32 (r, x) = CT16 (r, x) low / (r, x + 8) high half-word within a block; the 16-bit page tables permute
  whole bands, so a full-extent column pass of one parity visits every pixel once). Detected purely from
  the DrawIr and run as one pass over the 32-bit image (TEXA expansion, DECAL, the CT16 write rule and
  FBMSK16 per half-word); the target keeps its 32-bit tags. The 5551 companion route stays for every other
  16-bit draw. This title saves / restores its G channel through the sibling buffer's alpha this way.
- `app/gui/session_gui`: Escape with the session menu closed submits the Close request (the window's
  close button's); with the menu open it stays ImGui's.
- `engine/title/title_profile.h` (`display_name`), `app/host/launcher` (`Launcher::TitleName`: the
  launcher names the linked title -- window title, the screen's heading, the disc hint -- from the
  profile instead of a placeholder; the disc id in both its forms + its region), `app/host/disc_verify`
  (the reports name the build's serial), `app/host/session_window` / `session_runtime`
  (`SetTitle` / `SetWindowTitle`: the game window's title bar).
- `gl_shader_sources` (the PS2 vertex program): the GS coverage rule. The GS samples pixel (i, j) at the
  INTEGER point of the XYOFFSET-relative space and draws it when x0 <= i < x1; GL samples at i + 0.5. The
  primitive is translated by +0.5 - 1/256 px so the two grids coincide with the GS's inclusive-low /
  exclusive-high edge rule (never the driver's tie rule). A sprite y in [-0.5, 223.5) -- this title's
  frame-end Z clear and both letterbox bars -- now covers rows 0..223 (row 223 was the faint coloured
  line on the last presented rows). `tests/render/test_gl_raster_rule.cpp` (new) pins the rule.
- ONE stored-alpha convention (`texture_decoder`, `clut_source`, `readback_engine`, `gl_shader_sources`,
  `blend_ff_map`): every texel, CLUT entry and colour attachment stores the GS alpha byte (0..0xFF); 0x80 =
  1.0 applies only where the GS applies it (the blend factor As, the alpha test). The x2 "GPU convention"
  (decoder prescale + shader inverse, the doubled alpha of fixed-function-blended frame writes, the halved
  PSMT8H index / SyncRTToVRAM encode) is gone; the fixed-function As factor is the fragment program's
  dual-source output (`GL_SRC1_ALPHA`). This title's PSMT8H colour-grade pass reads the exact alpha bytes
  (the sky streaks). `tests/render/test_gl_alpha_convention.cpp` (new) pins it (CT16 + TEXA texels, the
  blend, the stored byte, the alpha test, DATE); Texsrc / Targets / shader-hash pins updated.
- `engine/platform/sdl/audio_pull_ring`: the standing lead adapts to the producer's cadence (grows by one
  quantum per pad that had to stand in, up to 10 of a 12-quantum ring; decays after 10 s without a pad);
  a game thread below 60 Hz delivers its SPU2 quanta in bursts every 60-90 ms, and the fixed 50 ms lead
  padded 37 % of the device stream with tail loops and trimmed 43 % of the produced audio. Plus a
  consumer-side `SetPullTap` (the device stream for a host's WAV capture; nothing flows back).

## License

This repository (the title module, the scripts, the documentation) is licensed under the **Apache License
2.0** (`LICENSE`). The engine, NativeEE, is **MIT** (`engine/LICENSE`); its bundled third-party notices are
in its tree. Sly 2: Band of Thieves is the property of Sony Interactive Entertainment / Sucker Punch
Productions; nothing of the game is in this repository and you need your own copy to build or run it.
