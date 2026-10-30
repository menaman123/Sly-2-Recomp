#include "games/scus_97316_sly2_band_of_thieves/hle/leg_agent.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <utility>

#include "app/host/savestate_slots.h"
#include "app/host/session_files.h"
#include "engine/core/log/log.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/recomp_abi/host_app/gs_hle.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/savestate/thread_section.h"
#include "engine/gif/draw_attribution.h"
#include "engine/gs/vram/vram_image.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

using ps2r::app::AgentJson;
using ps2r::app::AgentServer;

LegAgent::LegAgent(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const LegKnobs& knobs, LegCore& core,
                   LegGs& gs, LegMtvu& mtvu, LegServices& services, LegAudio& audio, LegSaves& saves, LegPad& pad, LegPark& park,
                   LegVerifySlot& verify, const char* build_id)
    : cfg_(cfg), host_(host), core_(core), gs_(gs), audio_(audio), saves_(saves), park_(park),
      verify_(verify), build_id_(build_id ? build_id : "") {
    if (!knobs.agent_socket) return;
    if (core.play_mode) {
        std::fprintf(stderr, "nativeee: NATIVEEE_AGENT_SOCKET: the agent control plane runs headless only (add --headless; --dump-frames <dir> for screen.grab)\n");
        failed_ = true;
        return;
    }
    AgentServer::Bindings b;
    b.title = title.serial;
    b.elf_sha256 = title.elf_sha256;
    b.build = build_id_;
    b.memory.ram = core.ram.Data();
    b.memory.ram_size = static_cast<uint32_t>(core.ram.Size());
    b.memory.spad = core.spad.Data();
    b.memory.spad_size = static_cast<uint32_t>(core.spad.Size());
    b.memory.vram = gs.gs_state.Vram().Bytes();
    b.memory.vram_size = ps2r::gs::VramImage::kBytes;
    b.grab = [this](AgentServer::Grab* g, std::string* err) {
        if (!gs_.consumer) {
            *err = "no display grab in this run: start with --dump-frames <dir> (NATIVEEE_DUMP_FROM=99999999 writes no file) to arm the renderer";
            return false;
        }
        if (gs_.gs_queue) gs_.gs_queue->Sync();
        const ps2r::gs::DrawPipeline::Frame& fr = gs_.consumer->LastFrame();
        if (!fr.grabbed || fr.rgba.empty()) { *err = "nothing presented yet"; return false; }
        g->rgba = fr.rgba;
        g->w = fr.w;
        g->h = fr.h;
        return true;
    };
    b.draws = [this] { return Draws(); };
    b.state_save = [this](const std::string& path, int slot, std::string* out, std::string* err) { return StateSave(path, slot, out, err); };
    b.state_save_admissible = [this](std::string* why) { return StateSaveAdmissible(why); };
    b.state_load_prepare = [this](const std::string& path, int slot, std::string* resolved, std::string* err) {
        return StateLoadPrepare(path, slot, resolved, err);
    };
    b.knob_set = [this](const std::string& name, const AgentJson& value, std::string* err) { return KnobSet(name, value, err); };
    b.quit = [this] { verify_.EndLegFromAgent("agent_quit"); };
    b.extra_info = [this](AgentJson* out) {
        out->Set("renderer", AgentJson::String(gs_.consumer ? "gl (hidden window; screen.grab available)" : "counting sink (no screen.grab)"));
        out->Set("engine_boundaries", AgentJson::UInt(core_.domain.BoundaryCount()));
        out->Set("restores", AgentJson::UInt(restores_));
        out->Set("mem_watch", AgentJson::String("boundary-granular (bytes compared at every frame boundary; pc/fn null)"));
    };
    server_ = std::make_unique<AgentServer>(std::move(b));
    std::string err;
    if (!server_->Open(*knobs.agent_socket, knobs.agent_tcp, &err)) {
        std::fprintf(stderr, "nativeee: NATIVEEE_AGENT_SOCKET=%s: %s\n", knobs.agent_socket->c_str(), err.c_str());
        server_.reset();
        failed_ = true;
        return;
    }
    std::fprintf(stderr, "nativeee: agent control plane on %s%s; paused at the first boundary\n", knobs.agent_socket->c_str(),
                 knobs.agent_tcp > 0 ? (" and tcp 127.0.0.1:" + std::to_string(knobs.agent_tcp)).c_str() : "");
    if (auto* meta = host_.Meta()) meta->Set("agent_socket", *knobs.agent_socket);

    // Slot 7: the boundary owner's register file.
    core.sequencer.SetStep(BoundarySequencer::Step::Savestate, [this](CpuContext& ctx) {
        server_->NoteContext(&ctx);
        return true;
    });
    // The completed boundary: the state machine waits here. A false return = a state.load was admitted.
    core.game_boundary.SetConsumer([this](uint64_t frame) {
        ++park_.boundary_consumer_calls;
        if (!server_->OnBoundary(frame - 1)) {
            verify_.session_workers.Cancel();
            throw ThreadExitUnwind{};
        }
    });
    pad.agent_overlay = [this](uint64_t stamp, PadProtocol::Input* in) {
        AgentServer::Pad p{in->buttons, in->lx, in->ly, in->rx, in->ry};
        server_->ApplyPad(stamp, &p);
        in->buttons = p.buttons;
        in->lx = p.lx;
        in->ly = p.ly;
        in->rx = p.rx;
        in->ry = p.ry;
    };
    gs.vert_trace.agent_draw = [this](const ps2r::gs::DrawIr& d) {
        Draw dr{};
        dr.prim = d.prim_type;
        dr.verts = d.vertex_count;
        dr.tex = d.tex_enabled;
        dr.tbp = d.tex.tbp;
        dr.psm = d.tex.psm;
        dr.w = d.tex.tw < 16 ? (1u << d.tex.tw) : 0;
        dr.h = d.tex.th < 16 ? (1u << d.tex.th) : 0;
        // The issuing function: DrawAttribution::CurrentFn() is the drain owner's thread-local for the GIF
        // packet being delivered; the DrawIR chain (this sink at its head) runs on the drain owner.
        dr.fn = DrawAttribution::CurrentFn();
        // The screen box: the vertices' x/y range (GS primitive space, pixels) minus XYOFFSET.
        if (!d.verts.empty()) {
            float x0 = d.verts[0].x, y0 = d.verts[0].y, x1 = x0, y1 = y0;
            for (const ps2r::gs::VertexRec& v : d.verts) {
                x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
                y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            }
            dr.x0 = x0 - d.off_x;
            dr.y0 = y0 - d.off_y;
            dr.x1 = x1 - d.off_x;
            dr.y1 = y1 - d.off_y;
        }
        std::lock_guard<std::mutex> lock(draws_mutex_);
        if (draws_cur_.size() < 65536) draws_cur_.push_back(dr);
    };
    gs.vert_trace.agent_frame = [this](uint32_t) {
        std::lock_guard<std::mutex> lock(draws_mutex_);
        draws_last_.swap(draws_cur_);
        draws_cur_.clear();
    };

    // The headless live restore (state.load): SessionRestart with this composition's owners.
    MtvuStack* mtvu_stack = mtvu.mtvu.get();
    if (mtvu_stack) live_probes_.mtvu_quiesce = [mtvu_stack](std::string* why) { return mtvu_stack->Quiesce(why); };
    live_probes_.events_pending = [] { return EeEventQueue::Instance().HasPending(); };
    RpcRouter* router = &services.rpc_router;
    live_probes_.rpc_pending = [router] { return router->PendingCount(); };
    live_probes_.deliveries_pending = [] { return size_t{0}; };
    live_probes_.owners = [](std::string&) { return true; };
    restart_ = std::make_unique<ps2r::app::SessionRestart>(
        ThreadManager::Instance(), saves.restorer.Registry(), RestartSources(), RestartHooks(),
        [this](uint32_t fn, uint32_t enter, CpuContext& ctx) {
            // The relaunched boundary owner, before its first instruction: answer the load, wait paused.
            if (ThreadManager::Instance().CurrentTid() == incoming_boundary_ && resume_gate_.exchange(false)) {
                server_->OnRestored(incoming_clock_.domain.boundaries - 1, &ctx);
                if (server_->LoadPending()) {
                    verify_.session_workers.Cancel();
                    throw ThreadExitUnwind{};
                }
            }
            return DispatchTable::InvokeAt(fn, enter, ctx);
        });
}

