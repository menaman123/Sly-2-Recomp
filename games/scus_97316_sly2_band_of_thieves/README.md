# Sly 2: Band of Thieves (NTSC-U) title module

This directory follows the engine's title contract (`engine/games/README.md`). A directory with
`hle/CMakeLists.txt` is a title, and `ps2r_title_hle` defines `ActiveTitleProfile()`. `recomp/`, `iop/`,
`vu1_library/` and `renderer_profile/` are optional targets that consumers gate on with
`if(TARGET ps2r_title_*)`. This title owns `iop/CMakeLists.txt` because it recompiles several IOP modules.

## Disc and ELF facts

- Disc: `SYSTEM.CNF` `BOOT2=cdrom0:\SCUS_973.16;1` VER=1.00 VMODE=NTSC. The volume root holds only
  `SYSTEM.CNF`, `SCUS_973.16` (2,597,048 bytes, sha256 `a511a94b...6a73`), `IOPRP300.IMG` (the stock SDK 3.0
  IOP reboot image) and `Z6TAIL.DUP` (padding). The game streams its data from raw sectors.
- ELF: entry `0x100008`. PT_LOAD #0 is at `0x100000` (filesz `0x1dd230`, memsz `0x352200`). PT_LOAD #1 is the
  `.irx` section `0x472200..0x4e5e40` with nine embedded IOP modules (sio2man, padman, mcman, mcserv, libsd,
  usbd, lgAud, 989snd, spiop), loaded with `sceSifLoadModuleBuffer` after
  `sceSifRebootIop("cdrom0:\IOPRP300.IMG;1")`.
- `TITLE_INPUTS.sha256` pins the ELF, the three embedded modules the engine has no native owner for
  (`SCUS_973.16!lgAud`, `SCUS_973.16!989snd_Library`, `SCUS_973.16!spiop`) and the reboot image's fileio
  (`IOPRP300.IMG!FILEIO_service`). Each is extracted at build time by `tools/elf_embedded` and recompiled
  (`iop/`).
- crt0: `_gp 0x2e5270`, `.stack 0x452200..0x472200`, initial `$sp` `0x472200`, argv block `0x2dd380`,
  `main 0x1a7518`, `exit 0x233828`.
- SDK: every `PsII<lib>` stamp reads 3000. Each row address in `hle/sdk_bindings.tsv` was confirmed by
  disassembly, and the comment on the row records the evidence.

The recompiled image is the whole EE code of the title. The only code the game installs at run time is the
SDK's three libkernl kernel patches (copied to `0x74000..0x77000` by `_InitSys`), which run only through the
HLE'd syscall table. `NATIVEEE_WATCH=text` (`hle/text_watch.h`) checks this at every frame boundary.

## What is HLE'd

Nothing per-title. `hle/sdk_bindings.tsv` binds the sce-library rows (libkernl SIF / RPC / IOP heap, libcdvd,
libmc) to the engine's generic bodies; everything else runs as recompiled MIPS. The frame boundary is the
game's own vsync wait (`0x16f560`, `TitleProfile::Gs::vblank_wait_entry`): this ELF links no `sceGsSyncV`, so
the composition installs the engine's `WaitForVblank` there.

## The play composition (`hle/`)

`hle/boot_leg.cpp` defines `TitlePlayMain` (the launcher hand-over) and `RunLeg`, which constructs the leg's
phases in order. Each phase is one class in a `leg_*.{h,cpp}` pair:

| Class | Owns |
|---|---|
| `LegCore` | memory, MMIO sink, time domain, INTC and timers, SIF registers, the frame boundary, kernel seams |
| `LegGs` | GsState, the video stack, the DrawIR queue and consumer chain, frame dumps |
| `LegDevices` | GS MMIO, GIF, VU0 / VU1 and the tier choice, VIF, DMAC |
| `LegMtvu` | the MTVU worker, the VU1 AOT cache and code generator |
| `LegDmaIpu` | VIF / VU / DMAC / IPU register windows, the IPU channels, COP2 |
| `LegInterrupts` | the GS interrupt observer, the DMAC handler install, syscall-entry ISRs |
| `LegDisc` | IsoFs and ReadEngine over the disc image |
| `LegServices` | RpcRouter, the mcserv / padman / sdrdrv faces, SpuCore, the mixer |
| `LegAudio`, `LegPlay` | the SDL audio device; the live pad, the session window and the presented image |
| `LegIop`, `LegPad` | the hosted IOP and SIF wiring; the pad stamp with the script / recorder overlays |
| `LegImage` | the ELF image and BootEnv, the generated dispatch, the SDK manifest |
| `LegSaves` | the save-state sections, F5 / F8, `--load-state` |
| `LegPark`, `LegParkSink`, `LegCensus` | shutdown counters and hooks; the `run.meta` rows |
| `LegVerifySlot`, `LegWatchdog` | harness output, frame cap, the join ladder; the no-boundary watchdog |

The engine's LintOneClass, LintSizeBudget and LintHeaderGlobals rules apply to this tree. Every `NATIVEEE_*`
environment variable is read once, in `hle/leg_knobs.cpp`; `hle/leg_knobs.h` documents each one.

