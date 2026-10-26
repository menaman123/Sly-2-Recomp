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
#include "engine/platform/host/platform.h"
#include "engine/sound/mixer.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

namespace {

constexpr uint64_t kGuestJoinDeadlineMs = 5000;

}  // namespace

LegVerifySlot::LegVerifySlot(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegGs& gs,
                             LegMtvu& mtvu, LegServices& services, LegAudio& audio, LegPlay& play, LegSaves& saves, LegPark& park,
                             LegCensus& census, TextWatch& text_watch)
    : verify_spans{{core.ram.Data(), core.ram.Size()}, {core.spad.Data(), core.spad.Size()}},
      tick_thread(core.tick_source, core.sequencer.Ticks(), core.protocol),
      session_workers(ThreadManager::Instance(), core.running, [this] { tick_thread.Stop(); }, [&core] { core.protocol.NotifyTick(); }),
      display_sequence(cfg.harness.display_sequence),
      cfg_(cfg), host_(host), knobs_(knobs), core_(core), gs_(gs), mtvu_(mtvu), services_(services), audio_(audio), play_(play),
      saves_(saves), park_(park), census_(census), text_watch_(text_watch) {
    // Only a harness or a frame dump reads the display grab; a player's session skips it.
    gs_.grab_wanted.store(host_.Armed() || gs_.frame_dump.Enabled(), std::memory_order_relaxed);
    // A boundary counts in the window of its first field's wall edge, not of when it was answered.
    play.clock_census.SetEdgeLookup([this](uint64_t tick, std::chrono::steady_clock::time_point* edge) {
        int64_t ns = 0;
        if (!tick_thread.EdgeAt(tick, &ns)) return false;
        *edge = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ns));
        return true;
    });
    // NATIVEEE_TEX_DUMP=0,<to>: arm before the first frame, since a cold-restored state decodes the whole
    // scene in its first frame.
    if (knobs.tex_dump_to && knobs.tex_dump_from == 0 && gs.video.Pipe()) {
        ps2r::gs::TextureDumpSink& sink = gs.video.Pipe()->Resolver().DumpSink();
        const std::string dir = (gs.frame_dump.Enabled() ? gs.frame_dump.Dir() : cfg.harness.out_dir) + "/textures";
        if (!sink.Arm(dir)) std::fprintf(stderr, "nativeee: NATIVEEE_TEX_DUMP: cannot create %s\n", dir.c_str());
    }
    // harness.inject_key=<KEY>@N[-M][,...]: KEY is F1..F12 or an SDL key name (Escape, Space, ...).
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
    census.SetDisplaySequence(&display_sequence);
    // The mem lane hashes 34 MB of guest RAM every 60 boundaries (~40 ms on the boundary thread), so it is
    // off in play unless NATIVEEE_MEM_LANE forces it.
    host.SetMemLane(knobs.mem_lane_set ? knobs.mem_lane_on : !core.play_mode);
    core.sequencer.SetStep(BoundarySequencer::Step::Verify, [this](CpuContext& ctx) {
        VerifyStep(ctx);
        return true;
    });
    if (play.session) {
        ps2r::app::SessionRuntime::Hooks hooks;
        // On main at the close request: cancel only. Main keeps pumping the window and enforcing the
        // shutdown deadline; the boundary thread sees Boundary() == false and unwinds.
        hooks.stop = [this] { session_workers.Cancel(); };
        // Pause (F9 / Escape): stop the field ticker so wall edges don't pile up behind a paused game, and
        // pause the audio device on main. Paced play only.
        hooks.pause = [this](bool paused) {
            if (paused) tick_thread.Stop(); else if (core_.play_mode && gs_.video_live && !knobs_.unpaced) tick_thread.Start();
            if (audio_.audio_sink.Running() && play_.session) {
                if (!play_.session->OnMain([this, paused] { audio_.audio_sink.SetPaused(paused); }) && !play_.session->Closing())
                    std::fprintf(stderr, "nativeee: pause: the audio owner (main) did not take the %s\n", paused ? "pause" : "resume");
            }
            ++pauses;
        };
        hooks.stopped = [this] {
            JoinLadder();
            census_.Write();
            park_.closer.Close("none", 0, 0, "window_closed");
            std::fflush(stdout);
        };
        // F5 / F8 must be bound before the hooks are set.
        saves.Bind(hooks, session_workers, tick_thread, audio);
        play.session->SetHooks(std::move(hooks));
        // The session's boundary work (pause, step, F5, F8, close) runs in the completed-boundary consumer,
        // after the vblank wait's body has returned: the F5 capture needs the completed caller chain. False
        // means the window closed or an F8 restore is unwinding this thread.
        core.game_boundary.SetConsumer([this](uint64_t frame) {
            ++park_.boundary_consumer_calls;
            if (play_.session && !play_.session->Boundary(frame)) throw ThreadExitUnwind{};
        });
    }
}

