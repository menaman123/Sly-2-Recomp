# Contributing to Sly-2-Recomp

This repository is the Sly 2 *title module* for the [NativeEE](https://github.com/menaman123/NativeEE)
engine. Two kinds of change land here: facts about this one game (`games/scus_97316_sly2_band_of_thieves/`)
and the scripts / CI around them. Engine changes go to NativeEE (see "Engine changes" below).

## The one rule: no game data, ever

Nothing derived from the game is committed -- not the disc image, not the ELF, not an IOP module, not
the recompiled output, not a texture, not a screenshot or frame dump, not a savestate, not a RAM dump,
not a byte array pasted into a source file. The build regenerates everything from the user's own disc,
SHA-256 gated against `TITLE_INPUTS.sha256`, into a build directory that `build.sh` refuses to place
inside the checkout.

What proves it, on every push: the `guard` job runs the engine's `tools/ci/asset_scan.py --tree games`
and `--tree engine` (file names, format magic at the formats' fixed offsets, byte windows of a title
ELF inside every file, raster images, files > 1 MB, pasted byte arrays) and `tools/ci/asset_guard.py`
over both trees. One finding fails the workflow before anything builds. Run them yourself before
pushing:

```
python3 engine/tools/ci/asset_scan.py --tree games
python3 engine/tools/ci/asset_guard.py --root games --mode ci
```

`.gitignore` lists the disc / executable / module / image / savestate extensions and the recompiled
output paths as a second line of defence; do not weaken it. If you need a picture to discuss a
rendering bug, describe it (frame key, region, expected vs. seen values) or attach it to the issue --
never commit it.

## Where title facts go

- `TITLE_INPUTS.sha256` -- the disc files the generated code is made from: path on the ISO9660 volume
  (or `<container>!<embedded module>`) and SHA-256. Hashes only.
- `hle/title_profile.cpp` -- `TitleProfile`: identity, ELF facts (entry, segments, `_gp`, stack, argv
  block), boot environment, display name. Every number here was read from the ELF / crt0 disassembly;
  say where in the comment.
- `hle/sdk_bindings.tsv` -- the SDK-library binding table: which function at which address binds to
  which engine body. **Every row needs disassembly evidence**: the comment on the row names the
  identifying instruction sequence, the SDK stamp (`PsII<lib>` 3000) or the call site that proves the
  address is that function. A row with a guessed address is a bug waiting in a hot path; the engine's
  `lint_sdk_bindings.py` checks the form, you supply the proof.
- `hle/boot_leg.cpp` + `hle/leg_*.{h,cpp}` (with `video_stack.*`, `frame_dump.*`, `iop_host.*`,
  `vu1_probe.*`, `pad_script.*`, `pad_recorder.*`, `store_watch.*`, `vert_trace_sink.*`,
  `gif_trace_sink.*`, `audio_dump.*`) -- the play composition: how the engine's generic pieces are
  wired for this title. `boot_leg.cpp` is the entry (`TitlePlayMain`, the launcher hand-over) and
  `RunLeg`, which is nothing but the construction order of the phase objects; each phase is one class
  in its own `leg_<phase>.{h,cpp}` pair (the engine's one-class-per-pair rule, declarations-only
  headers, the size budgets of `engine/tests/lint/`), and the chain's order is the first title's
  phase order so object lifetimes nest the same way. A new piece of wiring goes into the phase that
  owns the objects it touches (the `README.md` layout map says what each owns); a new census row goes
  into `leg_census.cpp` at its place in the frozen order; a new `NATIVEEE_*` knob is parsed in
  `leg_knobs.cpp` (the title's ONE `getenv` site) and documented where it is used. Title-specific
  *behaviour* (a replaced EE function, a VU1 program, a renderer class) is per-title HLE and belongs in
  `vu1_library/` / `renderer_profile/` / a new `hle/` TU once it exists -- today there is none, and
  "it renders wrong" is first an engine question (see below).
- `recomp/INVOCATION.md`, `recomp/MANIFEST.sha256`, `iop/*/MANIFEST.sha256` -- how the recompiler was
  run and the hashes of its output. Re-record them (the build prints the command) whenever the
  recompiler or its flags change; the build fails on a mismatch by design.
- `lint_denylist.txt` -- the tokens that mark something as THIS title's (names, the ELF's address
  range). The engine's purity lint reads it; add a token when you add a title-specific name.
- `README.md` (title) -- provenance of the facts and the STATUS section. Update STATUS with what you
  verified and how (the command, the `run.meta` row, the test).

## Engine changes

`engine/` is a submodule pinned to NativeEE's `sly2-upstream` branch. If the port needs an engine fix:

1. It must be title-neutral: no Sly name, address or constant in `engine/` (the purity lint fails
   otherwise); semantics come from the hardware manuals (EE User's Manual, GS User's Manual, the IOP
   module interfaces), not from "what makes this game look right".
2. One class per `.h/.cpp` pair, `-Wall -Wextra -Werror`, no `getenv` outside the config loader, the
   engine's lint rules (`engine/tests/lint/`), a test that pins the fix.
3. Land it on NativeEE first, then bump the submodule pin here in its own commit
   (`chore: Pin NativeEE <sha>`), with the README's engine-changes list updated.

## Building and testing before a PR

```
./build.sh --no-disc --tests                                  # what CI runs: no game file needed
./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"     # the full build (your disc)
engine/verify/run_bounded.sh 120 leg.log -- <build>/app/host/nativeee --headless --frames 600 \
    --harness-out <out> --iso "/path/to/the.iso"             # a bounded boot leg; read <out>/run.meta
```

The build must end with `0 warning:` / `0 ld: warning`; the component suite must pass minus the rows in
`.github/ci/ctest_exclusions.txt`. If you need a new exclusion, add it with its reason to that file AND
to the README's "Known issues" -- an exclusion without a reason is not accepted. If a row's reason has
gone away (the engine fixed it), delete the row.

Run every game session bounded (`engine/verify/run_bounded.sh`, `NATIVEEE_WATCHDOG_S`): a hang must
still end as a reported `park=`.

## Commit style

Conventional Commits: `feat(hle): ...`, `fix(iop): ...`, `docs: ...`, `chore: Pin NativeEE ...`,
`ci: ...`. Imperative mood, capitalised subject, no trailing period, body wrapped at 72 columns
explaining what and why. One logical change per commit; title facts and engine pins in separate
commits. Do not commit build output, logs, frames or anything under a build directory.

## Reporting a bug

Open an issue with: the engine pin (`git submodule status`), your OS / compiler / CMake versions,
the exact command, and the `run.meta` rows (`park=`, `park_detail=`, `boundaries=`, `iop_modules=`)
or the test name and its output. Never attach a disc image, an ELF, or a frame dump of the game.

For anything seen while playing, record the session and attach the recording with the `run.meta`:

```
NATIVEEE_PAD_RECORD=session.pad engine/verify/run_bounded.sh 1800 play.log -- <build>/app/host/nativeee --harness-window \
    --harness-out <out> --iso "/path/to/your.iso"
```

Attach `session.pad` and `<out>/run.meta`, and say at which frame (`boundaries=` counts them; the
file's `frames=` header) the problem shows. Both files are free of game data: the recording holds
only frame indices, button bits and stick values (`<from>:<buttons_hex>:<hold>:<lx>,<ly>,<rx>,<ry>`
lines under a `#` header), `run.meta` holds the engine's census rows. A maintainer replays it
headless (`NATIVEEE_PAD_SCRIPT=session.pad ... --headless --frames N --dump-frames <dir>` with
`NATIVEEE_DUMP_FROM` / `NATIVEEE_DUMP_EVERY`, README "Replaying headless") and sees the same frames.
