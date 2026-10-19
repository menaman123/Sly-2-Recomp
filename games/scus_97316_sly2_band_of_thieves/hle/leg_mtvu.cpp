// games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.cpp -- LegMtvu (leg_mtvu.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"

#include <cstdio>
#include <string>
#include <utility>

#include "app/host/build_id.h"
#include "engine/core/log/log.h"
#include "engine/ee/intc/intc.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"

namespace ps2r::title::sly2 {

LegMtvu::LegMtvu(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core, LegGs& gs,
                 LegDevices& devices)
    : devices_(devices) {
    // NATIVEEE_MTVU=1|0 (boot triage) forces the stack on / off in every mode (the deterministic
    // profile pins engine.mtvu=off; =1 under it is the frame-identity oracle for the worker). The
    // Config default is on and the loader records no "was set", so the env knob is the headless opt-in.
    const bool mtvu_wanted = knobs.mtvu_set ? knobs.mtvu_on : (cfg.engine.mtvu && core.play_mode);
    if (mtvu_wanted) {
        MtvuSettings mtvu_settings;
        mtvu_settings.enabled = true;
        mtvu_settings.depth_frames = cfg.engine.mtvu_depth;
        mtvu = std::make_unique<MtvuStack>(devices.vif, devices.dmac.walker(), devices.gif_arbiter, devices.vu1.dispatch(),
                                           devices.vu1.dispatch().settings(), title.quirks.xgkick_defer, mtvu_settings, &core.running);
        mtvu->SetKickDrop(false);
        mtvu->SetDeferBoundaries(false);
        // A completed EE-side sync (the pipeline ran empty on the EE's demand) is an observation
        // point for the GS causes the pipeline latched (E422, leg_interrupts: the FINISH /
        // SIGNAL observer raises deferred; the INTC collects at observation points only).
        mtvu->sync().SetAfterSyncHook(+[](void* user) { static_cast<Intc*>(user)->CollectDeferred(); }, &core.intc);
        // The drain thread only where the DrawIR chain's consumer never draws on the pushing thread.
        const bool drain_possible = !gs.video_live || gs.gs_queue_thread;
        // Default ON (E420-E422: the drain thread's FINISH is applied on the holder's context at a
        // scheduling point, never on a woken thread's -- idle_isrs 0, the GS census and the kernel
        // census equal to the inline lane); NATIVEEE_GIF_DRAIN=0|1 forces (the A/B lever).
        const bool drain_wanted = knobs.gif_drain_set ? knobs.gif_drain_on : drain_possible;
        mtvu->Start(/*with_drain_thread=*/drain_wanted && drain_possible);
        if (mtvu->drain().Running()) {
            GifArbiter& arbiter = devices.gif_arbiter;
            // The EE's pushes into the DrawIR chain ride the arbiter in stream order: the boundary's
            // VSync job (then fenced: the Verify slot / the vblank read a completed frame) and the
            // privileged display registers' jobs (one kick, no wait).
            gs.ordered_submit = [&arbiter](std::function<void()> work) {
                arbiter.submitOrdered(std::move(work));
                arbiter.drain();
                arbiter.DrainFence();
            };
            gs.gs_state.Priv().SetJobDeferrer([&arbiter](std::function<void()> push) {
                arbiter.submitOrdered(std::move(push));
                arbiter.drain();
            });
        }
        PS2R_LOG(HOST, Info, "MTVU: VU1 worker started (drain %s, terminal boundary sync, depth_frames=%d)",
                 mtvu->drain().Running() ? "thread" : "inline", cfg.engine.mtvu_depth);
    }
    // The AOT tier's persistent side on the LIVE VU1 unit (the worker's under MTVU: its dispatch copied
    // the settings above). Only when the tier is selected. No compiler on the host = one notice, the
    // specialised tier continues.
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
            bs.prelude_dir = cfg.engine.vu1_aot_prelude;
            bs.pack_dir = cfg.engine.vu1_aot_pack;   // "" = <executable dir>/vu1_aot_pack (the shipped pack)
            bs.opt_level = cfg.engine.vu1_aot_opt;
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