void LegVerifySlot::EndLegFromBoundary(const char* detail) {
    census_.Write();
    park_.closer.Close("none", 0, ThreadManager::Instance().CurrentTid(), detail);
    std::fflush(stdout);
    if (play_.session) play_.session->RequestClose();
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
    if (!display_sequence.Empty()) {
        display_sequence.OnBoundary(at_boundary, audio_.audio_ring.padded(), [this, at_boundary](const ps2r::app::DisplaySequence::Step& step) {
            const uint64_t id = play_.session->Controls().Submit(step.action, step.value);
            std::fprintf(stderr, "nativeee: harness.display_sequence %s at boundary %llu: %s\n", step.mode.c_str(),
                         static_cast<unsigned long long>(at_boundary), id ? "submitted to the presenter" : "refused by SessionControls");
            return id;
        }, [this](uint64_t id) {
            const auto snapshot = play_.session->Controls().Read();
            for (const auto& result : snapshot.recent)
                if (result.request.id == id) return true;
            return false;
        });
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

void LegVerifySlot::VerifyStep(CpuContext& ctx) {
    using ps2r::ringlog::RingLog;
    ps2r::sound::Mixer& mixer = services_.mixer;
    HarnessVerbs(park_.boundaries);
    // Play-mode field pump: catch the mixer up to one quantum per domain tick, on the device ring only; the
    // boundary's own quantum below is the hashed one (exactly 800 frames per boundary). Each catch-up field
    // also runs one IOP quantum, so 989snd refills a stream half within one field. With only one IOP
    // quantum per boundary, audio corrupted below 60 boundaries/s.
    if (audio_.audio_sink.Running()) {
        const uint64_t now = core_.domain.DomainTick();
        if (audio_.audio_pumped_tick == 0) audio_.audio_pumped_tick = now;
        while (audio_.audio_pumped_tick + 1 < now) {
            mixer.Pump();
            play_.PushAudioRing(mixer.Output());
            ++audio_.audio_pumped_tick;
            ++audio_.audio_field_pumps;
            if (audio_.field_quantum) audio_.field_quantum(ctx);
        }
        audio_.audio_pumped_tick = now;
    }
    mixer.Pump();
    play_.PushAudioRing(mixer.Output());
    audio_.TapMixer(mixer.Output());
    const uint64_t boundary = host_.OnBoundary(mixer.Output(), ps2r::sound::Mixer::kFramesPerVsync, verify_spans);
    park_.boundaries = boundary + 1;
    text_watch_.OnBoundary(park_.boundaries);
    park_.NoteBoundaryWall();
    // Clock census (wall-locked boundaries only): this boundary and the domain tick it completed at.
    if (core_.sequencer.WallLocked()) {
        play_.clock_census.NoteBoundary(core_.domain.DomainTick(), core_.sequencer.FastForwardFields());
        // Process memory counters for the same window.
        play_.clock_census.NoteMemory(ps2r::host::ProcessPageFaults(), ps2r::host::ProcessFootprintBytes());
    }
    RingLog::SetFrame(static_cast<uint32_t>(park_.boundaries));
    if (gs_.consumer) {
        const uint32_t present_key = host_.PresentKey(0, boundary);
        if (knobs_.gs_verts_key) gs_.vert_trace.on.store(present_key + 1 == knobs_.gs_verts_key);
        if (gs_.rt_dump_key && gs_.video.Pipe() && !core_.sequencer.WallLocked()) {   // lockstep: consumer idle
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
        // NATIVEEE_GS_TEXRES: set once the pipeline exists.
        if (gs_.texres_filter.enabled && !gs_.texres_armed && gs_.video.Pipe() && !core_.sequencer.WallLocked()) {
            gs_.video.Pipe()->Resolver().Trace().SetFilter(gs_.texres_filter);
            gs_.texres_armed = true;
        }
        // NATIVEEE_TEX_DUMP=<from>,<to>: armed at the first boundary at or past `from` (a cold-restored
        // state may start past it), disarmed after the boundary closing `to`.
        if (knobs_.tex_dump_to && gs_.video.Pipe() && !core_.sequencer.WallLocked()) {
            ps2r::gs::TextureDumpSink& sink = gs_.video.Pipe()->Resolver().DumpSink();
            if (present_key + 1 >= knobs_.tex_dump_from && !sink.Armed() && !tex_dump_done_) {
                const std::string dir = (gs_.frame_dump.Enabled() ? gs_.frame_dump.Dir() : cfg_.harness.out_dir) + "/textures";
                if (!sink.Arm(dir)) std::fprintf(stderr, "nativeee: NATIVEEE_TEX_DUMP: cannot create %s\n", dir.c_str());
            } else if (sink.Armed() && (present_key >= knobs_.tex_dump_to ||
                                        (cfg_.harness.frames > 0 && boundary + 1 >= static_cast<uint64_t>(cfg_.harness.frames)))) {
                sink.Disarm();
                tex_dump_done_ = true;
                const ps2r::gs::TextureDumpSink::Stats& st = sink.GetStats();
                std::fprintf(stderr,
                             "nativeee: NATIVEEE_TEX_DUMP keys %u..%u: decodes=%llu distinct=%llu oracle_b_bad=%llu serves=%llu "
                             "files=%llu failed=%llu\n",
                             knobs_.tex_dump_from, knobs_.tex_dump_to, static_cast<unsigned long long>(st.decodes),
                             static_cast<unsigned long long>(st.distinct), static_cast<unsigned long long>(st.oracle_b_bad),
                             static_cast<unsigned long long>(st.serves), static_cast<unsigned long long>(st.written),
                             static_cast<unsigned long long>(st.failed));
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
        // NATIVEEE_GS_ROUTES=<key>[,...]: per-draw route trace, CRTC registers and grab source of that frame,
        // as routes_<key>.txt.
        if (knobs_.gs_routes && !wall_locked) {
            const std::string want = std::string(",") + *knobs_.gs_routes + ",";
            if (want.find("," + std::to_string(present_key) + ",") != std::string::npos) WriteRoutes(present_key, *fr);
        }
    }
    if (cfg_.harness.frames > 0 && boundary + 1 >= static_cast<uint64_t>(cfg_.harness.frames)) EndLegFromBoundary("frames_cap");
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
    if (gs_.texres_armed) {
        const ps2r::gs::TexresTrace& tr = gs_.video.Pipe()->Resolver().Trace();
        if (std::FILE* tf = std::fopen((dir + "/texres_" + std::to_string(present_key) + ".txt").c_str(), "w")) {
            tr.Write(tf);
            std::fclose(tf);
        }
        const uint32_t n = tr.WriteBytes(dir, "texres_" + std::to_string(present_key) + "_");
        std::fprintf(stderr, "nativeee: NATIVEEE_GS_TEXRES key %u: %zu reads, %u byte files in %s\n", present_key,
                     tr.LastFrame().size(), n, dir.c_str());
    }
    // The GS local memory at that frame (4 MiB, native layout).
    if (std::FILE* vf = std::fopen((dir + "/vram_" + std::to_string(present_key) + ".bin").c_str(), "wb")) {
        std::fwrite(gs_.gs_state.Vram().Bytes(), 1, ps2r::gs::VramImage::kBytes, vf);
        std::fclose(vf);
    }
}

void LegVerifySlot::JoinLadder() {
    // Dependency order: guest workers first (they feed everything below; one stuck in a wait the cancel
    // cannot reach is abandoned after the deadline and named), then the pacer, the GS backlog and the
    // audio device.
    const auto t0 = std::chrono::steady_clock::now();
    // In play, each step is also logged so main's shutdown watchdog can name the step that hung.
    ps2r::app::ShutdownCensus* shutdown = play_.session ? &play_.session->Shutdown() : nullptr;
    auto enter = [&](const char* step) { if (shutdown) shutdown->Enter(step); };
    auto note = [&](const char* step) {
        if (shutdown) shutdown->Leave();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        park_.shutdown_steps += (park_.shutdown_steps.empty() ? "" : ",") + std::string(step) + "@" + std::to_string(ms) + "ms";
    };
    enter("guest_join");
    if (session_workers.Join(kGuestJoinDeadlineMs, &park_.guest_join_abandoned) > 0)
        std::fprintf(stderr, "nativeee: shutdown: guest workers abandoned after %llu ms: %s\n",
                     static_cast<unsigned long long>(kGuestJoinDeadlineMs), park_.guest_join_abandoned.c_str());
    note("guest_join");
    enter("pacer");
    tick_thread.Stop();
    note("pacer");
    enter("savestate_writer");
    saves_.JoinWriter();
    note("savestate_writer");
    enter("mtvu_worker");
    if (mtvu_.mtvu) mtvu_.mtvu->Stop();
    note("mtvu_worker");
    enter("vu1_aot");
    if (mtvu_.vu1_aot_cache) mtvu_.vu1_aot_cache->Close(/*drain=*/false);
    note("vu1_aot");
    enter("vu1_aot_codegen");
    if (mtvu_.vu1_aot_builder) mtvu_.vu1_aot_builder->Close();   // finishes a running compile, drops the queue
    note("vu1_aot_codegen");
    enter("gs_quiesce");
    if (gs_.gs_queue) gs_.gs_queue->Quiesce();
    note("gs_quiesce");
    enter("audio_close");
    if (audio_.audio_sink.Running()) audio_.audio_sink.Close();
    audio_.audio_ring.SetPullTap(nullptr, nullptr);   // safe: Close waited for the device callback
    audio_.audio_dump.Close();
    note("audio_close");
}

}  // namespace ps2r::title::sly2
