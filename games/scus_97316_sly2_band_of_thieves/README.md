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
`restore_iop=rows=:threads=<armed>/<recorded>:servers=:timers=:libs=:frames=`, `relaunch=`,
`restore_mc_bound=derived=1|0|no_client_addr`; a refusal ends the leg with `restore_route=cold_refused|cold_failed`
+ `restore_refusal=`. libmc readiness after a cold load: `sceMcInit` never re-runs in the new process, so the
engine's `McCommandModel::Bound` (every `sceMc*` row but `sceMcSync` returns -100 while it is false) is derived
from the restored image -- the bound-server word (+0x24) of libmc's `sceSifClientData` at `TitleProfile::mc.client_addr`
= **0x450880** in this ELF (`hle/sdk_bindings.tsv`: `sceMcInit @0x243cb0 binds client 0x450880 to 0x80000400`).
Before this fact was wired, every cold-loaded process answered the game's first autosave (at the safehouse roof,
~440 stamps after the drop from the high ledge) with "Auto-save on the Memory Card ... has failed / Auto-save has
been disabled" and froze the player under the notice; with it the autosave writes `save1` into the private card and
the game's top bar (the saving notice; its text is D2-empty) stays until a 20-stamp CIRCLE.

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
                     face:<from>:<yaw_deg> wait_still:<from>[:<timeout>] nav_pause:<from>:<stamps>
                     climb:<from>:<x>,<y>,<z>[:<timeout>]  rail:<from>:<x>,<y>,<z>:<ex>,<ey>,<ez>[:<timeout>]
                     until:<from>:<probe><op><value>[:<timeout>]  interact:<from>[:<hold>[:<buttons_hex>]]
                     path:<from>:<name>[:<radius>]  warp:<from>:<x>,<y>,<z>[:<off_hex>]              (hle/pad_script.h)
