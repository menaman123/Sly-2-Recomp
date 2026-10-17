// games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h -- LegGs: the GS phase of the title's play composition
// (after LegCore, before the GIF / VU / VIF / DMAC devices). Owns GsState and its DrawIR chain: GsState's
// DrawIR goes to a counting sink (run.meta gs_draws=) which, when a video stack is wanted (play mode /
// --dump-frames), chains to the DrawIR thread queue -> the queue consumer -> the generic DrawPipeline
// over GL (hle/video_stack.h: no class renderers, an empty VramLayout); VertTraceSink sits at the head
// of the chain for the gs_pages= census and the GS_VERTS / GS_SKIP_* knobs. Play mode with
// gs.queue=pipelined (the Config default): the SPLIT image -- the pipeline runs on ITS OWN VramImage on
// the consumer thread; the EE keeps GsState's image for its writes, marks and upload epoch, and every
// EE-side call on it is logged (VramMirror) and replayed on the pipeline's image in stream order by the
// queue (draw_ir_thread_queue.h). The headless dump / harness legs keep the LOCKSTEP shared image: their
// frames lane is byte-identical to the inline path by construction, and the Verify slot reads a complete
// grab. Also the --dump-frames writer (frame_dump.h) with the DUMP_EVERY / DUMP_FROM policy, the grab
// source counters (census gs_grab_sources=) and the slot-0 PresentRendezvous step (the GS frame
// boundary: the VSync job the engine does not push itself).
#pragma once

#include <cstdint>
#include <memory>

#include "engine/core/config/config.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/hle/classrdr/target_dump.h"
#include "engine/gs/hle/queue/draw_ir_queue_consumer.h"
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"
#include "engine/gs/state/gs_state.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_dump.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/vert_trace_sink.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/video_stack.h"

namespace ps2r::title::sly2 {

class LegGs {
public:
    LegGs(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core);
    LegGs(const LegGs&) = delete;
    LegGs& operator=(const LegGs&) = delete;

    // Which present keys --dump-frames writes: n >= DUMP_FROM and a multiple of DUMP_EVERY.
    bool DumpWanted(uint32_t n) const { return n >= dump_from && n % dump_every == 0; }

    ps2r::gs::GsState gs_state;
    VideoStack video;
    bool gs_queue_split_image = false;
    std::unique_ptr<ps2r::gs::VramImage> pipeline_vram;   // the pipeline's own image (split); null = shared
    bool video_live = false;
    std::unique_ptr<ps2r::gs::DrawIrQueueConsumer> consumer;
    std::unique_ptr<ps2r::gs::DrawIrThreadQueue> gs_queue;
    bool gs_queue_thread = false;
    bool gs_queue_lockstep = true;
    VertTraceSink vert_trace;
    ps2r::gs::CountingDrawIrSink draw_sink;
    // The queue stops (drains, joins) before the consumer and the video stack it draws into go (the
    // members unwind in reverse order; this guard runs first).
    struct GsQueueStop {
        std::unique_ptr<ps2r::gs::DrawIrThreadQueue>& q;
        ~GsQueueStop() { if (q) q->Stop(); }
    } gs_queue_stop;
    FrameDump frame_dump;
    uint64_t grab_local_memory = 0, grab_fallback = 0, grabs = 0;   // census gs_grab_sources=
    uint32_t dump_every = 1, dump_from = 0;
    // NATIVEEE_GS_RT_DUMP (boot triage, headless lockstep): the colour image INCLUDING the stored alpha
    // byte of the target at <fbp> before (and after) the qualifying draws of the frame that ENDS at
    // present key <key> -- the frame-local ordinals `at` and/or the class rule (an indexed-high read
    // of it, a 16-bit FRAME or a partial FBMSK into it; engine target_dump.h) -- as rt_*.pam +
    // rt_dump_index.txt in the dump / harness-out directory. Parsed here; armed / disarmed by the
    // Verify slot (leg_verify_slot.cpp) at the frame's boundaries. rt_dump_key 0 = off.
    ps2r::gs::TargetDump::Request rt_dump_req;
    uint32_t rt_dump_key = 0;
    uint64_t gs_frame_boundaries = 0;   // the slot-0 VSync jobs pushed (JobRec::src_frame)
};

}  // namespace ps2r::title::sly2
