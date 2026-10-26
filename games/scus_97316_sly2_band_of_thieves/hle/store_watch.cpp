#include "games/scus_97316_sly2_band_of_thieves/hle/store_watch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/mem/fast_mem.h"

namespace ps2r::title::sly2 {

void StoreWatch::Install(const char* spec_cstr) {
    if (!spec_cstr) return;
    std::string spec(spec_cstr);
    uint32_t hull_lo = 0xFFFFFFFFu, hull_hi = 0;
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t semi = spec.find(';', pos);
        if (semi == std::string::npos) semi = spec.size();
        const std::string item = spec.substr(pos, semi - pos);
        pos = semi + 1;
        if (item.rfind("max=", 0) == 0) { max_ = std::strtoull(item.c_str() + 4, nullptr, 0); continue; }
        if (item.rfind("val=", 0) == 0) { vals_.push_back(std::strtoull(item.c_str() + 4, nullptr, 16)); continue; }
        char* end = nullptr;
        const uint32_t lo = static_cast<uint32_t>(std::strtoul(item.c_str(), &end, 16)) & 0x1FFFFFFFu;
        const uint32_t bytes = (end && *end == ',') ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 0)) : 16u;
        ranges_.emplace_back(lo, lo + bytes);
        hull_lo = std::min(hull_lo, lo);
        hull_hi = std::max(hull_hi, lo + bytes);
    }
    if (!ranges_.empty() && !FastMem::HasWriteObserver()) {
        FastMem::SetWriteObserver(&StoreWatch::OnWrite, this, hull_lo, hull_hi);
        std::fprintf(stderr, "watch: %zu range(s), hull [0x%08x, 0x%08x) max=%llu\n", ranges_.size(), hull_lo, hull_hi,
                     (unsigned long long)max_);
    }
#ifdef NDEBUG
    std::fprintf(stderr, "watch: NDEBUG build -- FastMem compiles the observer out; use a RelWithDebInfo/-UNDEBUG build\n");
#endif
}

void StoreWatch::OnWrite(void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits) {
    auto* st = static_cast<StoreWatch*>(user);
    const uint32_t phys = addr & 0x1FFFFFFFu;
    bool hit = false;
    for (const auto& r : st->ranges_) hit = hit || (phys < r.second && phys + width > r.first);
    if (!hit) return;
    if (!st->vals_.empty()) {
        const uint64_t mask = width >= 8 ? ~0ull : ((1ull << (8 * width)) - 1);
        bool match = false;
        for (uint64_t v : st->vals_) match = match || (new_bits & mask) == (v & mask);
        if (!match) return;
    }
    if (st->printed_++ >= st->max_) return;
    ShadowCallStack::Frame f0{}, f1{};
    ShadowCallStack::Top(0, f0);
    ShadowCallStack::Top(1, f1);
    std::fprintf(stderr, "watch: tid=%d fn=0x%08x ra=0x%08x caller=0x%08x addr=0x%08x w=%u old=0x%016llx new=0x%016llx\n",
                 ThreadManager::Instance().CurrentTid(), f0.func_addr, f0.ra, f1.func_addr, addr, width,
                 (unsigned long long)old_bits, (unsigned long long)new_bits);
}

}  // namespace ps2r::title::sly2
