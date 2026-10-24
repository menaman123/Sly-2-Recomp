# Releasing Sly-2-Recomp

The maintainer's half of the "Downloads" section of README.md: what a release is made of, what it must never contain,
how the launcher consumes it, how to cut one, and what was measured. Decisions D-A and D-B are taken (not re-opened
here): **nothing game-derived is downloaded or published; the player's machine compiles the player's game**;
targets are macOS arm64 + x86_64, Windows x64, Linux x86_64; **no Vita**.

## What a release contains

Three files per platform, plus three small files for the whole release. Every one is made from this repository and
the NativeEE engine alone, on GitHub's runners, from a checkout that never saw a disc.

| File | What it is | Made by | Size (macOS arm64, measured 2026-10-24) |
|---|---|---|---|
| `sly2-launcher-<tag>-<platform>.dmg` / `.AppImage` / `.zip` | the launcher: the engine's Qt 6 application (`engine/app/launcher`, title-neutral) carrying `launcher/title.json`; Qt linked dynamically (LGPL v3) with the notices; macOS: signed (ad hoc unless a Developer ID is configured), Linux: an AppImage, Windows: a portable folder in a zip | ci.yml `launcher` job -> `engine/tools/ci/release_launcher.sh` / `.ps1` | **40 MB** (.dmg; the app is 99 MB unpacked -- with Homebrew's Qt, which drags QtQml/QtQuick and ~35 dylibs; the official kit on the runner should be smaller) |
| `engine-bundle-<platform>.tar.xz` | the **engine bundle**: `lib/` -- every engine static library + the title's HLE library + static SDL2 + the LGPL FFmpeg recipe's three libraries (45 archives, 14 MB); `bin/` -- `ps2r_disc_extract`, `ps2r_elf_embedded`, `ps2r_recompiler_v2`, `ps2r_iop_recompiler`, `vu1_aot_pack`; `include/` -- the engine's 620 headers; `games/scus_97316_sly2_band_of_thieves/` -- this repository's title module (source); `tools/recompiler_v2/continuations.py`; `BUNDLE.json`, `BUILD-ID`, `LICENSE-NativeEE.txt`, `THIRD_PARTY_NOTICES.md`, `licenses/` (route B, the public runner's fallback shape; route A's shape is `tools/dist/engine_bundle.py`'s -- "Two routes" below) | release.yml `engine-bundle` job -> `build.sh --no-disc --preset macos-dist` (resp. `linux-dist`, `windows-clang-cl-release`) -> `engine/tools/ci/stage_engine_bundle.sh` | **4.5 MB** (25 MB unpacked) |
| `compiler-<platform>.tar.xz` | the **compiler kit** (top folder `compiler/`): `bin/clang++` (+ `clang`; Linux also `lld`, `ld.lld`; Windows `clang++.exe`, `clang-cl.exe`, `lld-link.exe`, `llvm-lib.exe`), `llvm-ar`, `llvm-ranlib`, `lib/clang/20/include` (the compiler's own headers), `LICENSE.TXT`, `COMPILER.json` -- cut from the OFFICIAL LLVM 20.1.7 release asset, sha256-pinned in `engine/tools/toolchain/COMPILER_PINS.sha256` | release.yml `engine-bundle` job -> `engine/tools/toolchain/fetch_compiler.sh` / `.ps1` | **39 MB** (226 MB unpacked; the official kit it is cut from is 1.4 GB) |
| `SHA256SUMS` | `sha256sum` format over the files above | `engine/tools/ci/release_index.py` | 1 KB |
| `release-index.json` | the release in the shape of one element of the GitHub Releases API (`tag_name`, `assets[{name,size,content_type,browser_download_url,digest}]`, ...) plus `x-nativeee` naming the three roles per platform, the platforms NOT in this release, and the manifest's `unsupported` rows | `release_index.py` | 3 KB |
| NOTES.md (the release page's text) | the table of downloads per computer, the Vita statement, the checksums | `release_index.py` | -- |

**A whole platform is ~85 MB** (macOS arm64 measured: 40 + 4.5 + 39). Across four platforms a release is ~350 MB
of assets, of which ~160 MB is compiler. The names `engine-bundle-<platform>.tar.xz` / `compiler-<platform>.tar.xz`
and the key `windows-x86_64` are what `nativeee-prepare` (engine/tools/prepare) looks up in a release -- the contract
is the consumer's, the packaging follows it.

Why the compiler at all: NativeEE is a *static* recompiler. The game program is C++ generated from the game's ELF;
it has to be compiled, and the ELF is the player's. Shipping a compiler that we pin and verify is what makes "press
Play" possible without asking the player to install Xcode or Visual Studio *for the compiler* (they still need the
system's own headers -- below).

### What is explicitly NOT in a release

- **No disc bytes, no extracted ELF or IOP module, no recompiled code**, no `nativeee` binary of a disc build (the
  old `release.yml` shipped exactly that from self-hosted lanes; that job is gone -- the lanes are a test now and
  upload only `run.meta` / `pack.txt`).
- **No VU1 AOT pack** (game-derived; README "The VU1 AOT pack"). The launcher compiles it on the player's machine at
  first run with the same compiler kit; the no-compiler fallback in the engine stays.
- No textures, audio, FMVs, saves, states, screenshots of the game.
- No system SDK: no macOS SDK, no Windows SDK / UCRT, no glibc headers. They are Apple's / Microsoft's / the
  distribution's and not ours to redistribute. The launcher must check for them and say so in plain words
  (macOS: `xcode-select --install`; Linux: `libc6-dev`; Windows: the Windows SDK -- the launcher offers Microsoft's
  installer). This is the `prepare` track's open item; `COMPILER.json` carries the requirement per platform
  (`system_sdk_required`).
- No Vita build. See below.

The gate is mechanical: `engine/tools/ci/asset_scan.py` (names, format magic, ELF byte windows when the title ships
a windows file) runs over the source trees (ci.yml `guard`), over every staged folder and zip inside the scripts, and
once more over every downloaded file in the `release` job before anything is attached. `stage_engine_bundle.sh`
additionally REFUSES a build dir whose `CMakeCache.txt` names `NATIVEEE_DISC` / `NATIVEEE_TITLE_ELF` or that holds any
`libps2r_title_recomp*` / `recompiled*.cpp`. `release_index.py` REFUSES a `.STUB` marker (a pipeline dry run's
placeholder launcher), a file matching no role, and an incomplete platform unless its lane failed (then the platform
is named in NOTES.md and `x-nativeee.not_in_this_release`).

## How the launcher consumes a release ("Preparing your game")

The launcher ships with `titles/title.json`, whose `release` block names the repository and the asset patterns. On
the first Play it:

1. reads the Releases API (or `release-index.json` of a chosen release -- same shape; the `launcher_cold_judge`'s
   offline fixture server serves `[ release-index.json ]` and the files, and the launcher cannot tell the two apart);
2. picks, for this computer's platform key (`macos-arm64`, `macos-x86_64`, `linux-x86_64`, `windows-x86_64`), the three
   assets by role (`x-nativeee.platforms.<key>.{launcher, engine_bundle, compiler}`; or by the manifest's
   `release.asset_pattern` for the bundle), downloads the bundle and the compiler, verifies each against `SHA256SUMS`
   (`digest` in the index is the same value), unpacks them (`tar -xf` on macOS / Windows, `unzip` on Linux -- zip on
   every platform for that reason) into its app-data folder;
3. checks the system SDK named in `COMPILER.json`;
4. runs, with the bundle's `bin/` tools: extract the ELF (and embedded IOP modules) from the player's disc image,
   SHA-256 gated against `games/<title>/TITLE_INPUTS.sha256`; recompile (EE: `ps2r_recompiler_v2` + `continuations.py`;
   IOP: `ps2r_iop_recompiler`); compile the generated shards and the title's play composition with the kit's compiler
   (`BUNDLE.json`: `shard_compile_flags`, `include_roots`); link against `BUNDLE.json`'s `libs_in_order` +
   `frameworks` / `system_libs` with the platform linker (macOS: Apple's `ld` -- see "Decisions"); the VU1 AOT pack
   with `vu1_aot_pack` and the same compiler;
5. shows ONE progress bar, "Preparing your game", with an honest time estimate; the result is cached per
   (disc fingerprint, release tag) so the next Play starts the game.

### Two routes to the bundle -- the open convergence item

`stage_engine_bundle.sh` has two routes. **Route A** runs the engine's `engine_bundle` CMake target
(`cmake/EngineBundle.cmake` -> `tools/dist/engine_bundle.py`): it reads the build's own `build.ninja` /
`compile_commands.json` and writes `BUNDLE.json` with the EXACT command templates `nativeee-prepare` replays
(`link.command`, `link.archives_on_machine`, the shard / IOP / hle-TU compile commands). That target defines itself
**only in a build that has the title's recompiled code** -- a DISC build -- because the shard commands it templates
exist only there. A public GitHub runner has no disc, so there route A is impossible and **route B** stages the
fallback shape above with a `format: 0` BUNDLE.json (link order read from the no-disc stub's link line, the shard
flags from `games/<title>/recomp/CMakeLists.txt`) -- which `nativeee-prepare` does NOT read today.

So, as of this writing: the public lanes' bundles are real, scanned, sized and smoked (the kit compiles a title TU
against them), but a player's `nativeee-prepare` can consume only a route-A bundle. release.yml therefore also
takes the self-hosted macOS title lane's route-A bundle (`engine-bundle-disc-macos`, built from the disc, scanned with
the LIVE ELF, nothing generated inside) and lets it REPLACE the public one for that platform. Closing the gap is one
of: (a) `engine_bundle.py` learns to template from a no-disc build (synthesising the shard commands from the hle TU's
command + the recomp CMakeLists flags), or (b) `nativeee-prepare` learns the `format: 0` shape, or (c) every
platform gets a self-hosted disc lane. (a) keeps the public-runner-only release and is the recommendation.

## Platforms -- and the Vita

| Platform key | Runner | Compiler kit (official LLVM 20.1.7 asset) | Linker on the player's machine |
|---|---|---|---|
| `macos-arm64` | macos-14 | `LLVM-20.1.7-macOS-ARM64.tar.xz` | Apple's `ld` (Command Line Tools) |
| `macos-x86_64` | macos-15-intel | `LLVM-20.1.7-macOS-X64.tar.xz` | Apple's `ld` |
| `linux-x86_64` | ubuntu-22.04 | `LLVM-20.1.7-Linux-X64.tar.xz` | the kit's `ld.lld` |
| `windows-x86_64` | windows-2022 | `clang+llvm-20.1.7-x86_64-pc-windows-msvc.tar.xz` | the kit's `lld-link` |

**No PlayStation Vita, and none planned.** The reason is the VU1 load: Sly 2 drives the PlayStation 2's second
vector unit hard -- every vertex of every frame goes through VU1 microprograms that this port recompiles and, at
60 fps, executes natively on the host's CPU (the engine's VU1 tiers; the AOT pack exists because even a desktop CPU
needs the pre-compiled form). The Vita's CPU cannot carry that work at speed, and nothing in D-B's set of targets
changes for it. `launcher/title.json` carries the same statement in player's words under `unsupported`, and
`release_index.py` copies it into NOTES.md and the index so the launcher can show it to anyone who asks.

## Cutting a release

```
git tag v0.1.0 && git push origin v0.1.0
```

release.yml then runs: ci.yml (guard, the no-disc matrix, the four launcher lanes, the title lanes if a self-hosted
runner is configured), `engine-bundle` x 4 (compiler kit + no-disc dist build + bundle + the two smokes), `release`
(re-scan, sums, index, DRAFT). Open the draft on the Releases page, read NOTES.md and the `release-reports` artifact
(the asset-scan reports, `BUNDLE-<platform>.json`), and publish. A platform whose lane failed is listed as "not in
this release"; fix and re-tag (`v0.1.1`) rather than editing the draft's files by hand -- the index would lie.

Before tagging, on a Mac:

```
engine/tools/ci/release_local.sh --title-root . --out ../release-local --tag v0.1.0
```

does the macOS lane step for step (the no-disc dist build, the compiler kit with its smoke, the bundle with its smoke,
the launcher .dmg, the scan over every file, SHA256SUMS + index + NOTES) and prints the sizes. `--stub-launcher` runs
the pipeline without Qt; the index step then shows the refusal a stub earns. The Windows twins (`.ps1`) are run by
hand on a Windows machine; there is no local Linux lane on a Mac (the ubuntu runner is the Linux lane; the engine's
`tools/ci/linux.Dockerfile` toolchain image runs `release_local.sh` under Docker).

### Secrets (all optional)

| Secret | Effect | Without it |
|---|---|---|
| `MACOS_CODESIGN_IDENTITY` | `codesign --sign <identity> --options runtime --timestamp` on the launcher bundle and the .dmg (the identity must already be in the runner's keychain -- add an import step of your own before the launcher job) | ad hoc signature; the player right-clicks > Open once |
| `MACOS_NOTARY_PROFILE` | `xcrun notarytool submit --keychain-profile <profile> --wait` + `stapler staple` on the .dmg (the profile is created with `notarytool store-credentials` on the runner) | no notarization; Gatekeeper's dialog |

No secret is needed for Windows (SmartScreen asks once; a signing certificate is not in scope) or Linux.

### Caches

- `compiler-kit-<platform>-<sha256 of COMPILER_PINS.sha256>`: the official LLVM tarball (0.9-1.9 GB per platform).
  The hash is re-verified on every run; a cache can only speed things up, never change what ships.
- `packaging-pins-linux-x86_64-<hash of PACKAGING_PINS.sha256>`: linuxdeploy + its Qt plugin (sha256-pinned).
- Qt: `jurplel/install-qt-action` with `cache: true` (Qt 6.8.3 per platform).
- vcpkg binary cache on Windows (as ci.yml).

## Decisions recorded while building this (not to be re-opened without a reason)

- **LLVM 20.1.7** is the pin because it is the one official release with all four kits (20.1.8 / 21.x lack
  macOS-X64) whose sha256 the GitHub Releases API publishes (`assets[].digest`; <= 20.1.6 have none).
- **The kit is cut, not shipped whole**: 62 MB vs 1.4 GB. `COMPILER.json` records the members cut and the upstream
  pin; `fetch_compiler.sh --verify-only` re-checks a cached tarball.
- **macOS links with Apple's `ld`**, not lld: lld 20.1.7's ld64 flavour cannot read the macOS 26 SDK's TBD stubs
  (`unknown architecture arm64e.x1-macos`), and the SDK comes from the Command Line Tools, which bring Apple's `ld`.
  The kit's clang drives it by default (its "-lto_library ... does not exist" warning is expected: LTO is not used).
- **`.tar.xz` for the bundle and the compiler** (what `nativeee-prepare` unpacks with `tar -xf`: bsdtar on macOS /
  Windows 10+, GNU tar on Linux; 39 MB vs 62 MB as a zip for the compiler); the Windows launcher stays a zip (no
  tool reads it; a player unzips it).
- **Asset names are `nativeee-prepare`'s lookup names**: `engine-bundle-<platform>.tar.xz`, `compiler-<platform>.tar.xz`
  (not per tag: the compiler changes only with the pin; the bundle is per release anyway). The launcher image is
  `${product}-launcher-${tag}-${platform}.<ext>`. `launcher/title.json`'s `release.asset_pattern`
  (`${product}-${tag}-${platform}.zip`) is TwinsanityEvolved's prebuilt-game model and matches nothing here -- it
  should be removed or re-pointed by the launcher track; `release-index.json`'s `x-nativeee` block names every role
  explicitly so neither side has to guess.
- **The launcher is built once per run** in ci.yml (push / PR / tag) and release.yml attaches that artefact.
- **DRAFT release**: a maintainer reads NOTES.md and the reports before anything is public.
- **The engine bundle comes from the `*-dist` presets** (static SDL2 from the pinned release; the LGPL FFmpeg
  recipe; Linux: static libstdc++/libgcc), never from a Homebrew / apt build: the player's machine has none of those.
  The local dry run with `macos-release` showed four Homebrew dylibs on the link line; `BUNDLE.json` would have
  listed them as `external_dynamic` -- the dist preset's bundle lists none.
- **`enable_language(OBJCXX)` moved to the engine's root CMakeLists** (Apple only): with FetchContent SDL2 the
  Metal adapter's `.mm` objects failed at generate time (`CMAKE_OBJCXX_COMPILE_OBJECT` unset) when the language
  was enabled only in its own directory.
- **The title lanes stay as a test, upload nothing of the build** (D-A). `stage_dist.sh` is their self-check.
- **macos-15-intel** for x86_64 (TwinsanityEvolved's choice; macos-13 is retired).
- **Windows system headers**: the kit has `clang-cl` + `lld-link`; the UCRT / Windows SDK must be on the player's
  machine (not redistributable in a zip). The launcher's prepare step owns the detection and the offer to install.
