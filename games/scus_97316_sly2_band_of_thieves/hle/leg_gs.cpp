#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "engine/core/log/log.h"
#include "engine/core/threads/park.h"
#include "engine/gs/hle/ir/job_rec.h"
#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/gs/state/vertex_kick.h"
#include "engine/platform/host/platform.h"

namespace ps2r::title::sly2 {

LegGs::LegGs(const ps2r::Config& cfg, const TitleProfile& title, const LegKnobs& knobs, LegCore& core)
    : gs_queue_stop{gs_queue} {
    // The GS privileged registers are real, so the game's FINISH handshake runs through the modelled
    // transfer: DMAC GIF channel -> GifParser -> FINISH -> GsPrivRegs -> the interrupt observer -> INTC.
    gs_state.SetQuirks(ps2r::gs::VertexKickQuirks{title.quirks.round_sprite_mode});
    gs_queue_split_image = core.play_mode && cfg.video.backend != ps2r::VideoBackend::Soft &&
                           cfg.gs.queue == ps2r::GsQueue::Pipelined;
    if (gs_queue_split_image) pipeline_vram = std::make_unique<ps2r::gs::VramImage>();
    video_live = core.want_video && cfg.video.backend != ps2r::VideoBackend::Soft &&
                 video.Create(cfg.video, "Sly 2 -- NativeEE (GS)", pipeline_vram ? *pipeline_vram : gs_state.Vram(), core.play_mode);
    if (core.want_video && !video_live) std::fprintf(stderr, "nativeee: no video stack (video=stub; see the log)\n");
    // gs.queue=inline runs the consumer on the pushing EE thread. Otherwise the consumer has its own
    // thread, which owns the GL context: lockstep headless (each push returns once consumed), pipelined
    // with frame skip in play (the renderer never blocks the game: one pending frame, older ones dropped).
    gs_queue_thread = video_live && cfg.gs.queue != ps2r::GsQueue::Inline;
    gs_queue_lockstep = !gs_queue_split_image;
    if (video_live) {
        // Play reads the window capture one present late behind a fence rather than draining the GPU each
        // present; the headless dump keeps the synchronous read.
        if (core.play_mode && knobs.gs_consumer_levers) video.SetPresentCaptureDeferred(true);
        // Prompt capture: a bounded wait at the present for this present's own picture.
        if (core.play_mode && cfg.video.capture_wait_ms > 0) video.SetPresentCaptureWaitNs(uint64_t(cfg.video.capture_wait_ms) * 1000000ull);
        video.SetSwapHiddenWindow(!knobs.gs_consumer_levers);
        if (knobs.gl_shader_blend) video.SetGlForceShaderBlend(true);
        video.Pipe()->Cache().Readback().SetOfferEnabled(knobs.gs_consumer_levers);
        // When no render target sits at the scanout base, grab like the CRTC: decode DISPFB from local
        // memory (movie frames are uploaded and shown without drawing). Fall back to the last drawn target.
        video.Pipe()->Scanout().SetGrabPolicy(ps2r::gs::ScanoutComposer::GrabPolicy::LocalMemory);
        auto crtc_now = [this] {
            using P = ps2r::gs::GsPrivRegs;
            const P& p = gs_state.Priv();
            return ps2r::gs::CrtcClassifier::Classify(p.Read(P::kPmode), p.Read(P::kDispfb1), p.Read(P::kDispfb2),
                                                      p.Read(P::kDisplay1), p.Read(P::kDisplay2));
        };
        ps2r::gs::DrawIrQueueConsumer::Hooks hooks;
        hooks.enter = [this] { video.EnsureCurrent(); };
        if (gs_queue_thread) {
            hooks.upload_pixels = [this] { return gs_queue->LastUploadPayload(); };
            hooks.crtc = [this] { return gs_queue->CurrentFrameCrtc(); };
        } else {
            hooks.upload_pixels = [this] { return &gs_state.Xfer().LastUploadPixels(); };
            hooks.crtc = crtc_now;
        }
        hooks.window_resized = [this](int w, int h) { video.OnWindowResized(w, h); };
        hooks.grab_wanted = [this] { return grab_wanted.load(std::memory_order_relaxed); };
        hooks.owned_frame = [this](bool, ps2r::gs::DrawPipeline::Frame*) {
            if (const int scale = scale_request.exchange(0, std::memory_order_relaxed)) {
                video.ApplyScale(scale);
                if (consumer) consumer->SetFrameHashScale(video.Scale());
                PS2R_LOG(HOST, Info, "video: internal scale %dx", video.Scale());
            }
            return false;
        };
        consumer = std::make_unique<ps2r::gs::DrawIrQueueConsumer>(*video.Pipe(), std::move(hooks));
        latency_tap.next = consumer.get();
        // Play hashes the grab on the consumer thread; the boundary reads only the header and hash.
        if (!gs_queue_lockstep) consumer->SetFrameHashScale(video.Scale());
        // Pipelined play queues the frame-end grab on the GPU and publishes it one boundary late, so the
        // consumer never stalls on glReadPixels. Lockstep and headless keep the synchronous grab.
        if (core.play_mode && knobs.gs_consumer_levers && !gs_queue_lockstep && gs_queue_split_image) {
            consumer->SetGrabDeferred(true);
            video.Pipe()->Cache().SetDeferredDeposit(true);
        }
        if (gs_queue_thread) {
            ps2r::gs::DrawIrThreadQueue::Hooks qh;
            qh.upload_pixels = [this] { return &gs_state.Xfer().LastUploadPixels(); };
            qh.crtc = crtc_now;
            gs_queue = std::make_unique<ps2r::gs::DrawIrThreadQueue>(
                latency_tap, std::move(qh),
                gs_queue_lockstep ? ps2r::gs::DrawIrThreadQueue::Policy::Lockstep : ps2r::gs::DrawIrThreadQueue::Policy::Pipelined,
                4096, pipeline_vram.get());
            if (knobs.gs_queue_stage) gs_queue->SetStageDepth(knobs.gs_queue_stage);
            // Wait for the prompt capture only when no complete frame is queued behind this one: a consumer
            // already a frame behind needs the CPU/GPU overlap to catch up. Partial records of the next
            // frame do not count.
            if (core.play_mode && cfg.video.capture_wait_ms > 0)
                video.SetPresentCaptureGate([q = gs_queue.get()] { return q->PendingFrames() == 0; });
            if (pipeline_vram) {
                gs_state.Vram().SetMirror(gs_queue->ProducerMirror());
                // A local->local copy under frame skip never waits: GPU target pixels come from the last
                // completed frame, and the staleness is counted (gs_queue l2l_*=).
                gs_state.Xfer().SetLocalToLocalPrepare(
                    [this](ps2r::gs::VramImage::Span source, ps2r::gs::VramImage& image) {
                        return gs_queue->ServeLocalToLocalSource(source, image);
                    });
                // Frame skip depth 1: a second pending frame would be rendered only for the 60 Hz pacer to
                // supersede it. NATIVEEE_GS_SKIP_DEPTH=2 overrides.
                gs_queue->SetFrameSkip(true, knobs.gs_skip_depth ? knobs.gs_skip_depth : 1u);
            }
            video.ReleaseCurrent();
            gs_queue->Start([this] {
                ps2r::platform::host::PinCurrentThreadInteractive("ps2r-gs");
                video.EnsureCurrent();
            });
            PS2R_LOG(HOST, Info, "GS queue: consumer thread started (%s%s)", gs_queue_lockstep ? "lockstep, shared VRAM image" : "pipelined",
                     pipeline_vram ? ", split VRAM image, frame skip" : "");
        }
    }
    ps2r::gs::DrawIrSink* gs_sink_head =
        gs_queue ? static_cast<ps2r::gs::DrawIrSink*>(gs_queue.get()) : (consumer ? static_cast<ps2r::gs::DrawIrSink*>(&latency_tap) : nullptr);
    // NATIVEEE_GS_CAPTURE=<file.gsstream>: records exactly the stream the consumer receives (after the
    // shadow split), in front of the queue.
    if (knobs.gs_capture && gs_sink_head) {
        ps2r::gs::GsStreamRecorder::Hooks ch;
        ch.upload_pixels = [this] { return &gs_state.Xfer().LastUploadPixels(); };
        ch.crtc = [this] {
            using P = ps2r::gs::GsPrivRegs;
            const P& p = gs_state.Priv();
            return ps2r::gs::CrtcClassifier::Classify(p.Read(P::kPmode), p.Read(P::kDispfb1), p.Read(P::kDispfb2),
                                                      p.Read(P::kDisplay1), p.Read(P::kDisplay2));
        };
        // <file>[,from=<frame>][,count=<frames>]
        std::string rest = *knobs.gs_capture, path;
        uint64_t from = 0, count = 0;
        for (int field = 0; !rest.empty(); ++field) {
            const size_t comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            if (field == 0) path = item;
            else if (item.rfind("from=", 0) == 0) from = std::strtoull(item.c_str() + 5, nullptr, 10);
            else if (item.rfind("count=", 0) == 0) count = std::strtoull(item.c_str() + 6, nullptr, 10);
        }
        gs_capture = std::make_unique<ps2r::gs::GsStreamRecorder>(gs_sink_head, gs_state.Vram().GetMirror(), std::move(ch));
        gs_capture->Arm(path, &gs_state.Vram(), from, count);
        gs_state.Vram().SetMirror(gs_capture.get());
        gs_sink_head = gs_capture.get();
        PS2R_LOG(HOST, Info, "GS capture: %s from frame %llu, %llu frames (0 = all)", path.c_str(),
                 static_cast<unsigned long long>(from), static_cast<unsigned long long>(count));
    }
    if (gs_sink_head) {   // always in the chain: gs_pages= counts every draw and transfer
        // The shadow rewrite goes after the trace, so the trace sees the game's own draws.
        shadow_split.next = gs_sink_head;
        shadow_split.enabled = knobs.shadow_split;
        gs_sink_head = &shadow_split;
        vert_trace.next = gs_sink_head;
        vert_trace.skip_from = knobs.gs_skip_from;
        vert_trace.skip_to = knobs.gs_skip_to;
        vert_trace.vert_cap = knobs.gs_verts_cap;
        vert_trace.SetSkipClasses(knobs.gs_skip_class ? knobs.gs_skip_class->c_str() : nullptr);
        gs_sink_head = &vert_trace;
    }
    draw_sink.SetNext(gs_sink_head);
    // Only Draws()/Frames() of the counting sink are read, so its FNV chain is off; the DrawIr snapshot
    // extras are computed only while a .gsstream capture is armed. Both would cost ~1 ms per frame.
    draw_sink.SetHashing(false);
    gs_state.Flush().SetRecordSnapshot(knobs.gs_capture.has_value());
    gs_state.SetSink(&draw_sink);
    // NATIVEEE_GS_RT_DUMP=<key>,<fbp hex>[,cap=<n>][,at=<i>[+<i>...]][,noclass][,nofbmsk][,nopost]
    if (knobs.gs_rt_dump) {
        std::string rest = *knobs.gs_rt_dump;
        for (int field = 0; !rest.empty(); ++field) {
            const size_t comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            if (field == 0) rt_dump_key = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 10));
            else if (field == 1) rt_dump_req.fbp = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
            else if (item.rfind("cap=", 0) == 0) rt_dump_req.cap = static_cast<uint32_t>(std::strtoul(item.c_str() + 4, nullptr, 10));
            else if (item == "noclass") rt_dump_req.classes = false;
            else if (item == "nofbmsk") rt_dump_req.fbmsk_class = false;
            else if (item == "nopost") rt_dump_req.post = false;
            else if (item.rfind("at=", 0) == 0)
                for (const char* q = item.c_str() + 3; *q;) {
                    char* e = nullptr;
                    rt_dump_req.ordinals.push_back(static_cast<uint32_t>(std::strtoul(q, &e, 10)));
                    q = (e && *e == '+') ? e + 1 : (e ? e : q + std::strlen(q));
                    if (e == q) break;
                }
        }
    }
    if (knobs.gs_texres) {
        char* end = nullptr;
        texres_filter.enabled = true;
        if (knobs.gs_texres->rfind("*", 0) == 0) {   // every read, counts only
            texres_filter.keep_bytes = false;
        } else {
            texres_filter.keep_bytes = true;
            texres_filter.tbp = static_cast<uint32_t>(std::strtoul(knobs.gs_texres->c_str(), &end, 16));
            if (end && *end == ',') texres_filter.cbp = static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 16));
        }
    }
    if (video_live && !frame_dump.Open(core.dump_frames_dir))
        std::fprintf(stderr, "nativeee: --dump-frames %s: cannot create the directory (no dumps)\n", core.dump_frames_dir.c_str());
    // NATIVEEE_DUMP_EVERY=N: only every Nth present key. NATIVEEE_DUMP_FROM=M: none before key M.
    dump_every = knobs.dump_every;
    dump_from = knobs.dump_from;
    // Slot 0 (PresentRendezvous): the rendezvous ticket, then the frame's VSync job, which nothing else
    // pushes. It travels the chain to DrawPipeline::OnFrameEnd (grab, display quad, present); in lockstep the
    // push returns once consumed. Then CSR.VSINT. Under the GIF drain thread the job goes through
    // ordered_submit so it lands behind the frame's packets.
    core.sequencer.SetStep(BoundarySequencer::Step::PresentRendezvous, [this, &core](CpuContext&) {
        const uint32_t ticket = core.rendezvous.Issue();
        if (!core.rendezvous.WaitFor(ticket, core.running)) return false;
        // Mark the boundary lane before the push: the drain thread may deliver the record before this returns.
        if (latency_tap.timeline) latency_tap.timeline->Note(FrameTimeline::Lane::Boundary, gs_frame_boundaries);
        // Capture the frame ordinal here: ordered_submit may return before the work runs, and the EE
        // increments gs_frame_boundaries right after, so reading it inside the work would race.
        const uint64_t frame = gs_frame_boundaries;
        const auto push = [this, frame] {
            ps2r::gs::JobRec vs{};
            vs.type = static_cast<uint32_t>(ps2r::gs::JobKind::VSync);
            vs.seq = gs_state.Seq();
            vs.src_frame = static_cast<uint32_t>(frame);
            vs.arg1 = 1;   // EE game-frame boundary
            vs.vram_epoch = gs_state.Vram().UploadEpoch();
            draw_sink.OnFrame(vs);
            // The frame's packets are parsed and its record is queued.
            if (latency_tap.timeline) latency_tap.timeline->Note(FrameTimeline::Lane::Drained, vs.src_frame);
        };
        if (ordered_submit) ordered_submit(push);
        else push();
        if (gs_queue && !core.sequencer.WallLocked()) gs_queue->WaitFrame(gs_queue->FrameSeq());
        ++gs_frame_boundaries;
        gs_state.Priv().SetVsint();
        return true;
    });
}

}  // namespace ps2r::title::sly2