LegAgent::~LegAgent() {
    if (server_) server_->Close();
}

AgentJson LegAgent::Draws() {
    std::vector<Draw> last;
    {
        std::lock_guard<std::mutex> lock(draws_mutex_);
        last = draws_last_;
    }
    AgentJson arr = AgentJson::Array();
    for (const Draw& d : last) {
        AgentJson o = AgentJson::Object();
        o.Set("prim", AgentJson::String(AgentServer::PrimName(d.prim)));
        o.Set("verts", AgentJson::UInt(d.verts));
        if (d.tex) {
            AgentJson t = AgentJson::Object();
            t.Set("addr", AgentJson::UInt(d.tbp));
            t.Set("psm", AgentJson::UInt(d.psm));
            t.Set("w", AgentJson::UInt(d.w));
            t.Set("h", AgentJson::UInt(d.h));
            o.Set("tex", t);
        } else {
            o.Set("tex", AgentJson::Null());
        }
        AgentJson box = AgentJson::Array();
        box.Push(AgentJson::Number(d.x0));
        box.Push(AgentJson::Number(d.y0));
        box.Push(AgentJson::Number(d.x1));
        box.Push(AgentJson::Number(d.y1));
        o.Set("box", box);
        o.Set("fn", d.fn ? AgentJson::UInt(d.fn) : AgentJson::Null());
        arr.Push(o);
    }
    return arr;
}

