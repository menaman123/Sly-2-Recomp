// games/scus_97316_sly2_band_of_thieves/hle/leg_census.cpp -- LegCensus (leg_census.h). The row order is
// the single-file composition's write_census, cut into the five groups below at the same keys.
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/sound/voice.h"

#include "engine/cdvd/hle/libcdvd_bindings.h"
#include "engine/dmac/dmac_channel.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "engine/gs/state/gs_mmio_handler.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/vu/macro/vu_macro_bridge.h"
#include "engine/vu/tiers/vu_lockstep.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {
namespace {
std::string FormatMs(double ms) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", ms);
    return b;
}
}  // namespace

void LegCensus::Write() {
    ps2r::verify::RunMeta* meta = p_.host.Meta();
    if (!meta) return;
    WriteKernel(meta);
    WriteGs(meta);
    WritePlay(meta);
    WriteDevices(meta);
    WriteVu1(meta);
    p_.text_watch.Report(p_.host);   // text_watch= / text_watch_pages= (NATIVEEE_WATCH=text only)
}

void LegCensus::WriteKernel(ps2r::verify::RunMeta* meta) {
    LegCore& core = p_.core;
    Intc& intc = core.intc;
    LegInterrupts& irq = p_.interrupts;
    meta->Set("threads", LegPark::ThreadCensus());
    meta->Set("first_syscalls", core.first_syscalls);
    meta->Set("syscall_count", std::to_string(core.syscall_count));
    std::string last;
    for (const std::string& s : core.last_syscalls) last += (last.empty() ? "" : ",") + s;
    meta->Set("last_syscalls", last);
    meta->Set("guest_call_unknown", std::to_string(core.guest_call_unknown.load()) + ":" + core.guest_call_unknown_list);
    meta->Set("boundaries", std::to_string(p_.park.boundaries));
    meta->Set("domain_tick", std::to_string(core.domain.DomainTick()));
    const SceSif::HeapCensus heap = SceSif::Heap();
    meta->Set("iop_heap_allocs", std::to_string(heap.allocs));
    meta->Set("iop_heap_refused", std::to_string(heap.refused));
    meta->Set("sif_init_rpc_calls", std::to_string(SceSif::InitRpcCalls()));
    meta->Set("sif_dma_bytes", std::to_string(SyscallTable::Instance().SifDmaBytes()));
    meta->Set("iop_module_loads", std::to_string(p_.image.iop_module_loads));
    meta->Set("iop_modules", p_.image.iop_modules);
    meta->Set("sif_bound", p_.services.rpc_router.BoundCensus());
    meta->Set("sif_cmds_delivered", std::to_string(SceSif::CommandsDelivered()));
    meta->Set("sif_cmds_dropped", std::to_string(SceSif::CommandsDropped()));
    meta->Set("sif_cmds_pending", std::to_string(SceSif::PendingCommands()));
    meta->Set("rpc_end_callbacks", std::to_string(p_.pad.rpc_end_callbacks) + ":pending=" + std::to_string(p_.services.rpc_router.PendingCount()));
    p_.iop.iop_host.WriteCensus(meta);
    meta->Set("cd_read_calls", std::to_string(LibcdvdBindings::ReadCalls()));
    meta->Set("boundary_site", p_.title.gs.vblank_wait_entry ? Hex(p_.title.gs.vblank_wait_entry) : "sceGsSyncV");
    meta->Set("boundary_site_hits", std::to_string(p_.image.boundary_site_hits));
    meta->Set("sdk_rows_hit", std::to_string(SdkBindingManifest::Instance().HitTotal()));
    meta->Set("sdk_rows_hit_list", SdkBindingManifest::Instance().HitList());
    meta->Set("ee_yield", EeYieldPoint::Instance().Stats().Format());
    {
        // The foreign-event queue: how many enqueues poked every park (no token holder) vs the
        // holder's parks only (E420: a blocked thread is never woken for an event the holder applies).
        const EeEventQueue& q = EeEventQueue::Instance();
        meta->Set("ee_events", "pokes_all=" + std::to_string(q.PokesAll()) + ":pokes_holder=" + std::to_string(q.PokesHolder()) +
                                   ":ring_drops=" + std::to_string(q.RingFullDrops()) +
                                   ":nonholder_drains=" + std::to_string(q.NonHolderDrainRequests()));
    }
    meta->Set("ee_yield_budget", std::to_string(LegIop::kYieldBudget) + ":spin_gate=" +
                                     (EeYieldPoint::Instance().SpinGate() ? "on" : "off"));
    meta->Set("vblank_waits", std::to_string(EeRunToken::Instance().VblankWaits()) + ":idle=" +
                                  std::to_string(EeRunToken::Instance().VblankWaitsIdle()));
    meta->Set("syscall_entry_isrs", std::to_string(irq.syscall_entry_isrs) + ":deferred=" + std::to_string(irq.syscall_entry_isrs_deferred));
    {
        // The VIFcode i-bit stall (leg_vif_irq.h): worker parks, handlers run in place / inside a sync
        // wait, and the refusals (no guest context; a wait inside an ISR; a stall seen before its cause).
        const LegVifIrq& v = p_.vif_irq;
        std::string row = "stalls=" + std::to_string(v.stalls) + ":inline_isrs=" + std::to_string(v.inline_isrs) +
                          ":inline_refused=" + std::to_string(v.inline_refused) + ":sync_isrs=" + std::to_string(v.sync_isrs) +
                          ":sync_not_pending=" + std::to_string(v.sync_not_pending) + ":sync_refused=" + std::to_string(v.sync_refused);
        if (p_.mtvu.mtvu)
            row += ":sync_services=" + std::to_string(p_.mtvu.mtvu->sync().IrqServices()) +
                   ":sync_service_refusals=" + std::to_string(p_.mtvu.mtvu->sync().IrqServiceRefusals()) +
                   ":worker_stalls=" + std::to_string(p_.mtvu.mtvu->worker().IrqStalls()) +
                   ":worker_stall_ns=" + std::to_string(p_.mtvu.mtvu->worker().IrqStallNs());
        meta->Set("vif_irq", row);
    }
    meta->Set("intc_raises", std::to_string(intc.RaiseCount()));
    meta->Set("intc_dispatches", std::to_string(intc.DispatchCount()));
    // Refusals are a cause raised WHILE a handler ran (the console: EXL masks interrupts
    // inside the handler; the cause stays pending in STAT and is taken at the next dispatch
    // point). intc_refused names them: site/<cause in dispatch>><cause refused>=n.
    // intc_edges=<cause>:raised/dispatched/guest-acked/still-pending -- a lost edge would be
    // raised > dispatched + acked + pending.
    meta->Set("intc_nested_refusals", std::to_string(intc.NestedRefusals()));
    meta->Set("intc_refused", intc.RefusalCensus());
    meta->Set("intc_edges", intc.EdgeCensus());
    meta->Set("gs_irq", "events=" + std::to_string(irq.gs_irq_events) + ":masked=" + std::to_string(irq.gs_irq_masked) +
                            ":raises=" + std::to_string(irq.gs_irq_raises));
    // E425 hazard census: the GS events by kind / parser path / raising thread, and the EE's
    // reads of the GS event state (CSR by the event bits it carried, SIGLBLID, the W1C clears).
    {
        std::string s;
        static const char* const kKind[2] = {"finish", "signal"};
        for (int k = 0; k < 2; ++k)
            for (int p = 0; p < 4; ++p)
                if (irq.gs_irq_kind_path[k][p])
                    s += std::string(s.empty() ? "" : ":") + kKind[k] + "_p" + std::to_string(p) + "=" +
                         std::to_string(irq.gs_irq_kind_path[k][p]);
        if (s.empty()) s = "none";
        meta->Set("gs_irq_kinds", s + ":on_ee=" + std::to_string(irq.gs_irq_on_ee) + ":on_drain=" + std::to_string(irq.gs_irq_on_drain));
        const ps2r::gs::GsMmioHandler::ReadCensus& rc = p_.devices.gs_mmio.Census();
        meta->Set("gs_priv_reads", "csr=" + std::to_string(rc.csr) + ":csr_finish_set=" + std::to_string(rc.csr_finish_set) +
                                       ":csr_signal_set=" + std::to_string(rc.csr_signal_set) + ":siglblid=" + std::to_string(rc.siglblid) +
                                       ":other=" + std::to_string(rc.other) + ":csr_clear_finish=" + std::to_string(rc.csr_clear_finish) +
                                       ":csr_clear_signal=" + std::to_string(rc.csr_clear_signal));
    }
    meta->Set("gs_priv_writes", std::to_string(p_.gs.gs_state.Priv().Writes()));
}

