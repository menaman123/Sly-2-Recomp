# games/scus_97316_sly2_band_of_thieves -- Sly 2: Band of Thieves (NTSC-U) title module

The engine's title contract (`engine/games/README.md`): a directory with `hle/CMakeLists.txt` is a
title; `ps2r_title_hle` defines `ActiveTitleProfile()`; `recomp/`, `iop/` (this title owns
`iop/CMakeLists.txt`: several recompiled modules instead of the one-module `iop/stream_irx/` layout),
`vu1_library/` and `renderer_profile/` are optional targets consumers gate on with `if(TARGET ps2r_title_*)`.

## Provenance of the facts here

- Disc: `SYSTEM.CNF` `BOOT2=cdrom0:\SCUS_973.16;1` VER=1.00 VMODE=NTSC. Volume root holds only
  `SYSTEM.CNF`, `SCUS_973.16` (2,597,048 bytes, sha256 `a511a94b...6a73`, crc32 `D67D6E1E`),
  `IOPRP300.IMG` (the stock SDK 3.0 IOP reboot image) and `Z6TAIL.DUP` (32 MiB padding). The game
  streams its data from raw sectors; nothing else is in the ISO9660 filesystem.
- ELF: entry `0x100008`; PT_LOAD #0 `0x100000` filesz `0x1dd230` memsz `0x352200`; PT_LOAD #1 =
  the `.irx` section `0x472200..0x4e5e40` with nine embedded IOP modules (sio2man, padman, mcman,
  mcserv, libsd, usbd, lgAud, 989snd, spiop) loaded with `sceSifLoadModuleBuffer` after
  `sceSifRebootIop("cdrom0:\IOPRP300.IMG;1")`. No disc-side IRX -> no `.IRX` row in
  `TITLE_INPUTS.sha256`; the modules the engine has no native owner for are EMBEDDED-module rows
  (`SCUS_973.16!lgAud`, `SCUS_973.16!989snd_Library`, `SCUS_973.16!spiop`) plus the reboot image's
  stock fileio (`IOPRP300.IMG!FILEIO_service`), each extracted by `tools/elf_embedded` SHA-256 gated
  and recompiled at build time (`iop/`).
- crt0 (`0x100008..0x100220`, disassembled): `_gp 0x2e5270`, `.stack 0x452200..0x472200`, the kernel's
  initial `$sp` `0x472200`, argv block `0x2dd380`, `main 0x1a7518`, `exit 0x233828`.
- SDK: every `PsII<lib>` stamp reads 3000 (libkernl, libgraph, libdma, libcdvd, libpad, libmc, libscf,
  libmpeg, libipu). Row addresses in `hle/sdk_bindings.tsv` were each confirmed by disassembly; the
  comment on each row is the evidence.

## What is HLE'd

Nothing per-title. `hle/sdk_bindings.tsv` binds 25 sce-library rows to the engine's generic bodies:
12 libkernl (sceSifInitRpc/InitCmd/AddCmdHandler/RebootIop/SyncIop/BindRpc/CallRpc, sceSifInitIopHeap/AllocIopHeap/
FreeIopHeap, _lf_bind, sceSifLoadModuleBuffer) and 13 libcdvd (sceCdInit/DiskReady/DiskReady_old/Sync/
SyncS/Read/GetDiskType/GetError/Break/Mmode, cmd_sem_init, _sceCd_ncmd_prechk/_sceCd_scmd_prechk --
every libcdvd routine this ELF links; it has no sceCdSearchFile/Status/ReadClock/St*). Everything
else runs as recompiled MIPS. The frame boundary is the game's own vsync wait (`0x16f560`, declared as
`TitleProfile::Gs::vblank_wait_entry`): the composition installs the engine's `WaitForVblank` mirror
there because this ELF links no `sceGsSyncV`.