bool LegAgent::StateSaveAdmissible(std::string* why) {
    // The capture's own admission, without a write: the composition's quiescence owners (MTVU, ...) and
    // the thread table (every worker parked at an immutable, restartable kernel wait: ThreadCapture::
    // CollectBoundary). Must run inside the completed-boundary consumer (GameBoundary::FreshThreads).
    if (!saves_.capture) { *why = "no savestate capture owner"; return false; }
    // Settle first: a queued wake (EeEventQueue) whose target has not become READY yet is a transition in
    // flight on another host thread; probing during it makes the verdict depend on host timing (the thread
    // reads "parked" now and "READY, not parked" a moment later). Bounded: 20 ms.
    EeRunToken& token = EeRunToken::Instance();
    const int me = token.CurrentTid();
    int settle_us = 0;
    while (settle_us < 20000 && EeEventQueue::Instance().HasPending() && !token.AnyReadyBesides(me)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        settle_us += 100;
    }
    if (settle_us) ++settle_waits_;
    std::string owner, reason;
    if (!saves_.capture->Conditions().Check(&owner, &reason)) { *why = owner + ": " + reason; return false; }
    ThreadCapture::Records records;
    std::string error;
    if (!core_.game_boundary.FreshThreads(records, error)) {
        *why = error + (settle_us ? " [settled " + std::to_string(settle_us) + " us]" : "");
        return false;
    }
    return true;
}

