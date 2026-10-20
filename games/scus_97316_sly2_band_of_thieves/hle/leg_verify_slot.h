// games/scus_97316_sly2_band_of_thieves/hle/leg_verify_slot.h -- LegVerifySlot: slot 7 (Verify) of the
// boundary and the clean leg end, in the title's play composition. The lanes: the play-mode FIELD PUMP
// (the SPU2 mixes 48000 frames a second whether or not the EE passes a boundary -- a late boundary
// delivered the elapsed fields, so the mixer is caught up to one quantum per domain tick, each pushed to
// the DEVICE ring only; the boundary's own pump is the hashed one), the memory + audio lanes
// (VerifyHost::OnBoundary), the present / frame dump (the consumer's reference grab in lockstep / inline;
// under the wall lock the published HEADER + hash -- no pixel copy crosses the threads), NATIVEEE_GS_ROUTES
// (the pipeline's per-draw route trace of a frame + the CRTC registers + the VRAM image), the harness
// verbs (--set harness.close_at_frame=N [harness.close_after_ms=M]: the window's close as a real SDL_QUIT
// from main; --set harness.inject_key=<KEY>@N[-M][,...]: a synthetic press through the REAL key route),
// the frames cap and the session's boundary (pause / step / the close). The clean leg end: every guest
// worker is cancelled (SessionWorkers) and the thread at the boundary unwinds through ThreadExitUnwind;
// the join ladder (guest workers, the pacer, the MTVU worker, the AOT tier, the GS consumer's backlog,
// the audio device; each step's wall offset in run.meta shutdown=) runs on the cold-boot thread
// (headless) or in the session's `stopped` hook (play). The play-mode field ticker lives here too.
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

    // True when harness.inject_key named an unknown key: the leg ends with exit 2.
    bool Failed() const { return failed_; }
    // The join ladder, in dependency order (see the file comment); also the cold boot's (headless).
    void JoinLadder();

    struct HarnessKeyPress { int scancode; uint64_t frame, until; bool pressed = false, released = false; std::string name; };

    const std::vector<ps2r::verify::MemorySpan> verify_spans;
    TickThread tick_thread;
    ps2r::app::SessionWorkers session_workers;
    std::vector<HarnessKeyPress> harness_keys;
    // --set harness.display_sequence=<frame>:<mode>[,...]: the Display page's requests through the
    // same SessionControls route the menu widget takes (run.meta display_sequence=).
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
};

}  // namespace ps2r::title::sly2
