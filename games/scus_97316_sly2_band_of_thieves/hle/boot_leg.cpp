// games/scus_97316_sly2_band_of_thieves/hle/boot_leg.cpp -- the title's play composition
// (TitlePlayMain). It arms the engine's title-neutral mechanisms (memory + MMIO sink, INTC + timers,
// the SIF register block, the emulated time domain and boundary owner, the kernel syscalls and the
// SDK binding manifest), hosts the title's three embedded IOP modules on the engine's cooperative IOP
// kernel (iop_host.cpp: lgAud, 989snd, spiop recompiled at build time; the stock Sony modules are the
// engine's HLE), the hardware fallbacks (GS / GIF / VU / VIF / DMAC / IPU), the memory card and pad
// faces, loads the ELF image, installs the generated dispatch table and runs the recompiled code from
// the ELF entry -- or, with --load-state <slot.ps2st>, restores every section of that slot (the hosted
// IOP included) and relaunches its threads in place of the entry (LegSaves::ColdBoot). Three shapes of
// one leg:
//   * the headless harness leg (--headless): a counting GS sink, no GL, the neutral pad, muted; the
//     park / frames-cap census in run.meta (LegCloser) + a `census:` block on stdout;
//   * the headless dump (--headless --dump-frames <dir>): the generic GS renderer over GL into a
//     hidden window, the DrawIR queue in LOCKSTEP over the shared VRAM image (the frames lane is the
//     inline path's by construction), every present as f<key>.ppm / p<key>.ppm;
//   * play (a window): the engine's first-run launcher on a plain start (the disc image verified
//     against the TitleProfile's ELF SHA-256, extracted under the user data root), the SessionRuntime
//     window on main with the live keyboard / gamepad pad, the SDL audio device fed one SPU2 quantum
//     per 60 Hz field, the DrawIR queue PIPELINED over a SPLIT VRAM image with frame skip (the
//     renderer never blocks the game), and the clean shutdown: the window close / Escape / the
//     --frames cap cancel the guest workers (ThreadExitUnwind), the join ladder runs, the census is
//     written, RunLeg returns through its destructors.
// No per-title HLE: hle/sdk_bindings.tsv is the only title data consulted at run time (iop/iop_modules.h
// is layout data); the disc image (--iso, else <game_files>/<disc_image_file>) feeds the engine's
// libcdvd rows (Sly 2 reads raw sectors with sceCdRead). The env knobs (NATIVEEE_*) are boot triage,
// read once by LegKnobs::FromEnvironment (hle/leg_knobs.h, the title's one getenv site).
//
// Shape: the first title's EeBootstrap cold-boot composition (private app/host/ee_bootstrap*.cpp),
// reduced to its title-neutral phases, in the same construction order so object lifetimes nest the
// same way. The leg is a chain of phase objects on RunLeg's frame, one class per hle/leg_*.{h,cpp}
// pair, constructed in the first title's phase order and destroyed in reverse:
//   LegCore        memory + MMIO sink, NATIVEEE_WATCH, time domain, INTC + timers, SIF registers, the
//                  tick source / rendezvous / running flag, the boundary (sequencer, owner, protocol),
//                  the kernel seams
//   LegGs          GsState, the video stack, the DrawIR queue / consumer chain, VertTraceSink, the
//                  counting sink, the frame dump + dump policy, the slot-0 PresentRendezvous step
//   LegDevices     GS MMIO, GIF regs / parser / arbiter (+ GifTraceSink), VU0 / VU1, Vu0Registry, VIF,
//                  the VU1 tier choice, the DMAC
//   LegMtvu        the MTVU worker stack, the VU1 AOT cache + code generator on the live unit
//   LegDmaIpu      the VIF / VU memory / DMAC / IPU register windows, the DMAC guest call + INT1 + mask
//                  sink, the IPU + FIFO port + its DMAC channel wiring (+ dumps), the COP2 bridge
//   LegInterrupts  GsSync::BindDmac, the DMAC handler install, the GS interrupt observer, the
//                  syscall-entry pre-dispatch
//   LegVifIrq      the VIFcode i-bit interrupt stall (the worker's park + the EE-side service; the
//                  inline walk's in-place handler) -- the title's VU1-lit vertex colours ride it
//   LegDisc        IsoFs / ReadEngine / StreamReader, the SifDma id hook
//   LegServices    RpcRouter, the memory card store + model + MCSERV face, PadProtocol + the padman
//                  face, SpuCore, Mixer, SdBlockTrans + the SDRDRV face
//   LegAudio       the SDL audio device + pull ring (+ AudioDump, + the SPU VoiceTrace knob)
//   LegPlay        the live pad + the SessionRuntime window, the presented-image output, the
//                  player's menu pages (Display / Controls / the pause menu)
//   LegIop         IopHost (+ its IOPH savestate section), the SIF DMA copy, the padman facts, the
//                  IopDispatch / EeStreamRpc steps, the SIF command path, the back-edge yield
//   LegSaves       the save states: the section registry (RAM / SPAD / GCLK / VU / THRD / kernel /
//                  INTC / DMAC / TIMR / VIFS / GS / GIFS / PADS / VU1R / IOPH / SPU2 / MCHL), the F5
//                  capture, the F8 live restore (bound into the session's hooks by LegVerifySlot), the
//                  --load-state cold route (ColdBoot: restore every section + relaunch the THRD records
//                  in place of the ELF entry)
//   LegPad         PadScript / PadRecorder / the key script, the slot-4 MaintDuties step
//   LegImage       the ELF image + BootEnv, the generated dispatch + the SDK manifest (+ FN_TRACE)
//   LegPark        LegCloser, the boundary / wall counters, Vu1Probe, the main context pointer
//   LegCensus      the run.meta census rows
//   LegParkSink    the terminate hook + the GuestPark sink, the IOP reboot image boot
//   TextWatch      the NATIVEEE_WATCH=text code-range snapshot compare (armed after the ELF load)
//   LegVerifySlot  the slot-7 Verify step (lanes, present / dump, harness verbs, frames cap), the
//                  session workers + tick thread, the join ladder, the session hooks
//   LegWatchdog    the no-boundary watchdog thread
// then the cold boot below. Entered from app/host/main.cpp through engine/title/title_play.h.
#include "engine/title/title_play.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include "app/host/build_id.h"
#include "app/host/first_run.h"
#include "app/host/launcher.h"
#include "app/host/user_data.h"
#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/log/log.h"
#include "engine/core/log/logger.h"
#include "engine/core/log/ring_log.h"
#include "engine/core/verify/frame_cap.h"
#include "engine/core/verify/run_meta.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/cpu/cpu_context_init.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/ps2_float.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/platform/host/platform.h"
#include "engine/platform/crash_handler.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_image.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park_sink.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_verify_slot.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_vif_irq.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_watchdog.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/warp_tool.h"

