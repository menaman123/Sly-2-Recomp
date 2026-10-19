// games/scus_97316_sly2_band_of_thieves/hle/leg_gs.cpp -- LegGs (leg_gs.h): GsState, the video stack, the
// DrawIR queue / consumer chain, the trace / counting sinks, the frame dump and the slot-0 step.
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
    // The GS privileged page (CSR / IMR / DISPFB ...) is the real register file, so the game's FINISH
    // handshake (CSR |= 2 W1C, GsPutIMR, GIF chain, WaitSema signalled by the GS INTC handler) is driven
    // by the modelled transfer: Dmac::StartChannel(GIF) -> ChainWalker -> GifParser ->
    // GsState::WriteRegister(FINISH) -> GsPrivRegs::Finish() -> the observer (leg_interrupts.cpp) -> Intc kGs.
    gs_state.SetQuirks(ps2r::gs::VertexKickQuirks{title.quirks.round_sprite_mode});
    gs_queue_split_image = core.play_mode && cfg.video.backend == ps2r::VideoBackend::Gl &&
                           cfg.gs.queue == ps2r::GsQueue::Pipelined;
    if (gs_queue_split_image) pipeline_vram = std::make_unique<ps2r::gs::VramImage>();
    video_live = core.want_video && cfg.video.backend == ps2r::VideoBackend::Gl &&
                 video.Create(cfg.video, "Sly 2 -- NativeEE (GS)", pipeline_vram ? *pipeline_vram : gs_state.Vram());
    if (core.want_video && !video_live) std::fprintf(stderr, "nativeee: no GL video stack (video=stub; see the log)\n");
    // gs.queue=inline: the consumer runs on the pushing EE thread (the context re-bound per
    // thread); otherwise the DrawIR ring carries the stream to the consumer on its own host
    // thread, which owns the GL context. Headless (dump): LOCKSTEP -- every push returns once
    // consumed, the shared VramImage interleaving is the inline one. Play: PIPELINED over the
    // split image with FRAME SKIP (the clock contract, rule 2: the renderer never blocks the
    // game -- one pending frame, the older dropped; pushes never wait for ring room; the
    // boundary does not WaitFrame).
    gs_queue_thread = video_live && cfg.gs.queue != ps2r::GsQueue::Inline;
    gs_queue_lockstep = !gs_queue_split_image;
    if (video_live) {
        // Play: the present capture (the session window's image) is read one present late behind a fence
        // instead of draining the GPU at every present (consumer_b B1); the headless dump keeps the
        // synchronous read (p<key>.ppm = that present).
        if (core.play_mode && knobs.gs_consumer_levers) video.SetPresentCaptureDeferred(true);
        video.SetSwapHiddenWindow(!knobs.gs_consumer_levers);   // levers on: the hidden window's swap is a flush
        video.Pipe()->Cache().Readback().SetOfferEnabled(knobs.gs_consumer_levers);
        // The display grab when no target sits at the scanout base: the CRTC's own read --
        // decode the DISPFB tuple out of local memory (an uploaded movie frame the title points
        // DISPFB at without drawing shows); the most-recently-drawn target only when the tuple
        // is unknown.
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
        consumer = std::make_unique<ps2r::gs::DrawIrQueueConsumer>(*video.Pipe(), std::move(hooks));
        // Play: the frames-lane hash is taken where the grab is made (the consumer thread, at
        // the present scale); the wall-locked boundary reads the header + hash, never the pixels.
        if (!gs_queue_lockstep) consumer->SetFrameHashScale(video.Scale());
        if (gs_queue_thread) {
            ps2r::gs::DrawIrThreadQueue::Hooks qh;
            qh.upload_pixels = [this] { return &gs_state.Xfer().LastUploadPixels(); };
            qh.crtc = crtc_now;
            gs_queue = std::make_unique<ps2r::gs::DrawIrThreadQueue>(
                *consumer, std::move(qh),
                gs_queue_lockstep ? ps2r::gs::DrawIrThreadQueue::Policy::Lockstep : ps2r::gs::DrawIrThreadQueue::Policy::Pipelined,
                4096, pipeline_vram.get());
            if (pipeline_vram) {
                gs_state.Vram().SetMirror(gs_queue->ProducerMirror());
                // A local->local copy's source under frame skip: never wait -- the EE image is
                // authoritative for what the EE wrote; a GPU target's pixels come from the
                // consumer's last completed frame, staleness counted (gs_queue l2l_*=).
                gs_state.Xfer().SetLocalToLocalPrepare(
                    [this](ps2r::gs::VramImage::Span source, ps2r::gs::VramImage& image) {
                        return gs_queue->ServeLocalToLocalSource(source, image);
                    });
                gs_queue->SetFrameSkip(true);
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
        gs_queue ? static_cast<ps2r::gs::DrawIrSink*>(gs_queue.get()) : static_cast<ps2r::gs::DrawIrSink*>(consumer.get());
    // NATIVEEE_GS_CAPTURE=<file.gsstream> (leg_knobs.h): the consumer-stream capture sits right in front of the
    // queue / consumer -- the file holds exactly the stream the consumer receives (after the shadow split) -- and
    // takes over the EE image's mirror, forwarding to the queue's producer mirror (the split image) or to none.
    if (knobs.gs_capture && gs_sink_head) {
        ps2r::gs::GsStreamRecorder::Hooks ch;
        ch.upload_pixels = [this] { return &gs_state.Xfer().LastUploadPixels(); };
        ch.crtc = [this] {
            using P = ps2r::gs::GsPrivRegs;
            const P& p = gs_state.Priv();
            return ps2r::gs::CrtcClassifier::Classify(p.Read(P::kPmode), p.Read(P::kDispfb1), p.Read(P::kDispfb2),
                                                      p.Read(P::kDisplay1), p.Read(P::kDisplay2));
        };
        // <file>[,from=<frame>][,count=<frames>]: open at that frame boundary (the image then), close after count.
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
    // NATIVEEE_GS_VERTS / NATIVEEE_GS_VERTS_CAP / NATIVEEE_GS_SKIP_FROM / NATIVEEE_GS_SKIP_TO / NATIVEEE_GS_SKIP_CLASS: vert_trace_sink.h.
    if (gs_sink_head) {   // always in the chain: the gs_pages= census counts every draw / transfer
        // The stencil-shadow counter rewrite sits between the trace (which sees the game's draws) and the renderer.
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
    gs_state.SetSink(&draw_sink);
    // NATIVEEE_GS_RT_DUMP=<key>,<fbp hex>[,cap=<n>][,at=<i>[+<i>...]][,noclass][,nofbmsk][,nopost] (leg_gs.h).
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
    // NATIVEEE_GS_TEXRES=<tbp hex>[,<cbp hex>] (leg_gs.h).
    if (knobs.gs_texres) {
        char* end = nullptr;
        texres_filter.enabled = true;
        if (knobs.gs_texres->rfind("*", 0) == 0) {   // every read (no bytes kept: the census only)
            texres_filter.keep_bytes = false;
        } else {
            texres_filter.keep_bytes = true;
            texres_filter.tbp = static_cast<uint32_t>(std::strtoul(knobs.gs_texres->c_str(), &end, 16));
            if (end && *end == ',') texres_filter.cbp = static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 16));
        }
    }
    // --dump-frames <dir>: the Verify slot writes f<key>.ppm / p<key>.ppm there (hle/frame_dump.h).
    if (video_live && !frame_dump.Open(core.dump_frames_dir))
        std::fprintf(stderr, "nativeee: --dump-frames %s: cannot create the directory (no dumps)\n", core.dump_frames_dir.c_str());
    // NATIVEEE_DUMP_EVERY=N (boot triage): write only every Nth present key (1 = all; long runs).
    // NATIVEEE_DUMP_FROM=M (boot triage): no dumps before present key M (a dense window late in a
    // long run without the hundreds of frames before it).
    dump_every = knobs.dump_every;
    dump_from = knobs.dump_from;
    // Slot 0 (PresentRendezvous): the Phase-3 ticket, then the GS frame boundary -- the VSync
    // job the engine does not push itself (nobody else marks the EE game frame): the counting
    // sink hands it down the chain to the consumer (DrawPipeline::OnFrameEnd: the grab, the
    // display quad, the present). Lockstep / inline: the push returns once the frame was
    // consumed, so the Verify slot reads a complete grab. Then CSR.VSINT. The GS SIGNAL /
    // FINISH causes are raised in-line by the interrupt observer (leg_interrupts.cpp) -- not
    // re-raised here.
    // Under the GIF drain thread the push travels as an ordered arbiter slot (ordered_submit) so it lands
    // behind the frame's packets on the chain's one producer; the seq / epoch stamps are read there.
    core.sequencer.SetStep(BoundarySequencer::Step::PresentRendezvous, [this, &core](CpuContext&) {
        const uint32_t ticket = core.rendezvous.Issue();
        if (!core.rendezvous.WaitFor(ticket, core.running)) return false;
        const auto push = [this] {
            ps2r::gs::JobRec vs{};
            vs.type = static_cast<uint32_t>(ps2r::gs::JobKind::VSync);
            vs.seq = gs_state.Seq();
            vs.src_frame = static_cast<uint32_t>(gs_frame_boundaries);
            vs.arg1 = 1;   // the EE game-frame boundary
            vs.vram_epoch = gs_state.Vram().UploadEpoch();
            draw_sink.OnFrame(vs);
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
