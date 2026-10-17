// games/scus_97316_sly2_band_of_thieves/hle/leg_park.cpp -- LegPark (leg_park.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park.h"

#include <cstdio>
#include <memory>

#include "engine/ee/hle/thread_manager.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegPark::LegPark(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu)
    : closer(host, core.mmio), leg_t0(std::chrono::steady_clock::now()) {
    // NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>] (boot triage): Vu1Case + PATH1 packet capture of
    // VU1 kicks (hle/vu1_probe.h); the probe reads `boundaries` at every kick.
    if (knobs.vu1_cap)
        if (!vu1_probe.Install(knobs.vu1_cap->c_str(), mtvu.LiveVu1(), devices.gif_arbiter, &boundaries, &devices.vif))
            std::fprintf(stderr, "nativeee: NATIVEEE_VU1_CAP=%s: malformed or unmakeable (ignored)\n", knobs.vu1_cap->c_str());
}

std::string LegPark::ThreadCensus() {
    std::string threads;
    for (const auto& e : ThreadManager::Instance().SnapshotTable()) {
        const std::shared_ptr<ThreadInfo>& info = e.second;
        if (!info) continue;
        threads += (threads.empty() ? "" : ",") + std::to_string(e.first) + ":st=" + std::to_string(info->status) +
                   ":wait=" + std::to_string(info->wait_type) + "/" + std::to_string(info->wait_id) +
                   ":prio=" + std::to_string(info->current_priority) + ":entry=" + Hex(info->entry);
        if (const CpuContext* c = info->ctx)
            threads += ":ra=" + Hex(c->cpuRegs.GPR.r[31].UL[0]) + ":pc=" + Hex(c->cpuRegs.pc);
    }
    return threads;
}

void LegPark::NoteBoundaryWall() {
    if (boundaries % 100 == 0)
        boundary_wall += (boundary_wall.empty() ? "" : ",") + std::to_string(boundaries) + ":" +
                         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - leg_t0).count());
}

}  // namespace ps2r::title::sly2