bool LegAgent::StateSave(const std::string& path, int slot, std::string* out_path, std::string* err) {
    if (!saves_.capture) { *err = "no savestate capture owner"; return false; }
    std::string p = path;
    if (p.empty() && slot >= 0) p = ps2r::app::SavestateSlots::SlotPath(ps2r::app::SavestateSlots::Directory(cfg_), slot);
    if (!p.empty()) {
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(p).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }
    static uint64_t request = 1;
    const uint64_t frame = core_.domain.BoundaryCount();
    const ps2r::app::SessionCheckpoint::Result admitted = saves_.capture->Capture(request++, frame, p);
    if (admitted.phase != ps2r::app::SessionControls::Phase::Working) {
        *err = admitted.detail.empty() ? "capture refused" : admitted.detail;
        return false;
    }
    // The writer runs off-thread; the game is paused here, so wait for the file.
    for (int i = 0; i < 60000; ++i) {
        if (auto done = saves_.capture->Poll()) {
            if (done->phase != ps2r::app::SessionControls::Phase::Succeeded) { *err = done->detail; return false; }
            *out_path = done->path;
            std::fprintf(stderr, "nativeee: state.save: frame %llu captured (engine boundary %llu) -> %s\n",
                         static_cast<unsigned long long>(frame - 1), static_cast<unsigned long long>(frame), done->path.c_str());
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    *err = "the savestate writer did not finish within 60 s";
    return false;
}

bool LegAgent::StateLoadPrepare(const std::string& path, int slot, std::string* resolved, std::string* err) {
    if (!restart_) { *err = "no restore owner"; return false; }
    std::string p = path;
    const std::string dir = ps2r::app::SavestateSlots::Directory(cfg_);
    if (p.empty() && slot >= 0) p = ps2r::app::SavestateSlots::SlotPath(dir, slot);
    if (p.empty()) {
        std::string why;
        p = ps2r::app::SavestateSlots::Latest(dir, &why);
        if (p.empty()) { *err = "no slot given and no savestate in " + dir + (why.empty() ? "" : ": " + why); return false; }
    }
    std::string warning;
    if (!ps2r::app::SavestateSlots::PreflightComposition(p, ps2r::app::SavestateSlots::Composition(cfg_), build_id_, err, &warning)) {
        *err = p + ": " + *err;
        return false;
    }
    if (!warning.empty()) std::fprintf(stderr, "nativeee: state.load %s: warning: %s\n", p.c_str(), warning.c_str());
    static uint64_t request = 1;
    const ps2r::app::SessionRestart::Result result = restart_->PrepareFile(request++, p);
    if (result.phase != ps2r::app::SessionControls::Phase::Working) {
        *err = p + ": " + (result.detail.empty() ? "refused" : result.detail);
        return false;
    }
    *resolved = p;
    return true;
}

bool LegAgent::KnobSet(const std::string& name, const AgentJson& value, std::string* err) {
    // Run-time knobs: only owners that re-read a value every boundary. The harness mem lane changes what
    // is HASHED, never the game.
    if (name == "NATIVEEE_MEM_LANE") {
        bool on = false;
        int64_t n = 0;
        if (value.IsBool()) on = value.AsBool();
        else if (value.AsInt64(&n)) on = n != 0;
        else if (value.IsString()) on = value.AsString() != "0" && value.AsString() != "off";
        else { *err = "value must be 1/0, true/false"; return false; }
        host_.SetMemLane(on);
        return true;
    }
    *err = "this knob is read once at boot (LegKnobs::FromEnvironment) and cannot change at run time; run-time knobs: NATIVEEE_MEM_LANE";
    return false;
}

ps2r::app::SessionRestart::Current LegAgent::Current() const {
    const auto clock = saves_.session_clock.Capture();
    ps2r::app::SessionRestart::Current current;
    current.frame = clock.domain.boundaries;
    current.recording = host_.RecordingInput();
    current.plan.providers = clock.providers;
    current.plan.clock = ps2r::app::SessionRestart::Clock::Rebase;
    current.plan.recording = current.recording ? ps2r::app::SessionRestart::Recording::NewSegment
                                               : ps2r::app::SessionRestart::Recording::Disabled;
    for (const auto& owner : saves_.restorer.Registry().Specs()) current.plan.required_sections.push_back(owner.tag);
    return current;
}

bool LegAgent::ReadImage(const ps2r::savestate::FileMeta& meta, const std::vector<ps2r::savestate::Blob>& blobs, std::string& error) {
    if (!saves_.session_clock.ValidateImage(meta, blobs, error)) return false;
    incoming_boundary_ = 0;
    for (const auto& blob : blobs) {
        if (blob.tag == ps2r::app::SessionClock::kTag && !ps2r::app::SessionClock::Decode(blob, incoming_clock_, error)) return false;
        if (blob.tag != ps2r::savestate::TAG_THRD) continue;
        ps2r::app::SessionCheckpoint::Records records;
        if (!ThreadSection::Parse(blob.payload, records, &error)) return false;
        for (const auto& record : records)
            if (record.is_boundary) {
                if (incoming_boundary_) { error = "the image has multiple boundary owners"; return false; }
                incoming_boundary_ = record.tid;
            }
    }
    if (!incoming_boundary_) { error = "the image has no boundary owner"; return false; }
    return true;
}

ps2r::app::SessionRestart::Sources LegAgent::RestartSources() {
    ps2r::app::SessionRestart::Sources s;
    s.on_game_owner = [] { return EeRunToken::Instance().CanCaptureCurrentThread(); };
    s.on_supervisor = [] { return ThreadManager::Instance().CurrentIncarnation() == 0; };
    s.closing = [this] { return !server_ || park_.closer.Closed(); };
    s.current = [this] { return Current(); };
    s.can_resume = DispatchTable::CanResumeAt;
    s.can_restart_wait = DispatchTable::CanRestartWait;
    s.validate_epoch = [this](const ps2r::app::SessionRestart::Epoch&, std::string& error) {
        if (host_.OutputFailed()) { error = host_.OutputError(); return false; }
        return true;
    };
    s.current_quiescent = [this](std::string& error) {
        if (ps2r::app::LiveRestoreGate::Inspect(live_probes_, live_refusals_, error)) return true;
        if (auto* meta = host_.Meta()) meta->Set("live_restore_refusals", live_refusals_.Render());
        return false;
    };
    s.validate_image = [this](const ps2r::savestate::FileMeta& meta, const std::vector<ps2r::savestate::Blob>& blobs, std::string& error) {
        return ReadImage(meta, blobs, error);
    };
    return s;
}

ps2r::app::SessionRestart::Hooks LegAgent::RestartHooks() {
    ps2r::app::SessionRestart::Hooks h;
    h.prepare_image = [this](const std::vector<uint8_t>& bytes, std::string& error) {
        if (gs_.gs_queue && gs_.gs_queue->Faulted()) { error = "the GS consumer faulted"; return false; }
        return saves_.restorer.PrepareImage(bytes.data(), bytes.size(), &error);
    };
    h.discard_prepared = [this] { saves_.restorer.DiscardPrepared(); };
    h.join_workers_and_writers = [this](std::string&) {
        verify_.session_workers.Join();
        saves_.JoinWriter();
        // Forget the consumer's last grab: it is the pre-restore picture.
        if (gs_.gs_queue) {
            gs_.gs_queue->Sync();
            if (gs_.consumer) {
                ps2r::gs::DrawIrQueueConsumer* consumer = gs_.consumer.get();
                gs_.gs_queue->RunOrdered([consumer] { consumer->ForgetLastFrame(); return true; });
            }
        } else if (gs_.consumer) {
            gs_.consumer->ForgetLastFrame();
        }
        return true;
    };
    h.begin_epoch = [this](const ps2r::app::SessionRestart::Epoch& epoch, std::string& error) {
        if (!host_.BeginOutputSegment(epoch.restored_frame, incoming_clock_.domain.tick, &error)) return false;
        saves_.session_files.Select(host_.LegDir(), host_.OutputSegment());
        return true;
    };
    h.apply_image = [this](const std::vector<uint8_t>& bytes, std::string& error) {
        return saves_.restorer.RestorePrepared(bytes.data(), bytes.size(), &error);
    };
    h.reset_after_restore = [this](std::string&) {
        core_.game_boundary.ResetAfterRestore();
        verify_.session_workers.SetBoundary(incoming_boundary_);
        resume_gate_.store(true);
        audio_.audio_pumped_tick = core_.domain.DomainTick();
        saves_.restored_vsync_frame = static_cast<uint32_t>(incoming_clock_.domain.boundaries);
        saves_.restore_route = "live";
        saves_.DeriveMcBound();   // libmc's Bound() from the restored RAM (the cold route does the same)
        // The hook table's frame stamp (FnHooks::Call::frame = g_vsync_frame) follows the boundary count; the
        // restored run's first boundary has not run yet, so seed it with the restored count.
        g_vsync_frame.store(static_cast<uint32_t>(incoming_clock_.domain.boundaries), std::memory_order_relaxed);
        saves_.restore_sections.clear();
        for (const auto& blob : saves_.restorer.LastSections())
            saves_.restore_sections += (saves_.restore_sections.empty() ? "" : ",") + ps2r::savestate::TagName(blob.tag);
        ++saves_.restores;
        if (auto* meta = host_.Meta()) {
            meta->Set("restore_mhash", Hex(saves_.restorer.RestoredMemoryHash()));
            meta->Set("restored_vsync_frame", std::to_string(saves_.restored_vsync_frame));
            meta->Set("restore_route", saves_.restore_route);
            meta->Set("restore_sections", saves_.restore_sections);
            meta->Set("restore_mc_bound", saves_.restore_mc_bound);
            meta->Set("restores", std::to_string(saves_.restores.load()));
            meta->Flush();
        }
        std::fprintf(stderr, "nativeee: state.load: frame %u restored live (mhash %s; sections %s)\n", saves_.restored_vsync_frame,
                     Hex(saves_.restorer.RestoredMemoryHash()).c_str(), saves_.restore_sections.c_str());
        core_.running.store(true);
        return true;
    };
    h.cancel_workers = [this] { verify_.session_workers.Cancel(); };
    return h;
}

bool LegAgent::SuperviseRestore() {
    if (!restart_ || !server_) return false;
    const ps2r::app::SessionRestart::Result result = restart_->RestartPrepared();
    if (result.phase != ps2r::app::SessionControls::Phase::Succeeded) {
        ++restore_failures_;
        std::fprintf(stderr, "nativeee: state.load: restart failed: %s\n", result.detail.c_str());
        server_->RestoreFailed(result.detail);
        return false;
    }
    ++restores_;
    PS2R_LOG(HOST, Info, "agent: live restore %llu: frame %u", static_cast<unsigned long long>(restores_), result.frame);
    return true;
}

std::string LegAgent::Census() const {
    if (!server_) return failed_ ? "failed" : "off";
    return server_->Census() + ":restores=" + std::to_string(restores_) + ":restore_failures=" + std::to_string(restore_failures_) +
           ":settle_waits=" + std::to_string(settle_waits_);
}

}  // namespace ps2r::title::sly2