void LegCensus::WriteGs(ps2r::verify::RunMeta* meta) {
    LegGs& gs = p_.gs;
    std::unique_ptr<MtvuStack>& mtvu = p_.mtvu.mtvu;
    GifArbiter& gif_arbiter = p_.devices.gif_arbiter;
    meta->Set("mtvu", mtvu ? mtvu->Census() + ",idle_wait_entries=" + std::to_string(p_.interrupts.mtvu_idle_wait_entries) +
                                 ",idle_wait_skipped=" + std::to_string(p_.interrupts.mtvu_idle_wait_skipped) + "/" +
                                 std::to_string(p_.interrupts.mtvu_idle_wait_vblank_skipped) +
                                 ",wait_probe=" + (p_.mtvu.wait_probe ? "on" : "off") +
                                 ",arbiter_slots=" + std::to_string(gif_arbiter.PendingSlots()) +
                                 ",arbiter_sealed_ready=" + (gif_arbiter.hasSealedReady() ? "1" : "0") +
                                 ",worker_busy=" + (mtvu->worker().IsBusy() ? "1" : "0")
                           : "off");
    // E430: every producer-side park on the pipeline by cause (count/ns), the VIF1 registers
    // the EE wrote / read through the gate.
    meta->Set("mtvu_waits", mtvu ? mtvu->WaitsCensus() : "off");
    WriteGsPages(meta);
    WriteShadowSplit(meta);
    meta->Set("gs_csr", Hex(gs.gs_state.Priv().Read(ps2r::gs::GsPrivRegs::kCsr)));
    meta->Set("gs_draws", std::to_string(gs.draw_sink.Draws()));
    meta->Set("gs_frames", std::to_string(gs.draw_sink.Frames()));
    // NATIVEEE_GS_CAPTURE: the consumer-stream capture's counters (closed here so the row counts every record).
    if (gs.gs_capture) {
        gs.gs_capture->Close();
        meta->Set("gs_capture", gs.gs_capture->Report());
    }
    meta->Set("video", gs.video_live ? "gl" : "stub");
    if (gs.video_live) {
        meta->Set("gl_renderer", gs.video.Renderer());
        // The backend in force and how it was chosen (video.backend=auto picks per host).
        meta->Set("video_backend", std::string(gs.video.ActiveBackend()) + (gs.video.BackendAuto() ? " (auto)" : ""));
        meta->Set("gl_version", gs.video.GlVersion());
        meta->Set("gs_window", std::to_string(gs.video.WindowW()) + "x" + std::to_string(gs.video.WindowH()) +
                                   ":rebinds=" + std::to_string(gs.video.Rebinds()) +
                                   ":output_frames=" + std::to_string(gs.video.OutputFrames()));
        if (gs.consumer) {
            const ps2r::gs::DrawIrQueueConsumer::Counters& c = gs.consumer->Count();
            meta->Set("gs_consumer", "draws=" + std::to_string(c.draws) + ":jobs=" + std::to_string(c.jobs) +
                                         ":frames=" + std::to_string(c.frames) + ":uploads=" + std::to_string(c.uploads) +
                                         ":uploads_dropped=" + std::to_string(c.uploads_dropped) +
                                         ":resizes=" + std::to_string(c.resizes) + ":presents=" + std::to_string(c.presents) +
                                         ":grab_empty=" + std::to_string(c.grab_empty) +
                                         ":grab_deferred=" + std::string(gs.consumer->GrabDeferred() ? "on" : "off") +
                                         ":grabs_skipped=" + std::to_string(c.grabs_skipped) +
                                         ":grabs_deferred=" + std::to_string(c.grabs_deferred) +
                                         ":grabs_completed=" + std::to_string(c.grabs_completed));
            // DrawCoalescer (engine gs/hle/ir): the ink line-pair fold ahead of the pipeline.
            const ps2r::gs::DrawCoalescer::Counters& k = gs.consumer->CoalesceCount();
            meta->Set("gs_coalesce", std::string(gs.consumer->Coalesce() ? "on" : "off") + ":in=" + std::to_string(k.in) +
                                         ":passed=" + std::to_string(k.passed) + ":out=" + std::to_string(k.out) +
                                         ":absorbed=" + std::to_string(k.absorbed) + ":runs=" + std::to_string(k.runs) +
                                         ":pairs=" + std::to_string(k.pairs) + ":reject_orphan_z=" + std::to_string(k.reject_orphan_z) +
                                         ":reject_colour=" + std::to_string(k.reject_colour) + ":reject_state=" + std::to_string(k.reject_state));
        }
        if (gs.video.Pipe()) {
            // The decode fallback's census and its warm-up series (per 1 s window of the
            // consumer's clock: frames/lookups/decodes/decode_us/upload_us).
            const ps2r::gs::TextureResolver::Census& t = gs.video.Pipe()->Resolver().Stats();
            meta->Set("texres", "lookups=" + std::to_string(t.lookups) + ":l1_hits=" + std::to_string(t.l1_hits) +
                                    ":l2_hits=" + std::to_string(t.l2_hits) + ":sig_hits=" + std::to_string(t.sig_hits) +
                                    ":decodes=" + std::to_string(t.decodes) + ":decode_ms=" + std::to_string(t.decode_ns / 1000000u) +
                                    ":upload_ms=" + std::to_string(t.upload_ns / 1000000u) +
                                    ":prep_bytes=" + std::to_string(t.prep_bytes) + ":view_bytes=" + std::to_string(t.view_bytes) +
                                    ":owned_serves=" + std::to_string(t.owned_serves) + ":owned_misses=" + std::to_string(t.owned_misses));
            const std::string warm = gs.video.Pipe()->Resolver().WarmSeries();
            meta->Set("texres_warm", warm.empty() ? std::string("none") : warm);
            // The consumer's work per 1 s window: frames/draws/draw_calls/uploads/upload_kb/
            // readbacks/readback_kb/copies/program_binds/framebuffer_binds/texture_binds.
            const std::string gw = gs.video.Pipe()->WarmSeries();
            meta->Set("gs_warm", gw.empty() ? std::string("none") : gw);
        }
        if (gs.gs_queue) {
            const ps2r::gs::DrawIrThreadQueue::Counters& q = gs.gs_queue->Count();
            meta->Set("gs_queue", std::string(gs.gs_queue->policy() == ps2r::gs::DrawIrThreadQueue::Policy::Lockstep ? "lockstep" : "pipelined") +
                                      (gs.gs_queue->FrameSkip() ? ":frame_skip=" + std::to_string(gs.gs_queue->FrameSkipDepth()) : "") +
                                      ":enqueued=" + std::to_string(q.enqueued) + ":consumed=" + std::to_string(q.consumed) +
                                      ":frames=" + std::to_string(q.frames) + ":frames_dropped=" + std::to_string(q.frames_dropped) +
                                      ":frame_waits=" + std::to_string(q.frame_waits) +
                                      ":max_depth=" + std::to_string(q.max_depth) + ":payload_bytes=" + std::to_string(q.payload_bytes) +
                                      ":vram_ops=" + std::to_string(q.vram_ops) + ":snapshots=" + std::to_string(q.snapshots) +
                                      ":l2l=" + std::to_string(q.l2l_ee_served) + "/" + std::to_string(q.l2l_snapshot_served) + "/" +
                                      std::to_string(q.l2l_stale_max) + "/" + std::to_string(q.l2l_unserved) +
                                      ":faulted=" + (gs.gs_queue->Faulted() ? "1" : "0") +
                                      // The consumer thread's own time inside the pipeline by record kind (ms), and
                                      // per presented frame -- the renderer's cost independent of the game's pace.
                                      ":deliver_ms=" + std::to_string(q.deliver_draw_ns / 1000000u) + "/" +
                                      std::to_string(q.deliver_frame_ns / 1000000u) + "/" + std::to_string(q.deliver_other_ns / 1000000u) +
                                      ":consumer_ms_per_frame=" + FormatMs(q.frames > q.frames_dropped
                                                                               ? static_cast<double>(q.deliver_draw_ns + q.deliver_frame_ns + q.deliver_other_ns) / 1e6 /
                                                                                     static_cast<double>(q.frames - q.frames_dropped)
                                                                               : 0.0));
        } else {
            meta->Set("gs_queue", "inline");
        }
        meta->Set("gs_grab_sources", "grabs=" + std::to_string(gs.grabs) + ",local_memory=" + std::to_string(gs.grab_local_memory) +
                                         ",fallback=" + std::to_string(gs.grab_fallback));
        meta->Set("dump_frames", gs.frame_dump.Enabled() ? gs.frame_dump.Dir() + ":written=" + std::to_string(gs.frame_dump.Written()) +
                                                               ":failed=" + std::to_string(gs.frame_dump.Failed())
                                                         : std::string("off"));
    }
}

