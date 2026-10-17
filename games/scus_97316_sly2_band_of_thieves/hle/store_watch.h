// games/scus_97316_sly2_band_of_thieves/hle/store_watch.h -- StoreWatch: the NATIVEEE_WATCH boot-triage
// knob. NATIVEEE_WATCH=<hexaddr>,<bytes>[;<hexaddr>,<bytes>...][;max=<n>][;val=<hex>] prints every EE
// store that overlaps one of the guest ranges (thread, innermost shadow-stack frame + its ra, the
// caller frame, address, width, old -> new bits) through FastMem's process-wide write observer.
// Boot triage only; needs a non-NDEBUG engine build (FastMem's observer is compiled out of Release).
// DMA writes (fromSPR / IPU / VIF) do not pass through FastMem: a watched quadword that changes
// without a line here was written by a device or never written at all. The observer sees the hull
// of the ranges; the callback filters. The object must outlive the leg (the observer is never
// cleared): RunLeg keeps it on its frame.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace ps2r::title::sly2 {

class StoreWatch {
public:
    StoreWatch() = default;
    StoreWatch(const StoreWatch&) = delete;
    StoreWatch& operator=(const StoreWatch&) = delete;

    // Parses `spec` (nullptr = the knob is unset: nothing happens) and installs the observer when
    // at least one range parsed and no observer is installed yet. Prints the installed hull, or the
    // NDEBUG notice when the build compiles the observer out.
    void Install(const char* spec);

private:
    static void OnWrite(void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits);

    std::vector<std::pair<uint32_t, uint32_t>> ranges_;   // [lo, hi) physical
    uint64_t printed_ = 0;
    uint64_t max_ = 2000;
    std::vector<uint64_t> vals_;                          // val=<hex> (repeatable): only stores of these values
};

}  // namespace ps2r::title::sly2
