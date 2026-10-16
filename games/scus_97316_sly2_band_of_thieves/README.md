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

`hle/boot_leg.cpp` (the composition) also: opens the disc image (`--iso`, else
`<game_files>/<disc_image_file>`) for the libcdvd rows; hosts the IOP (`hle/iop_host.cpp`, see below);
and answers the kernel's transfer id for `sceSifSetDma` lists without the command attribute (the engine
keeps its first title's rule there -- an upstream question, see the work notes).

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

## STATUS

- `recomp/`: generated at build time (`recomp/INVOCATION.md`, `MANIFEST.sha256`); 6574 functions,
  0 unhandled instructions, no glue TUs needed.
- `iop/`: four modules recompiled at build time (fileio 53 functions, lgAud 140, 989snd 503, spiop 89;
  0 unimplemented instructions), all four boot and stay resident (`iop_hosted=` in run.meta; lgAud
  returns 2 = removable, its "no RPC server" path with `rpc=0`); `iop_imports_unresolved=` empty.
- `hle/boot_leg.cpp`: the headless cold-boot leg. Current park (60 s watchdog):
  `park=watchdog park_pc=0x80075000 watchdog_ra=0x160ec0` -- the main thread in the game's display
  handshake `FUN_00160e68`, spinning on the render worker's idle flag `[0x2deb20]` after
  `WaitSema([0x2deb1c])`x2 / `SignalSema`x2; the worker `FUN_00160b70` sets it at its loop top after
  its vsync wait (`0x16f590` -> the `0x16f560` boundary site). Passed before it: the 989snd RPC
  (`0x123456` answered twice: 36 bytes in, 24 out -- the previous park), `sceFsInit`'s fileio init and
  `rom0:ROMVER` open against the hosted FILEIO (1 SIF command delivered), `scePadInit`, one frame
  boundary. The frontier is the EE side now: a busy spin that expects an interrupt-driven higher-
  priority thread to run (the console's VBlank) while no boundary is reached -- an EE scheduling /
  boundary-ownership mechanism, not an IOP one.
- `hle/video_stack.cpp` + `hle/frame_dump.cpp` + the play arm in `boot_leg.cpp`: the engine's generic GS
  renderer (DrawPipeline with no class registry over an empty VramLayout, GL 4.1, a hidden window whose
  presented image feeds the SessionRuntime window / `--dump-frames`), the DrawIR thread queue in lockstep,
  the pad stamped every boundary (live PlatformInput in the window, neutral headless). Picture status: the
  intro FMV (IPU BDEC/CSC -> 640x480 RGB32 texture -> a sprite into the 512x224 field buffer) is correct
  and moving; the post-FMV text screen draws with black vertex colours and zero STQ on its VU1-output
  strips (glyph fragments only) -- the next target (the VU1 microprogram output / its interpreter).
- libpad/libmc/libgraph rows and statics in `title_profile.cpp`: UNKNOWN / not bound (libpad runs raw
  through the engine's SIF rows and padman server).
- `vu1_library`, `renderer_profile`: STATUS-only, no target.
