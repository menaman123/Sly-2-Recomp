#include "games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

#include "app/host/build_id.h"
#include "app/host/savestate_slots.h"
#include "engine/core/log/log.h"
#include "engine/dmac/dmac_channel.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/hle/event_flags.h"
#include "engine/ee/hle/semaphores.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/vif/vif.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegSaves::LegSaves(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, LegCore& core, LegGs& gs,
                   LegDevices& devices, LegMtvu& mtvu, LegServices& services, LegIop& iop, LegPlay& play, const char* build_id)
    : memory_sections(core.ram, core.spad, nullptr, 0),
      restorer(memory_sections),
      // The slot's identity: the title, the clock shape and the hle.* composition, not the whole config.
      // Video settings, the GS queue mode and harness flags change only what is presented, so a slot taken
      // in a windowed run restores headless.
      session_clock(core.domain, core.tick_source, core.sequencer.Ticks(),
                    title.serial + ":play-v1:" + ps2r::app::SavestateSlots::Composition(cfg)),
      vu_memory_section(devices.vu0, devices.vu1),
      thread_section(ThreadManager::Instance()),
      kernel_sections(Semaphores::Instance(), EventFlags::Instance()),
      intc_section(core.intc, SyscallTable::Instance()),
      timer_section(core.timers, title.quirks.boot_t0_mode),
      vifs_section(devices.vif),
      gs_sections(gs.gs_state),
      gifs_section(devices.gif_regs),
      pads_section(services.pad_protocol, [](uint32_t a) { return memory::read<uint32_t>(a); }, PadsSection::SeqRestore::FromImage),
      vu1_register_section(devices.vu1),
      sound_state_codec(services.spu_core, services.sd_block_trans),
      mchl_section(services.mc_model),
      session_files(core.dump_frames_dir),
      cfg_(cfg), host_(host), core_(core), gs_(gs), mtvu_(mtvu), services_(services), iop_(iop), play_(play), title_mc_client_addr_(title.mc.client_addr),
      build_id_(build_id ? build_id : "") {
    // Registration order is restore order: memory, the game clock, then the owners that depend on them.
    ps2r::savestate::SectionRegistry& registry = restorer.Registry();
    if (!registry.Register(session_clock.Section()))
        std::fprintf(stderr, "nativeee: savestate: the session clock section did not register\n");
    // An image whose required sections have no owner here is refused before anything is touched.
    restorer.RequireSectionOwners();
    // A restored VU1 is mirrored into the MTVU worker's unit, so the first kick runs the restored program.
    MtvuStack* mtvu_stack = mtvu.mtvu.get();
    VuUnit& vu1 = devices.vu1;
    vu_memory_section.SetOnVu1Restored([mtvu_stack, &vu1] { if (mtvu_stack) mtvu_stack->MirrorVu1(vu1); });
    vu_memory_section.Register(registry);
    thread_section.Register(registry);
    kernel_sections.Register(registry);
    intc_section.Register(registry);
    // DMAC after INTC. Capture flushes the deferred VIF1 kick first, so a file never carries a parked kick.
    {
        Dmac& dmac = devices.dmac;
        uint64_t* unquiesced = &dmac_save_unquiesced;
        ps2r::savestate::SectionSpec dmac_spec = dmac.SavestateSection();
        dmac_spec.serialize = [&dmac, unquiesced](std::vector<uint8_t>& out) {
            dmac.FlushDeferredVif1();
            if (dmac.HasDeferredVif1() || (dmac.channel(static_cast<int>(DmacChannelId::Vif1)).chcr & DmacChcr::kStr)) ++*unquiesced;
            dmac.SerializeSection(out);
        };
        registry.Register(dmac_spec);
    }
    timer_section.Register(registry);
    vifs_section.Register(registry);
    // With a split image, the VRAM section reads the pipeline's copy (after draining it).
    if (gs.gs_queue && gs.pipeline_vram) {
        ps2r::gs::DrawIrThreadQueue* queue = gs.gs_queue.get();
        const ps2r::gs::VramImage* image = gs.pipeline_vram.get();
        gs_sections.SetVramSource([queue, image]() -> const ps2r::gs::VramImage* {
            queue->Sync();
            return image;
        });
    }
    gs_sections.Register(registry);
    {
        GifParser& parser = devices.gif_parser;
        uint64_t* tail = &gifs_restore_tail_bytes;
        gifs_section.SetOnRestored([&parser, tail] { *tail += parser.BufferedBytes(); });   // counted, never dropped
    }
    gifs_section.Register(registry);
    pads_section.Register(registry);
    // VIFS (restored before this) rewrote VIF1's CYCLE: re-seed the early-FINISH scan's CL / WL.
    Vif& vif = devices.vif;
    vu1_register_section.SetOnRestored([mtvu_stack, &vu1, &vif] {
        if (!mtvu_stack) return;
        mtvu_stack->MirrorVu1(vu1);
        mtvu_stack->ResyncEarlyFinish(VifRegsCl(vif.regs(1)), VifRegsWl(vif.regs(1)));
    });
    vu1_register_section.Register(registry);
    // IOPH before the SPU sections: a re-armed IOP thread may touch the SPU on its way to its first park.
    iop.iop_host.Section().Register(registry);
    sound_state_codec.Register(registry);
    mchl_section.Register(registry);
    // Before any section restore, re-anchor the emulated time domain on the raw tick.
    EmuDomain& domain = core.domain;
    TickSource& tick_source = core.tick_source;
    restorer.SetPreRestore([&domain, &tick_source](std::string*) {
        domain.ReAnchor(tick_source.RawCount());
        return true;
    });
    // F5, on the game thread at a completed boundary, once the owners below are quiet.
    capture = std::make_unique<ps2r::app::SessionCapture>(registry, core.game_boundary, cfg, mtvu_stack, build_id_);
    if (gs.gs_queue) {
        ps2r::gs::DrawIrThreadQueue* queue = gs.gs_queue.get();
        capture->Conditions().Register("gs_queue", [queue](std::string* why) {
            if (queue->Faulted()) { if (why) *why = "the GS consumer faulted"; return false; }
            queue->Sync();   // so the VRAM section reads the pipeline's latest deposits
            return true;
        });
    }
    RpcRouter* router = &services.rpc_router;
    capture->Conditions().Register("rpc", [router](std::string* why) {
        if (router->PendingCount() == 0) return true;
        if (why) *why = std::to_string(router->PendingCount()) + " native RPC completion(s) in flight";
        return false;
    });
}

