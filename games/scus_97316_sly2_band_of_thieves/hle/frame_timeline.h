// games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h -- FrameTimeline: the wall instants of the
// three frame trains of a play run, the record "it does not feel smooth" is judged on (presented-frame
// STEADINESS, not the p50):
//   boundary  the EE frame boundary (the game tick; the game thread)         -- game-rate hitches
//   output    the GS consumer's present: a new picture posted to the window  -- consumer hitches
//   shown     the window draw that showed a NEW picture (the presenter)      -- what the player sees
// Each lane is a vector of steady_clock nanoseconds since this object's construction, appended from
// its own thread under its own mutex (three 60 Hz appends: no contention). Written at the census as
// <harness-out>/present_times.csv (`lane,ordinal,t_ns`; verify/judge_smooth.py reads it), plus the
// run.meta row present_hist= (per lane: n, p50/p99/max of the inter-frame gaps in ms, gaps > 33 ms,
// gaps > 50 ms) so a histogram needs no file.
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::title::sly2 {

class FrameTimeline {
public:
    enum class Lane : uint8_t { Boundary = 0, Output = 1, Shown = 2 };
    static constexpr int kLanes = 3;

    FrameTimeline();
    FrameTimeline(const FrameTimeline&) = delete;
    FrameTimeline& operator=(const FrameTimeline&) = delete;

    // The instant now (steady_clock) appended to `lane`; any thread.
    void Note(Lane lane);
    // The given instant (the presenter's own stamp) appended to `lane`; any thread.
    void NoteAt(Lane lane, std::chrono::steady_clock::time_point at);
    // The lane's instants (ns since construction), a copy.
    std::vector<uint64_t> Instants(Lane lane) const;

    // The gap statistics of one lane: n = instants, gaps = n - 1 inter-frame gaps.
    struct Stats {
        uint64_t n = 0, gaps = 0;
        double p50_ms = 0, p99_ms = 0, max_ms = 0;
        uint64_t over_33 = 0, over_50 = 0;
    };
    Stats StatsOf(Lane lane) const;
    static Stats StatsOfInstants(const std::vector<uint64_t>& t_ns);
    // "boundary=n:gaps:p50:p99:max:>33:>50;output=...;shown=..." (ms with two decimals).
    std::string Census() const;
    // `lane,ordinal,t_ns` rows, one per instant, lanes in enum order. False = could not open.
    bool WriteCsv(const std::string& path) const;

    static const char* LaneName(Lane lane);

private:
    const std::chrono::steady_clock::time_point t0_;
    mutable std::mutex mutex_[kLanes];
    std::vector<uint64_t> lanes_[kLanes];
};

}  // namespace ps2r::title::sly2
