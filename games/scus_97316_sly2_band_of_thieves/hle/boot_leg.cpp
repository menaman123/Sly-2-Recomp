// Sly 2's play composition (TitlePlayMain, entered from app/host/main.cpp).
//
// RunLeg builds the machine as a chain of phase objects (one per hle/leg_*.{h,cpp}), constructed in
// dependency order and destroyed in reverse, then cold-boots the recompiled code from the ELF entry, or,
// with --load-state, restores a save state and relaunches its threads instead.
//
// Three run shapes:
//   --headless                    counting GS sink, no window, neutral pad, muted; census in run.meta
//   --headless --dump-frames DIR  the GL renderer into a hidden window, every presented frame as a PPM
//   play (default)                the session window, live pad, SDL audio, pipelined renderer
//
// hle/sdk_bindings.tsv is the only title data read at run time. NATIVEEE_* environment knobs are read
// once, by LegKnobs::FromEnvironment.
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

// The ELF: <game_files>/<elf_dir>/<elf_path>, else (a development build run from its tree) the build's
// extracted copy, PS2R_TITLE_ELF_PATH.
std::string ResolveElf(const Config& cfg, const TitleProfile& title) {
    const std::string shipped = cfg.paths.game_files + "/" + title.elf_dir + "/" + title.elf_path;
    if (FileExists(shipped)) return shipped;
#ifdef PS2R_TITLE_ELF_PATH
    if (FileExists(PS2R_TITLE_ELF_PATH)) return PS2R_TITLE_ELF_PATH;
#endif
    return shipped;
}

// Constructs the phases, then cold-boots. A phase that cannot arm reports and RunLeg returns 2 through
// the destructors of the phases before it; a park or the frames cap exits via LegCloser::Close.
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
    // NATIVEEE_WARP rewrites save1 before the card is opened, so it refuses the player's real card.
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
    // One IOP quantum per catch-up field, interleaved with the SPU quanta (play mode only).
    audio.field_quantum = [&iop](CpuContext& ctx) { iop.PumpFieldQuantum(ctx); };
    LegSaves saves(cfg, host, title, core, gs, devices, mtvu, services, iop, play, g_build_id);
    LegPad pad(cfg, title_dir, knobs, core, services, play);
    LegImage image(cfg, host, title, elf_path, title_dir, knobs, core, disc, services, iop);
    if (image.Error()) return image.Error();
    // Must follow the ELF load: it snapshots code ranges.
    TextWatch text_watch(knobs.watch ? knobs.watch->c_str() : nullptr, core.ram);
    LegPark park(host, knobs, core, devices, mtvu);
    LegCensus census(LegCensus::Phases{cfg, host, title, core, gs, devices, mtvu, dma_ipu, interrupts, vif_irq, services, audio, play, saves,
                                       iop, pad, image, text_watch, park});
    LegParkSink park_sink(host, census, park, play, disc, iop);
    LegVerifySlot verify(cfg, host, knobs, core, gs, mtvu, services, audio, play, saves, park, census, text_watch);
    if (verify.Failed()) return 2;
    LegWatchdog watchdog(host, knobs, core, park, census);

    // Play mode locks the frame boundary to the 60 Hz wall clock; NATIVEEE_UNPACED=1 ticks it inline.
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
        // An F8 restore unwound this thread; the session relaunches the restored threads.
        if (session && session->RestorePending() && !failure) return 0;
        core.running.store(false);
        // In play mode the session joins the workers; headless, this thread does.
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
        // The game runs on the session's worker thread while main pumps the window.
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
        // A guest worker that never joined still uses objects on this frame: exit without destructors.
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
    loader.SetRetainedStartupAvailable(false);
    loader.SetUserDataRoot(ps2r::app::UserData::PlatformRoot());
    {
        // A settings file beside the executable (a launcher-prepared game folder) is found without flags.
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
    // --extract-disc IMAGE: verify the disc against this build, lay out the game files, and exit.
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

    // The built-in first-run / settings screen on a plain start. Any harness flag (--iso, --headless,
    // --set, ...) skips it; --launcher forces it.
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
            meta->Set("settings_file", r.settings_file + ":source=" + r.settings_source + (r.settings_migrated ? ":migrated=1" : ""));
        }
    }
    ps2r::verify::FrameCap cap;
    cap.Arm(static_cast<uint64_t>(cfg.harness.frames > 0 ? cfg.harness.frames : 0));
    cap.AddFinalizer([&] { host.Finalize(); });
    cap.AddFinalizer([] { RingLog::ExitDump(); });
    cap.AddFinalizer([] { Logger::Instance().Flush(); });

    // The title tables: the source tree when run from the build, else the title/ folder beside the exe.
    std::string title_dir = PS2R_TITLE_DIR;
    if (!FileExists(title_dir + "/" + title.sdk_bindings_file)) {
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of("/\\");
        const std::string beside = (slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/title";
        if (FileExists(beside + "/" + title.sdk_bindings_file)) title_dir = beside;
        else std::fprintf(stderr, "nativeee: title directory not found: neither %s nor %s has %s\n", PS2R_TITLE_DIR,
                          beside.c_str(), title.sdk_bindings_file.c_str());
    }
    // First start from the launcher (--iso skips the screen above): extract the game files from the
    // disc image, which stays where it is.
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
