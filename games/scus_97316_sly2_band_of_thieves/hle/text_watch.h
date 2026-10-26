// TextWatch: NATIVEEE_WATCH's text mode. Detects the game writing code anywhere outside the recompiled image.
//
//   NATIVEEE_WATCH=text[;<hexaddr>,<bytes>...][;every=<n>][;max=<n>][;ramdump=<file>]
//
// `text` = the ELF's code ranges (.text 0x100000..0x259df0 + .vutext ..0x262380, the bytes the recompiler
// consumed); each extra <hexaddr>,<bytes> item is one more watched range (an executable-looking region).
// Unlike StoreWatch (store_watch.h) this is NOT built on FastMem's write observer: that observer is compiled
// out of Release, and the writers an overlay loader would use -- the sceCdRead HLE block copy, SIF / IPU /
// VIF DMA -- never pass through FastMem anyway. TextWatch snapshots the watched bytes once the ELF is in RAM
// (constructed after LegImage) and, at every `every`-th EE boundary (default 1), memcmp's each range against
// the snapshot: a difference is walked word by word, every changed word is one EVENT (boundary, address,
// old -> new, printed up to `max` lines), the snapshot is re-based so a later write to the same word counts
// again, and a per-4-KiB-page histogram (events, first / last boundary) accumulates. The baseline is the
// loaded image, so a patch applied before the first boundary (crt0 / init) is still seen at boundary 1.
// Cost: one 1.5 MB memcmp per boundary (~0.1 ms); nothing else until a difference.
// Report() (the census) prints the histogram and writes run.meta text_watch= / text_watch_pages=; with
// ramdump=<file> it also writes the 32 MB guest RAM once, for offline analysis.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/core/verify/verify_host.h"
#include "engine/ee/mem/main_memory.h"

namespace ps2r::title::sly2 {

class TextWatch {
public:
    // The ELF's code ranges (the recompiler's input): .text + .vutext.
    static constexpr uint32_t kTextLo = 0x00100000u;
    static constexpr uint32_t kTextHi = 0x00262380u;

    // `spec` = the NATIVEEE_WATCH value; the watch arms only when its first item is `text`
    // (IsTextSpec). `ram` must already hold the loaded ELF.
    TextWatch(const char* spec, MainMemory& ram);
    TextWatch(const TextWatch&) = delete;
    TextWatch& operator=(const TextWatch&) = delete;

    // True when `spec` selects this mode (first ';'-item == "text"); LegCore hands such a spec to
    // StoreWatch as unset.
    static bool IsTextSpec(const char* spec);

    bool Armed() const { return armed_; }
    // The boundary step (LegVerifySlot::VerifyStep): compare + account. `boundary` = 1-based count.
    void OnBoundary(uint64_t boundary);
    // The census: the histogram to stderr + run.meta rows; the RAM dump once. Idempotent.
    void Report(ps2r::verify::VerifyHost& host);

private:
    struct Range {
        uint32_t lo, hi;              // physical [lo, hi)
        std::vector<uint8_t> base;    // the snapshot
    };
    struct Page {
        uint64_t events = 0, first = 0, last = 0;
    };
    void Scan(Range& r, uint64_t boundary);

    MainMemory& ram_;
    bool armed_ = false;
    bool reported_ = false;
    uint64_t every_ = 1;
    uint64_t max_lines_ = 200;
    uint64_t printed_ = 0;
    uint64_t checks_ = 0, events_ = 0, dirty_boundaries_ = 0;
    std::string ramdump_;
    std::vector<Range> ranges_;
    std::map<uint32_t, Page> pages_;   // 4 KiB page base -> histogram
};

}  // namespace ps2r::title::sly2
