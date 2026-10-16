// games/scus_97316_sly2_band_of_thieves/hle/vu1_probe.cpp -- Vu1Probe (see vu1_probe.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/vu1_probe.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "engine/gif/gif_arbiter.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_unit.h"

namespace ps2r::title::sly2 {

bool Vu1Probe::Install(const char* spec, VuUnit& vu1, GifArbiter& arbiter, const uint64_t* frame, const Vif* vif) {
    if (!spec || !frame) return false;
    const std::string s(spec);
    const size_t c1 = s.find(':');
    if (c1 == std::string::npos || c1 == 0) return false;
    const size_t c2 = s.find(':', c1 + 1);
    if (c2 == std::string::npos) return false;
    const size_t c3 = s.find(':', c2 + 1);
    const std::string dir = s.substr(0, c1);
    after_frame_ = std::strtoull(s.c_str() + c1 + 1, nullptr, 10);
    nth_ = std::strtoull(s.c_str() + c2 + 1, nullptr, 10);
    count_ = (c3 == std::string::npos) ? 1 : std::strtoull(s.c_str() + c3 + 1, nullptr, 10);
    if (nth_ == 0) nth_ = 1;
    if (count_ == 0) count_ = 1;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec && !std::filesystem::is_directory(dir)) return false;
    dir_ = dir;
    frame_ = frame;
    vif_ = vif;
    vu1.dispatch().settings().hle_enabled = true;
    vu1.dispatch().SetHleHook(&Vu1Probe::Hook, this);
    arbiter.SetSubmitObserver(&Vu1Probe::OnSubmit, this);
    std::fprintf(stderr, "vu1_probe: dir=%s after_frame=%llu nth=%llu count=%llu\n", dir_.c_str(),
                 static_cast<unsigned long long>(after_frame_), static_cast<unsigned long long>(nth_),
                 static_cast<unsigned long long>(count_));
    return true;
}

bool Vu1Probe::Hook(void* user, VuUnit& unit, uint32_t start_pc, uint64_t) {
    static_cast<Vu1Probe*>(user)->OnKick(unit, start_pc);
    return false;  // never runs the program: the kick falls through to the tier
}

void Vu1Probe::OnSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
    Vu1Probe* p = static_cast<Vu1Probe*>(user);
    if (!p->armed_ || path != GifPath::Path1 || !data || size == 0) return;
    p->packets_.emplace_back(data, data + size);
}

void Vu1Probe::OnKick(VuUnit& unit, uint32_t start_pc) {
    ++kicks_total_;
    if (armed_) CloseCapture();
    if (*frame_ < after_frame_) return;
    ++kicks_after_;
    if (kicks_after_ < nth_ || kicks_after_ >= nth_ + count_) return;
    const uint64_t idx = kicks_after_ - nth_;
    Vu1Case c;
    unit.SaveCase(c, start_pc);
    char name[64];
    std::snprintf(name, sizeof(name), "/case_%03llu_%04x.bin", static_cast<unsigned long long>(idx), start_pc & 0x3FFFu);
    const std::string path = dir_ + name;
    const bool ok = vu1_case::WriteCase(path.c_str(), c);
    std::snprintf(name, sizeof(name), "/case_%03llu.txt", static_cast<unsigned long long>(idx));
    if (std::FILE* f = std::fopen((dir_ + name).c_str(), "w")) {
        std::fprintf(f, "frame=%llu kick_total=%llu kick_after=%llu start_pc=0x%04x top=%u itop=%u cycle=%llu mode=%s\n",
                     static_cast<unsigned long long>(*frame_), static_cast<unsigned long long>(kicks_total_),
                     static_cast<unsigned long long>(kicks_after_), start_pc, unsigned(unit.vif_top()), unsigned(unit.vif_itop()),
                     static_cast<unsigned long long>(unit.cycle()),
                     unit.dispatch().settings().vu1_mode == VuTierMode::Interp ? "interp" : "cached");
        const VUregs& r = unit.regs();
        for (int i = 0; i < 32; ++i)
            std::fprintf(f, "VF%02d %08x %08x %08x %08x\n", i, r.VF[i].I[0], r.VF[i].I[1], r.VF[i].I[2], r.VF[i].I[3]);
        for (int i = 0; i < 16; ++i) std::fprintf(f, "VI%02d %04x\n", i, unsigned(r.VI[i]));
        if (vif_) {
            const VifRegs& v = vif_->regs(1);
            std::fprintf(f, "VIF1 cycle=%08x mode=%u mask=%08x row=%08x,%08x,%08x,%08x col=%08x,%08x,%08x,%08x base=%u ofst=%u tops=%u\n",
                         v.cycle, v.mode, v.mask, v.row[0], v.row[1], v.row[2], v.row[3], v.col[0], v.col[1], v.col[2], v.col[3], v.base,
                         v.ofst, v.tops);
        }
        std::fclose(f);
    }
    std::fprintf(stderr, "vu1_probe: captured kick %llu (frame %llu, pc 0x%04x) -> %s%s\n", static_cast<unsigned long long>(kicks_after_),
                 static_cast<unsigned long long>(*frame_), start_pc, path.c_str(), ok ? "" : " (WRITE FAILED)");
    ++captured_;
    open_index_ = idx;
    packets_.clear();
    armed_ = true;
}

// Writes the PATH1 packets accepted since the open kick: <dir>/kicks_<idx>.bin is a concatenation
// of [u32 size][size bytes] records (the replay harness reads the same shape).
void Vu1Probe::CloseCapture() {
    armed_ = false;
    char name[64];
    std::snprintf(name, sizeof(name), "/kicks_%03llu.bin", static_cast<unsigned long long>(open_index_));
    std::FILE* f = std::fopen((dir_ + name).c_str(), "wb");
    if (!f) return;
    size_t bytes = 0;
    for (const auto& p : packets_) {
        const uint32_t n = static_cast<uint32_t>(p.size());
        std::fwrite(&n, 4, 1, f);
        std::fwrite(p.data(), 1, p.size(), f);
        bytes += p.size();
    }
    std::fclose(f);
    std::fprintf(stderr, "vu1_probe: capture %llu closed: %zu PATH1 packets, %zu bytes\n", static_cast<unsigned long long>(open_index_),
                 packets_.size(), bytes);
    packets_.clear();
}

}  // namespace ps2r::title::sly2
