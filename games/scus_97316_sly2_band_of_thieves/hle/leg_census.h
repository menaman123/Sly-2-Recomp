// games/scus_97316_sly2_band_of_thieves/hle/leg_census.h -- LegCensus: the run.meta census rows of the
// title's play composition, written by every exit path (the frames cap, the window close, a park, the
// watchdog, an exception, the guest's return) in ONE frozen order: the kernel / SIF / INTC rows, the GS
// rows (gs_irq, gs_pages, gs_draws, the video stack, the queue, the grab sources, the dump), the play rows
// (pad, session, the audio device, the pace census, boundary_wall_ms, shutdown), the device rows (gif_sink,
// the DMAC, the IPU, the COP2 bridge, the VU1 tier, the AOT tier). RunMeta::Set replaces a key in place,
// so a repeated Write keeps the first write's order. Holds references to every earlier phase; constructed
// after LegPark and before the park sink (leg_park_sink.h), which is the first caller.
#pragma once

#include "engine/core/config/config.h"
#include "engine/core/verify/run_meta.h"
#include "engine/core/verify/verify_host.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_image.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/text_watch.h"

namespace ps2r::title::sly2 {

class LegCensus {
public:
    // Every phase the rows read, in construction order.
    struct Phases {
        const ps2r::Config& cfg;
        ps2r::verify::VerifyHost& host;
        const TitleProfile& title;
        LegCore& core;
        LegGs& gs;
        LegDevices& devices;
        LegMtvu& mtvu;
        LegDmaIpu& dma_ipu;
        LegInterrupts& interrupts;
        LegServices& services;
        LegAudio& audio;
        LegPlay& play;
        LegIop& iop;
        LegPad& pad;
        LegImage& image;
        TextWatch& text_watch;
        LegPark& park;
    };

    explicit LegCensus(const Phases& p) : p_(p) {}
    LegCensus(const LegCensus&) = delete;
    LegCensus& operator=(const LegCensus&) = delete;

    // Writes every census row to the host's RunMeta (nothing when the host has none).
    void Write();

private:
    void WriteKernel(ps2r::verify::RunMeta* meta);
    void WriteGs(ps2r::verify::RunMeta* meta);
    void WriteGsPages(ps2r::verify::RunMeta* meta);
    void WritePlay(ps2r::verify::RunMeta* meta);
    void WriteDevices(ps2r::verify::RunMeta* meta);
    void WriteVu1(ps2r::verify::RunMeta* meta);

    Phases p_;
};

}  // namespace ps2r::title::sly2
