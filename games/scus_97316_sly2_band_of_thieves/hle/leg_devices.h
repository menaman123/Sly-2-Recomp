// LegDevices: the GS, GIF, VU, VIF and DMAC hardware models: the GS privileged registers, the GIF
// registers, parser and arbiter, both VU units, the VU0 registry, the VIF and the DMAC. The VU1 tier is
// chosen here, before LegMtvu copies the dispatch settings for its worker. LegDmaIpu registers the
// register windows later, once the MTVU stack exists.
#pragma once

#include "engine/core/config/config.h"
#include "engine/dmac/dmac.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/hle/gs_sink_fanout.h"
#include "engine/gif/hle/gs_state_sink.h"
#include "engine/gs/state/gs_mmio_handler.h"
#include "engine/title/title_profile.h"
#include "engine/vif/vif.h"
#include "engine/vu/vu0/vu0_registry.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/gif_trace_sink.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"

namespace ps2r::title::sly2 {

class LegDevices {
public:
    LegDevices(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core, LegGs& gs);
    LegDevices(const LegDevices&) = delete;
    LegDevices& operator=(const LegDevices&) = delete;

    ps2r::gs::GsMmioHandler gs_mmio;
    GifRegs gif_regs;
    GsStateSink gs_state_sink;
    GifTraceSink gif_trace;
    GsSinkFanout gif_sink;
    GifParser gif_parser;
    GifArbiter gif_arbiter;
    VuUnit vu0;
    VuUnit vu1;
    Vu0Registry vu0_registry;
    Vif vif;
    Dmac dmac;
};

}  // namespace ps2r::title::sly2