```

`climb` is Sly's pipe / pole climb (walk to within 70, face it, leap with CIRCLE held through the arc = the
grab, then stick UP until `pos_z` reaches z-15; "climb stalled" when z stops rising -- proven on the lamp post of
the roof south of the spawn: z 787 -> 928 in 40 stamps, and on a street pole at (-5122,-168): -178 -> 535);
`rail` leaps onto a rope / rail the same way and steers along it to the end point; `until` waits for a probe
expression (`pos_z>=900`, `level_time>60`, any name of the probe table; `== / !=` within 0.5); `interact` is a
queued press (CIRCLE by default: the autosave notice, markers, switches); `path` expands a named list of these
tokens from the DATA table `verify/nav/ep1_waypoints.json` (`{"paths": {"<name>": ["goto:0:...", "climb:0:..."]}}`
plus a `landmarks` block of surveyed coordinates); `warp` is TOOLING (never part of a judged route): it writes
the triple as three f32 into the player's authoritative position (the position-only transform block's local
position, level object + 0x2190; `NavProbe::PokeF32`, census `pokes=`) -- Sly appears there next frame and floats
until the first input, then falls onto whatever is below: the terrain survey (landing z per (x,y)) and the
transport shortcut past a traversal the navigator cannot do yet. The probe table also carries the camera's orientation rows
(`cam_fwd_*`, `cam_left_*`, `cam_up_*`; screen right = -left) and the frustum half-tangents (`cam_tan_h` 0.796,
`cam_tan_v` 0.613): pixel (x, y) of a world point = (256 + 256 (d.right/d.fwd)/0.796, 224 - 224 (d.up/d.fwd)/0.613),
the tool that turns a sighting in a dumped frame into a world ray (`verify/nav/nav_plot.py` draws tracks; the ray
scripts live in the work notes).

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

Checkpoints (game-derived, outside the repo, `$WORK/states/`): `ep1_paris_spawn` (frame 881, the high ledge before
the briefing), `ep1_paris_ledge` (frame 1104, the briefing about to start), **`ep1_postcall_ledge`** (frame 3320:
the briefing done, Sly free on the high ledge (-2322,-3495,1268), the job marker active -- the base of every job
run; made by `--load-state ep1_paris_ledge` + `harness.inject_key=F5@2215`). Both pre-autosave cards: the
Satellite Sabotage briefing (2200 frames) plays once at the high ledge; the game AUTOSAVES when Sly first lands on
the safehouse roof below (-1828,-4327,558) and shows the saving notice (CIRCLE 20 stamps dismisses it); the
post-autosave card resumes there (the nav stage's spawn (-1683,-4066,532)).

| mission / structure | reached | played | completed | notes |
|---|---|---|---|---|
| Resume Game -> Paris spawn ledge | yes, every run | -- | n/a | D2: the carousel / notice bar text is empty |
| Paris rooftop hub: ledges, camera, walking / jumping, the drop to the fountain square, the safehouse roof, the south roofs, the street under the arch | yes | ~25 min over the nav + jobs runs | n/a | `goto` reaches the lamp roof (-3945,-1749,782) from the high ledge in 360 stamps (one unstick jump) and the street below the marker in 950; waypoint paths in `verify/nav/ep1_waypoints.json` |
| Satellite Sabotage briefing (binocucom, 2200 frames) | yes | full | yes (the call runs to its end; the marker + "Satellite Sabotage" label then show on screen) | voice-timed, unskippable |
| The game's first autosave (safehouse roof) | yes | -- | yes after the libmc fix (`restore_mc_bound=derived=1`, `save1` written to the private card; `sceMcGetInfo` 54 / `sceMcSync` 57 / `sceMcWrite` 1) | before the fix every cold-loaded process got "Auto-save ... has failed" (the game then disables autosave) |
| Satellite Sabotage (Sly; 3 dishes) | marker FOUND and the job STARTED: the marker is the street plaza (-8189,2413,77) under the arch (the earlier estimate (-5659,42,567) was wrong: two near-collinear rays); the briefing (~1800 stamps) plays; dish 1 = the 1400-high water tower at (-6140,4294): its control panel (-6052,4238,1185) + CIRCLE runs the repositioning sequence (~600 stamps, the player object swapped out, letterboxed dish view) and the counter goes 0/3 -> **1/3** (`fj_dish_counter_0_1_1.png`, `fj_C7_sheet.png`) | job started, dish 1 done; ~6 min | 1 of 3 dishes | reached by `warp` (tooling), not by a legit route (the tower needs a pipe climb from the street); dishes 2 and 3 not located (decoys recorded in `firstjob_notes.md`: the four constantly rotating dish-like objects, the lantern clusters, the 850-high lamp posts). Checkpoints `ep1_job1_started`, `ep1_job1_dish1`. D6 (cold route): the briefing's typewriter text shows one glyph + cursor for the whole call (the live route types every line) |
| Breaking and Entering .. Printing Press Duel (10 jobs) | no | -- | no | after Satellite Sabotage |
| Gadget Grid (SELECT), pause menu, binocucom bar (L1) | yes | -- | -- | D4: L1 + right stick (the binocucom pan) renders the whole world flat MAGENTA (`nav_D4_magenta_L1_pan_sheet.png`); L1 alone or the pan alone are fine; PCSX2 oracle pending |

Open defects seen in the jobs runs (evidence in the work notes `ep1jobs_notes.md`, `firstjob_notes.md`): D1/D3 the
canopy blob / ghosted near wall -- the "VU1 clip semantics" lead is CLOSED without an engine change: at frame 3100 our
TEX 0x3e9000 wall strip is PCSX2's (its 12 recorded vertices are our triangles 2-5 to 0.3 px / 10 z units; our 2
extra triangles lie entirely left of the scissor, which PCSX2's `GSState::VertexKick` drops from its record before
drawing), and the engine's CLIP / FCAND / FCOR / 4-cycle clip-flag model is the same as PCSX2 microVU's
(`microVU_Flags.inl`: instance written at cycles+4, newest instance with time <= cycles read); what remains is the
pervasive half-transparency of ours vs the twin (Sly and the walls show the background through them in
`def_E270_before_after_pcsx2_p3100.png`), a GS/render question (candidates: the full-screen PSMT8H-of-FRAME post
pass that maps the frame's alpha byte through a CLUT, or the stored alpha the world's lerp + additive passes leave
behind); D2 the HUD bar text (the saving notice reads nothing; PCSX2 shows text; the cold route's briefing text stops
after its first glyph = D6, so a text-advance clock stalls on the cold route and the bar scroller may share it); D5 the street under the arch by the marker renders with a
flat blue haze plane over the lower half of the frame (`jobs_L7_street_pair.png`, `jobs_L19_sheet.png`: the
"safehouse haze" class seen from outside); the PCSX2 oracle for the hub after the briefing could not be driven:
the slot-2 twin resumes with "Memory Card ... was removed" (PCSX2 re-inserts the cards after `-state`) and PINE
pad bursts into 0x2dfc00 no longer clear the notice (the IOP refill wins at 25-36 % speed) -- `ep1_pine_pad.py`
needs a hold that survives the refill, or a twin state taken after the notice.

## The episode warp (`hle/warp_tool.*`, `verify/nav/sly2_probe.json` "save") and the episode completion map

Blind scripting completes about one objective per round, so every episode's hub is reached by setting the game's
own save state. The save file (`save1`, 21512 bytes) is a fixed-offset struct the game serialises through a
runtime schema (RAM 0x3e0ea8: 5461 entries of 10 bytes {name id, type, count, offset, first child, next sibling}
+ a 53-bucket name table; the trailer `+0x5400 = 0x5098` layout size, `+0x5404 = 0x2767b4` schema checksum is all
FUN_001d7d30 validates). The fields that select where "Resume Game" lands: `episodeCurrent` (+0, 0 intro .. 8
blimp), `episodeBest` (+4), `widCurrent` (+8: the 44 levels in schema order, the exteriors 0 2 8 12 14 17 27 32
38), `oidPoCurrent` (+0xc: the spawn object id; -1 = the loader's default), `dt` (+0x10, play time; 0 = empty
slot). FUN_00165e38 (the slot-selected handler) reads `episodeCurrent`, indexes the per-episode hub-file table
`[0x3e1c50]` (0x40-byte obfuscated descriptors: sector = (w0 ^ w13) + 0xca0, bytes = w1 ^ w15, id = w2 ^ w14;
intro 0x51e80, nightclub 0x56640, palace 0x68540, temple 0x75dc0, prison 0x7f480, castle 0x890a0, train 0xa3ea0,
sawmill 0xb2560, blimp 0xc5500 -- each a 4.7-6.0k-sector level stream) and posts the loader request
(0x3dd490+0x3bf4, FUN_0020e890) unless the episode's `percent_complete` is 100. Missions are 16-byte
`/m<ep>_<name>/{focus_count, is_complete, time_played, suck_value}` blocks from 0xd9c (78), tasks 20-byte
`/t<ep>_<name>/{focus_count, complete_count, is_complete, state, from_memcard}` blocks from 0x127c (795); all
names and offsets are in the table.

```
NATIVEEE_WARP=<episode>[,<job>]      (hle/warp_tool.h; applied to <paths.mc_dir>/mc0/BASCUS-97316YAOTWTD!/save1 BEFORE the boot)
```
The rewrite sets `episodeCurrent = episodeBest = <episode>`, `widCurrent` = the exterior wid, `oidPoCurrent = -1`,
every earlier episode's `percent_complete = 100`, a non-zero `dt`; the optional `<job>` count marks the first jobs of
the table's per-episode list complete (mission `is_complete` + the `t<ep>_<job>_intro/_complete` tasks state 3) and
`t<ep>_intro` complete. The game then loads the save through its own path: the resume route (START at 500, START at
650, CROSS from 760) lands in the hub at ~boundary 850-950. run.meta row `warp=`. Proof: warp 1 reproduces the Paris
spawn ledge ((-1673,-4079,516); texture Oracle A 361/361 vs the PCSX2 Paris twin); the PCSX2 twin of the warp-2
palace hub was made with the SAME rewritten save on a folder card (`Sly2Warp`) and PINE presses: Oracle A 251/251.
Marking `t2_intro` complete changed nothing visible in the first 1150 frames (the cave shot at p800-850 is the
level's spawn camera, also in PCSX2's twin), so the job parameter is tooling without a judged effect yet.

### All episodes -- completion map (round G; checkpoints `$WORK/states/hub_ep<N>.ps2st`, PNGs `$WORK/warp_hub_ep<N>_p1200.png`)

Every hub: 1700 boundaries headless lockstep (`park=none`, `unhandled_mmio=0`, `syscalls_unknown=0`,
`sdk_rows_trap=0`, `iop_imports_unresolved` empty, 0 `[IOP-RECOMP]` lines, `vu_micro_absent` absent, the boot
constant `guest_call_unknown=13`); the level stream read whole (`sceCdRead` lbn runs below); texture Oracle B PASS on
every distinct decode across the load; the frame judge PASS on p1000..p1700 (no black / flat frame); a window run
(aot + MTVU, shared host at load 8-15) with F5 at 1500 = the cold checkpoint (`--load-state` of ep5 / ep8 verified,
`restore_route=cold`).

| ep | hub | reached | render / HUD | textures (Oracle B / A) | fps p50 hub (window) | missions reached by type | completed | defects seen |
|---|---|---|---|---|---|---|---|---|
| 0 | intro (Cairo museum) 0x51e80 | yes (the museum hall, Sly free, (4931,-5236,568)) | OK, HUD | 6 / -- | 34 | none triggered (the prologue is scripted tasks t0_*) | -- | -- |
| 1 | nightclub (Paris) 0x56640 | yes (spawn ledge) | OK, HUD | 442 / 361 pass | 23 | Sly: Satellite Sabotage (dish 1 of 3 done in round F) | 1/3 dishes | D1 canopy shards, D1b moon, D2 empty bar, D3 ghosted wall, D5 haze |
| 2 | palace (India) 0x68540 | yes (jungle ledge (-4603,2194,451)) | OK, HUD; the "Start locations" carousel bar EMPTY in ours (text in PCSX2) | 337 / 251 pass | 17 | none (no marker located) | -- | D2; purple haze plane over the lower frame (D5 class) |
| 3 | temple (India) 0x75dc0 | yes (waterfall terraces (-3135,-5595,1937)) | OK, HUD | 353 / -- | 15 | none | -- | the spawn-in cutscene (p850) shows the Guru / the gang at the table with the bar text empty (D2) |
| 4 | prison (Prague) 0x7f480 | yes; the save's `oidPoCurrent=-1` spawn is the SAFEHOUSE: "Connecting to ThiefNet..." then the Buy Gadgets shop (the resume CROSS taps browse it, "not enough coins"), RIGHT+CROSS leaves the safehouse for the prison hub (as Bentley) (Bentley at the window, the green Bentley marker) | OK; the shop menu renders every glyph | 376 / -- | 23 | ThiefNet shop (menu) reached; a Bentley marker visible | -- | 7-sector extra read 0x83da0 (the shop's data) |
| 5 | castle (Prague) 0x890a0 | yes (Sly on a roof (11543,2313,774), blood moon) | HUD; **large white / rainbow sheets** across the view (the D1 shard class at its worst: whole roofs render as flat white quads) | 381 / -- | 22 | none | -- | D7 (new): the castle hub's white sheets |
| 6 | train (Canada lumber camp) 0xa3ea0 | yes ((-1032,-11043,0), ground level) | HUD; the whole frame under a cyan haze (D5 class, strongest here) | 437 / -- | 17 | none | -- | D5 cyan haze |
| 7 | sawmill (Canada) 0xb2560 | yes; the hub immediately starts a binocucom call (Bentley / Sly portraits over the snowy camp view, "01") | OK (binocucom frame: no HUD badge by design) | 1036 / -- | 17 | job intro call auto-triggered (a Sly job) | -- | -- |
| 8 | blimp (Arctic) 0xc5500 | yes (the blimp deck (11457,170,910), purple sky) | HUD; Sly ghosted (D3) | 327 / -- | 25 | none | -- | D3 |

Missions by type (Bentley hacking, Murray, Carmelita turret, minigames): NOT reached in this round -- the hubs' job
markers are not located yet (the navigator needs per-hub coordinates; the first marker is found by the firstjob
stage's method: off-axis warps + icon rays). The hub audio twin (PCSX2 SDL disk capture of the warp-2 route,
`$WORK/wp_pcsx2/ep2.raw`, hub music from ~85 s) judged against our mixer dump: FAIL / inconclusive like Paris
(ours +4.1 dB, envelope corr 0.15, centroid ratio 2.4 -- loop phase and state differ; the audio stage's open item).

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
