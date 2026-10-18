// games/scus_97316_sly2_band_of_thieves/hle/leg_pad.cpp -- LegPad (leg_pad.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_pad.h"

#include <cstdio>
#include <cstdlib>

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

}  // namespace ps2r::title::sly2