void LegCensus::WriteGsPages(ps2r::verify::RunMeta* meta) {
    // gs_pages=written=<n>:sampled=<n>:sampled_never_written=<n>:<page ranges, draws>:rtt=<ranges>:targets=<pages>
    // rtt= : pages sampled and never transferred to but inside a drawn FRAME / ZBUF footprint
    // (render-to-texture; served from the live target). The orphan ranges exclude them.
    const VertTraceSink& vert_trace = p_.gs.vert_trace;
    uint32_t written = 0, sampled = 0, orphan = 0;
    std::string ranges, rtt;
    const auto orphan_at = [&](uint32_t pg) { return vert_trace.page_sampled[pg] && !vert_trace.page_written[pg] && !vert_trace.page_target[pg]; };
    const auto rtt_at = [&](uint32_t pg) { return vert_trace.page_sampled[pg] && !vert_trace.page_written[pg] && vert_trace.page_target[pg]; };
    for (uint32_t pg = 0; pg < 512; ++pg) {
        written += vert_trace.page_written[pg] != 0;
        sampled += vert_trace.page_sampled[pg] != 0;
        if (orphan_at(pg)) {
            ++orphan;
            uint32_t end = pg, draws = 0;
            while (end < 512 && orphan_at(end)) draws += vert_trace.page_sampled[end++];
            ranges += ":" + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(draws);
            orphan += end - 1 - pg;
            pg = end - 1;
        } else if (rtt_at(pg)) {
            uint32_t end = pg, draws = 0;
            while (end < 512 && rtt_at(end)) draws += vert_trace.page_sampled[end++];
            rtt += (rtt.empty() ? "" : ",") + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(draws);
            pg = end - 1;
        }
    }
    std::string targets;
    for (uint32_t pg = 0; pg < 512; ++pg) {
        if (!vert_trace.page_target[pg]) continue;
        uint32_t end = pg;
        while (end < 512 && vert_trace.page_target[end]) ++end;
        targets += (targets.empty() ? "" : ",") + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(vert_trace.page_target[pg]);
        pg = end - 1;
    }
    meta->Set("gs_pages", "written=" + std::to_string(written) + ":sampled=" + std::to_string(sampled) +
                              ":sampled_never_written=" + std::to_string(orphan) + ranges + ":rtt=" + rtt + ":targets=" + targets);
}

