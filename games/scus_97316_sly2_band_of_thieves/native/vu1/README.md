# `native/vu1/` -- Sly 2's VU1 microprograms as native host code (STAGE NATIVE, `title.native.vu1`)

The engine models VU1 per kick: a tier (cached / block / interpreter, `engine/vu/tiers/`) walks the
microprogram's bundles -- 61-77 % of the EE thread's CPU on the boot lane with everything modelled
(the harness's `nat_profile.sh`, --no-video arm). Sly 2 uploads a FIXED set of microprogram images
(`images.txt`: 10 known, 8 kicked on the boot + Paris lanes), so their native C++ is generated ONCE, AT
BUILD TIME, by the engine's own generic VU1 AOT generator and compiled into the title. At run time no
tier walks a listed image: no JIT, no first-run compile, no host compiler, no `dlopen`.

## How it works

| Step | Where | What |
|---|---|---|
| Recipe | `images.txt` | the authoritative image list (`VuImageHash::Fast` of the 16 KB micro memory = the key every engine table uses) with the per-lane kick census; hashes and counts only |
| Input | `NATIVEEE_VU1_CORPUS` (CMake cache var; defaults to the environment variable at first configure) | directories of `Vu1Case` records (`engine/vu/vu1_case.h`: the unit state at a kick -- the image bytes ARE game data, so the corpus lives under `~/recomp-agent-data/scus_97316/corpora/vu1/`, never here). Captured with the title's probe `NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>:<count>` (`hle/vu1_probe.h`) and the engine's AOT cache collector |
| Generator | `vu1_native_gen.cpp` -> `<build>/generated/games/<title>/native/vu1/vu1_<hash>.cpp` | per image: `Vu1AotVerifier::Verify(cases)` (gate 1: the specialised table == the interpreter; its native legs name the JR / JALR-reached blocks), `Vu1AotEmitter::ReachableBlocks` over {the cases' entries, those blocks} (the closure over fall-through, static targets, the wrapped delay slot), `Vu1AotEmitter::Emit` -- one REGION function per block with the VU register file in locals, every slot a `constexpr`, the engine's lane kernels / pipelines as the primitives, block chaining by guaranteed tail call (`engine/vu/vu1_aot/README.md`). Deterministic text (`engine_build = "title-native"`, no toolchain or timestamp) |
| Pin | `MANIFEST.sha256` | the generated TUs byte for byte (`cmake/ps2r_verify_manifest.cmake`, the recomp shards' rule; strict under `PS2R_RECOMP_CHECK_MANIFEST`). Re-record with `gen.sh <build> --record` after a generator / corpus / `images.txt` change and say so in the commit |
| Library | `ps2r_title_native_vu1` | `native_vu1.cpp` + the generated TUs (compiled with the engine's VU semantic flags: `-O2 -fno-strict-aliasing -fwrapv`, GNU adds `-ffp-contract=off -frounding-math`, `-w`) + the generated table `vu1_native_images.cpp` (`native_vu1_table.h`: hash -> `vu1_aot_exports_<hash>()`). Without a corpus: `vu1_native_images_empty.cpp`, the knob is inert |
| Seam | `NativeVu1` (`native_vu1.h`) | the engine's EXISTING registry: `Vu1AotTier::SetStatus(hash, Verified)` + `SetNativeExports(hash, exports)` per image, consulted at every kick before anything runs (`Vu1AotTier::Execute` -> `InstallNative` on the VU thread at the image's first kick -> the block loop calls the generated functions). `NativeVu1::Select` puts the dispatch on the AOT tier (`hle/leg_devices.cpp`, before the MTVU worker copies the settings); `Install` fills the inline unit and the worker's unit before `Start` (`hle/leg_mtvu.cpp`); the run-time cache / builder are not created. A kick of an image NOT in the table falls back to the tier's fallback (the block tier, proven == the interpreter) and is counted |
| Census | run.meta `native=vu1=on:<native kicks>/<fallback kicks>:images=N.native_bundles=N.native_blocks=N.installs=N.refused=N.progs=<hash>x<kicks>/<bundles><n|m>|....misses=<hash>x<count>|...` | `progs` = every image the live unit's block table decoded (`VuBlockTier::ForEachProgram`), `n` = its generated code installed, `m` = modelled; `misses` = kicks of images the table lacks. Plus the engine's `vu1_aot=` / `vu1_tier=` rows (`blocks=<runs>/<native runs>/<native ops>/...`) |
| Gate | `Vu1NativeCorpus` (`test_vu1_native_corpus.cpp`) | every case of the corpus replayed through the LINKED code vs the interpreter (registers, data memory, clock, bundles, kicked PATH1 bytes) and again under a sweep of cycle caps; the same gate the engine's run-time builder applies before publishing a library |

Why this seam and not the HLE program contract (`engine/vu/hle/vu1_hle_contract.h`): the HLE contract
describes a whole-program native routine producing complete kicks (hand-written programs); the AOT
registry is the contract the engine's generator was built for -- block functions dispatched by the
block loop (JR / JALR targets, the E bit, the cycle cap, the NOP sled, XTOP yields stay the loop's), so
every rule the interpreter has is kept op for op and the corpus / lockstep oracles apply unchanged.

## Verified (2026-10-30, build ~/ra-orch/wt/sly-nat-vu1-build; details in ~/ra-orch/notes/nat-vu1.md)

- The lane (det_lane.sh 600, no pad + boot.pad): knob OFF = the stage baseline; knob ON = mem / audio /
  frames / domain_tick / syscall_count / gs_draws identical, env_ps2 + `title.native.vu1=on;` (the
  fingerprinted knob). 1200 frames: identical hashes both arms.
- Vu1NativeCorpus: 10811 / 10811 cases identical, every op inside a generated function; cap sweep
  {1 .. 1000} 10 / 10 images.
- Paris 1200 lockstep (hub_ep1_fix + sm_paris.pad, MTVU=1, `diag.vu1_lockstep=on`): 2 631 574 kicks,
  divergent = 0, fallback kicks 0, 26 295 887 of 26 295 895 block runs native (24 of 1.55 G ops on the
  specialised table).
- Profile (--no-video, 1200 frames boot.pad, same minute): EE-thread CPU VU1 77.0 % -> 58.0 % and the
  wall 27.0 s -> 15.8 s; what remains of VU1 is the generated bodies' own cost (the fixed per-kick costs
  -- the block loop, the MAC-flag materialise, the XGKICK copy 2 %, the AOT tier's steady-clock pair 2 % --
  are small).

## Portability

Every hand-written TU here is C++17 with no POSIX / `_WIN32` / `__APPLE__` (the engine's
`tools/ci/win_cross_smoke.sh --tu-dir` compiles them for x86_64-pc-windows-msvc). The generated TUs
chain their block functions with a guaranteed tail call (`VU1_AOT_MUSTTAIL`: clang, gcc >= 15); an MSVC
front end generates with `--chain 0` (`PS2R_SLY2_VU1_NATIVE_CHAIN`), and the MANIFEST check (recorded
for chain=1) is skipped there.

## Does not belong here

Image bytes, corpora, captures (game data: `~/recomp-agent-data`); the generated TUs (`<build>/generated`);
op semantics (the engine's `vu_cached_tier_exec.inl` owns them; the generator names them).