The composition (`hle/boot_leg.cpp` + the `hle/leg_*.{h,cpp}` phase classes) also: opens the disc image
(`--iso`, else `<game_files>/<disc_image_file>`) for the libcdvd rows (`leg_disc`); hosts the IOP
(`leg_iop` over `hle/iop_host.cpp`, see below); and answers the kernel's transfer id for `sceSifSetDma`
lists without the command attribute (`leg_disc`; the engine keeps its first title's rule there -- an
upstream question, see the work notes).

`hle/boot_leg.cpp` is `TitlePlayMain` (the launcher hand-over) and `RunLeg`, which is only the
construction order of the leg's phases, one class per `.h/.cpp` pair under `hle/` (the engine's
LintOneClass / LintSizeBudget / LintHeaderGlobals rules apply to the title tree; every `NATIVEEE_*` knob
is read once in `leg_knobs.cpp`): `LegCore` (memory, MMIO sink, time domain, INTC + timers, SIF regs, the
boundary, kernel seams) -> `LegGs` (GsState, video stack, DrawIR queue, frame dump, the GS frame boundary)
-> `LegDevices` (GS MMIO, GIF, VU0/VU1, VIF, DMAC) -> `LegMtvu` (MTVU worker, VU1 AOT) -> `LegDmaIpu`
(register windows, IPU, COP2) -> `LegInterrupts` (GS IRQ observer, syscall-entry ISRs) -> `LegDisc` ->
`LegServices` (RpcRouter, mcserv / padman / sdrdrv faces, SPU2 + mixer) -> `LegAudio` -> `LegPlay`
(live pad, session window) -> `LegIop` -> `LegPad` (slot-4 pad stamp + scripts) -> `LegImage` (ELF,
dispatch, SDK manifest) -> `LegPark` (LegCloser + counters) -> `LegCensus` (the run.meta rows) ->
`LegParkSink` (park / terminate hooks, the IOP reboot image) -> `LegVerifySlot` (slot-7 lanes / present /
dump / frames cap, the join ladder) -> `LegWatchdog` -> the cold boot. The repository README's layout
map says what each pair owns.

## The IOP arm (`iop/`, `hle/iop_host.cpp`)