// shadow_split=draws=<n>:tris_sub=<n>:tris_add=<n>:declined=<n>:enabled=<0|1> -- the stencil-shadow counter
// draws ShadowCounterSplit rewrote into clamped fixed-function pairs (hle/shadow_counter_split.h).
void LegCensus::WriteShadowSplit(ps2r::verify::RunMeta* meta) {
    const ShadowCounterSplit& s = p_.gs.shadow_split;
    meta->Set("shadow_split", "draws=" + std::to_string(s.draws_split.load()) + ":tris_sub=" + std::to_string(s.tris_sub.load()) +
                                  ":tris_add=" + std::to_string(s.tris_add.load()) + ":declined=" + std::to_string(s.declined.load()) +
                                  ":enabled=" + std::string(s.enabled ? "1" : "0"));
}

void LegCensus::WritePlay(ps2r::verify::RunMeta* meta) {
    LegPad& pad = p_.pad;
    LegAudio& audio = p_.audio;
    LegPlay& play = p_.play;
    BoundaryProtocol& protocol = p_.core.protocol;
    meta->Set("pad_stamps", std::to_string(pad.pad_stamps));
    meta->Set("pad_served", "active=" + std::to_string(pad.pad_served_active) + ":last=" + (pad.pad_served_last.empty() ? "none" : pad.pad_served_last));
    pad.pad_recorder.Close();   // every exit path: the open run + the header counts land before _Exit
    meta->Set("pad_record", pad.pad_recorder.Census());
    meta->Set("pad_script", pad.pad_script.Census());
    meta->Set("nav_probe", pad.nav_probe.Census());
    if (play.live_input) {
        meta->Set("pad_live", play.live_input->Census());
        meta->Set("input_sources", play.live_input->Sources());
    }
    if (play.session) meta->Set("session_presents", std::to_string(play.session->Presents()));
    // The frame trains (frame_timeline.h): the per-lane gap statistics and, with a harness out dir, the
    // full record as present_times.csv (verify/judge_smooth.py).
    if (play.session) {
        std::string row = play.timeline.Census();
        if (!p_.cfg.harness.out_dir.empty()) {
            const std::string csv = p_.cfg.harness.out_dir + "/present_times.csv";
            row += play.timeline.WriteCsv(csv) ? ";csv=" + csv : ";csv=unwritable";
        }
        meta->Set("present_hist", row);
        // The presented-frame cadence in force and the pacer's census (video.frame_cap; engine FramePacer).
        meta->Set("frame_cap", play.session->FrameCapCensus());
    }
    // The player's settings + save states: ps2r.ini writes, the Controls page's bindings saves, the
    // section registry, the F5 capture census, the F8 route and the restore rows.
    meta->Set("settings_persisted", std::to_string(play.settings_persisted) + ":failed=" + std::to_string(play.settings_persist_failed) +
                                        ":bindings_saves=" + std::to_string(play.controls_page ? play.controls_page->Saves() : 0) +
                                        ":bindings_reloads=" + std::to_string(play.live_input ? play.live_input->BindingsReloads() : 0));
    p_.saves.WriteCensus(*meta);
    if (p_.verify_display_sequence && !p_.verify_display_sequence->Empty())
        meta->Set("display_sequence", p_.verify_display_sequence->Census(audio.audio_ring.padded(), protocol.WorkMaxUs()));
    meta->Set("boundary_consumer", std::to_string(p_.park.boundary_consumer_calls) + "/" + std::to_string(p_.park.boundaries) +
                                       ":rejected_returns=" + std::to_string(p_.core.sequencer.RejectedServiceReturns()));
    // The audio device (play): quanta pushed to the ring / with a non-zero sample, the
    // ring's own counters (frames pushed/pulled, underruns, padded frames, re-latches, trimmed).
    meta->Set("audio_device", audio.audio_sink.Running() || audio.audio_device_quanta
                                  ? "sdl:" + std::to_string(audio.audio_sink.DeviceSampleRate()) + "hz:quanta=" + std::to_string(audio.audio_device_quanta) +
                                        ":nonsilent=" + std::to_string(audio.audio_device_nonsilent) + ":pushed=" + std::to_string(audio.audio_ring.pushed()) +
                                        ":pulled=" + std::to_string(audio.audio_ring.pulled()) + ":underruns=" + std::to_string(audio.audio_ring.underruns()) +
                                        ":padded=" + std::to_string(audio.audio_ring.padded()) + ":relatches=" + std::to_string(audio.audio_ring.relatches()) +
                                        ":trimmed=" + std::to_string(audio.audio_ring.trimmed()) +
                                        ":field_pumps=" + std::to_string(audio.audio_field_pumps) +
                                        ":lead=" + std::to_string(audio.audio_ring.LeadQuanta())
                                  : std::string("none"));
    meta->Set("audio_mixed", "pumps=" + std::to_string(p_.services.mixer.Pumps()) + ":frames=" + std::to_string(p_.services.mixer.FramesMixed()) +
                                 ":interp=" + (ps2r::sound::Voice::Gaussian() ? "gaussian" : "linear"));
    // The pace census (play; the pacer's protocol): boundaries that parked for the wall
    // edge / arrived late, the longest frame's work, the first long frames with the
    // TickProfile breakdown (Boundary / GifDrain / ... / other = recompiled code), the
    // per-site totals (ms/calls).
    meta->Set("pace", "parks=" + std::to_string(protocol.PaceParks()) + ":late=" + std::to_string(protocol.PaceLate()) +
                          ":timeouts=" + std::to_string(protocol.PaceTimeouts()) + ":wakes=" + std::to_string(protocol.PaceWakes()) + ":late_fields=" + std::to_string(p_.core.sequencer.PaceLateFields()) +
                          ":slept_ms=" + std::to_string(protocol.PaceSleptUs() / 1000) + ":work_max_ms=" + std::to_string(protocol.WorkMaxUs() / 1000) +
                          ":long=" + std::to_string(protocol.LongTraceCount()));
    meta->Set("pace_work_max_breakdown", protocol.WorkMaxBreakdown().empty() ? std::string("none") : protocol.WorkMaxBreakdown());
    meta->Set("pace_long_trace", protocol.LongTrace().empty() ? std::string("none") : protocol.LongTrace());
    meta->Set("tick_profile", protocol.SiteTotals());
    // The clock contract's census (engine ClockCensus; the series per 1 s wall window) and the INVARIANT row
    // `clock=`: logic_hz = fields of game time per wall second (the game's vblank clock; 60.00 +- 0.1 at real
    // time whatever the frame rate), frame_hz = boundaries per wall second (the picture-production rate),
    // catchup_ticks = boundaries that found their field already noted (frame work > one period) with the
    // extra fields they delivered, slow_motion = pacer ticks noted and still undelivered at the census beyond
    // the ONE a running frame always has in flight (a domain that fell behind the wall). The summary skips the
    // first kClockRampWindows windows (the boot / state-restore ramp) and the partial last one; the series
    // keep every completed window.
    {
        const ps2r::app::ClockCensus& cc = play.clock_census;
        meta->Set("clock_game_hz", cc.GameHzSeries().empty() ? std::string("none") : cc.GameHzSeries());
        meta->Set("clock_field_hz", cc.FieldHzSeries().empty() ? std::string("none") : cc.FieldHzSeries());
        meta->Set("clock_render_fps", cc.RenderFpsSeries().empty() ? std::string("none") : cc.RenderFpsSeries());
        meta->Set("clock_ee_cpu_ms", cc.EeCpuMsSeries().empty() ? std::string("none") : cc.EeCpuMsSeries());
        meta->Set("clock_gs_cpu_ms", cc.GsCpuMsSeries().empty() ? std::string("none") : cc.GsCpuMsSeries());
        meta->Set("clock_faults", cc.FaultsSeries().empty() ? std::string("none") : cc.FaultsSeries());
        meta->Set("clock_footprint_kb", cc.FootprintKbSeries().empty() ? std::string("none") : cc.FootprintKbSeries());
        constexpr size_t kClockRampWindows = 5;
        const std::vector<ps2r::app::ClockCensus::Window> ws = cc.Windows();
        double logic_sum = 0, frame_sum = 0;
        uint32_t logic_min = 0, logic_max = 0;
        size_t n = 0;
        for (size_t i = kClockRampWindows; i < ws.size(); i++) {
            const ps2r::app::ClockCensus::Window& w = ws[i];
            if (n == 0) logic_min = logic_max = w.fields;
            logic_min = std::min(logic_min, w.fields);
            logic_max = std::max(logic_max, w.fields);
            logic_sum += w.fields;
            frame_sum += w.boundaries;
            n++;
        }
        char row[256];
        std::snprintf(row, sizeof row, "logic_hz=%.2f:logic_hz_min=%u:logic_hz_max=%u:frame_hz=%.2f:windows=%zu/%zu:catchup_ticks=%llu:catchup_fields=%llu:slow_motion=%llu",
                      n ? logic_sum / static_cast<double>(n) : 0.0, logic_min, logic_max, n ? frame_sum / static_cast<double>(n) : 0.0, n, ws.size(),
                      static_cast<unsigned long long>(protocol.PaceLate()), static_cast<unsigned long long>(p_.core.sequencer.PaceLateFields()),
                      static_cast<unsigned long long>(p_.core.sequencer.Ticks().Pending() > 1 ? p_.core.sequencer.Ticks().Pending() - 1 : 0));
        meta->Set("clock", row);
    }
    meta->Set("boundary_wall_ms", p_.park.boundary_wall.empty() ? std::string("none") : p_.park.boundary_wall);
    meta->Set("shutdown", p_.park.shutdown_steps.empty() ? std::string("none") : p_.park.shutdown_steps);
}

