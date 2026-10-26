#include "games/scus_97316_sly2_band_of_thieves/hle/text_watch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/core/verify/run_meta.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

namespace {
constexpr uint32_t kPageBytes = 4096;
}  // namespace

bool TextWatch::IsTextSpec(const char* spec) {
    if (!spec) return false;
    const char* semi = std::strchr(spec, ';');
    const size_t len = semi ? static_cast<size_t>(semi - spec) : std::strlen(spec);
    return len == 4 && std::strncmp(spec, "text", 4) == 0;
}

TextWatch::TextWatch(const char* spec_cstr, MainMemory& ram) : ram_(ram) {
    if (!IsTextSpec(spec_cstr)) return;
    const std::string spec(spec_cstr);
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t semi = spec.find(';', pos);
        if (semi == std::string::npos) semi = spec.size();
        const std::string item = spec.substr(pos, semi - pos);
        pos = semi + 1;
        if (item == "text") { ranges_.push_back({kTextLo, kTextHi, {}}); continue; }
        if (item.rfind("every=", 0) == 0) { every_ = std::max<uint64_t>(1, std::strtoull(item.c_str() + 6, nullptr, 0)); continue; }
        if (item.rfind("max=", 0) == 0) { max_lines_ = std::strtoull(item.c_str() + 4, nullptr, 0); continue; }
        if (item.rfind("ramdump=", 0) == 0) { ramdump_ = item.substr(8); continue; }
        char* end = nullptr;
        const uint32_t lo = static_cast<uint32_t>(std::strtoul(item.c_str(), &end, 16)) & 0x1FFFFFFFu;
        const uint32_t bytes = (end && *end == ',') ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 0)) : kPageBytes;
        ranges_.push_back({lo, lo + bytes, {}});
    }
    const uint32_t ram_size = static_cast<uint32_t>(ram_.Size());
    for (Range& r : ranges_) {
        r.lo &= ~3u;
        r.hi = std::min(r.hi, ram_size) & ~3u;
        if (r.hi <= r.lo) { r.base.clear(); continue; }
        r.base.assign(ram_.Data() + r.lo, ram_.Data() + r.hi);
    }
    ranges_.erase(std::remove_if(ranges_.begin(), ranges_.end(), [](const Range& r) { return r.base.empty(); }), ranges_.end());
    armed_ = !ranges_.empty();
    for (const Range& r : ranges_)
        std::fprintf(stderr, "textwatch: range [0x%08x, 0x%08x) %zu bytes snapshotted (every=%llu)\n", r.lo, r.hi, r.base.size(),
                     (unsigned long long)every_);
}

void TextWatch::OnBoundary(uint64_t boundary) {
    if (!armed_ || boundary % every_ != 0) return;
    ++checks_;
    bool dirty = false;
    for (Range& r : ranges_) {
        if (std::memcmp(ram_.Data() + r.lo, r.base.data(), r.base.size()) == 0) continue;
        dirty = true;
        Scan(r, boundary);
    }
    if (dirty) ++dirty_boundaries_;
}

// Word walk of one dirty range: each changed word is an event; the snapshot is re-based.
void TextWatch::Scan(Range& r, uint64_t boundary) {
    const uint8_t* now = ram_.Data() + r.lo;
    uint8_t* base = r.base.data();
    const size_t words = r.base.size() / 4;
    for (size_t i = 0; i < words; i++) {
        uint32_t a, b;
        std::memcpy(&a, now + 4 * i, 4);
        std::memcpy(&b, base + 4 * i, 4);
        if (a == b) continue;
        const uint32_t addr = r.lo + static_cast<uint32_t>(4 * i);
        ++events_;
        Page& p = pages_[addr & ~(kPageBytes - 1)];
        if (p.events++ == 0) p.first = boundary;
        p.last = boundary;
        if (printed_ < max_lines_) {
            ++printed_;
            std::fprintf(stderr, "textwatch: boundary=%llu addr=0x%08x old=0x%08x new=0x%08x\n", (unsigned long long)boundary, addr, b, a);
        }
        std::memcpy(base + 4 * i, &a, 4);
    }
}

void TextWatch::Report(ps2r::verify::VerifyHost& host) {
    if (!armed_ || reported_) return;
    reported_ = true;
    std::string ranges, pages;
    for (const Range& r : ranges_) ranges += (ranges.empty() ? "" : "+") + Hex(r.lo) + "-" + Hex(r.hi);
    std::fprintf(stderr, "textwatch: ranges=%s checks=%llu dirty_boundaries=%llu events=%llu pages=%zu\n", ranges.c_str(),
                 (unsigned long long)checks_, (unsigned long long)dirty_boundaries_, (unsigned long long)events_, pages_.size());
    for (const auto& [page, p] : pages_) {
        std::fprintf(stderr, "textwatch:   page 0x%08x events=%llu boundaries %llu..%llu\n", page, (unsigned long long)p.events,
                     (unsigned long long)p.first, (unsigned long long)p.last);
        pages += (pages.empty() ? "" : ",") + Hex(page) + ":" + std::to_string(p.events) + ":" + std::to_string(p.first) + "-" +
                 std::to_string(p.last);
    }
    if (auto* meta = host.Meta()) {
        meta->Set("text_watch", "ranges=" + ranges + ":checks=" + std::to_string(checks_) + ":dirty_boundaries=" +
                                    std::to_string(dirty_boundaries_) + ":events=" + std::to_string(events_) + ":pages=" +
                                    std::to_string(pages_.size()));
        meta->Set("text_watch_pages", pages);
    }
    if (!ramdump_.empty()) {
        if (std::FILE* f = std::fopen(ramdump_.c_str(), "wb")) {
            std::fwrite(ram_.Data(), 1, ram_.Size(), f);
            std::fclose(f);
            std::fprintf(stderr, "textwatch: RAM dumped to %s\n", ramdump_.c_str());
        } else {
            std::fprintf(stderr, "textwatch: cannot write %s\n", ramdump_.c_str());
        }
    }
}

}  // namespace ps2r::title::sly2