void LegSaves::Bind(ps2r::app::SessionRuntime::Hooks& hooks, ps2r::app::SessionWorkers& workers, TickThread& tick_thread,
                    LegAudio& audio) {
    ps2r::app::SessionRuntime* session = play_.session.get();
    if (!session) return;
    capture->Bind(hooks);
    // F8 admission: an asynchronous owner holding state no section carries refuses by name, and the
    // restore is retried at the next boundary.
    MtvuStack* mtvu_stack = mtvu_.mtvu.get();
    if (mtvu_stack) live_probes.mtvu_quiesce = [mtvu_stack](std::string* why) { return mtvu_stack->Quiesce(why); };
    live_probes.events_pending = [] { return EeEventQueue::Instance().HasPending(); };
    RpcRouter* router = &services_.rpc_router;
    live_probes.rpc_pending = [router] { return router->PendingCount(); };
    live_probes.deliveries_pending = [] { return size_t{0}; };   // disc reads complete in-line
    live_probes.owners = [](std::string&) { return true; };
    ps2r::app::SessionRestore::Lifecycle lifecycle;
    lifecycle.inspect = [this](std::string& error) {
        if (ps2r::app::LiveRestoreGate::Inspect(live_probes, live_refusals, error)) return true;
        if (auto* meta = host_.Meta()) { meta->Set("live_restore_refusals", live_refusals.Render()); meta->Flush(); }
        return false;
    };
    lifecycle.prepare = [this](std::string& error) {
        if (gs_.gs_queue && gs_.gs_queue->Faulted()) { error = "the GS consumer faulted"; return false; }
        return true;
    };
    lifecycle.joined = [this](std::string&) {
        capture->Join();
        // Forget the consumer's last grab: it is the pre-restore picture.
        if (gs_.gs_queue) {
            gs_.gs_queue->Sync();
            if (gs_.consumer) {
                ps2r::gs::DrawIrQueueConsumer* consumer = gs_.consumer.get();
                gs_.gs_queue->RunOrdered([consumer] { consumer->ForgetLastFrame(); return true; });
            }
        } else if (gs_.consumer) {
            gs_.consumer->ForgetLastFrame();
        }
        return true;
    };
    const bool paced = core_.play_mode && gs_.video_live;
    lifecycle.resumed = [this, &tick_thread, &audio, paced](uint32_t frame, std::string&) {
        restored_vsync_frame = frame;
        audio.audio_pumped_tick = core_.domain.DomainTick();
        ++restores;
        restore_route = "live";
        restore_sections.clear();
        for (const auto& blob : restorer.LastSections())
            restore_sections += (restore_sections.empty() ? "" : ",") + ps2r::savestate::TagName(blob.tag);
        if (auto* meta = host_.Meta()) {
            meta->Set("restore_mhash", Hex(restorer.RestoredMemoryHash()));
            meta->Set("restored_vsync_frame", std::to_string(frame));
            meta->Set("restore_route", restore_route);
            meta->Set("restore_sections", restore_sections);
            meta->Set("restores", std::to_string(restores.load()));
            meta->Flush();
        }
        std::fprintf(stderr, "nativeee: F8 restore: frame %u restored live (mhash %s; sections %s)\n", frame,
                     Hex(restorer.RestoredMemoryHash()).c_str(), restore_sections.c_str());
        if (play_.live_input) play_.live_input->ResetPending();
        core_.running.store(true);
        if (paced && !play_.session->Paused()) tick_thread.Start();
        return true;
    };
    restore = std::make_unique<ps2r::app::SessionRestore>(*session, workers, restorer, session_clock, core_.game_boundary, host_,
                                                           session_files, std::move(lifecycle));
    // F8 restores in this process. The slot's sidecar is checked first: another profile or hle.*
    // composition is refused; a different build_id is a warning.
    f8_route = "live,iop=section";
    if (auto* meta = host_.Meta()) meta->Set("f8", f8_route);
    if (!restore->Bind(hooks, true)) {
        std::fprintf(stderr, "nativeee: F8 restore: the live transaction did not bind (no boundary owner?)\n");
        return;
    }
    const auto live_restore = hooks.restore;
    const std::string live_composition = ps2r::app::SavestateSlots::Composition(cfg_);
    const std::string build = build_id_;
    hooks.restore = [live_restore, live_composition, build](const ps2r::app::SessionControls::Request& request, uint64_t frame) {
        std::string error, warning;
        if (!request.path.empty() &&
            !ps2r::app::SavestateSlots::PreflightComposition(request.path, live_composition, build, &error, &warning))
            return ps2r::app::SessionRuntime::CaptureResult{ps2r::app::SessionControls::Phase::Refused, request.path + ": " + error};
        if (!warning.empty()) ps2r::app::SavestateSlots::Report("F8 restore", "Warning", request.path + ": " + warning);
        return live_restore(request, frame);
    };
    hooks.alive = [&workers] { return workers.BoundaryAlive(); };
    // F8 with nothing selected restores the newest slot. Every F5/F8 result goes to stderr and the log.
    const std::string slots = ps2r::app::SavestateSlots::Directory(cfg_);
    session->Controls().SetRestoreFallback([slots](std::string* reason) { return ps2r::app::SavestateSlots::Latest(slots, reason); });
    session->Controls().SetObserver([](const ps2r::app::SessionControls::Result& result) {
        using Action = ps2r::app::SessionControls::Action;
        if (result.request.action != Action::SaveReproduction && result.request.action != Action::RestoreReproduction) return;
        ps2r::app::SavestateSlots::Report(result.request.action == Action::SaveReproduction ? "F5 save" : "F8 restore",
                                          ps2r::app::SessionControls::PhaseLabel(result.phase), result.message);
    });
    session->ConfigureReproductions(slots);
    PS2R_LOG(HOST, Info, "savestates: slots %s, F8 route %s, %zu sections", slots.c_str(), f8_route.c_str(),
             restorer.Registry().Specs().size());
}

