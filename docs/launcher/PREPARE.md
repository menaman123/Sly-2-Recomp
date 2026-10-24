# Preparing your game -- the pipeline behind the launcher (decision D-A)

A release of Sly 2 on NativeEE ships THREE things and nothing derived from the game:

| asset | made by | what it is |
|---|---|---|
| the launcher | `engine/app/launcher` (the launcher track) | the Qt 6 window: find the disc, one progress bar "Preparing your game", Play |
| `engine-bundle-<platform>.tar.xz` | `cmake --build <build> --target engine_bundle` (`engine/cmake/EngineBundle.cmake`, `engine/tools/dist/engine_bundle.py`) | the engine as static archives, the host objects (with the release's build id), the headers the generated code compiles against, the tools (`ps2r_disc_extract`, `ps2r_elf_embedded`, `ps2r_recompiler_v2`, `ps2r_continuations`, `ps2r_iop_recompiler`, `vu1_aot_pack`, `nativeee-prepare`), the VU1 AOT prelude, this title's non-game-derived inputs (`TITLE_INPUTS.sha256`, `recomp/MANIFEST.sha256`, `iop/*/MANIFEST.sha256`, `hle/sdk_bindings.tsv`, `hle/iop_host.cpp`), `BUNDLE.json` (the exact command templates of that build) and `SHA256SUMS`. Asset-scanned before it is packed. |
| `compiler-<platform>.tar.xz` | `engine/tools/ci/make_compiler_bundle.py` over the pinned LLVM archive | clang + lld + llvm-ar + the compiler's headers + libc++ headers, under one `compiler/` directory |

The player's machine does the rest once per (disc, release), through `ps2r_prepare` (`engine/tools/prepare/`; the launcher
links it, `nativeee-prepare` is the CLI over it): download + verify the two archives, extract the ELF and the IOP images
from the player's disc (sha256 gated against `TITLE_INPUTS.sha256`), regenerate the EE and IOP code, refuse to go on unless
the text equals the `MANIFEST.sha256` pins, compile and link `sly2` the way the developer build was linked, write the game
folder:

```
<folder>/sly2                 the game (BUILD-ID == the release's developer build)
<folder>/nativeee.ini         paths.user_data=cache  paths.mc_dir=memcards  paths.game_files=game  paths.iso=<the disc>
                              engine.vu1_aot_prelude=vu1_aot_prelude  engine.vu1_aot_compiler=cache/compiler/compiler/bin/clang++
                              engine.vu1_aot_sysroot=<sdk>  engine.vu1_aot_pack=   (no pack: it would be game-derived)
<folder>/title/               hle/sdk_bindings.tsv (+ the pins)            <folder>/game/Sly2.rep/SCUS_973.16   the player's ELF
<folder>/memcards/            <folder>/cache/{downloads,bundle,compiler,work,vu1_aot,logs,saves}   <folder>/cache/prepared.json
```

The first run then fills the VU1 native tier with the same bundled compiler (`engine.vu1_aot_*`); the no-compiler fallback
(the specialised tier) is unchanged.

## The launcher's contract (what the launcher track wires)
- link `ps2r_prepare`; run `PrepareRunner::Run()` on a worker; the `Listener` gives `progress(fraction, eta_seconds, text)`,
  `failed(text, log_path)`, `finished(binary_path)`. Every `text` comes from `engine/tools/prepare/ui_strings.h` -- the only
  player-facing vocabulary of the step ("Preparing your game" is its only name); `lint_ui_strings.py` runs over the launcher
  sources too.
- `PrepareRunner::IsPrepared(folder, elf_sha256, release_tag)` is "ready to play".
- the release index is `https://api.github.com/repos/<owner>/<repo>/releases` (or a local `releases.json` for development:
  `tools/prepare_fixture_server.py`); assets are `engine-bundle-<platform>.tar.xz`, `compiler-<platform>.tar.xz`, `SHA256SUMS`.
- a headless `--prepare --index URL --iso ISO --folder DIR --jobs N --json REPORT` mode with the CLI's exit codes, so
  `games/<title>/verify/launcher_cold_judge.py --driver launcher` drives the launcher the way it drives the CLI.
- platforms: macOS arm64 + x86_64, Windows x64, Linux x86_64. **No PlayStation Vita**: the game compiles its own VU1 code
  on the player's machine at first run, and the Vita has neither a compiler nor a way to run freshly made code -- say so.
- the estimate: this class of machine prepares in about 1-1.5 minutes with 4 jobs (section 1 of the track report); the
  runner's live ETA is honest after the first few translation units.

## Pins (fetched 2026-10-24 from the GitHub release digests)
| platform | archive | sha256 |
|---|---|---|
| macos-arm64 | `LLVM-23.1.4-macOS-ARM64.tar.xz` (llvmorg-23.1.4) | `5d3179db39adeca5b17a34d82708fe976bc60e055b05fe557878fb3fb64d39ef` |
| macos-x86_64 | no x86_64 macOS asset in LLVM 23.x: the Homebrew `llvm` bottle for the x86_64 macOS runner (pinned by the formula's bottle sha256 at release time) | (per release) |
| windows-x86_64 | `llvm-mingw-20261020-ucrt-x86_64.zip` -- self-contained (mingw-w64 UCRT sysroot inside); the Windows engine bundle must be built with the SAME llvm-mingw | `fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd` |
| linux-x86_64 | `LLVM-23.1.4-Linux-X64.tar.xz` + a glibc sysroot (Debian `libc6-dev`, LGPL) under `compiler/sysroot/` | `25d814fda50d5fb0084b42be09605d8b1595d49d5e2e03a3345d7e112ff6f9f3` |

macOS needs the SDK (frameworks + `libSystem.tbd`); the runner looks for `sysroot/` in the bundle, then the Command Line
Tools SDK, else fails with the plain-language line that the launcher turns into the system's installer prompt.

## CI
`tools/ci/release_prepare_assets.sh <build> <tag> <llvm root>` produces `dist/engine-bundle-<platform>.tar.xz`,
`dist/compiler-<platform>.tar.xz`, `dist/SHA256SUMS` (+ the asset scan report); the release workflow uploads `dist/*` per
platform runner. The engine bundle of a release must come from the SAME build as the published developer binary (the
build id is regenerated on every build; the judge's hash gate compares them).

## The judge
`games/scus_97316_sly2_band_of_thieves/verify/launcher_cold_judge.py` (frozen): empty HOME, the offline fixture, the disc ->
prepare -> gates prepared / download / hash gate / boot / replay / again. RED on a branch without the tool, GREEN x2 on
`fix/prepare` (the track report has the evidence).
