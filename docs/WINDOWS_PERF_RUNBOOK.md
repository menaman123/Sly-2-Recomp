# WINDOWS_PERF_RUNBOOK -- measuring the Windows build (x86-64) against the macOS GL numbers

Status: the Windows tree is cross-built and LINKED on the Mac (engine commit `adf5f63` on
`fix/windows` + games `4cd8ba5`); nothing of it has ever RUN. Everything below is what a Windows
host does the first time, in order, and exactly what to send back. One Windows 10/11 x64 PC with a
discrete GPU (OpenGL 4.1+ driver) and ~15 GB free disk is enough. No Visual Studio needed for
route A.

## 1. Receiving the binaries -- route A (cross-built here, the default) or route B (built there)

### Route A: the kit from the Mac (`$WORK/wt/windows/winkit/`, `make_winkit.sh`)

```
nativeee.exe            36.6 MB   PE32+ console; imports SDL2.dll + the VC++ 2015-2022 runtime only
gs_replay.exe            0.4 MB   the GS capture replayer (the identity + GL-thread perf tool)
nativeee.pdb                      crash backtrace symbols (host_windows.cpp / DbgHelp), if present
SDL2.dll                 1.6 MB   SDL 2.30.11 (the official SDL2-devel-2.30.11-VC package)
title\hle\sdk_bindings.tsv        the title tables the exe reads at start (no game bytes); the exe
                                  looks for title\ BESIDE itself when its build-dir path is absent
states\hub_ep1_fixed.ps2st  42 MB the Paris hub savestate every measurement starts from
pads\sm_paris.pad                 the input script (NATIVEEE_PAD_SCRIPT)
mac_reference\*.csv               the Mac GL gs_replay CSVs of the three captures (section 4)
WINDOWS_PERF_RUNBOOK.md, BUILD-ID.mac.txt, ENGINE-COMMIT.txt, SHA256SUMS.txt
```
Zip the folder (`zip -r winkit.zip winkit`) and transfer. On the PC: unzip to `C:\sly2\winkit\`.
Prerequisite on the PC: the Microsoft Visual C++ 2015-2022 x64 redistributable (MSVCP140.dll /
VCRUNTIME140_1.dll; `vc_redist.x64.exe` from Microsoft). Check: `nativeee.exe --version` prints
the build id (the same string as `BUILD-ID.mac.txt` modulo the date); `gs_replay.exe` with no
arguments prints its usage. Verify `SHA256SUMS.txt` with `certutil -hashfile <file> SHA256`.

NOT in the kit (sizes): the three GS captures `paris_600_1800.gsstream` (4.68 GB),
`cairo_450_600.gsstream` (723 MB), `castle_1000_1150.gsstream` (302 MB) from
`$WORK/gs_cap_paris/` -- copy them on a USB drive / network share to `C:\sly2\caps\`; and the
user's own disc image (`Sly 2 - Band of Thieves.iso`, SCUS-97316) to `C:\sly2\disc\`.

### Route B: build on Windows (the way CI does it; needs ~1 h on a cold vcpkg cache)
Prerequisites: Git, CMake >= 3.21, Ninja, Python 3, LLVM (clang-cl; the VS 2022 "C++ Clang tools"
component or the LLVM installer), a vcpkg checkout with `VCPKG_ROOT` set, the SCUS-97316 disc
image. Then in a Developer PowerShell:
```
git clone <repo> sly2recomp; cd sly2recomp; git submodule update --init
cd engine; git checkout fix/windows          # or sly2-upstream once merged
.\build.ps1 -Disc "C:\sly2\disc\Sly 2 - Band of Thieves.iso" -Preset windows-release
```
`build.ps1` configures the `windows-release` preset (clang-cl + tools\build\clang_cl_shim.py,
SDL2 + FFmpeg from vcpkg.json), builds, runs the no-disc ctest and stages
`dist\windows-x86_64\` (nativeee.exe + every DLL + title\ + BUILD-ID). `gs_replay.exe` is
`build-windows-release\tools\gs_replay\gs_replay.exe`. The recompiled code is regenerated from
the disc at build time (recomp\MANIFEST.sha256 must match -- it does on the Mac; a mismatch on
Windows is a finding, send `build-windows-release\games\...\recomp\manifest.verified`'s log).

## 2. Layout on the PC
```
C:\sly2\winkit\            the kit (or dist\windows-x86_64\ + gs_replay.exe for route B)
C:\sly2\disc\Sly 2 - Band of Thieves.iso
C:\sly2\caps\paris_600_1800.gsstream, castle_1000_1150.gsstream, cairo_450_600.gsstream
C:\sly2\out\               every output below lands here (send this folder back, zipped)
C:\sly2\ud\                user data (settings.ini, logs\, savestates) -- created by the exe
C:\sly2\mc\                memory-card images -- created by the exe
```
Forward slashes are accepted everywhere; quote paths with spaces. Run everything from a
PowerShell in `C:\sly2\winkit\` (SDL2.dll is found beside the exe).

Before the first run: the GPU vendor control panel's "preferred graphics processor" = the
discrete GPU for nativeee.exe / gs_replay.exe (the exes also export NvOptimusEnablement /
AmdPowerXpressRequestHighPerformance, which usually suffices); Windows "Game Mode" off;
Power plan = High performance; no overlay (GeForce Experience / Xbox Game Bar) on the
window. Note the GPU, driver version, CPU, RAM, Windows build, monitor refresh (60 Hz is what
the Mac numbers assume) in `C:\sly2\out\HOST.txt` (`dxdiag /t C:\sly2\out\dxdiag.txt` gives all
of it).

## 3. gs_replay on the three captures (identity first, then GL-thread perf)

Exactly the Mac commands (`$WORK/wt/windows/replay/`), two passes each; `--scale 1` is the
identity run (the row hashes are host-independent by contract: same bytes -> same hash), `--scale 2`
is the perf run at the window's native resolution. Expected first-run outcome for identity: every
`hash` column row equal to the Mac CSV's and the final `STREAM_HASH` line equal:
`81307003bb7c5595` (paris), `823c19fd2f7b7dc8` (castle), `70ec8ff50702399d` (cairo).

```
cd C:\sly2\winkit
.\gs_replay.exe stream C:\sly2\caps\paris_600_1800.gsstream   --scale 1 --warmup 30 --quiet --csv C:\sly2\out\paris_s1.csv  > C:\sly2\out\paris_s1.log 2>&1
.\gs_replay.exe stream C:\sly2\caps\castle_1000_1150.gsstream --scale 1 --warmup 30 --quiet --csv C:\sly2\out\castle_s1.csv > C:\sly2\out\castle_s1.log 2>&1
.\gs_replay.exe stream C:\sly2\caps\cairo_450_600.gsstream    --scale 1 --warmup 30 --quiet --csv C:\sly2\out\cairo_s1.csv  > C:\sly2\out\cairo_s1.log 2>&1
.\gs_replay.exe stream C:\sly2\caps\paris_600_1800.gsstream   --scale 2 --warmup 30 --quiet --csv C:\sly2\out\paris_s2.csv  > C:\sly2\out\paris_s2.log 2>&1
.\gs_replay.exe stream C:\sly2\caps\paris_600_1800.gsstream   --scale 2 --warmup 30 --quiet --csv C:\sly2\out\paris_s2b.csv > C:\sly2\out\paris_s2b.log 2>&1
.\gs_replay.exe stream C:\sly2\caps\paris_600_1800.gsstream   --scale 1 --warmup 30 --quiet --deferred-grab --csv C:\sly2\out\paris_s1_deferred.csv > C:\sly2\out\paris_s1_deferred.log 2>&1
```
Exit code 77 = "no GL device" (the driver gave no 4.1 core context: send the .log; it names the
GL_VERSION / GL_RENDERER strings or the SDL error). The CSV columns are
`frame,ms,draws,jobs,hash,grabbed,readbacks,copies,draw_calls,submits,gpu_ms,phash`; `ms` is the
GL consumer thread's time per replayed frame -- the number that is 12-14 ms on the M4 for Paris
at scale 2 (the thing round 3 is about). Compare on the Mac side with
`python3 -P $WORK/wt/windows/win_compare.py replay paris_s1.csv $WORK/wt/windows/replay/paris_600_1800_ours.csv` (row-hash identity + ms p50/p95 of both).
Also dump three frames for a look: add `--dump-ppm C:\sly2\out\ppm --dump-frames 100-102` to one
scale-1 Paris run (byte-compare g100.ppm with the Mac's `wt/perf2/perf2_sync_grab_paris_g100.png`
after conversion, or just send them).

## 4. One windowed session with --harness-out (the pacing numbers)

The same lane as `$WORK/pipe_run.sh ... window` on the Mac: Paris hub from the savestate, the
input script, 600 then 2400 frames, pipelined (gs.queue default), MTVU on, VU1 AOT native tier
OFF on Windows (`engine.vu1_aot_compiler=none` -- the image libraries cannot resolve the
executable's symbols on Windows yet, see section 6; the specialised VU1 tier runs instead, so the
EE/VU side is slower than the Mac's by design and the gs/present side is what to compare).

```
cd C:\sly2\winkit
$env:NATIVEEE_WATCHDOG_S = "150"; $env:NATIVEEE_MTVU = "1"; $env:NATIVEEE_VU1_MODE = "aot"
$env:NATIVEEE_PAD_SCRIPT = "C:\sly2\winkit\pads\sm_paris.pad"
$env:NATIVEEE_DUMP_EVERY = "100"; $env:NATIVEEE_DUMP_FROM = "0"
# identity lane (headless, unpaced): gs_draws / restore mhash must equal the Mac's
$env:NATIVEEE_UNPACED = "1"
.\nativeee.exe --headless --frames 600 --harness-out C:\sly2\out\hl600 --iso "C:\sly2\disc\Sly 2 - Band of Thieves.iso" `
  --set paths.mc_dir=C:\sly2\mc --set paths.user_data=C:\sly2\ud --set engine.vu1_aot_compiler=none `
  --load-state C:\sly2\winkit\states\hub_ep1_fixed.ps2st > C:\sly2\out\hl600.log 2>&1
