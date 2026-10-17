// games/scus_97316_sly2_band_of_thieves/hle/leg_devices.cpp -- LegDevices (leg_devices.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"

#include <string>

#include "engine/vu/tiers/vu_tier_dispatch.h"

namespace ps2r::title::sly2 {

LegDevices::LegDevices(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core, LegGs& gs)
    : gs_mmio(gs.gs_state),
      gs_state_sink(gs.gs_state),
      gif_sink({&gs_state_sink}),
      gif_parser(gif_sink, gif_regs),
      gif_arbiter(gif_parser, nullptr),
      vu0(0),
      vu1(1, &gif_arbiter, title.quirks.xgkick_defer),
      vif(vu0, vu1, gif_arbiter, gif_regs),
      dmac(core.ram, core.spad, vif, gif_arbiter) {
    core.mmio.Register(MmioRouter::kGsPrivBase, MmioRouter::kGsPrivEnd - MmioRouter::kGsPrivBase, gs_mmio, "GS_PRIV");
    core.mmio.Register(GifRegs::kBase, GifRegs::kSize, gif_regs, "GIF");
    // NATIVEEE_GIF_TRACE=1: every GIF tag / register write to stderr (boot triage only).
    if (knobs.gif_trace) gif_sink.Add(&gif_trace);
    // The VU1 fallback tier from Config engine.vu1_mode (hle = the library ladder, which this title
    // has none of: cached; cached; interp; block = the pre-resolved straight-line block tier;
    // aot = the generic run-time recompiler: a verified image on its specialised block table,
    // the rest on engine.vu1_aot_fallback), then NATIVEEE_VU1_MODE=interp|cached|block|aot (boot
    // triage) on top. Applied here, before the MTVU stack copies the inline dispatch's settings
    // for the worker's unit (the AOT tier's status map and cache attach to the LIVE unit, leg_mtvu.cpp).
    {
        auto tier_of = [](const std::string& name, VuTierMode fallback) {
            return name == "interp" ? VuTierMode::Interp
                 : name == "block"  ? VuTierMode::Block
                 : name == "aot"    ? VuTierMode::Aot
                 : name == "cached" ? VuTierMode::Cached
                                    : fallback;
        };
        VuTierMode mode = cfg.engine.vu1_mode == ps2r::Vu1Mode::Interp ? VuTierMode::Interp
                        : cfg.engine.vu1_mode == ps2r::Vu1Mode::Block  ? VuTierMode::Block
                        : cfg.engine.vu1_mode == ps2r::Vu1Mode::Aot    ? VuTierMode::Aot
                                                                       : VuTierMode::Cached;
        if (knobs.vu1_mode) mode = tier_of(*knobs.vu1_mode, mode);
        vu1.dispatch().settings().vu1_mode = mode;
    }
}

}  // namespace ps2r::title::sly2
