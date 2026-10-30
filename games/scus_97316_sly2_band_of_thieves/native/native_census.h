// native_census.h -- the per-unit counters behind run.meta's `native=` row.
//
// Every native path counts what it served natively and what it handed back to the engine's
// model (the fallback), per unit; LegCensus writes the row with the knobs' state. The counters
// are process-wide (one composition per process) and relaxed atomics: a native path may run on
// the EE thread, the VU1 worker or the GS consumer. A unit may add a short free-form detail
// ("images=12:entries=31") the row appends after its counters.
//
// Row grammar (run.meta):  native=<unit>=<on|off>:<served>/<fallback>[:<detail>],...;any=<on|off>
// e.g. native=vu1=on:734612/0:images=12,gif=off:0/0,vif=off:0/0,dmac=off:0/0,iop=off:0/0,spu=off:0/0,gs=off:0/0,vu0=off:0/0;any=on
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"

namespace ps2r::title::sly2 {

class NativeCensus {
public:
    static NativeCensus& Instance();

    // One call / kick / packet / RPC served by the native path (`n` of them).
    void Served(NativeUnit u, uint64_t n = 1) { c_[Idx(u)].served.fetch_add(n, std::memory_order_relaxed); }
    // One handed back to the engine's model (a shape the native path does not recognise).
    void Fallback(NativeUnit u, uint64_t n = 1) { c_[Idx(u)].fallback.fetch_add(n, std::memory_order_relaxed); }
    uint64_t ServedCount(NativeUnit u) const { return c_[Idx(u)].served.load(std::memory_order_relaxed); }
    uint64_t FallbackCount(NativeUnit u) const { return c_[Idx(u)].fallback.load(std::memory_order_relaxed); }
    // A unit's free-form detail (no ',' or ';': the row's separators). Not thread-safe against Row();
    // set it from the composition thread (install time / the census write).
    void SetDetail(NativeUnit u, const std::string& detail);

    // The run.meta row (grammar above) for the knobs in force.
    std::string Row(const NativeKnobs& knobs) const;
    // Every counter to zero, details cleared (a restore / a test).
    void Reset();

private:
    struct Counters {
        std::atomic<uint64_t> served{0};
        std::atomic<uint64_t> fallback{0};
    };
    static int Idx(NativeUnit u) { return static_cast<int>(u); }
    Counters c_[static_cast<int>(NativeUnit::kCount)];
    std::string detail_[static_cast<int>(NativeUnit::kCount)];
};

}  // namespace ps2r::title::sly2
