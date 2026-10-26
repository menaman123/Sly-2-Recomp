// LegCensus: the run.meta census rows, written by every exit path (frames cap, window close, park,
// watchdog, exception, guest return) in a fixed order: kernel/SIF/INTC, GS, play, then device rows.
// RunMeta::Set replaces keys in place, so a repeated Write keeps the first write's order.
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
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_vif_irq.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"
#include "app/host/display_sequence.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/text_watch.h"

namespace ps2r::title::sly2 {

class LegCensus {
public:
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
        LegVifIrq& vif_irq;
        LegServices& services;
        LegAudio& audio;
        LegPlay& play;
        LegSaves& saves;
        LegIop& iop;
        LegPad& pad;
        LegImage& image;
        TextWatch& text_watch;
        LegPark& park;
        // Set later by LegVerifySlot.
        const ps2r::app::DisplaySequence* verify_display_sequence = nullptr;
    };
    void SetDisplaySequence(const ps2r::app::DisplaySequence* seq) { p_.verify_display_sequence = seq; }

    explicit LegCensus(const Phases& p) : p_(p) {}
    LegCensus(const LegCensus&) = delete;
    LegCensus& operator=(const LegCensus&) = delete;

    void Write();

private:
    void WriteKernel(ps2r::verify::RunMeta* meta);
    void WriteGs(ps2r::verify::RunMeta* meta);
    void WriteGsPages(ps2r::verify::RunMeta* meta);
    void WriteShadowSplit(ps2r::verify::RunMeta* meta);
    void WritePlay(ps2r::verify::RunMeta* meta);
    void WriteDevices(ps2r::verify::RunMeta* meta);
    void WriteVu1(ps2r::verify::RunMeta* meta);

    Phases p_;
};

}  // namespace ps2r::title::sly2
