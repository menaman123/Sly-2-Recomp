#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"

#include <cstdio>
#include <string>
#include <utility>

#include "app/host/build_id.h"
#include "engine/core/log/log.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/intc/intc.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"

namespace ps2r::title::sly2 {

LegMtvu::LegMtvu(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core, LegGs& gs,
                 LegDevices& devices)
    : devices_(devices) {
    // The DrawIR queue batches pipelined records; the end of every drain pass flushes the batch so no
    // record waits for the next kick.
    if (gs.gs_queue)
        devices.gif_arbiter.SetPassEndHook(+[](void* user) { static_cast<ps2r::gs::DrawIrThreadQueue*>(user)->EndOfPass(); },
                                           gs.gs_queue.get());
    // NATIVEEE_MTVU=1|0 forces the worker on or off in every mode; =1 under the deterministic profile checks
    // the worker's frames against the inline path.
    const bool mtvu_wanted = knobs.mtvu_set ? knobs.mtvu_on : (cfg.engine.mtvu && core.play_mode);
    if (mtvu_wanted) {
        MtvuSettings mtvu_settings;
        mtvu_settings.enabled = true;
        mtvu_settings.depth_frames = cfg.engine.mtvu_depth;
        mtvu_settings.depth_hold = cfg.engine.mtvu_depth_hold;
        mtvu = std::make_unique<MtvuStack>(devices.vif, devices.dmac.walker(), devices.gif_arbiter, devices.vu1.dispatch(),
                                           devices.vu1.dispatch().settings(), title.quirks.xgkick_defer, mtvu_settings, &core.running);
        mtvu->SetKickDrop(false);
        // engine.mtvu_early_finish (default off). The early FINISH applies wherever the worker runs; the
        // deferred boundary (no terminal sync, no VSync fence, composed GIF_STAT) only in paced play, so the
        // headless grab still reads the frame its boundary closed.
        early_finish = knobs.early_finish_set ? knobs.early_finish_on : cfg.engine.mtvu_early_finish;
        // engine.mtvu_wait_probe: before idling on the worker, ask the kernel object whether the wait would
        // park at all, so a WaitSema already satisfied by the early FINISH does not wait out the replay.
        wait_probe = knobs.wait_probe_set ? knobs.wait_probe_on : cfg.engine.mtvu_wait_probe;
        const bool paced = core.play_mode && gs.video_live && !knobs.unpaced;
        deferred_boundary = early_finish && paced;
        mtvu->SetDeferBoundaries(deferred_boundary);
        mtvu->SetComposedGifStat(deferred_boundary);
        // With the boundary deferred, the vsync thread's sceGsSyncPath (0x2403d8, which polls VIF1_STAT /
        // GIF_STAT until idle) would park on the worker and, at priority 2 over the logic thread's 5,
        // serialise the frame. The EE-side view answers it instead.
        mtvu->SetVif1IdleView(deferred_boundary);
        mtvu->SetVif1PollSite(+[](void*) -> uint32_t {
            ShadowCallStack::Frame f{};
            return ShadowCallStack::Top(0, f) ? f.func_addr : 0u;
        }, nullptr);
        // A completed EE-side sync is an observation point: collect the GS causes the pipeline latched.
        mtvu->sync().SetAfterSyncHook(+[](void* user) { static_cast<Intc*>(user)->CollectDeferred(); }, &core.intc);
        const bool drain_possible = !gs.video_live || gs.gs_queue_thread;
        // On by default: the drain thread's FINISH is applied on the token holder at a scheduling point,
        // never on a woken thread. NATIVEEE_GIF_DRAIN=0|1 overrides.
        const bool drain_wanted = knobs.gif_drain_set ? knobs.gif_drain_on : drain_possible;
        mtvu->Start(/*with_drain_thread=*/drain_wanted && drain_possible);
        if (mtvu->drain().Running()) {
            GifArbiter& arbiter = devices.gif_arbiter;
            // The EE's pushes go through the arbiter in stream order: the VSync job (fenced, so readers see a
            // completed frame) and the display register jobs. With the boundary deferred there is no fence:
            // the frame may still be unparsed, and the Verify grab reads LastFrameHeaderCopy.
            const bool fence = !deferred_boundary;
            MtvuStack* stack = mtvu.get();
            gs.ordered_submit = [&arbiter, stack, fence](std::function<void()> work) {
                arbiter.submitOrdered(std::move(work));
                arbiter.drain();
                if (fence) stack->VsyncFence();
            };
            gs.gs_state.Priv().SetJobDeferrer([&arbiter](std::function<void()> push) {
                arbiter.submitOrdered(std::move(push));
                arbiter.drain();
            });
        }
        // Early FINISH: the EE raises GS FINISH (through the same observer, so masking is unchanged) as soon
        // as it has staged the chain carrying the write; the parser's later Finish() for that write consumes
        // the pre-raised count, so there is still one edge per FINISH write.
        if (early_finish) {
            GifArbiter& arbiter = devices.gif_arbiter;
            ps2r::gs::GsPrivRegs& priv = gs.gs_state.Priv();
            mtvu->EnableEarlyFinish(+[](void* user) { static_cast<ps2r::gs::GsPrivRegs*>(user)->FinishNow(); }, &priv);
            priv.SetFinishConsumer([&arbiter] { return arbiter.ConsumePreRaisedFinish(); });
        }
        PS2R_LOG(HOST, Info, "MTVU: VU1 worker started (drain %s, %s boundary, depth_frames=%d, early_finish=%s, wait_probe=%s)",
                 mtvu->drain().Running() ? "thread" : "inline", deferred_boundary ? "deferred" : "terminal sync",
                 cfg.engine.mtvu_depth, early_finish ? "on" : "off", wait_probe ? "on" : "off");
    }
    // The AOT tier on the live VU1 unit, only when selected. Without a compiler on the host it prints one
    // notice and the specialised tier carries on.
    {
        VuUnit& live = LiveVu1();
        live.dispatch().aot().set_fallback(cfg.engine.vu1_aot_fallback == ps2r::Vu1AotFallback::Interp
                                               ? Vu1AotTier::Fallback::Interp
                                               : Vu1AotTier::Fallback::Block);
        live.dispatch().aot().set_assume_verified(!cfg.engine.vu1_aot_verify);
    }
    if (devices.vu1.dispatch().settings().vu1_mode == VuTierMode::Aot && cfg.engine.vu1_aot_verify) {
        VuUnit& live = LiveVu1();
        ps2r::app::Vu1AotCache::Settings aot_settings;
        aot_settings.root = (cfg.paths.user_data.empty() ? std::string(".") : cfg.paths.user_data) + "/vu1_aot";
        aot_settings.build_id = g_build_id;
        aot_settings.extra_corpus = cfg.engine.vu1_aot_corpus;
        vu1_aot_cache = std::make_unique<ps2r::app::Vu1AotCache>(live.dispatch().aot(), aot_settings);
        if (cfg.engine.vu1_aot_codegen) {
            ps2r::app::Vu1AotBuilder::Settings bs;
            bs.root = aot_settings.root;
            bs.build_id = g_build_id;
            bs.compiler = cfg.engine.vu1_aot_compiler;
            bs.sysroot = cfg.engine.vu1_aot_sysroot;
            bs.prelude_dir = cfg.engine.vu1_aot_prelude;
            bs.pack_dir = cfg.engine.vu1_aot_pack;   // "" = <executable dir>/vu1_aot_pack
            bs.opt_level = cfg.engine.vu1_aot_opt;
            bs.region.block_cap = cfg.engine.vu1_aot_region_blocks;
            bs.region.op_cap = cfg.engine.vu1_aot_region_ops;
            bs.region.taken_targets = cfg.engine.vu1_aot_region_taken;
            bs.region.chain = cfg.engine.vu1_aot_chain;
            bs.region.mac_acc = cfg.engine.vu1_aot_mac_acc;
            vu1_aot_builder = std::make_unique<ps2r::app::Vu1AotBuilder>(live.dispatch().aot(), bs);
            std::string bnote;
            if (vu1_aot_builder->Open(&bnote)) {
                PS2R_LOG(HOST, Info, "%s", bnote.c_str());
                vu1_aot_cache->SetBuilder(vu1_aot_builder.get());
            } else {
                std::fprintf(stderr, "nativeee: %s\n", bnote.c_str());
                vu1_aot_builder.reset();
            }
        }
        std::string note;
        if (!vu1_aot_cache->Open(&note)) {
            std::fprintf(stderr, "nativeee: %s (the AOT tier runs its fallback for every image)\n", note.c_str());
            vu1_aot_cache.reset();
        } else {
            PS2R_LOG(HOST, Info, "%s", note.c_str());
        }
    }
}

}  // namespace ps2r::title::sly2
