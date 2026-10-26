// LegGs: GsState and its DrawIR chain. Draws go to a counting sink and, when there is a video stack
// (play or --dump-frames), on through the DrawIR thread queue to the generic GL DrawPipeline.
// VertTraceSink heads the chain for the gs_pages= census and the GS_VERTS / GS_SKIP_* knobs.
//
// Play mode with gs.queue=pipelined (the default) splits the VRAM image: the pipeline renders from its
// own copy on the consumer thread, and EE-side writes to GsState's image are logged and replayed onto it
// in stream order. Headless runs share one image in lockstep, so their frames lane matches the inline
// path exactly and the Verify slot always reads a complete grab.
//
// Also owns the --dump-frames writer and the slot-0 PresentRendezvous step, which pushes the per-frame
// VSync job the engine does not push itself.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "engine/core/config/config.h"
#include "engine/gs/hle/capture/gs_stream_recorder.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/hle/classrdr/target_dump.h"
#include "engine/gs/hle/texsrc/texres_trace.h"
#include "engine/gs/hle/queue/draw_ir_queue_consumer.h"
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"
#include "engine/gs/state/gs_state.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_dump.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/shadow_counter_split.h"
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
    std::unique_ptr<ps2r::gs::VramImage> pipeline_vram;   // split mode's pipeline image; null when shared
    bool video_live = false;
    std::unique_ptr<ps2r::gs::DrawIrQueueConsumer> consumer;
    // Just in front of the consumer, on its thread: marks the `consumed` lane when a frame record is
    // delivered, and publishes the frame the consumer is on so the output callback can tag its picture.
    struct LatencyTap : ps2r::gs::DrawIrSink {
        ps2r::gs::DrawIrSink* next = nullptr;
        FrameTimeline* timeline = nullptr;           // set by LegPlay; read on the consumer thread
        std::atomic<uint64_t> current_frame{0};     // JobRec::src_frame of the record being delivered
        void OnDraw(const ps2r::gs::DrawIr& d) override { next->OnDraw(d); }
        void OnDrawOwned(ps2r::gs::DrawIr&& d) override { next->OnDrawOwned(std::move(d)); }
        void OnJob(const ps2r::gs::JobRec& j) override { next->OnJob(j); }
        void OnFrame(const ps2r::gs::JobRec& v) override {
            current_frame.store(v.src_frame, std::memory_order_relaxed);
            next->OnFrame(v);
            if (timeline && v.arg1 == 1) timeline->Note(FrameTimeline::Lane::Consumed, v.src_frame);
        }
    } latency_tap;
    std::unique_ptr<ps2r::gs::DrawIrThreadQueue> gs_queue;
    bool gs_queue_thread = false;
    bool gs_queue_lockstep = true;
    VertTraceSink vert_trace;
    ShadowCounterSplit shadow_split;   // between the trace and the renderer
    // NATIVEEE_GS_CAPTURE: the .gsstream recorder between the shadow split and the queue (null = off).
    // Declared before the queue stop guard so it closes after the queue drained.
    std::unique_ptr<ps2r::gs::GsStreamRecorder> gs_capture;
    ps2r::gs::CountingDrawIrSink draw_sink;
    // Stops the queue before the consumer and video stack are destroyed (members unwind in reverse).
    struct GsQueueStop {
        std::unique_ptr<ps2r::gs::DrawIrThreadQueue>& q;
        ~GsQueueStop() { if (q) q->Stop(); }
    } gs_queue_stop;
    FrameDump frame_dump;
    // Whether anything reads the display grab (harness lanes, F5, frame dumps). A player's session
    // needs none, so the grab and its hash are skipped. Set before the first frame.
    std::atomic<bool> grab_wanted{true};
    uint64_t grab_local_memory = 0, grab_fallback = 0, grabs = 0;   // census gs_grab_sources=
    uint32_t dump_every = 1, dump_from = 0;
    // NATIVEEE_GS_RT_DUMP (headless lockstep): the target at <fbp>, alpha included, before and after the
    // qualifying draws of the frame ending at present key <key>, as rt_*.pam. Armed by the Verify slot.
    // rt_dump_key 0 = off.
    ps2r::gs::TargetDump::Request rt_dump_req;
    uint32_t rt_dump_key = 0;
    // NATIVEEE_GS_TEXRES=<tbp hex>[,<cbp hex>]: the texture resolver trace filter, applied by the Verify
    // slot once the video stack is live.
    ps2r::gs::TexresTrace::Filter texres_filter;
    bool texres_armed = false;
    uint64_t gs_frame_boundaries = 0;   // VSync jobs pushed by slot 0
    // With the GIF drain thread running (installed by LegMtvu): runs `work` on the drain thread after
    // every packet submitted so far, then returns. The VSync job must follow the frame's packets. Null when
    // the drain is inline: the work runs in place.
    std::function<void(std::function<void()>)> ordered_submit;
};

}  // namespace ps2r::title::sly2