void LegCensus::WriteDevices(ps2r::verify::RunMeta* meta) {
    LegDevices& devices = p_.devices;
    const GsStateSink& gs_state_sink = devices.gs_state_sink;
    Dmac& dmac = devices.dmac;
    const ps2r::ipu::IpuDevice& ipu = p_.dma_ipu.ipu;
    meta->Set("gif_sink", "tags=" + std::to_string(gs_state_sink.Tags()) +
                          ":p1=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path1)) +
                          ":p2=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path2)) +
                          ":p3=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path3)) +
                          ":regs=" + std::to_string(gs_state_sink.RegisterWrites()) +
                          ":packed=" + std::to_string(gs_state_sink.PackedWrites()) +
                          ":kicks=" + std::to_string(gs_state_sink.Kicks()) +
                          ":image_qw=" + std::to_string(gs_state_sink.ImageQwords()) +
                          ":eops=" + std::to_string(gs_state_sink.Eops()) +
                          ":p3_finishes=" + std::to_string(gs_state_sink.Path3Finishes()) +
                          ":drained=" + std::to_string(devices.gif_arbiter.DrainedPackets()) +
                          ":pending=" + std::to_string(devices.gif_arbiter.PendingBytes()));
    meta->Set("dmac_int1_raises", std::to_string(p_.dma_ipu.dmac_int1_raises));
    {
        // gs_xfer=: the GS transfer engine's counters + host->local starts per BITBLTBUF.DPSM (hex field=count).
        const ps2r::gs::TransferEngine::Counters& xc = p_.gs.gs_state.Xfer().Stats();
        std::string by;
        for (uint32_t f = 0; f < 64; ++f)
            if (xc.host_local_by_dpsm[f]) by += (by.empty() ? "" : ",") + Hex(f) + "=" + std::to_string(xc.host_local_by_dpsm[f]);
        meta->Set("gs_xfer", "bitbltbuf=" + std::to_string(xc.bitbltbuf) + ":trxdir=" + std::to_string(xc.trxdir) +
                                 ":hwreg=" + std::to_string(xc.hwreg) + ":completed=" + std::to_string(xc.completed) +
                                 ":l2l=" + std::to_string(xc.local_local) + ":l2h=" + std::to_string(xc.local_host) +
                                 ":owned=" + std::to_string(xc.owned) + ":dpsm=" + (by.empty() ? "none" : by));
    }
    {
        const DmacTransferStats& ts = dmac.TransferStats();
        std::string spr;
        for (int m = 0; m < 4; ++m)
            spr += (m ? ",from" : "from") + std::to_string(m) + "=" + std::to_string(ts.spr_from[m].starts) + "/" +
                   std::to_string(ts.spr_from[m].completed) + "/" + std::to_string(ts.spr_from[m].rejected) +
                   ",to" + std::to_string(m) + "=" + std::to_string(ts.spr_to[m].starts) + "/" +
                   std::to_string(ts.spr_to[m].completed) + "/" + std::to_string(ts.spr_to[m].rejected);
        meta->Set("dmac_spr", spr);  // per MOD: starts/completed/rejected
    }
    std::string dmac_chcr;
    for (int ch = 0; ch < kDmacChannelCount; ++ch)
        dmac_chcr += (ch ? "," : "") + std::to_string(ch) + "=" + Hex(dmac.channel(ch).chcr) + "/" +
                     std::to_string(dmac.channel(ch).qwc);
    meta->Set("dmac_chcr_qwc", dmac_chcr);
    std::string ipu_cmds;
    for (uint8_t code = 0; code < 10; ++code)
        if (ipu.CommandCount(code))
            ipu_cmds += (ipu_cmds.empty() ? "" : ",") + std::to_string(code) + "=" + std::to_string(ipu.CommandCount(code)) +
                        "/" + std::to_string(ipu.UnsupportedCommandCount(code));
    meta->Set("ipu_commands", ipu_cmds.empty() ? "none" : ipu_cmds);
    meta->Set("ipu_fifo", "in=" + std::to_string(ipu.InputSize()) + ":out=" + std::to_string(ipu.OutputSize()));
    meta->Set("ipu_fifo_port", "in_bytes=" + std::to_string(p_.dma_ipu.ipu_fifo.InBytes()) + ":out_bytes=" + std::to_string(p_.dma_ipu.ipu_fifo.OutBytes()) + ":unhandled=" + std::to_string(p_.dma_ipu.ipu_fifo.Unhandled()));
    meta->Set("vu_macro", "vu0_calls=" + std::to_string(VuMacroBridge::Stats().vu0_calls) +
                              ":vu1_kicks=" + std::to_string(VuMacroBridge::Stats().vu1_kicks));
}

