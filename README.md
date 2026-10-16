# Sly 2: Band of Thieves -- a NativeEE title module

A static-recompilation port of **Sly 2: Band of Thieves** (PlayStation 2, NTSC-U, `SCUS_973.16`) on the
[NativeEE](https://github.com/menaman123/NativeEE) engine. This repository is the *title module*:
the facts of this one game (its ELF identity, boot environment, SDK-library binding table) and the
composition that boots it. The engine -- the recompiler, the EE/IOP/GS/VU/DMAC/SPU2 models, the
harness -- is the `engine/` git submodule and knows nothing about Sly.

**Nothing derived from the game is in this repository**, by construction: the build extracts the ELF
from *your* disc image, checks its SHA-256 against `games/<title>/TITLE_INPUTS.sha256` and recompiles
it into the build directory. The build directory lives outside the repository.

## Status (2026-10-15): boot stage, no per-title HLE

What works, verified on macOS (Apple clang, cmake + ninja):

- `cmake` configures NativeEE as the source tree with this repository's `games/` as the title
  directory (`-DPS2R_GAMES_DIR`), auto-discovering `scus_97316_sly2_band_of_thieves`.
- With `-DNATIVEEE_DISC=<your iso>` the build extracts `SCUS_973.16` (SHA-256 gated), runs the
  in-tree recompiler over it (6574 functions, 265 jump-table sites, 32 shards, 0 unhandled
  instructions) and links the result into `nativeee` with the title's play composition.
- `nativeee --headless --frames 600 --harness-out <dir> --iso <your iso>` boots the recompiled ELF on
  the engine's fallbacks (no GS/VU/DMAC/IOP models armed yet): crt0 runs, the kernel syscalls are served
  (InitMainThread, InitHeap, CreateSema, SetSysCall x12 -- the game installs custom syscalls --,
  AddIntcHandler, CreateThread, GetOsdConfigParam ...), `main` runs the whole IOP boot sequence
  (sceCdInit / GetDiskType / Mmode / DiskReady on the engine's libcdvd rows, `sceSifRebootIop`, IOP heap,
  the nine embedded IRX images pushed with `sceSifSetDma` and `sceSifLoadModuleBuffer` -- all answered
  without an IOP), the display init reaches the first frame boundary (the game's own vsync wait, bound
  as the engine's `WaitForVblank` mirror), libpad's `scePadInit` talks to the engine's padman server,
  and the run parks cleanly at the first RPC call into 989snd's IOP module:

  ```
  park=sif_server_unbound  park_pc=0x232788  park_tid=1  park_detail=sid=0x123456 func=0 client=0x443800
  boundaries=1  iop_module_loads=9  iop_modules=sio2man(3.01),padman(4.10),mcman(2.26),mcserv(2.10),
  Sound_Device_Library(3.03),USB_driver(2.04),lgAud(1.0a),989snd_Library(3.01),spiop(1.00)
  sdk_rows_installed=24  (libkernl SIF/heap/loadfile 11 rows, libcdvd 13 rows)
  ```

  That is `snd_Init` (the 989snd EE library at `0x2319e8`, `p3/989snd.cpp`) sending its first command
  to the `989snd_Library` IOP module (server `0x123456`) -- a custom module this binary has no model
  for. It is where the fallbacks stop carrying the title: everything behind it (the disc reader
  `0x14ba38`, pads, memory cards, rendering) needs the per-title IOP side.

Not done: any per-title HLE (the IOP modules -- Sly 2's nine, including Sucker Punch's own `spiop` and
989snd, are embedded in the ELF and loaded with `sceSifLoadModuleBuffer`; there is no disc-side IRX --,
the GS/GIF/VIF/DMAC/VU hardware fallbacks, streaming from raw sectors, audio), libpad/libmc/libgraph
rows and statics, the launcher / first-run flow.

## Layout

```
engine/                                  NativeEE (submodule) + small local, title-neutral fixes (see below)
games/scus_97316_sly2_band_of_thieves/   the title module (the engine's games/<serial>_<slug>/ contract)
  TITLE_INPUTS.sha256                    the disc file(s) the generated code is made from (the ELF only)
  hle/title_profile.cpp                  TitleProfile: identity, ELF facts, boot env (UNKNOWNs documented)
  hle/sdk_bindings.tsv                   the SDK-library binding table (6 libkernl rows, each with its evidence)
  hle/boot_leg.cpp                       the play composition: the headless cold-boot leg (TitlePlayMain)
  recomp/CMakeLists.txt                  runs the recompiler at build time into <build>/generated/...
  recomp/MANIFEST.sha256, INVOCATION.md  hashes of the generated output + how it was produced
  iop/stream_irx, vu1_library, renderer_profile   STATUS-only stubs (no target)
  lint_denylist.txt, README.md
build.sh                                 configure + build wrapper (the engine is the CMake source dir)
CMakePresets.json                        the same cache variables, as documentation
```

## Building with your own disc

Prerequisites: cmake >= 3.21, ninja, a C++17 compiler (Apple clang / clang / gcc), python3, SDL2 and
FFmpeg as the engine's README describes (Homebrew on macOS). Your own disc image of Sly 2 NTSC-U.

```
git clone --recurse-submodules https://github.com/menaman123/Sly-2-Recomp
cd Sly-2-Recomp
./build.sh --disc "/path/to/Sly 2 - Band of Thieves.iso"          # or: --elf /path/to/SCUS_973.16
```

The build directory defaults to `../sly2recomp_work/build-sly2` (override with `--build-dir`, it must be
outside the repository). The ELF's SHA-256 must be
`a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73` (NTSC-U, 2,597,048 bytes); any other
revision is refused with the reason.

Run the boot leg (always bounded):

```
engine/verify/run_bounded.sh 120 leg.log -- <build>/app/host/nativeee --headless --frames 600 --harness-out <out> \
    --iso "/path/to/Sly 2 - Band of Thieves.iso"
grep -E "^(park|park_pc|park_detail|sdk_rows_hit_list|iop_modules|boundaries|first_syscalls)" <out>/run.meta
```

See the picture (the engine's generic GS renderer over OpenGL -- no per-title class renderers):

```
# headless, every presented frame as <dir>/p<key>.ppm (the window image) + f<key>.ppm (the display grab)
engine/verify/run_bounded.sh 240 dump.log -- <build>/app/host/nativeee --headless --frames 400 --dump-frames <dir> \
    --harness-out <out> --iso "..."
# windowed play (SDL window on main, keyboard/gamepad pad through libpad -> padman; --frames caps the run)
engine/verify/run_bounded.sh 120 play.log -- <build>/app/host/nativeee --harness-window --frames 600 --harness-out <out> --iso "..."
```

`NATIVEEE_UNPACED=1` runs the window without the 60 Hz field pacer; `NATIVEEE_DUMP_EVERY=N` writes every Nth key;
`NATIVEEE_GS_ROUTES=<key,...>` / `NATIVEEE_GS_VERTS=<key>` write a frame's draw routes (+ `vram_<key>.bin`) /
every DrawIr with its vertices; `NATIVEEE_IPU_OUT_DUMP=<file>` is the fromIPU side of `NATIVEEE_IPU_DUMP`;
`NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>]` records VU1 kicks as `Vu1Case` files + their PATH1 packets
(hle/vu1_probe.h; replay with `tests/vu/test_vu1_capture_replay <case.bin> [--trace]`); `NATIVEEE_VU1_MODE=interp`
selects the VU1 interpreter tier; `NATIVEEE_PAD_SCRIPT=<stamp>:<buttons_hex>:<hold>[,...]` presses buttons in headless.

`NATIVEEE_WATCHDOG_S` (default 60) ends a leg that reaches no frame boundary with `park=watchdog` and the
spinning pc, so a hang is still a reported result. Boot-triage tooling in the composition (all env-gated,
title-side): `NATIVEEE_FN_TRACE=<hex,...>` (entry/return prints of generated functions), `NATIVEEE_GIF_TRACE=1`,
`NATIVEEE_IPU_DUMP=<file>`, `NATIVEEE_RAM_DUMP=<file>` (the 32 MB RAM image at a watchdog park) and
`NATIVEEE_WATCH=<hex>,<bytes>[;<hex>,<bytes>...][;max=<n>][;val=<hex>...]` (every EE store into the ranges with
the shadow-stack frame; needs a non-NDEBUG engine build, e.g. `--type RelWithDebInfo` with
`-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g"`). The engine's own per-tag DMAC / VIF narration is
`--log-level narrate` (`[DMAC] dmac-vif1: tag@...`, `[DMAC] dmac-gif:`, `[DMAC] dmac-sprfrom:`, `[VIF] vif1: cmd=`).

## Local changes to the engine submodule (uncommitted; upstream candidates)

All title-neutral; none mentions Sly. Listed in full in the work notes (`implement_notes.md`).

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

## License

MIT (see `LICENSE`). The engine has its own license in `engine/`. Sly 2 is the property of Sony
Interactive Entertainment / Sucker Punch Productions; you need your own copy of the game.
