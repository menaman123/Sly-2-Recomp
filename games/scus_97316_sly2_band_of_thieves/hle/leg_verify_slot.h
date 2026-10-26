// LegVerifySlot: boundary slot 7 and the clean shutdown.
//
// Per boundary: the play-mode field pump (the SPU2 mixes 48000 frames/s whether or not the EE reaches a
// boundary, so a late boundary catches the mixer up on the device ring only; the boundary's own quantum
// is the hashed one), the memory and audio lanes, the frame grab and dumps, NATIVEEE_GS_ROUTES, the
// harness verbs (harness.close_at_frame, harness.inject_key through the real key route) and the frames cap.
//
// Shutdown: every guest worker is cancelled and unwinds; then the join ladder (workers, pacer, MTVU, AOT
// tier, GS backlog, audio device; each step timed in run.meta shutdown=) runs on the boot thread when
// headless, or in the session's `stopped` hook in play.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/host/display_sequence.h"
#include "app/host/session_workers.h"
#include "app/host/tick_thread.h"
#include "engine/core/config/config.h"
#include "engine/core/verify/memory_checkpointer.h"
#include "engine/core/verify/verify_host.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_census.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_play.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/text_watch.h"

namespace ps2r::title::sly2 {

class LegVerifySlot {
public:
    LegVerifySlot(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegGs& gs,
                  LegMtvu& mtvu, LegServices& services, LegAudio& audio, LegPlay& play, LegSaves& saves, LegPark& park,
                  LegCensus& census, TextWatch& text_watch);
    LegVerifySlot(const LegVerifySlot&) = delete;
    LegVerifySlot& operator=(const LegVerifySlot&) = delete;

    // True if harness.inject_key named an unknown key; RunLeg then exits with 2.
    bool Failed() const { return failed_; }
    void JoinLadder();

    struct HarnessKeyPress { int scancode; uint64_t frame, until; bool pressed = false, released = false; std::string name; };

    const std::vector<ps2r::verify::MemorySpan> verify_spans;
    TickThread tick_thread;
    ps2r::app::SessionWorkers session_workers;
    std::vector<HarnessKeyPress> harness_keys;
    // harness.display_sequence=<frame>:<mode>[,...]: Display page requests through the same route the menu uses.
    ps2r::app::DisplaySequence display_sequence;
    bool close_pushed = false;
    uint64_t pauses = 0;   // census: pause hook calls (pause + resume)

private:
    [[noreturn]] void EndLegFromBoundary(const char* detail);
    void HarnessVerbs(uint64_t at_boundary);
    void VerifyStep(CpuContext& ctx);
    void WriteRoutes(uint32_t present_key, const ps2r::gs::DrawPipeline::Frame& fr);

    const ps2r::Config& cfg_;
    ps2r::verify::VerifyHost& host_;
    const LegKnobs& knobs_;
    LegCore& core_;
    LegGs& gs_;
    LegMtvu& mtvu_;
    LegServices& services_;
    LegAudio& audio_;
    LegPlay& play_;
    LegSaves& saves_;
    LegPark& park_;
    LegCensus& census_;
    TextWatch& text_watch_;
    bool failed_ = false;
    bool tex_dump_done_ = false;
};

}  // namespace ps2r::title::sly2