void LegCensus::WriteVu1(ps2r::verify::RunMeta* meta) {
    // The VU1 fallback tier's work: bundles retired by the cached tier (the title's
    // microprograms interpreted on the kicking EE thread), the unit's cycle count.
    // Under MTVU the worker's unit is the live VU1 (the inline unit's counters stay 0).
    const bool worker = static_cast<bool>(p_.mtvu.mtvu);
    VuUnit& live_vu1 = p_.mtvu.LiveVu1();
    // block_ns= / block_ns_per_bundle= : the block tier's wall time inside its runs (the AOT tier's native
    // path runs there too), the per-frame VU1 cost the gameplay fps is bounded by.
    const VuBlockTier::Stats& bs = live_vu1.dispatch().block().stats();
    char bnpb[32];
    std::snprintf(bnpb, sizeof(bnpb), "%.2f", live_vu1.dispatch().block().bundles()
                                                   ? static_cast<double>(bs.run_ns) / static_cast<double>(live_vu1.dispatch().block().bundles())
                                                   : 0.0);
    // xgkick= : arms / packets submitted / bytes / Arms that allocated a fresh buffer / batches taken from the
    // arbiter's PATH1 pool (the PATH1 hand-off census).
    const XgKick::Census* xc = live_vu1.xgkick() ? &live_vu1.xgkick()->census() : nullptr;
    const std::string xg = xc ? ":xgkick=" + std::to_string(xc->arms) + "/" + std::to_string(xc->submits) + "/" +
                                    std::to_string(xc->submit_bytes) + "/" + std::to_string(xc->staging_allocs) + "/" +
                                    std::to_string(xc->pool_refills)
                              : std::string();
    meta->Set("vu1_tier", "bundles=" + std::to_string(live_vu1.dispatch().bundles()) + ":cycle=" + std::to_string(live_vu1.cycle()) +
                              ":dropped_kicks=" + std::to_string(live_vu1.DroppedKicks()) + ":unit=" + (worker ? "worker" : "inline") +
                              ":mode=" + (live_vu1.dispatch().settings().vu1_mode == VuTierMode::Aot ? "aot"
                                          : live_vu1.dispatch().settings().vu1_mode == VuTierMode::Block ? "block"
                                          : live_vu1.dispatch().settings().vu1_mode == VuTierMode::Interp ? "interp" : "cached") +
                              ":block_runs=" + std::to_string(bs.runs) + ":block_ns=" + std::to_string(bs.run_ns) + ":block_ns_per_bundle=" + bnpb +
                              // blocks= block executions (a delay slot counts one) / through a generated REGION function /
                              // ops run inside them / ops whose stall came from the static schedule / ops that consulted the model.
                              ":blocks=" + std::to_string(bs.blocks_run) + "/" + std::to_string(bs.blocks_native) + "/" +
                              std::to_string(bs.ops_native) + "/" + std::to_string(bs.ops_static) + "/" + std::to_string(bs.ops_model) + xg);
    // diag.vu1_lockstep: the tier A/B census (kicks compared, divergent, the first divergent
    // kick's ordinal, the categories) -- the invariant's in-game oracle for a tier.
    if (const VuLockstep* ls = live_vu1.dispatch().lockstep()) meta->Set("vu1_lockstep", ls->Summary());
    // The AOT tier: images seen / verified / refused / pending a verdict, kicks run native vs
    // on the fallback, ns per bundle on the native path, the corpus and verifier census.
    if (live_vu1.dispatch().settings().vu1_mode == VuTierMode::Aot) {
        const Vu1AotTier& aot = live_vu1.dispatch().aot();
        if (p_.mtvu.vu1_aot_cache) {
            meta->Set("vu1_aot", p_.mtvu.vu1_aot_cache->CensusRow());
            // The code generator: compiler, images built / cached / refused, compile and
            // re-verify wall, block functions installed on the live table.
            if (p_.mtvu.vu1_aot_builder) meta->Set("vu1_aot_codegen", p_.mtvu.vu1_aot_builder->CensusRow());
            else meta->Set("vu1_aot_codegen", p_.cfg.engine.vu1_aot_codegen ? "no_compiler:1" : "off:1");
        } else {
            char ns[32];
            std::snprintf(ns, sizeof(ns), "%.2f", aot.native_ns_per_bundle());
            meta->Set("vu1_aot", "programs:" + std::to_string(aot.images()) + ":verified:" + std::to_string(aot.verified()) +
                                     ":refused:" + std::to_string(aot.refused()) + ":pending:0:native_kicks:" +
                                     std::to_string(aot.stats().native_kicks) + ":fallback_kicks:" +
                                     std::to_string(aot.stats().fallback_kicks) + ":native_bundles:" +
                                     std::to_string(aot.stats().native_bundles) + ":ns_per_bundle:" + ns +
                                     (aot.assume_verified() ? ":assume_verified:1" : ":no_cache:1"));
        }
    }
}

}  // namespace ps2r::title::sly2
