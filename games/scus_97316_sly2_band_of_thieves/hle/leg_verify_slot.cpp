// games/scus_97316_sly2_band_of_thieves/hle/leg_verify_slot.cpp -- LegVerifySlot (leg_verify_slot.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_verify_slot.h"

#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "engine/core/log/ring_log.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/sound/mixer.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

namespace {

constexpr uint64_t kGuestJoinDeadlineMs = 5000;

}  // namespace

LegVerifySlot::LegVerifySlot(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegGs& gs,
                             LegMtvu& mtvu, LegServices& services, LegAudio& audio, LegPlay& play, LegPark& park, LegCensus& census)
    : verify_spans{{core.ram.Data(), core.ram.Size()}, {core.spad.Data(), core.spad.Size()}},
      tick_thread(core.tick_source, core.sequencer.Ticks(), core.protocol),
      session_workers(ThreadManager::Instance(), core.running, [this] { tick_thread.Stop(); }, [&core] { core.protocol.NotifyTick(); }),
      cfg_(cfg), host_(host), knobs_(knobs), core_(core), gs_(gs), mtvu_(mtvu), services_(services), audio_(audio), play_(play),
      park_(park), census_(census) {
    // --set harness.inject_key=<KEY>@N[-M][,...]: KEY = F1..F12 or an SDL key name (Escape, Space ...).
    // The validator pinned the format; an unknown name is refused here.
    for (std::string rest = cfg.harness.inject_key; !rest.empty();) {
        const size_t comma = rest.find(',');
        const std::string item = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const size_t at = item.find('@');
        HarnessKeyPress k{};
        k.name = item.substr(0, at);
        const std::string frames = item.substr(at + 1);
        const size_t dash = frames.find('-');
        k.frame = std::strtoull(frames.substr(0, dash).c_str(), nullptr, 10);
        k.until = dash == std::string::npos ? k.frame : std::strtoull(frames.substr(dash + 1).c_str(), nullptr, 10);
        k.scancode = static_cast<int>(SDL_GetScancodeFromName(k.name.c_str()));
        if (k.scancode == SDL_SCANCODE_UNKNOWN) {
            std::fprintf(stderr, "nativeee: harness.inject_key: unknown key name '%s'\n", k.name.c_str());
            failed_ = true;
            return;
        }
        harness_keys.push_back(k);
    }
    core.sequencer.SetStep(BoundarySequencer::Step::Verify, [this](CpuContext&) {
        VerifyStep();
        return true;
    });
    if (play.session) {
        ps2r::app::SessionRuntime::Hooks hooks;
        // Main, at the close request: cancel only (no join here -- main keeps pumping the
        // window and enforcing the shutdown deadline); the boundary thread sees
        // Boundary() == false and unwinds, the waiters wake terminated.
        hooks.stop = [this] { session_workers.Cancel(); };
        // The game worker, after the cold boot returned: the join ladder, then the census.
        hooks.stopped = [this] {
            JoinLadder();
            census_.Write();
            park_.closer.Close("none", 0, 0, "window_closed");
            std::fflush(stdout);
        };
        play.session->SetHooks(std::move(hooks));
    }
}

void LegVerifySlot::EndLegFromBoundary(const char* detail) {
    census_.Write();
    park_.closer.Close("none", 0, ThreadManager::Instance().CurrentTid(), detail);
    std::fflush(stdout);
    if (play_.session) play_.session->RequestClose();   // main: hooks.stop -> the cancel below
    else session_workers.Cancel();
    throw ThreadExitUnwind{};
}

void LegVerifySlot::HarnessVerbs(uint64_t at_boundary) {
    ps2r::app::SessionRuntime* session = play_.session.get();
    if (!session) return;
    for (HarnessKeyPress& k : harness_keys) {
        if (!k.pressed && at_boundary >= k.frame) {
            k.pressed = true;
            std::fprintf(stderr, "nativeee: harness.inject_key %s at boundary %llu: %s\n", k.name.c_str(),
                         static_cast<unsigned long long>(at_boundary),
                         session->InjectKey(k.scancode, true) ? "SDL_KEYDOWN pushed into SDL's queue" : "SDL_PushEvent refused");
        }
        if (k.pressed && !k.released && at_boundary > k.until) {
            k.released = true;
            session->InjectKey(k.scancode, false);
            std::fprintf(stderr, "nativeee: harness.inject_key %s released at boundary %llu\n", k.name.c_str(),
                         static_cast<unsigned long long>(at_boundary));
        }
    }
    if (cfg_.harness.close_at_frame > 0 && !close_pushed && at_boundary >= static_cast<uint64_t>(cfg_.harness.close_at_frame)) {
        close_pushed = true;
        if (cfg_.harness.close_after_ms > 0) {
            session->ArmCloseAfter(uint64_t(cfg_.harness.close_after_ms) * 1000000ull);
            std::fprintf(stderr, "nativeee: harness.close_at_frame at boundary %llu: SDL_QUIT armed on main in %d ms\n",
                         static_cast<unsigned long long>(at_boundary), cfg_.harness.close_after_ms);
        } else {
            std::fprintf(stderr, "nativeee: harness.close_at_frame at boundary %llu: %s\n", static_cast<unsigned long long>(at_boundary),
                         session->InjectClose() ? "SDL_QUIT pushed into SDL's queue" : "SDL_PushEvent refused");
        }
    }
}