Nothing per-title is HLE'd on the IOP either: the title's own modules RUN. `iop/CMakeLists.txt`
recompiles each extracted image with the in-tree IOP recompiler (`--base <load address> --namespace
iop_<id> --import-call`; bases and the RAM layout in `iop/iop_modules.h`; `iop/<id>/MANIFEST.sha256`
pins the generated output) into `ps2r_title_iop_<id>`, aggregated as `ps2r_title_iop` (+ `ps2r_iop`,
`ps2r_sif_iop`). `hle/iop_host.cpp` (`IopHost`) arms the engine's cooperative IOP kernel with the stock
Sony modules served by the engine's HLE (sysmem, intrman, ioman, loadcore, stdio, sysclib, thbase,
thsemap, thevent, thmsgbx, timrman, sifman, sifcmd, libsd over SpuCore, cdvdman over the disc, modload,
usbd idle) registered in the engine's `IopLibraryTable` under their export ordinals; the EE's
`sceSifSetDma` pushes land in IOP RAM, `sceSifAllocIopHeap` is the sysmem heap, and each
`sceSifLoadModuleBuffer` boots the recompiled module whose `.iopmod` name matches (SHA-256 of the pushed
image checked against the build's row, relocated to its base, entry run with the module's argv, threads
dispatched to their first block) or records a row for a natively served one (its image placed in RAM
so a probe of its text -- lgAud's usbd version scan -- finds real bytes). Every `sceSifRegisterRpc` a
hosted module makes becomes an `IopRpcServer` on the EE's RpcRouter: 989snd's `0x123456`/`0x123457`
and fileio's `0x80000001`/`0x80000003` answer the EE with the modules' own handlers. An import no
library serves parks (`iop_import_unresolved`). FILEIO is booted from the reboot image read off the
disc (the EE's libfileio talks to it: `sceFsInit`, then `rom0:ROMVER` inside `sceGsSetDefDispEnv`); its
completion commands reach the EE's `sceSifAddCmdHandler` handler through the engine's SIF command path
(`SceSif::QueueCommand` / `DeliverPendingCommands`; the libkernl command buffer `0x44ed80` is title
data in `iop_modules.h`).

`PS2R_IOP_ORACLE` stays 0 on `ps2r_app_host`: `ps2r_title_play` links `ps2r_title_iop` directly and
never includes `app/host/iop_arm.h`. For this title the kernel-hosted IOP IS the play shape (its audio
and streaming owners are its own modules); `LintPlayBinaryNoIop` is registered only when an oracle
target exists, so nothing is skipped or edited.

## The texture gate (`verify/judge_textures.py`)

Every distinct texture the resolver decodes in a leg is dumped by the engine's `TextureDumpSink`
when the composition arms it: `NATIVEEE_TEX_DUMP=<from>,<to>` (present keys; `hle/leg_knobs.h`,
armed / disarmed at the frame boundaries by the Verify slot) writes `<dump dir | harness out>/
textures/<hash>.{ppm,alpha.pgm,json,win,clut}` once per distinct (tuple, bytes) with the engine's
own Oracle B verdict (fast decode tier == the per-pixel PsmLayout reference) in the json, and
`serves.tsv` for the reads served from targets. `verify/judge_textures.py --textures <dir>
[--pcsx2 <PCSX2 HWDumpDirectory with SaveTexture + SaveAlpha + SaveInfo dumps>] [--artefacts <dir>]`
is the judge: PASS only with 0 Oracle B differences and 0 PCSX2-matched textures beyond tolerance
(mean abs error <= 1/255 per channel, no structural periodicity; 16-bit-sourced colours at 5-bit
precision), findings attributed to a class (swizzle / clut / format_tbw / source / value) with a
contact sheet (ours | oracle | diff). ctest rows (`verify/CMakeLists.txt`, registered by the engine's
tests/ after `enable_testing()`): `Sly2_JudgeTexturesSelftest` always; `Sly2_JudgeTexturesCairo` over
the Cairo-opening fixture under `$NATIVEEE_PRIVATE_FIXTURES/games/<this title>/verify/textures/
cairo_opening/{textures,pcsx2}` (game-derived images: never in the repository; SKIP 77 without them).
The gate's first run found every PSMT4 texture of the opening wrong (the engine dropped PSMT4
uploads); after the engine fix all 132 PCSX2-matched textures are byte-identical. The census row
`gs_xfer=` (run.meta) counts host->local transfers per BITBLTBUF.DPSM.

## Overlays -- does the game load code outside the recompiled image? NO

Verdict (overlays stage, 2026-10-17; `$WORK/overlay_notes.md`): the recompiled image is the whole EE code
of this title. Three independent evidences, each over the boot + the Cairo prologue + the Episode 1 (Paris,
world `nightclub`) load reached through the game's pause-menu "Advance to Episode 1" code:

1. Writes into the code ranges: `NATIVEEE_WATCH=text[;<hexaddr>,<bytes>...][;every=<n>][;max=<n>][;ramdump=<file>]`
   (`hle/text_watch.{h,cpp}`, class `TextWatch`) snapshots `.text`+`.vutext` `[0x100000,0x262380)` once the ELF is
   in RAM and memcmp's it at every EE boundary (any writer: CPU stores, the sceCdRead block copy, SIF/IPU/VIF DMA
   -- unlike `StoreWatch`, which rides FastMem's Debug-only observer). 9000 boundaries, `text_watch=...dirty_boundaries=1:
   events=1041:pages=3` -- and those 1041 words are all in the extra range `0x74000,0x3000`, at boundary 1: the SDK's
   three libkernl kernel patches (the game's own self-installed code, the only one): `_InitSys` 0x2396c0 ->
   0x2397e8 (0x7a8 bytes `.data` 0x2c4ac0 -> 0x80074000), 0x239968 (0x740 bytes 0x2c5288 -> 0x80076000),
   0x23b290 (0x330 bytes 0x2c5a68 -> 0x80075000), each copied with the kernel's syscall 0x5a and registered with
   SetSyscall (0x5b, then the TLB helpers 0x54..0x58) -- reached only through the HLE'd syscall table, never
   dispatched as EE code. `.text` itself: 0 events in every run.
2. Jumps outside the function table: every indirect target resolves through `find_containing_function`
   (tiers 1-3) or parks `fatal_no_jump`; all runs end `park=none`. The composition also counts guest calls the
   kernel makes to a non-function-start (`guest_call_unknown=` in run.meta): 13, all at boot, all the kernel-patch
   blobs above (`DispatchTable::Invoke` is silent for them; the patched TLB helpers are replaced by the engine's
   `hle_InitTLB`). A RAM census (`$WORK/code_census.py`, MIPS function-shape idioms per 4 KiB page) after the Paris
   load finds code-shaped pages outside `.text` ONLY at 0x74000..0x77000 (the patches), 0x2c4000..0x2c6000 (their
   `.data` source) and 0x473000..0x4be000 (the `.irx` section = IOP MIPS code).
3. Disc: the game reads raw sectors only (36 `sceCdRead` for the FMV + the `intro` world, 28 more for `nightclub`
   LBA 0x53420/0x53430 header + stream 0x56640..0x57b1e, all through the one wrapper 0x14bc78); the streams are
   LZ-compressed. A sector scan of the whole 2.85 GB image finds code-shaped sectors only in LBA 271..1675 = the
   ELF + IOPRP300.IMG. IOP: `iop_module_loads=9` before and after the Paris load (the nine ELF-embedded IRX; no
   disc-side module exists).

The transition exposed E210 (fixed, `tools/recompiler_v2`): the discovery walk fell through the kernel's no-return
`ExitThread` syscall of crt0's InitMainThread root (0x100220) into the address-taken function 0x100228 (the first
Splice native, pointed to from `.data` 0x2ac8e0), so an indirect call into it entered at the ExitThread and killed the
main thread as soon as Paris ran its first script native. `rv2_decode.h is_noreturn_syscall_imm` makes
`addiu v1,zero,{0x04,0x23,0x24}; syscall` a hard stop; 6575 functions, `recomp/MANIFEST.sha256` re-recorded.

## Controls, the pause menu and the save states (`hle/leg_play.cpp`, `hle/leg_saves.cpp`)

Keyboard defaults (`engine/platform/sdl/input/default_bindings.ini`; rebind in the menu's Controls page):
WASD = left stick, mouse = right stick, Space = cross, Left Shift = square, right mouse button = circle,
E = triangle, 1/2/3/4 = L1/L2/R1/R2, 5/6 = L3/R3, Y = START, Tab = SELECT, arrow keys = D-pad. A game
controller uses SDL's standard layout (A cross, B circle, X square, Y triangle).

Host keys (the engine's SessionGui, title-neutral): **Escape** = the pause menu (the game pauses; Resume /
Quit), **F1** = the same menu without the pause, **F5** = save state, **F8** = load the newest save state,
**F9** = pause / resume, **F10** = one tick while paused. The menu's pages: *Display* (display mode, aspect
ratio, internal scale, render rate, vsync, texture filter, anisotropic filtering, performance overlay --
applied live by the presenter / the GL device and persisted to `<user data>/ps2r.ini`; the internal scale
takes effect at the next start in this title), *Controls* (click a PS2 button, press a key: written to
`<user data>/bindings.ini`, `input.bindings_file` in ps2r.ini, applied to the live pad at once),
*Reproduction saves* (F5 / F8 slots under `<save root>/savestates/slot<save.slot>.ps2st` + `.meta`; the
save root is `paths.save_dir`, else the harness out dir, else `paths.log_dir`).

Save states (`LegSaves`): the section registry carries RAM0, SPAD, GCLK, VU0/VU1 memories, THRD (threads +
parked contexts + shadow call chains), SEMA/EVFL, INTC, DMAC, TIMR, VIFS, VRAM/GSPR/GSST, GIFS, PADS,
VU1R, IOPH (the hosted IOP, below), SDRX/SNDC (SPU2), MCHL (libmc) -- 24 sections, every one required to
have an owner. F8 is the LIVE route: the guest workers unwind at a boundary, every section restores on the
live composition and the threads relaunch from their continuations (run.meta `f8=live,iop=section`).

The hosted IOP's section (IOPH, the engine's `IopHostSection` over `IopHost`'s kernel-facing modules):
IOP RAM, the sysmem heap tail, the kernel clock, the kernel tables (threads with their thbase records --
entry / $gp / heap stack / $a0 / park kind -- semaphores, event flags, message boxes), the timrman hard
timers, the module rows past the 22 stock rows, the hosted modules' libraries (RegisterLibraryEntries
tables), the sifcmd RPC queues + server records, the cdvdman callback, the sifman dma id, the usbd drivers.
A restore resets the live kernel, lands RAM and the records, RE-ARMS every started thread from its record
(a fresh coroutine runs the module's thread entry to its first park on the restored RAM -- the host stack
of the saved coroutine cannot be rebuilt; this is the first title's contract for its STREAM.IRX), hands
the server records back to `IopHost::OnServerRegistered` (one `IopRpcServer` per id, replaced), re-arms the
989snd heartbeat timer, rebases the IOP clock to the image's frame. IOPH is registered BEFORE the SPU
sections so the restored SPU image wins over anything a re-armed entry touched. A client the EE bound at
boot resolves its RPC server through the handle word against the router's live ids (989snd's 0x123456/7
included), since the router's binding table is not an image's.

`--load-state <slot.ps2st>` (`LegSaves::ColdBoot`, the COLD route, the engine's `app/host/cold_restore.h`):
the composition boots and arms everything, then -- before the first guest instruction -- the slot is
preflighted (every required owner's section present, the `.meta` sidecar's composition equal to the live
one; a differing build_id is a warning; the THRD boundary owner), every section restored in registration
order, and the THRD records relaunched (ThreadRelauncher) in place of the ELF entry. In a window the
session's supervisor keeps running while the boundary owner lives; headless, the leg waits for it and runs
the join ladder. The host pacing policy (tick-source mode, pacer release) is the running process's: a slot
taken in a windowed run restores headless. The clock identity (GCLK providers) is the title + the hle.*
composition, not the whole config fingerprint, so the GS queue mode / video settings / harness flags may
differ between the save and the load. run.meta: `restore_route=cold`, `restore_boundary_tid=`,
`restore_iop=rows=:threads=<armed>/<recorded>:servers=:timers=:libs=:frames=`, `relaunch=`; a refusal ends
the leg with `restore_route=cold_refused|cold_failed` + `restore_refusal=`.

Coverage workflow (the agents' checkpoints; game-derived files live OUTSIDE this repo):
    # 1. make a checkpoint: play to the spot in a window, F5 at boundary N (one press; the admission needs
    #    the boundary owner's shadow chain complete -- a saturated chain in the past no longer refuses)
    NATIVEEE_PAD_SCRIPT=... nativeee --harness-window --frames N+1 --harness-out $OUT --iso "$ISO" \
        --set paths.mc_dir=<private card> --set paths.user_data=<private dir> --set harness.inject_key=F5@N
    cp $OUT/savestates/slot0.ps2st  <states>/ep1_<name>.ps2st; cp $OUT/savestates/slot0.ps2st.meta <states>/ep1_<name>.ps2st.meta
    # 2. resume from it headless and keep playing with a pad script (stamps count from the resumed process's boundary 0)
    NATIVEEE_PAD_SCRIPT="5:0:140:128,0,128,128" nativeee --headless --frames 300 --dump-frames $FR --harness-out $OUT2 \
        --iso "$ISO" --set paths.mc_dir=<private card> --set paths.user_data=<private dir> --load-state <states>/ep1_<name>.ps2st
Harness: `--set harness.inject_key=F5@N,F8@M` presses the keys; `--set harness.display_sequence=
<frame>:<mode>` submits a Display row (modes: windowed|fullscreen|borderless|scale1..4|vsync|novsync|
aspect_<auto|4_3|16_9|16_10|21_9>|filter_<title|point|bilinear|trilinear>|aniso<1|2|4|8|16>|overlay|
nooverlay|rate60|ratedisplay). Census rows: `savestate_sections=`, `savestate_capture=`, `f8=`,
`restores=`, `restored_vsync_frame=`, `restore_mhash=`, `restore_sections=`, `boundary_consumer=`,
`settings_persisted=`, `display_sequence=`.

Agent runs: NEVER on the user's card -- every nativeee run passes `--set paths.mc_dir=<private copy>`
(and `--set paths.user_data=<private dir>` when the menu may write ps2r.ini / bindings.ini).

## The navigator: closed-loop pad control (`hle/nav_probe.*`, `hle/pad_script.*`, `verify/nav/`)

Agents reach places in the hub by a closed loop instead of blind stick timings. `NavProbe` reads a few
reverse-engineered EE RAM facts at every pad stamp (the table `verify/nav/sly2_probe.json`: name, pointer
chain, type -- data about the title, no game bytes) and the pad script gains closed-loop steps that steer
the left stick from the camera-relative heading to a target:

```
NATIVEEE_NAV=probe=<json>;trace[=<csv>];every=<n>;ramdump=<dir>@<stamp>+<stamp>...   (hle/nav_probe.h)
NATIVEEE_PAD_SCRIPT="... goto:<from>:<x>,<y>,<z>[:<radius>[:<timeout>]] jump_when_near:<from>:<x>,<y>,<z>:<radius>
                     face:<from>:<yaw_deg> wait_still:<from>[:<timeout>] nav_pause:<from>:<stamps>"      (hle/pad_script.h)
```

The closed-loop steps form a queue in script order (each starts at the later of its `from` stamp and the
previous step's end); a `goto` that makes no progress for 30 stamps unsticks itself (a jump, then escalating
detours along the obstacle). The trace lands in `<harness out>/nav_trace.csv` (`stamp,valid,pos_x,pos_y,pos_z,
yaw,cam_yaw,...`), run.meta gets `nav_probe=` and the per-step outcomes in `pad_script=`; `verify/nav/nav_plot.py`
draws the top-down track, `verify/nav/nav_hunt.py` is the RAM-diff tool that found the facts.

Facts (Paris, verified over seven headless runs; details and evidence in the work notes): the world is
right-handed with z UP (the spawn roof at z ~532, the street at ~0..100); the player's transform blocks sit
at fixed offsets from the level object `[0x2de1a0]` (root bone world position at +0x45d0, the facing matrix
row0 at +0x22a0: yaw = atan2(y, x)); the camera object is static at 0x2ddf80.. (orientation 3x3 at 0x2ddfa0
with row0 = forward, position 0x2ddfd0, target 0x2de1c0, orbit yaw/pitch/distance 0x2de308/0x2de30c/0x2de310).
Stick convention (calibrated): forward = camera yaw, right = yaw - 90, left = yaw + 90 (`"nav"` block of the
table: `forward_offset_deg 0, right_sign -1`). The POST-autosave Paris card resumes at the ledge WITHOUT the
2000-frame Satellite Sabotage briefing (it replays only on the pre-autosave card).

### Episode 1 "The Black Chateau" (Paris) -- completion map

| mission / structure | reached | played | completed | notes |
|---|---|---|---|---|
| Resume Game -> Paris spawn ledge (post-autosave card) | yes, every run (~p850) | -- | n/a | D2: the carousel bar text is empty |
| Paris rooftop hub: ledge, camera, walking / jumping, the drop to the fountain square | yes | ~15 min over the nav runs | n/a | `goto` round trip ledge -> A -> ledge in 30 / 35 stamps; ledge -> fountain square (-760,-2955,95) in 235 stamps with one jump; pad `nav_j1.pad` replays it |
| Satellite Sabotage (Sly; 3 dishes) | the job is ACTIVE on the post-autosave card (the briefing is done); two rotating dishes located in RAM at (-1368,-6723,910) and (4470,-793,878) -- on towers; the street route toward dish A ends at a building wall (-1920,-5730) | -- | no | needs waypoints / climbing (a pipe) the blind navigator does not have yet; the holo Sly-head marker left of the spawn is the safehouse door, not a job beacon |
| Breaking and Entering .. Printing Press Duel (10 jobs) | no | -- | no | after Satellite Sabotage |
| Gadget Grid (SELECT), pause menu, binocucom bar (L1) | yes | -- | -- | D4: L1 + right stick (the binocucom pan) renders the whole world flat MAGENTA (`nav_D4_magenta_L1_pan_sheet.png`); L1 alone or the pan alone are fine; PCSX2 oracle pending |

## STATUS

- `recomp/`: generated at build time (`recomp/INVOCATION.md`, `MANIFEST.sha256`); 6575 functions,
  0 unhandled instructions, no glue TUs needed.
- `iop/`: four modules recompiled at build time (fileio 53 functions, lgAud 140, 989snd 503, spiop 89;
  0 unimplemented instructions), all four boot and stay resident (`iop_hosted=` in run.meta; lgAud
  returns 2 = removable, its "no RPC server" path with `rpc=0`); `iop_imports_unresolved=` empty.
- `hle/boot_leg.cpp` + `hle/leg_*`: the play composition. Boots through the FMV / legal / logo / memory-card screens
  (the libpad SDK-3 padman face, the libmc rows over the engine's host card), the intro cinematic, the
  title screen, New Game, the Binocucom briefings and the first playable area (the Cairo museum): the
  left stick walks Sly and the camera follows (a windowed run with the keyboard 'A' held 2 s changes
  ~50 % of the picture's pixels; idle changes < 2 %). The play arm: the engine's first-run launcher on a
  plain start (`Launcher::Wanted`; `--extract-disc` for the CLI route), the SessionRuntime window on
  main with the live pad, the SDL audio device fed one SPU2 quantum per 60 Hz field (headless legs
  stay muted), the DrawIR queue PIPELINED over a split VRAM image with frame skip (headless legs keep
  the lockstep shared image so their frames lane is unchanged), the clean shutdown (SessionWorkers
  cancel + join ladder; the frames cap, the window close and Escape all end with exit 0 and the census).
  Performance: ~15 fps in gameplay on an M4 (was 7.7 fps lockstep); the bound is the engine's VU1
  interpreter on the EE render thread (~78 % of its time, `vu1_tier=bundles=` ~1 M per frame).
- `hle/video_stack.cpp` + `hle/frame_dump.cpp`: the engine's generic GS renderer (DrawPipeline with no
  class registry over an empty VramLayout, GL 4.1, a hidden window whose presented image feeds the
  SessionRuntime window / `--dump-frames`). The last presented rows (the GS coverage rule) and the sky's
  streak noise (the stored-alpha convention) are fixed in the engine; known picture gaps: strips over
  never-uploaded texture pages, a GL "zero texture" warning once. Audio: the device stream is continuous
  in gameplay (adaptive ring lead; `NATIVEEE_AUDIO_DUMP=<wav>` captures it); `[IOP-RECOMP] Unresolved JALR`
  no longer prints (0 lines over a 3900-frame run into gameplay).
- libpad/libmc/libgraph rows and statics in `title_profile.cpp`: UNKNOWN / not bound (libpad runs raw
  through the engine's SIF rows and padman server).
- `vu1_library`, `renderer_profile`: STATUS-only, no target.
