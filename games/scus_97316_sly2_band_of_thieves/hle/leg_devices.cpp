#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1.h"

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
    if (knobs.gif_trace) {
        gif_trace.SetFilter(knobs.gif_trace_value ? knobs.gif_trace_value->c_str() : nullptr);
        gif_sink.Add(&gif_trace);
    }
    if (knobs.gif_census && !knobs.vu1_cap) {   // the probe owns the arbiter's submit tap
        gif_census = std::make_unique<GifShapeCensus>(*knobs.gif_census, &gif_arbiter, &vu1);
        gif_sink.Add(gif_census.get());
        if (knobs.gif_pkt_capture) {   // <file>:<max MiB>
            const std::string& spec = *knobs.gif_pkt_capture;
            const size_t colon = spec.rfind(':');
            const uint64_t mib = colon == std::string::npos ? 64 : std::strtoull(spec.c_str() + colon + 1, nullptr, 10);
            gif_census->StartPacketCapture(spec.substr(0, colon), mib << 20);
        }
    }
    // VU1 tier: engine.vu1_mode, overridden by NATIVEEE_VU1_MODE=interp|cached|block|aot.
    //   hle     (no VU1 library in this title: behaves as cached)
    //   block   pre-resolved straight-line blocks
    //   aot     run-time recompilation of verified microprograms; the rest on engine.vu1_aot_fallback
    // Must be set before LegMtvu copies these settings for its worker.
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
        // STAGE NATIVE: title.native.vu1 selects the AOT tier whatever the mode above says (its registry is
        // the build-time generated table, filled by LegMtvu; a miss runs the tier's fallback = the block tier).
        if (NativeKnobs::From(cfg).vu1) NativeVu1::Select(vu1.dispatch().settings());
        // diag.vu1_lockstep: compare the tier against the interpreter at every kick.
        vu1.dispatch().settings().lockstep = cfg.diag.vu1_lockstep;
        if (cfg.diag.vu1_lockstep && !cfg.paths.user_data.empty()) {
            const std::string dir = cfg.paths.user_data + "/vu1_lockstep";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!ec || std::filesystem::is_directory(dir)) vu1.dispatch().settings().lockstep_dump_dir = dir;
        }
    }
}

}  // namespace ps2r::title::sly2