# the windowed, paced sessions (a window opens; do not touch it): 600 frames twice, then 2400 once
Remove-Item Env:NATIVEEE_UNPACED
.\nativeee.exe --harness-window --frames 600 --harness-out C:\sly2\out\win600a --iso "C:\sly2\disc\Sly 2 - Band of Thieves.iso" `
  --set paths.mc_dir=C:\sly2\mc --set paths.user_data=C:\sly2\ud --set engine.vu1_aot_compiler=none `
  --load-state C:\sly2\winkit\states\hub_ep1_fixed.ps2st > C:\sly2\out\win600a.log 2>&1
.\nativeee.exe --harness-window --frames 600 --harness-out C:\sly2\out\win600b ... (same) > C:\sly2\out\win600b.log 2>&1
.\nativeee.exe --harness-window --frames 2400 --harness-out C:\sly2\out\win2400 --dump-frames C:\sly2\out\win2400_frames ... (same) > C:\sly2\out\win2400.log 2>&1
```
Expected for the headless identity lane (the Mac, both binaries, 2026-10-22): `gs_draws=1963834`,
`restore_mhash=0xbfb9ce3a713d1e97`, `boundaries` reaching 600, `frames=0` (the headless lane
writes no frames.fhash on this tree -- a known property, not a Windows finding). The savestate is
host-independent (guest memory + engine state); a refusal at `--load-state` is a finding (send
the log's `restore_route` / `restores` lines).

## 5. What to send back (zip `C:\sly2\out\`)

| File | Mac counterpart | What it answers |
|---|---|---|
| `*_s1.csv` (3 captures) | `$WORK/wt/windows/replay/*_ours.csv` | IDENTITY: `hash` per row + STREAM_HASH equal = the Windows GL path renders the same bytes |
| `paris_s2.csv`, `paris_s2b.csv` | Mac scale-2 Paris (perf2 track) | GL-thread ms/frame p50/p95 (the `ms` column) -- the cross-platform perf number |
| `paris_s1_deferred.csv` | perf2's deferred-grab run | the deferred grab's gain on this GPU |
| `*.log` of gs_replay | | GL_VERSION / GL_RENDERER / the device line; exit 77 reasons |
| `hl600\run.meta` | `$WORK/pipe_win_base_hl/run.meta` | gs_draws, restore_mhash, boundaries, vu1_tier, clock rows |
| `win600a\`, `win600b\`, `win2400\`: `run.meta`, `present_times.csv`, `frames.fhash` | `$WORK/pipe_<name>/` of the Mac window runs | pictures/s, frame_hz, the shown-gap histogram (doubled frames), the presenter's timing; frames.fhash rows = windowed identity vs the Mac's (same GL path, so expected equal where the Mac's own runs are equal) |
| `win2400_frames\*.ppm` | the Mac's dumps | a look at the pictures (every 100th frame) |
| `C:\sly2\ud\logs\*` (ring logs, `crash_report.txt`, `ring_crash.rlog` if any) | | a crash: the SEH handler's backtrace (needs nativeee.pdb beside the exe) |
| `HOST.txt` / `dxdiag.txt` | | GPU, driver, CPU, refresh rate |

Reading `present_times.csv` here: `python3 -P $WORK/wt/windows/win_compare.py present <present_times.csv>` (the shown-gap
histogram; 16.7 vs 33 ms buckets). The Mac reference: Paris windowed pictures/s p50 54, frame_hz
52-54, logic 60, 16 % doubled frames; GL consumer thread 12-14 ms/frame.

## 6. Known Windows gaps the first run will show (not failures of the run)

1. VU1 AOT native tier OFF (`engine.vu1_aot_compiler=none`): the generated image libraries
   import 9 engine symbols (`FlagPipeline::{PushP,ReleasePEarly,VisibleP,MaterializeMacStatus,
   SetClipImmediate,LazyRingOverflow}`, `XgKick::{Arm,Pace}`, `VuFloat::kLaneMask`) by dynamic
   lookup against the executable; a Windows DLL needs the exe to export them + an import library
   at image-compile time (and a host compiler on the PC). Design item for the AOT owner; the
   specialised tier runs meanwhile (identity unaffected, VU1 worker slower).
2. Timer granularity: the exe asks for 1 ms (timeBeginPeriod + no background throttling); the
   pacer's sub-ms sleeps are the pacing track's `PreciseSleepUntilNs`
   (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION on Win32) -- until it lands, expect the tick/pacer
   waits to quantise at 1 ms, visible as a wider shown-gap histogram than the Mac's.
3. Windowed vsync under DWM: `SDL_GL_SetSwapInterval(1)` blocks in the compositor; borderless
   fullscreen (`--set video.display_mode=2` if the setting exists in settings.ini) is the
   comparable mode. Report both if time allows.
4. The crash handler (SEH) and `host::MakeTempDirectory`, `GetThreadTimes` census, the GL loader
   itself: first execution ever. A `[gl] unresolved GL entry points: ...` line at start names the
   entry points the driver lacks -- send it verbatim.