void LegVerifySlot::VerifyStep() {
    using ps2r::ringlog::RingLog;
    ps2r::sound::Mixer& mixer = services_.mixer;
    HarnessVerbs(park_.boundaries);
    // The play-mode FIELD PUMP (the first title's rule, ee_bootstrap_play.cpp): the mixer is caught up
    // to one quantum per domain tick, each pushed to the DEVICE ring only; the boundary's own pump
    // below is the hashed one (the lane keeps exactly 800 frames per boundary). Play only: the
    // headless lane is the boundary-only pump it always was.
    if (audio_.audio_sink.Running()) {
        const uint64_t now = core_.domain.DomainTick();
        if (audio_.audio_pumped_tick == 0) audio_.audio_pumped_tick = now;   // the first boundary anchors
        while (audio_.audio_pumped_tick + 1 < now) {
            mixer.Pump();
            play_.PushAudioRing(mixer.Output());
            ++audio_.audio_pumped_tick;
            ++audio_.audio_field_pumps;
        }
        audio_.audio_pumped_tick = now;
    }
    mixer.Pump();
    play_.PushAudioRing(mixer.Output());
    const uint64_t boundary = host_.OnBoundary(mixer.Output(), ps2r::sound::Mixer::kFramesPerVsync, verify_spans);
    park_.boundaries = boundary + 1;
    park_.NoteBoundaryWall();
    RingLog::SetFrame(static_cast<uint32_t>(park_.boundaries));
    if (gs_.consumer) {
        const uint32_t present_key = host_.PresentKey(0, boundary);
        if (knobs_.gs_verts_key) gs_.vert_trace.on.store(present_key + 1 == knobs_.gs_verts_key);
        if (gs_.rt_dump_key && gs_.video.Pipe() && !core_.sequencer.WallLocked()) {   // lockstep: the consumer is idle here
            ps2r::gs::TargetDump& td = gs_.video.Pipe()->Dump();
            if (present_key + 1 == gs_.rt_dump_key) {
                gs_.rt_dump_req.dir = gs_.frame_dump.Enabled() ? gs_.frame_dump.Dir() : cfg_.harness.out_dir;
                td.Arm(gs_.rt_dump_req);
            } else if (td.Armed()) {
                std::fprintf(stderr, "nativeee: NATIVEEE_GS_RT_DUMP key %u fbp 0x%x: %u files in %s\n", gs_.rt_dump_key,
                             gs_.rt_dump_req.fbp, td.Written(), gs_.rt_dump_req.dir.c_str());
                td.Disarm();
            }
        }
        const bool wall_locked = core_.sequencer.WallLocked();
        ps2r::gs::DrawPipeline::Frame wall_locked_grab;
        const ps2r::gs::DrawPipeline::Frame* fr = &gs_.consumer->LastFrame();
        if (wall_locked) {
            wall_locked_grab = gs_.consumer->LastFrameHeaderCopy();
            fr = &wall_locked_grab;
        }
        if (fr->grabbed) {
            ++gs_.grabs;
            if (fr->source == ps2r::gs::ScanoutComposer::Source::Mirror) ++gs_.grab_local_memory;
            if (fr->source_fallback) ++gs_.grab_fallback;
            if (wall_locked) host_.OnPresentHashed(present_key, fr->hashed ? fr->hash : 0, gs_.video.Renderer());
            else host_.OnPresent(present_key, fr->rgba.data(), fr->w, fr->h, gs_.video.Scale(), gs_.video.Renderer());
            if (!wall_locked && gs_.frame_dump.Enabled() && gs_.DumpWanted(present_key)) {
                gs_.frame_dump.Write("f", present_key, fr->w, fr->h, fr->rgba.data(), /*flip=*/false);
                std::vector<uint8_t> presented;
                int pw = 0, ph = 0;
                if (gs_.video.Device().ReadPresented(&presented, &pw, &ph)) gs_.frame_dump.Write("p", present_key, pw, ph, presented.data(), /*flip=*/true);
            }
        } else {
            host_.OnPresentHashed(present_key, 0, gs_.video.Renderer());
        }
        // NATIVEEE_GS_ROUTES=<key>[,<key>...] (boot triage): the pipeline's per-draw route trace of
        // that frame + the CRTC registers and the grab's source as <dump dir or harness out>/routes_<key>.txt.
        if (knobs_.gs_routes && !wall_locked) {
            const std::string want = std::string(",") + *knobs_.gs_routes + ",";
            if (want.find("," + std::to_string(present_key) + ",") != std::string::npos) WriteRoutes(present_key, *fr);
        }
    }
    if (cfg_.harness.frames > 0 && boundary + 1 >= static_cast<uint64_t>(cfg_.harness.frames)) EndLegFromBoundary("frames_cap");
    // The session's boundary (pause / step / the close): false = the window was closed --
    // the leg ends here with its census (the stop hook on main may be closing it too; the
    // closer closes exactly once) and the guest workers unwind.
    if (play_.session && !play_.session->Boundary(park_.boundaries)) EndLegFromBoundary("session_closed");
}

