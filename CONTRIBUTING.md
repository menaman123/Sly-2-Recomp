# Contributing to Sly-2-Recomp

This repository is the Sly 2 *title module* for the [NativeEE](https://github.com/menaman123/NativeEE)
engine. Changes here are either facts about this game (`games/scus_97316_sly2_band_of_thieves/`) or the
scripts and CI around them. Engine changes go to NativeEE (see "Engine changes" below).

## No game data, ever

Nothing derived from the game is committed. That includes the disc image, the ELF, IOP modules, recompiled
output, textures, screenshots, frame dumps, save states, RAM dumps, and byte arrays pasted into source files.
The build regenerates everything from the user's own disc, SHA-256 gated against `TITLE_INPUTS.sha256`, into
a build directory outside the checkout.

On every push, the `guard` job runs the engine's asset scan and asset guard over `games/` and `engine/`, and
any finding fails the workflow. Run them yourself before pushing:

```
python3 engine/tools/ci/asset_scan.py --tree games
python3 engine/tools/ci/asset_guard.py --root games --mode ci
```

The VU1 AOT corpus and pack are game-derived too. Build them for your own use and never commit or attach
them. `.gitignore` is a second line of defence; do not weaken it. To discuss a rendering bug, describe it
(frame, region, expected vs. seen) instead of committing a picture.

## Where title facts go

- `TITLE_INPUTS.sha256`: the disc files the generated code is made from, by path on the ISO9660 volume
  (or `<container>!<embedded module>`) and SHA-256.
- `hle/title_profile.cpp`: identity, ELF facts (entry, segments, `_gp`, stack, argv block) and boot
  environment. Each number was read from the ELF or crt0 disassembly; say where in the comment.
- `hle/sdk_bindings.tsv`: which function at which address binds to which engine body. **Every row needs
  disassembly evidence** in its comment: the identifying instruction sequence, the SDK stamp, or the call
  site. The engine's `lint_sdk_bindings.py` checks the form; you supply the proof.
- `hle/boot_leg.cpp` and `hle/leg_*.{h,cpp}`: the play composition. Each phase is one class in its own
  `leg_<phase>.{h,cpp}` pair, following the engine's lint rules (`engine/tests/lint/`). New wiring goes into
  the phase that owns the objects it touches; a new `run.meta` row goes into `leg_census.cpp`; a new
  `NATIVEEE_*` variable is parsed in `leg_knobs.cpp`, the only `getenv` site, and documented in
  `leg_knobs.h`.
- `recomp/INVOCATION.md`, `recomp/MANIFEST.sha256`, `iop/*/MANIFEST.sha256`: how the recompilers run and
  the hashes of their output. Re-record them when the recompiler or its flags change; the build fails on a
  mismatch by design.
- `lint_denylist.txt`: the tokens that mark something as this title's. The engine's purity lint reads it;
  add a token when you add a title-specific name.

## Engine changes

`engine/` is a submodule pinned to NativeEE's `sly2-upstream` branch. An engine fix must:

1. Be title-neutral: no Sly name, address or constant in `engine/` (the purity lint enforces this).
   Semantics come from the hardware manuals, not from what makes this game look right.
2. Follow the engine's rules: one class per `.h/.cpp` pair, `-Wall -Wextra -Werror`, no `getenv` outside the
   config loader, and a test that pins the fix.
3. Land on NativeEE first, then bump the submodule pin here in its own commit.

## Building and testing before a PR

```
./build.sh --no-disc --tests                                  # what CI runs; no game file needed
./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"     # the full build
engine/verify/run_bounded.sh 120 leg.log -- <build>/app/host/nativeee --headless --frames 600 \
    --harness-out <out> --iso "/path/to/the.iso"             # a bounded boot; read <out>/run.meta
```

The build must have no warnings, and the component suite must pass apart from the rows in
`.github/ci/ctest_exclusions.txt`. A new exclusion needs a reason in that file; delete a row once its reason
is gone. Run game sessions bounded (`engine/verify/run_bounded.sh`, `NATIVEEE_WATCHDOG_S`) so a hang still
ends with a reported `park=`.

## Portability

The title module is plain C++17 over the engine's platform layer and builds with clang-cl, Clang and GCC:

- No POSIX headers, GNU extensions or platform `#ifdef`s. Use `<filesystem>`, `<thread>`, `<chrono>` or
  `engine/engine/platform/host/platform.h`.
- Join paths with `/`, but split on both separators (`find_last_of("/\\")`).
- Open binary files in binary mode.
- `long` is 32-bit on Windows: use `uint64_t` for 64-bit counters, and `%zu` for `size_t`.
- Keep GNU flag spellings in CMake; the engine's `clang_cl_shim.py` translates them.

```
grep -rnE '#include <(unistd|dirent|dlfcn|pthread|execinfo|sys/[a-z]+)\.h>' games/
grep -rnE '__attribute__|__builtin_|_WIN32|__APPLE__|__linux__' games/
```

## Commit style

Conventional Commits (`feat(hle): ...`, `fix(iop): ...`, `docs: ...`, `ci: ...`), imperative mood,
capitalised subject, no trailing period, body wrapped at 72 columns. One logical change per commit, with
title facts and engine pins in separate commits.

## Reporting a bug

Open an issue with the engine pin (`git submodule status`), your OS, compiler and CMake versions, the exact
command, and the relevant `run.meta` rows (`park=`, `park_detail=`, `boundaries=`, `iop_modules=`). Never
attach a disc image, an ELF or a frame dump of the game.

For anything seen while playing, record the session and attach the recording with the `run.meta`:

```
NATIVEEE_PAD_RECORD=session.pad <build>/app/host/nativeee --harness-window --harness-out <out> --iso "/path/to/your.iso"
```

Say at which frame the problem shows. Both files are free of game data: the recording holds only frame
indices, button bits and stick values, and a maintainer can replay it headless to see the same frames.
