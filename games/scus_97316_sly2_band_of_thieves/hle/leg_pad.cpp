// games/scus_97316_sly2_band_of_thieves/hle/leg_pad.cpp -- LegPad (leg_pad.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app/host/platform_input.h"

#include "app/host/build_id.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegPad::LegPad(const ps2r::Config& cfg, const std::string& title_dir, const LegKnobs& knobs, LegCore& core, LegServices& services, LegPlay& play)
    : nav_probe(knobs.nav ? knobs.nav->c_str() : nullptr, title_dir, cfg.harness.out_dir, core.ram) {
    if (knobs.pad_script) {
        const std::string& env = *knobs.pad_script;
        pad_script.LoadPaths(title_dir + "/verify/nav/ep1_waypoints.json");
        if (!pad_script.PathsWarning().empty()) std::fprintf(stderr, "nativeee: NATIVEEE_PAD_SCRIPT: path table %s (no paths)\n", pad_script.PathsWarning().c_str());
        if (!pad_script.Load(env)) std::fprintf(stderr, "nativeee: NATIVEEE_PAD_SCRIPT=%s: file not readable (ignored)\n", env.c_str());
        if (!pad_script.Warning().empty())
            std::fprintf(stderr, "nativeee: NATIVEEE_PAD_SCRIPT: stray token '%s' (script truncated here)\n", pad_script.Warning().c_str());
        if (pad_script.ClosedLoop()) {
            nav_probe.EnsureArmed();
            if (!nav_probe.Armed()) std::fprintf(stderr, "nativeee: NATIVEEE_PAD_SCRIPT has closed-loop steps but no probe table: they time out blind\n");
        }
    }
    if (knobs.pad_record)
        if (!pad_recorder.Open(*knobs.pad_record, g_build_id)) std::fprintf(stderr, "nativeee: NATIVEEE_PAD_RECORD=%s: cannot open (ignored)\n", knobs.pad_record->c_str());
    if (knobs.key_script) {
        for (const char* p = knobs.key_script->c_str(); *p;) {
            char* e = nullptr;
            KeyScriptStep st{};
            st.frame = std::strtoull(p, &e, 10);
            if (*e != ':') break;
            st.scancode = static_cast<int>(std::strtol(e + 1, &e, 10));
            if (*e != ':') break;
            st.hold = std::strtoull(e + 1, &e, 10);
            key_script.push_back(st);
            if (*e != ',') break;
            p = e + 1;
        }
    }
    if (knobs.lat_pulse && play.live_input) {
        // "<scancode>:<period_ms>:<hold_ms>": the latency census's edge source (leg_pad.h).
        char* e = nullptr;
        const char* p = knobs.lat_pulse->c_str();
        pulse_.scancode = static_cast<int>(std::strtol(p, &e, 10));
        if (*e == ':') pulse_.period_ms = std::strtoull(e + 1, &e, 10);
        if (*e == ':') pulse_.hold_ms = std::strtoull(e + 1, &e, 10);
        if (pulse_.scancode <= 0 || pulse_.period_ms < 50 || pulse_.hold_ms == 0 || pulse_.hold_ms >= pulse_.period_ms) {
            std::fprintf(stderr, "nativeee: NATIVEEE_LAT_PULSE=%s: want <scancode>:<period_ms>=50..:<hold_ms> < period (ignored)\n", p);
            pulse_ = Pulse{};
        } else {
            pulse_run_.store(true);
            PlatformInput* input = play.live_input.get();
            pulse_thread_ = std::thread([this, input] {
                // A 0..16 ms sweep on top of the period so the edges land at every phase of the field
                // (a fixed period is a fixed phase at 60 Hz: one sample of the 0..1 field wait).
                uint32_t lcg = 12345u;
                auto sleep_ms = [this](uint64_t ms) {
                    for (uint64_t slept = 0; slept < ms && pulse_run_.load(std::memory_order_relaxed); slept += 2)
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                };
                sleep_ms(1500);   // the leg settles (the restored state's first frames) before the first edge
                while (pulse_run_.load(std::memory_order_relaxed)) {
                    lcg = lcg * 1664525u + 1013904223u;
                    const uint64_t jitter_ms = (lcg >> 24) % 17u;
                    input->HoldKey(pulse_.scancode, true);
                    sleep_ms(pulse_.hold_ms);
                    input->HoldKey(pulse_.scancode, false);
                    pulses_.fetch_add(1, std::memory_order_relaxed);
                    sleep_ms(pulse_.period_ms - pulse_.hold_ms + jitter_ms);
                }
            });
        }
    }
    core.sequencer.SetStep(BoundarySequencer::Step::MaintDuties, [this, &core, &services, &play](CpuContext& ctx) {
        rpc_end_callbacks += static_cast<uint64_t>(services.rpc_router.CompletePending(
            ctx, [](uint32_t addr, CpuContext& c) { DispatchTable::Invoke(addr, c); },
            [](uint32_t a) { return memory::read<uint32_t>(a); }));
        if (play.live_input)
            for (KeyScriptStep& st : key_script) {
                const bool want = pad_stamps >= st.frame && pad_stamps < st.frame + st.hold;
                if (want != st.down) {
                    play.live_input->HoldKey(st.scancode, want);
                    st.down = want;
                }
            }
        PadProtocol::Input in = play.live_input ? play.live_input->Sample() : PadProtocol::Input{};
        if (play.live_input) {
            // The latency census (frame_timeline.h): a FRESH sample's instant and the input edge it carried,
            // tagged with this boundary (the Boundary lane's last mark, noted in slot 0 of this sequence).
            uint64_t sampled_ns = 0, edge_ns = 0;
            if (play.live_input->TakeSampleStamps(&sampled_ns, &edge_ns)) {
                const uint64_t tag = play.timeline.LastFrame(FrameTimeline::Lane::Boundary);
                if (tag != 0) {
                    using TP = std::chrono::steady_clock::time_point;
                    play.timeline.NoteAt(FrameTimeline::Lane::Pad, TP(std::chrono::nanoseconds(sampled_ns)), tag);
                    if (edge_ns != 0) play.timeline.NoteAt(FrameTimeline::Lane::Input, TP(std::chrono::nanoseconds(edge_ns)), tag);
                }
            }
        }
        nav_probe.Read(pad_stamps);
        pad_script.Apply(pad_stamps, nav_probe, &in);
        pad_recorder.Record(pad_stamps, in);
        if (in.buttons || in.lx != 0x80 || in.ly != 0x80 || in.rx != 0x80 || in.ry != 0x80) {
            ++pad_served_active;
            pad_served_last = std::to_string(pad_stamps) + ":" + Hex(in.buttons) + ":" + std::to_string(in.lx) + "," + std::to_string(in.ly) + "," +
                              std::to_string(in.rx) + "," + std::to_string(in.ry);
        }
        services.pad_protocol.Stamp(in, core.domain.DomainTick());
        ++pad_stamps;
        return true;
    });
}

LegPad::~LegPad() {
    pulse_run_.store(false);
    if (pulse_thread_.joinable()) pulse_thread_.join();
}

std::string LegPad::PulseCensus() const {
    if (pulse_.scancode == 0) return "off";
    return "scancode=" + std::to_string(pulse_.scancode) + ":period_ms=" + std::to_string(pulse_.period_ms) +
           ":hold_ms=" + std::to_string(pulse_.hold_ms) + ":pulses=" + std::to_string(pulses_.load());
}

}  // namespace ps2r::title::sly2