int LegSaves::ColdBoot(const std::string& path, const ColdDeps& deps) {
    cold_restore = std::make_unique<ps2r::app::ColdRestore>(restorer, thread_section, ThreadManager::Instance());
    ps2r::app::ColdRestore& cold = *cold_restore;
    std::string err;
    // A refusal before any thread ran: the leg ends through its destructors.
    auto refuse = [&](const char* route, const std::string& why) {
        std::fprintf(stderr, "nativeee: --load-state %s: %s: %s\n", path.c_str(), route, why.c_str());
        if (auto* meta = host_.Meta()) {
            meta->Set("restore_route", route);
            meta->Set("restore_refusal", why);
        }
        core_.running.store(false);
        if (!play_.session) {
            if (deps.join_ladder) deps.join_ladder();
            deps.census.Write();
            deps.park.closer.Close("none", 0, 0, route);
        }
        return 2;
    };
    if (!cold.Preflight(path, ps2r::app::SavestateSlots::Composition(cfg_), build_id_, &err)) return refuse("cold_refused", err);
    // The slot may come from a run with different pacing; this process's pacing policy wins.
    session_clock.SetAcceptHostPolicy(true);
    if (!cold.Result().warning.empty())
        ps2r::app::SavestateSlots::Report("--load-state", "Warning", path + ": " + cold.Result().warning);
    if (!cold.Apply(&err)) return refuse("cold_failed", err);
    const ps2r::app::ColdRestore::Report& report = cold.Result();
    deps.workers.SetBoundary(report.boundary_tid);
    core_.game_boundary.ResetAfterRestore();
    const uint64_t boundaries = core_.domain.BoundaryCount();
    if (play_.session) play_.session->RebaseFrame(boundaries);
    deps.audio.audio_pumped_tick = core_.domain.DomainTick();
    if (play_.live_input) play_.live_input->ResetPending();
    // libmc's readiness is in the image: the bound-server word of its sceSifClientData (client_addr + 0x24),
    // written by sceMcInit. mcserv's init never re-runs here, so derive Bound from it (0 = taken before
    // sceMcInit; the rows answer -100 until a live init binds).
    if (title_mc_client_addr_ != 0) {
        services_.mc_model.DeriveBound(memory::read<uint32_t>(title_mc_client_addr_ + SceSif::kClientServer));
        restore_mc_bound = services_.mc_model.Bound() ? "derived=1" : "derived=0";
    } else {
        restore_mc_bound = "no_client_addr";
    }
    restored_vsync_frame = report.vsync_frame;
    restore_route = "cold";
    restore_sections = report.sections;
    ++restores;
    const ps2r::iop::IopHostSection::RestoreCensus& iop = iop_.iop_host.Section().LastRestore();
    if (auto* meta = host_.Meta()) {
        meta->Set("restore_mhash", Hex(report.mhash));
        meta->Set("restored_vsync_frame", std::to_string(report.vsync_frame));
        meta->Set("restore_route", restore_route);
        meta->Set("restore_sections", restore_sections);
        meta->Set("restore_boundary_tid", std::to_string(report.boundary_tid));
        meta->Set("restore_mc_bound", restore_mc_bound);
        meta->Set("restore_iop", "rows=" + std::to_string(iop.rows) + ":threads=" + std::to_string(iop.threads_armed) + "/" +
                                     std::to_string(iop.threads_recorded) + ":servers=" + std::to_string(iop.servers) +
                                     ":timers=" + std::to_string(iop.timers_armed) + ":libs=" + std::to_string(iop.libraries) +
                                     ":frames=" + std::to_string(iop.frames));
        meta->Set("restores", std::to_string(restores.load()));
        meta->Flush();
    }
    std::fprintf(stderr, "nativeee: --load-state %s: frame %u restored cold (mhash %s; boundary tid %d; sections %s; IOP %u/%u threads, %u servers, %u timers)\n",
                 path.c_str(), report.vsync_frame, Hex(report.mhash).c_str(), report.boundary_tid, restore_sections.c_str(),
                 iop.threads_armed, iop.threads_recorded, iop.servers, iop.timers_armed);
    // Every started THRD record with a context gets a worker, resuming at its continuation.
    const int count = cold.Relaunch([](uint32_t fn, uint32_t enter, CpuContext& ctx) { return DispatchTable::InvokeAt(fn, enter, ctx); }, &err);
    if (auto* meta = host_.Meta()) meta->Set("relaunch", std::to_string(count));
    if (count < 0) {
        std::fprintf(stderr, "nativeee: --load-state: relaunch failed: %s\n", err.c_str());
        deps.park.closer.Close("relaunch_failed", 0, 0, err.c_str());
        return 2;
    }
    PS2R_LOG(HOST, Info, "cold restore: %d continuation(s) relaunched from %s (frame %u)", count, path.c_str(), report.vsync_frame);
    // Play: the session keeps the window while the boundary thread lives. Headless: wait here for it to end,
    // without holding the thread manager's host lock (a guest StartThread joins its predecessor under it).
    if (play_.session) return 0;
    while (deps.workers.BoundaryAlive() && core_.running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    core_.running.store(false);
    if (deps.join_ladder) deps.join_ladder();
    deps.census.Write();
    deps.park.closer.Close("none", 0, 0, "relaunched_threads_exited");
    return 0;
}

void LegSaves::JoinWriter() {
    if (capture) capture->Join();
}

void LegSaves::WriteCensus(ps2r::verify::RunMeta& meta) {
    std::string sections;
    for (const auto& spec : restorer.Registry().Specs()) sections += (sections.empty() ? "" : ",") + ps2r::savestate::TagName(spec.tag);
    meta.Set("savestate_sections", sections);
    if (capture) meta.Set("savestate_capture", capture->Census() + ":dmac_unquiesced=" + std::to_string(dmac_save_unquiesced));
    meta.Set("f8", f8_route);
    if (restores.load()) {
        meta.Set("restores", std::to_string(restores.load()));
        meta.Set("restored_vsync_frame", std::to_string(restored_vsync_frame));
        meta.Set("restore_route", restore_route);
        meta.Set("restore_sections", restore_sections);
        meta.Set("gifs_restore_tail_bytes", std::to_string(gifs_restore_tail_bytes));
    }
}

}  // namespace ps2r::title::sly2
