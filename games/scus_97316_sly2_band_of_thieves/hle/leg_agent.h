// LegAgent: the agent control plane of the Sly 2 composition (app/host/agent_server.h), armed by
// NATIVEEE_AGENT_SOCKET=<path> (+ NATIVEEE_AGENT_TCP=<port> on 127.0.0.1). Unset, nothing here exists:
// no thread, no hook, no cost.
//
// Wiring:
//   boundary slot 7 (Savestate)   notes the boundary owner's register file (cpu.regs)
//   GameBoundary consumer         AgentServer::OnBoundary(frame): the run/pause state machine waits here;
//                                 false = a state.load was admitted: the guest workers are cancelled and
//                                 this (boundary) thread unwinds; RunLeg's supervisor restores
//   LegPad::agent_overlay         the agent's pad on the sample served at every stamp
//   VertTraceSink::agent_*        the draw list of the frame in flight -> gs.frame (the last completed)
//   LegSaves                      state.save = SessionCapture on the completed boundary, the writer awaited
//                                 (StateSaveAdmissible = the capture's admission without a write: the server
//                                 drives the game to the next admissible boundary when refused);
//                                 state.load = a HEADLESS LIVE RESTORE over SessionRestart (the F8 machinery
//                                 without a window): preflight here, unwind, SuperviseRestore on the boot
//                                 thread (restore + relaunch), OnRestored before the first instruction.
// `frame` = the 0-based boundary ordinal = domain.BoundaryCount() - 1 = the pad stamp.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/host/agent_server.h"
#include "app/host/live_restore_gate.h"
#include "app/host/session_restart.h"
#include "engine/core/config/config.h"
#include "engine/core/verify/verify_host.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_audio.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_gs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_saves.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_verify_slot.h"

namespace ps2r::title::sly2 {

class LegAgent {
public:
    LegAgent(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const LegKnobs& knobs, LegCore& core,
             LegGs& gs, LegMtvu& mtvu, LegServices& services, LegAudio& audio, LegSaves& saves, LegPad& pad, LegPark& park,
             LegVerifySlot& verify, const char* build_id);
    ~LegAgent();
    LegAgent(const LegAgent&) = delete;
    LegAgent& operator=(const LegAgent&) = delete;

    // True when the knob is set and the socket opened (RunLeg exits with 2 when set but not opened).
    bool Enabled() const { return static_cast<bool>(server_); }
    bool Failed() const { return failed_; }
    // The boot thread after the boundary owner unwound for an admitted state.load: restore the prepared
    // image and relaunch the threads. False = failed (the leg ends; the load was answered).
    bool SuperviseRestore();
    bool LoadPending() const { return server_ && server_->LoadPending(); }
    // run.meta agent=
    std::string Census() const;

private:
    struct Draw {
        uint8_t prim;
        uint32_t verts;
        bool tex;
        uint32_t tbp, psm, w, h;
        float x0, y0, x1, y1;
        uint32_t fn;   // DrawAttribution's submitting function (0 = unknown)
    };
    ps2r::app::AgentJson Draws();
    bool StateSave(const std::string& path, int slot, std::string* out_path, std::string* err);
    bool StateSaveAdmissible(std::string* why);
    bool StateLoadPrepare(const std::string& path, int slot, std::string* resolved, std::string* err);
    bool KnobSet(const std::string& name, const ps2r::app::AgentJson& value, std::string* err);
    ps2r::app::SessionRestart::Sources RestartSources();
    ps2r::app::SessionRestart::Hooks RestartHooks();
    ps2r::app::SessionRestart::Current Current() const;
    bool ReadImage(const ps2r::savestate::FileMeta& meta, const std::vector<ps2r::savestate::Blob>& blobs, std::string& error);

    const ps2r::Config& cfg_;
    ps2r::verify::VerifyHost& host_;
    LegCore& core_;
    LegGs& gs_;
    [[maybe_unused]] LegAudio& audio_;   // kept for the restore hook (the audio tick reseed went with upstream's field pump)
    LegSaves& saves_;
    LegPark& park_;
    LegVerifySlot& verify_;
    std::string build_id_;
    bool failed_ = false;
    std::unique_ptr<ps2r::app::AgentServer> server_;
    // The draw list: the frame in flight and the last completed one (the consumer thread writes).
    std::mutex draws_mutex_;
    std::vector<Draw> draws_cur_, draws_last_;
    // The headless live restore.
    std::unique_ptr<ps2r::app::SessionRestart> restart_;
    ps2r::app::LiveRestoreGate::Probes live_probes_;
    ps2r::app::LiveRestoreGate::Census live_refusals_;
    ps2r::app::SessionClock::Snapshot incoming_clock_;
    int incoming_boundary_ = 0;
    std::atomic<bool> resume_gate_{false};
    uint64_t restores_ = 0, restore_failures_ = 0, settle_waits_ = 0;
};

}  // namespace ps2r::title::sly2
