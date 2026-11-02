# `games/scus_97316_sly2_band_of_thieves/native/` -- Sly 2's native paths

The PS2 subsystems the engine still MODELS every frame (the VU1 microprograms, the
GIF packet -> DrawIR parse, VIF unpack + DMAC chain walks, the hosted IOP + SPU2 driver path, VU0
macro mode, the GS register state) are replaced, **for Sly 2's specific use of each unit**, by native
host code living HERE -- in the title tree, outside the engine submodule -- and selected through the
engine's existing seams. This is NOT the decompilation: the game's recompiled EE logic is untouched.

## The contract (every native path)

1. **It replaces the engine's model of THIS title's specific use of a unit**, never the unit in
   general. A VU1 kick handler serves the microprogram images Sly 2 uploads (keyed by image hash +
   MSCAL entry); a GIF builder serves the packet shapes Sly 2 kicks; an IOP server answers the SIF
   RPC ids Sly 2's EE client calls. Anything else is handed back to the engine.
2. **It plugs into an engine seam** (the seam inventory, with file:line, is kept with the
   design notes): the VU1 AOT image registry / the kick dispatch, `DrawIrSink`, the VIF
   unpack span, the DMAC chain walker, the IOP module table, the SPU2 driver, the recompiler's COP2
   emit. No engine model is deleted or edited into a title-specific shape: the engine's generic model
   is **the fallback and the oracle**.
3. **It is behind a Config knob `title.native.<unit>`** (`vu1|gif|vif|dmac|iop|spu|gs|vu0`; bool), read
   ONLY through `native_knobs.h` (`NativeKnobs::From(cfg)`). Since 2026-10-31 `vu1`, `gif`,
   `vif`, `dmac`, `vu0` default ON (each proved with zero fallbacks on the lane and the Paris play lane;
   `--set title.native.<unit>=off` = the modelled path, the oracle); `iop`, `spu`, `gs` default off. The
   keys are in the engine's fingerprint, so a NON-DEFAULT value shows in run.meta `env_ps2` / `env_hash`;
   the default leaves both unchanged. The deterministic profile does not pin them.
4. **Hash-identical on the lane is the acceptance test.** With the knob OFF the run reproduces the
   stage BASELINE (det_lane.sh, 600 frames, no pad / boot.pad): `mem=0df0a2278a02` (pad
   `1b9cd487a3bc`), `audio=797c19071ebf`, `frames=2a66c7ab3d3d` (600 lines), `domain_tick=1013`,
   `syscall_count=2153418`, `gs_draws=585846`. With the knob ON the run reproduces the SAME
   mem/audio/frames/domain_tick/syscall_count/gs_draws; only `env_ps2` and `env_hash` may move
   (mem.mhash hashes guest RAM only: engine/core/verify/verify_host.cpp). The gate:
   the deterministic lane at 600 frames, no pad and boot.pad, `--set title.native.<unit>=on` vs the baseline rows.
   2026-11-01: the boot rows were RE-RECORDED: the engine's IOP clock now keeps its
   frame base with a forced advance past the next boundary's ceiling (`iop.frame_base_follows_skips`,
   default on), so the IOP no longer sits dark for the ~54 boundaries after boot's 989snd bank load (the
   dark window that keyed off every level's music streams): no pad `mem=c30e736339a1` / pad
   `mem=3fc18d5e7bdb`, `audio=931fbdc8ad54`, `syscall_count=2153334`; frames/domain_tick/gs_draws unchanged.
   `--set iop.frame_base_follows_skips=off` reproduces the old rows byte for byte (the oracle arm). The
   run.meta row `iop_clock_skips=follow=:skips=:skipped_frames=:max_skip_us=` counts the skips.
5. **It counts.** Every path reports to `NativeCensus` (served natively / fallen back to the model,
   per unit, plus an optional detail string); LegCensus writes run.meta `native=` from it:
   `native=vu1=on:734612/0:images=12,gif=off:0/0,...;any=on`.
6. **It is portable** (the Windows rule): no POSIX, no `_WIN32` / `__APPLE__` in this directory; host
   services only through `engine/platform`; SIMD only through `native_simd.h` (NEON / SSE2 behind one
   type, scalar fallback = the specification; `-DPS2R_NATIVE_SIMD_SCALAR=1` forces scalar for the
   A/B). Every TU here compiles with `-Wall -Wextra -Werror -ffp-contract=off` and is listed in the
   engine's `tools/ci/win_cross_smoke.sh` (x86_64-pc-windows-msvc syntax check).
7. **No game data.** Microprogram images, VIF/GIF captures, corpora, RAM/frame dumps live ONLY under
   `~/recomp-agent-data/scus_97316/` or the build dir. Code derived from game bytes (a per-image
   native body, a packet template table) is GENERATED AT BUILD TIME like the recomp shards, pinned by
   a MANIFEST, never committed. Hashes, addresses, counts and names are fine here.

## Layout

| file | role |
|---|---|
| `CMakeLists.txt` | `ps2r_title_native` (STATIC), added by `../hle/CMakeLists.txt`, linked into `ps2r_title_play` |
| `native_knobs.{h,cpp}` | `NativeKnobs::From(const Config&)` -- the eight `title.native.*` switches; `NativeUnit` |
| `native_census.{h,cpp}` | `NativeCensus::Instance()` -- per-unit served/fallback counters; `Row()` = run.meta `native=` |
| `native_simd.{h,cpp}` | the portable `Vec4f` (NEON / SSE2 / scalar) |
| `native_<unit>.{h,cpp}` | one per unit: `vu1`, `gif`, `vif` + `dmac`, `iop` + `spu`, `gs` + `vu0` -- the native paths themselves |
| `vu0/` | `title.native.vu0`: the seven VU0 microprograms the game's VCALLMS sites start (0x0 / 0x10 / 0x60 / 0xf0 / 0x120 / 0x150 / 0x1c0) as host bodies GENERATED AT BUILD TIME from the ELF's VU0 image by `vu0_native_gen` (pinned by `vu0/MANIFEST.sha256`); `native_vu0.{h,cpp}` installs the `VuTierDispatch::SetVu0Hook` body (fingerprint-gated, the cached tier is the fallback); `test_sly2_vu0_native` = the oracle (cached == interp == native on random register files). The COP2 inline ops are the recompiler's `--vu0-native` (`recomp/CMakeLists.txt` `PS2R_SLY2_VU0_NATIVE`, `recomp/INVOCATION.md`) |

Install order: `boot_leg.cpp` RunLeg builds `NativeKnobs` from `cfg` after the devices exist and
before the census is constructed; each native path's installer takes the knobs and the seam's
owner, installs only when its knob is on, and the engine's model runs when it is off or when the
path declines a shape (`NativeCensus::Fallback`).

## Why these units (the lane's profile, 1200 frames boot.pad, `sample` of the EE thread)

59.8 % GIF parse -> DrawIR, 26.9 % VU1 (cached tier), 11.9 % the game's recompiled code, 1.0 % VIF,
0.4 % IOP/SPU; gif_sink 30.2M kicks / 40.7M packed words / 24.5M image qw, 2.17M draws, 734.6M VU1
bundles. Sampling the headless run (macOS `sample`, 1 ms, past the boot screens) over the same frames reproduces the breakdown.
