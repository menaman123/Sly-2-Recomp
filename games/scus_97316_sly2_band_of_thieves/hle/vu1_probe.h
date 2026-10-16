// games/scus_97316_sly2_band_of_thieves/hle/vu1_probe.h -- Vu1Probe: boot-triage capture of the
// title's VU1 program runs (NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>]). The probe sits in the
// tier dispatch's HLE hook slot (it never runs a program: the hook returns false and the kick falls
// through to the configured tier) and, for the <count> kicks starting at the <nth> VU1 kick seen
// once the boundary counter reaches <frame>, writes the complete unit state at the kick as a
// Vu1Case record (engine/vu/vu1_case.h: registers, both 16 KB memories, TOP/ITOP, Q/P latches, the
// clock) plus every PATH1 packet the arbiter accepts until the next kick (the XGKICKs of that run)
// and a small text sidecar. The files live under <dir> (outside the repository).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/gif/gif_tag.h"

class VuUnit;
class GifArbiter;
class Vif;

namespace ps2r::title::sly2 {

class Vu1Probe {
public:
    Vu1Probe() = default;
    // Parses `spec` ("<dir>:<frame>:<nth>[:<count>]"; count defaults to 1), creates <dir>, installs the
    // hook on `vu1` and the submit tap on `arbiter`. `frame` is read at every kick (the composition's
    // boundary counter). False (and nothing installed) on a malformed spec or an unmakeable dir.
    // `vif` (optional) adds the VIF1 register file (CYCLE/MODE/MASK/ROW/COL) to the sidecar.
    bool Install(const char* spec, VuUnit& vu1, GifArbiter& arbiter, const uint64_t* frame, const Vif* vif = nullptr);
    bool Enabled() const { return !dir_.empty(); }
    uint64_t Kicks() const { return kicks_total_; }
    uint64_t Captured() const { return captured_; }

private:
    static bool Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t micro_generation);
    static void OnSubmit(void* user, GifPath path, const uint8_t* data, size_t size);
    void OnKick(VuUnit& unit, uint32_t start_pc);
    void CloseCapture();

    std::string dir_;
    const uint64_t* frame_ = nullptr;
    const Vif* vif_ = nullptr;
    uint64_t after_frame_ = 0;
    uint64_t nth_ = 1;
    uint64_t count_ = 1;
    uint64_t kicks_total_ = 0;
    uint64_t kicks_after_ = 0;
    uint64_t captured_ = 0;
    bool armed_ = false;        // a capture is open: PATH1 submits are appended to packets_
    uint64_t open_index_ = 0;
    std::vector<std::vector<uint8_t>> packets_;
};

}  // namespace ps2r::title::sly2