namespace {

using ps2r::Config;
using ps2r::ConfigLoader;
using ps2r::LogLevelSetting;
using ps2r::log::Logger;
using ps2r::ringlog::RingLog;
using namespace ps2r::title::sly2;

constexpr int kMainThreadPriority = 0;   // KernlBoot adopts tid 1 at priority 0

ps2r::log::Level ToLoggerLevel(LogLevelSetting s) {
    switch (s) {
        case LogLevelSetting::Quiet: return ps2r::log::Level::Quiet;
        case LogLevelSetting::Error: return ps2r::log::Level::Error;
        case LogLevelSetting::Warn: return ps2r::log::Level::Warn;
        case LogLevelSetting::Info: return ps2r::log::Level::Info;
        case LogLevelSetting::Narrate: return ps2r::log::Level::Narrate;
    }
    return ps2r::log::Level::Quiet;
}

bool FileExists(const std::string& path) {
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

// Where the ELF comes from, in order: --game-files <dir>/<elf_dir>/<elf_path> (the shipped
// layout), else the build's SHA-gated copy under <build>/generated/.../inputs (PS2R_TITLE_ELF_PATH,
// a compile definition from hle/CMakeLists.txt: this is a development build running from its tree).
std::string ResolveElf(const Config& cfg, const TitleProfile& title) {
    const std::string shipped = cfg.paths.game_files + "/" + title.elf_dir + "/" + title.elf_path;
    if (FileExists(shipped)) return shipped;
#ifdef PS2R_TITLE_ELF_PATH
    if (FileExists(PS2R_TITLE_ELF_PATH)) return PS2R_TITLE_ELF_PATH;
#endif
    return shipped;
}

// The leg proper: the phase objects in construction order (the first title's phase order; the file
// comment names what each owns), then the cold boot. A phase that cannot arm reports and the leg
// returns 2 through the destructors of what came before; a park / the frames cap leave through
// std::_Exit after LegCloser::Close.
int RunLeg(const Config& cfg, ps2r::verify::VerifyHost& host, ps2r::verify::FrameCap& cap, const TitleProfile& title,
           const std::string& elf_path, const std::string& title_dir) {
    const LegKnobs knobs = LegKnobs::FromEnvironment();
    LegCore core(cfg, knobs);
    if (!core.Attached()) return 2;
    LegGs gs(cfg, title, knobs, core);
    LegDevices devices(cfg, title, knobs, core, gs);
    LegMtvu mtvu(cfg, title, knobs, core, gs, devices);
    LegDmaIpu dma_ipu(knobs, core, devices, mtvu);
    LegInterrupts interrupts(knobs, core, gs, devices, mtvu);
    LegVifIrq vif_irq(core, devices, mtvu);
    LegDisc disc(cfg, title, host);
    LegServices services(cfg, core);
    LegAudio audio(cfg, knobs, core, services.spu_core);
    // NATIVEEE_WARP=<episode>[,<job>]: rewrite the private card's save1 BEFORE the session opens the card
    // (warp_tool.h; the game then loads it through its own Resume Game path).
    // The warp rewrites a save file, so it refuses the platform user-data card (a player's real card):
    // NATIVEEE_WARP needs an explicit private card (--set paths.mc_dir=<copy>).
    if (knobs.warp && cfg.paths.mc_dir == ps2r::app::UserData::PlatformRoot() + "/memcards") {
        std::fprintf(stderr, "nativeee: warp: refused -- paths.mc_dir is the platform card dir %s; point --set paths.mc_dir at a private copy\n",
                     cfg.paths.mc_dir.c_str());
        return 2;
    }
    WarpTool warp(knobs.warp ? knobs.warp->c_str() : nullptr, cfg.paths.mc_dir, title_dir);
    if (auto* meta = host.Meta()) meta->Set("warp", warp.Census());
    if (warp.Requested() && !warp.Applied()) return 2;
    LegPlay play(cfg, title, core, gs, audio);
    if (play.Failed()) return 2;
    LegIop iop(cfg, knobs, core, devices, mtvu, interrupts, disc, services);
    // The IOP's frame clock follows the SPU's: the Verify slot's field pump runs one IOP quantum per
    // catch-up field between the SPU quanta it mixes (play mode; headless has no catch-up fields).
    audio.field_quantum = [&iop](CpuContext& ctx) { iop.PumpFieldQuantum(ctx); };
    LegSaves saves(cfg, host, title, core, gs, devices, mtvu, services, iop, play, g_build_id);
    LegPad pad(cfg, title_dir, knobs, core, services, play);
    LegImage image(cfg, host, title, elf_path, title_dir, knobs, core, disc, services, iop);
    if (image.Error()) return image.Error();
    // NATIVEEE_WATCH=text[;...]: the code-range snapshot compare (text_watch.h) -- after the ELF load.
    TextWatch text_watch(knobs.watch ? knobs.watch->c_str() : nullptr, core.ram);
    LegPark park(host, knobs, core, devices, mtvu);
    LegCensus census(LegCensus::Phases{cfg, host, title, core, gs, devices, mtvu, dma_ipu, interrupts, vif_irq, services, audio, play, saves,
                                       iop, pad, image, text_watch, park});
    LegParkSink park_sink(host, census, park, play, disc, iop);
    LegVerifySlot verify(cfg, host, knobs, core, gs, mtvu, services, audio, play, saves, park, census, text_watch);
    if (verify.Failed()) return 2;
    LegWatchdog watchdog(host, knobs, core, park, census);

    // --- the cold boot ------------------------------------------------------------------------------
    // Play mode: the wall-locked boundary -- the field ticker notes one tick per 60 Hz wall edge
    // and the boundary's TickAdvance slot parks for it (BindPacer); NATIVEEE_UNPACED=1 keeps the
    // boundary ticking inline (the title's pacer/render thread handshake under instant
    // completions was the sched stage's starvation case -- the knob is the comparison).
    const bool paced = core.play_mode && gs.video_live && !knobs.unpaced;
    if (paced) {
        verify.tick_thread.Start();
        core.sequencer.BindPacer(&core.protocol);
    }
    if (auto* meta = host.Meta()) meta->Set("play_mode", core.play_mode ? (paced ? "paced" : "unpaced") : "headless");
    int result = 0;
    uint32_t fault_pc = 0;
    LegCloser& closer = park.closer;
    std::unique_ptr<ps2r::app::SessionRuntime>& session = play.session;
    auto cold_boot = [&]() {
        // --load-state: the cold restore route in place of the ELF entry (LegSaves::ColdBoot).
        if (!cfg.harness.load_state.empty()) {
            LegSaves::ColdDeps deps{verify.session_workers, audio, park, census, [&verify] { verify.JoinLadder(); }};
            return saves.ColdBoot(cfg.harness.load_state, deps);
        }
        CpuContext ctx;
        CpuContextInit::Reset(ctx, title.entry, title.main_stack_top);
        std::shared_ptr<ThreadInfo> main_info = ThreadManager::Instance().AdoptCurrentThread(1, kMainThreadPriority);
        main_info->ctx = &ctx;
        park.main_ctx.store(&ctx);
        EeRunToken::Instance().GuestEnter(1, kMainThreadPriority);
        PS2R_LOG(HOST, Info, "cold boot: entry 0x%08x sp 0x%08x", title.entry, title.main_stack_top);
        std::exception_ptr failure;
        try {
            Ps2Float::SetGuestThreadRounding();
            DispatchTable::Invoke(title.entry, ctx);
        } catch (const ThreadExitUnwind&) {
        } catch (...) {
            failure = std::current_exception();
            fault_pc = ctx.cpuRegs.pc;
        }
        park.main_ctx.store(nullptr);
        std::string release_error;
        if (!ThreadManager::Instance().ReleaseCurrentThread(&release_error) && !failure)
            failure = std::make_exception_ptr(std::runtime_error(release_error));
        // An F8 restore unwound this owner: the session's supervisor relaunches the restored threads
        // (the leg goes on; nothing is closed here -- the census and the closer run at the real end).
        if (session && session->RestorePending() && !failure) return 0;
        core.running.store(false);
        // Headless: this thread joins the rest (the session's `stopped` ladder does it in play).
        if (!session) verify.JoinLadder();
        census.Write();
        if (failure) {
            std::string what = "guest fault";
            try { std::rethrow_exception(failure); } catch (const std::exception& e) { what = e.what(); } catch (...) {}
            closer.Close("exception", ctx.cpuRegs.pc, 1, what.c_str());
            return 1;
        }
        closer.Close("none", ctx.cpuRegs.pc, 1, session && session->Closing() ? "session_closed" : "guest_returned");
        return 0;
    };
    if (session) {
        // The game on the session's 1 GiB worker; main pumps the window until it returns or
        // the window closes; the `stopped` ladder joins the workers and writes the census; Run
        // returns on main once the worker is joined -- the frame below unwinds normally (the GS
        // queue's Stop, the video stack, the session window: on main, in reverse construction order).
        result = session->Run(cold_boot, static_cast<unsigned>(cfg.engine.shutdown_wait_s));
        census.Write();
        closer.Close("none", fault_pc, fault_pc ? 1 : 0, "session_closed");
    } else {
        result = cold_boot();
    }
    watchdog.Join();
    std::fprintf(stderr, "nativeee: leg shutdown: %s%s%s\n", park.shutdown_steps.empty() ? "(no ladder)" : park.shutdown_steps.c_str(),
                 park.guest_join_abandoned.empty() ? "" : " abandoned=", park.guest_join_abandoned.c_str());
    cap.RunFinalizers();
    if (!park.guest_join_abandoned.empty()) {
        // An abandoned guest worker is still parked inside objects this frame owns: leave now,
        // census written, without their destructors.
        std::fflush(stderr);
        std::fflush(stdout);
        std::_Exit(result ? result : 3);
    }
    return result;
}

}  // namespace

int TitlePlayMain(int argc, char** argv) {
    Config cfg;
    ConfigLoader loader;
    loader.SetRetainedStartupAvailable(false);   // no IOP in this binary (PS2R_IOP_ORACLE=0)
    loader.SetUserDataRoot(ps2r::app::UserData::PlatformRoot());
    {
        // The settings store beside the executable (a game folder the launcher prepared) is found
        // without flags: ConfigStore's rule 2.
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of("/\\");
        if (slash != std::string::npos) loader.SetExecutableDir(exe.substr(0, slash));
    }
    const ConfigLoader::Result r = loader.Load(argc, argv, cfg);
    if (r.show_version) {
        std::printf("%s\n", g_build_id);
        return 0;
    }
    if (r.show_help) {
        std::printf("%s", ConfigLoader::Usage());
        return 0;
    }
    if (!r.ok) {
        std::fprintf(stderr, "nativeee: %s\n", r.error.c_str());
        return 2;
    }
    const TitleProfile& title = ActiveTitleProfile();
    // The CLI route of the first-run extraction (`--extract-disc <image>`): verify the disc image
    // against this build's ELF (TitleProfile::elf_sha256) and lay the game files, memcards,
    // settings and stamp under the resolved user data root, then exit. No window, no game.
    if (!r.extract_disc.empty())
        return ps2r::app::FirstRun::RunCli(ps2r::app::FirstRun::PlanFor(cfg, title, g_build_id, r.extract_disc, true));

    const std::string log_dir = cfg.paths.log_dir.empty() ? std::string("logs") : cfg.paths.log_dir;
    Logger::Instance().Start(cfg.harness.headless ? std::string() : log_dir + "/runtime_log.txt",
                             ToLoggerLevel(cfg.dev.log_level), true);
    const uint32_t mask_normal = RingLog::ParseChannelList(cfg.log.channels.c_str(), ~0u);
    RingLog::Configure(mask_normal, 0, static_cast<uint32_t>(cfg.log.ring_events), log_dir.c_str());
    RingLog::RegisterThread("main");
    ps2r::platform::host::RegisterSelf();
    ps2r::platform::host::PinCurrentThreadInteractive("nativeee-main");
    const std::string crash_ring = log_dir + "/ring_crash.rlog";
    ps2r::platform::CrashHandler::Install(crash_ring.c_str());
    for (const std::string& note : r.notes) std::fprintf(stderr, "[config] %s\n", note.c_str());

    PS2R_LOG(HOST, Info, "nativeee %s title %s (%s) config sources=%zu", g_build_id, title.serial.c_str(),
             PS2R_TITLE_ID, r.sources.size());

    // The launcher (the engine's first-run / settings screen, app/host/launcher.h) runs BEFORE
    // the game on a plain start: no game files under the user data root -> drop the disc image,
    // verify (ISO9660, SCUS_973.16 present, its SHA-256 == the ELF this build recompiled),
    // extract (ELF, disc copy or reference, memcards, settings, stamp), Play; afterwards the
    // settings screen (launcher.show). Any harness flag (--iso, --headless, --frames, --set,
    // --game-files ...) bypasses it, so the harness legs are unchanged; --launcher forces it.
    // Play lands the files in cfg.paths.game_files / cfg.paths.iso, where ResolveElf and the
    // disc open below look first.
    if (ps2r::app::Launcher::Wanted(cfg, title, r.harness_cli, r.launcher)) {
        ps2r::app::Launcher launcher(cfg, title, g_build_id);
        if (!r.launcher_screenshot.empty()) launcher.SetScreenshot(r.launcher_screenshot, r.launcher_then_play);
        else if (r.launcher_then_play) launcher.SetAutoPlay();
        if (!r.launcher_disc.empty()) launcher.SetScriptedDrop(r.launcher_disc);
        if (!r.launcher_page.empty()) launcher.SetHarnessPage(r.launcher_page);
        const ps2r::app::Launcher::Outcome outcome = launcher.Run();
        if (outcome == ps2r::app::Launcher::Outcome::Quit) return 0;
        if (outcome == ps2r::app::Launcher::Outcome::Failed) {
            std::fprintf(stderr, "nativeee: the launcher could not open or present a window; use --extract-disc <image> and --game-files\n");
            return 2;
        }
        PS2R_LOG(HOST, Info, "launcher: Play (game files %s, disc %s)", cfg.paths.game_files.c_str(), cfg.paths.iso.c_str());
    }

    ps2r::verify::VerifyHost host;
    {
        std::string err;
        if (!host.Arm(cfg, g_build_id, PS2R_TITLE_ID, &err)) {
            std::fprintf(stderr, "nativeee: %s\n", err.c_str());
            return 2;
        }
        if (auto* meta = host.Meta()) {
            meta->Set("user_data_root", cfg.paths.user_data);
            // The ONE settings file this run read and the pages write (ConfigStore), and how it was chosen.
            meta->Set("settings_file", r.settings_file + ":source=" + r.settings_source + (r.settings_migrated ? ":migrated=1" : ""));
        }
    }
    ps2r::verify::FrameCap cap;
    cap.Arm(static_cast<uint64_t>(cfg.harness.frames > 0 ? cfg.harness.frames : 0));
    cap.AddFinalizer([&] { host.Finalize(); });
    cap.AddFinalizer([] { RingLog::ExitDump(); });
    cap.AddFinalizer([] { Logger::Instance().Flush(); });

    // The title directory (hle/sdk_bindings.tsv): the source tree's games/<title>/ when this binary
    // runs from its build (PS2R_TITLE_DIR), else the staged copy beside the executable.
    std::string title_dir = PS2R_TITLE_DIR;
    if (!FileExists(title_dir + "/" + title.sdk_bindings_file)) {
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of("/\\");   // ExecutablePath is '\\'-joined on Windows (first_run.cpp does the same)
        const std::string beside = (slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/title";
        if (FileExists(beside + "/" + title.sdk_bindings_file)) title_dir = beside;
        else std::fprintf(stderr, "nativeee: title directory not found: neither %s nor %s has %s\n", PS2R_TITLE_DIR,
                          beside.c_str(), title.sdk_bindings_file.c_str());
    }
    // A disc image named on the command line installs itself on the first start: the first-run extraction
    // without its screen (the ELF verified against this build and extracted, the image REFERENCED in place --
    // no 4 GB copy --, memcards, settings, stamp). The player's launcher starts the game this way (--iso is a
    // harness flag, so the screen above is bypassed), and Play needs no second disc prompt.
    if (!FileExists(ResolveElf(cfg, title)) && !cfg.paths.iso.empty() && FileExists(cfg.paths.iso)) {
        std::fprintf(stderr, "nativeee: first start: extracting the game's files from %s (the image stays where it is)\n",
                     cfg.paths.iso.c_str());
        ps2r::app::FirstRun::RunCli(ps2r::app::FirstRun::PlanFor(cfg, title, g_build_id, cfg.paths.iso, /*copy_disc=*/false));
    }
    const std::string elf_path = ResolveElf(cfg, title);
    if (!FileExists(elf_path)) {
        std::fprintf(stderr,
                     "nativeee: ELF not found at %s (give --game-files <dir> holding %s/%s, or build with "
                     "NATIVEEE_DISC / NATIVEEE_TITLE_ELF so the build's extracted copy exists)\n",
                     elf_path.c_str(), title.elf_dir.c_str(), title.elf_path.c_str());
        cap.RunFinalizers();
        return 2;
    }
    return RunLeg(cfg, host, cap, title, elf_path, title_dir);
}