## The IOP (`iop/`, `hle/iop_host.cpp`)

The title's own IOP modules run recompiled. `iop/CMakeLists.txt` recompiles each extracted image with the IOP
recompiler (`--base <load address> --namespace iop_<id> --import-call`; bases in `iop/iop_modules.h`, output
pinned by `iop/<id>/MANIFEST.sha256`) into `ps2r_title_iop_<id>`, aggregated as `ps2r_title_iop`.

`IopHost` arms the engine's cooperative IOP kernel with the stock modules the engine serves (sysmem, intrman,
ioman, loadcore, thbase, sifman, sifcmd, libsd over SpuCore, cdvdman over the disc, ...). Each
`sceSifLoadModuleBuffer` boots the recompiled module whose name matches, after checking the pushed image
against the build's SHA-256. Every `sceSifRegisterRpc` a hosted module makes becomes an `IopRpcServer` on the
EE's RpcRouter, so 989snd and fileio answer the EE with their own handlers. An import no library serves parks
the run (`iop_import_unresolved`). FILEIO is booted from the reboot image on the disc.

`PS2R_IOP_ORACLE` stays 0: `ps2r_title_play` links `ps2r_title_iop` directly and never includes
`app/host/iop_arm.h`.

## Controls, menu and save states

Keyboard and controller defaults are in the repository README. Host keys: **Escape** opens the pause menu,
**F1** opens it without pausing, **F5** saves a state, **F8** loads the newest one, **F9** pauses / resumes,
**F10** steps one tick while paused. The menu's Display page (display mode, aspect, internal scale, frame cap,
vsync, filtering, overlay) and Controls page persist to `<user data>/ps2r.ini` and `bindings.ini`.

`video.frame_cap` (`30 | 60 | off`, default 60) chooses which 60 Hz fields show a new picture; the game's logic
always ticks at 60 Hz. When the game produces fewer than 60 pictures per second, `30` gives a steadier cadence
and `60` lower latency. A run with `--harness-out` writes `present_times.csv`; `verify/judge_smooth.py hist`
reads it.

Save states carry 24 sections (RAM, scratchpad, clocks, VU memories, threads, kernel objects, INTC, DMAC,
timers, VIF, VRAM and GS state, GIF, pad, the hosted IOP, SPU2, libmc), each with a required owner. F8 restores
on the live composition. `--load-state <slot.ps2st>` restores before the first guest instruction after
checking that every section is present and the composition matches. Because `sceMcInit` does not re-run after
a cold load, libmc's bound state is derived from the restored client word at `TitleProfile::mc.client_addr`
(`0x450880`).

## Tooling

All of these are environment-gated and off by default:

- **Navigator** (`hle/nav_probe.*`, `verify/nav/`): `NATIVEEE_NAV=probe=<json>;trace` reads player and camera
  facts from EE RAM (the table `verify/nav/sly2_probe.json`), and `NATIVEEE_PAD_SCRIPT` gains closed-loop steps
  (`goto`, `face`, `climb`, `rail`, `until`, `interact`, `path`, `warp`; grammar in `hle/pad_script.h`).
  Named waypoint paths live in `verify/nav/ep1_waypoints.json`.
- **Episode warp** (`hle/warp_tool.h`): `NATIVEEE_WARP=<episode>[,<job>]` rewrites the save on the configured
  memory card before boot so "Resume Game" loads that episode's hub. The save layout is described in the
  probe table's `save` block.
- **Shadow counter split** (`hle/shadow_counter_split.*`): Sly 2 counts shadow-volume faces in the frame
  buffer's green byte. The sink splits each counter draw into subtract and add batches so overlapping faces
  sum exactly. `NATIVEEE_SHADOW_SPLIT=0` disables it.
- **GS capture**: `NATIVEEE_GS_CAPTURE=<file.gsstream>` records the GS consumer's input for
  `tools/gs_replay stream`; `verify/judge_render_throughput.py` judges the replay.
- **Texture dump**: `NATIVEEE_TEX_DUMP=<from>,<to>` dumps every decoded texture for
  `verify/judge_textures.py`, which compares against the engine's reference decoder and, optionally, PCSX2
  texture dumps.

`verify/` holds the judges for harness output (textures, audio, pacing, smoothness, shadows, solidity,
pictures, the scene matrix). Their selftests are ctest rows (`Sly2_*`); rows that need captured game output
read it from `$NATIVEEE_PRIVATE_FIXTURES` and skip without it. Captures, save states and dumps are
game-derived and never committed.

## Status

- `recomp/`: generated at build time (`recomp/INVOCATION.md`, `MANIFEST.sha256`); 6575 functions, 0 unhandled
  instructions.
- `iop/`: fileio (53 functions), lgAud (140), 989snd (503) and spiop (89) are recompiled at build time; all four
  boot and stay resident.
- Every episode hub (0-8) loads and renders. Known picture defects include haze planes over some hubs, white
  sheets in the castle hub, a ghosted Sly in places, and empty text in the HUD bar.
- libpad, libmc and libgraph statics in `title_profile.cpp` are unbound; libpad runs raw through the engine's
  SIF rows and padman server.
- `vu1_library/`, `renderer_profile/`: placeholders with no target.