void LegVerifySlot::WriteRoutes(uint32_t present_key, const ps2r::gs::DrawPipeline::Frame& fr) {
    const std::string dir = gs_.frame_dump.Enabled() ? gs_.frame_dump.Dir() : cfg_.harness.out_dir;
    if (std::FILE* rf = std::fopen((dir + "/routes_" + std::to_string(present_key) + ".txt").c_str(), "w")) {
        using P = ps2r::gs::GsPrivRegs;
        const P& p = gs_.gs_state.Priv();
        std::fprintf(rf, "key=%u pmode=%s dispfb1=%s display1=%s dispfb2=%s display2=%s smode2=%s\n", present_key,
                     Hex(p.Read(P::kPmode)).c_str(), Hex(p.Read(P::kDispfb1)).c_str(), Hex(p.Read(P::kDisplay1)).c_str(),
                     Hex(p.Read(P::kDispfb2)).c_str(), Hex(p.Read(P::kDisplay2)).c_str(), Hex(p.Read(P::kSmode2)).c_str());
        const ps2r::gs::ScanoutComposer& sc = gs_.video.Pipe()->Scanout();
        std::fprintf(rf, "scanout_fbp=0x%x dispfb_fbw=%u psm=%u content=%dx%d grab=%s@0x%x fallback=%d %dx%d\n",
                     sc.ScanoutFbp(), sc.DispfbFbw(), sc.DispfbPsm(), sc.ContentW(), sc.ContentH(),
                     fr.source == ps2r::gs::ScanoutComposer::Source::Target ? "target"
                     : fr.source == ps2r::gs::ScanoutComposer::Source::Mirror ? "local_memory" : "none",
                     fr.source_fbp, fr.source_fallback ? 1 : 0, fr.w, fr.h);
        gs_.video.Pipe()->RouteTrace().Write(rf);
        std::fclose(rf);
    }
    // The GS local-memory image at that frame (4 MiB, native layout), for the texel check.
    if (std::FILE* vf = std::fopen((dir + "/vram_" + std::to_string(present_key) + ".bin").c_str(), "wb")) {
        std::fwrite(gs_.gs_state.Vram().Bytes(), 1, ps2r::gs::VramImage::kBytes, vf);
        std::fclose(vf);
    }
}

void LegVerifySlot::JoinLadder() {
    // In dependency order: the guest workers first (they produce for every consumer below; bounded --
    // a worker parked in a wait the cancel cannot reach is abandoned after the deadline and NAMED),
    // the pacer, the GS consumer's backlog (Quiesce: the census reads its counters after it drained;
    // the queue's own Stop/join and the video stack's teardown run in the frame's destructor order,
    // on main), the audio device, the census.
    const auto t0 = std::chrono::steady_clock::now();
    auto note = [&](const char* step) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        park_.shutdown_steps += (park_.shutdown_steps.empty() ? "" : ",") + std::string(step) + "@" + std::to_string(ms) + "ms";
    };
    if (session_workers.Join(kGuestJoinDeadlineMs, &park_.guest_join_abandoned) > 0)
        std::fprintf(stderr, "nativeee: shutdown: guest workers abandoned after %llu ms: %s\n",
                     static_cast<unsigned long long>(kGuestJoinDeadlineMs), park_.guest_join_abandoned.c_str());
    note("guest_join");
    tick_thread.Stop();
    note("pacer");
    if (mtvu_.mtvu) mtvu_.mtvu->Stop();   // drains its queue, joins, unbinds VIF1 (no producer is left)
    note("mtvu_worker");
    if (mtvu_.vu1_aot_cache) mtvu_.vu1_aot_cache->Close(/*drain=*/false);   // the live unit is quiet: no more kicks to observe
    note("vu1_aot");
    if (mtvu_.vu1_aot_builder) mtvu_.vu1_aot_builder->Close();   // a running compile finishes; queued images are dropped
    note("vu1_aot_codegen");
    if (gs_.gs_queue) gs_.gs_queue->Quiesce();
    note("gs_quiesce");
    if (audio_.audio_sink.Running()) audio_.audio_sink.Close();
    audio_.audio_ring.SetPullTap(nullptr, nullptr);   // the device callback has returned (Close waits for it)
    audio_.audio_dump.Close();
    note("audio_close");
}

}  // namespace ps2r::title::sly2
