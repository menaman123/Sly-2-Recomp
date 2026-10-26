// FrameTimeline: wall-clock instants of a play run's frame pipeline, each tagged with the EE frame it
// belongs to, for judging smoothness and input latency.
//
// Lanes:
//   boundary  the EE frame boundary (game thread)
//   drained   the GIF drain thread queued the frame's VSync record
//   consumed  the GS consumer finished the frame's draws
//   output    the GS consumer posted a new picture to the window
//   shown     the window draw that showed a new picture
//   input     a host input edge, tagged with the first boundary whose pad sample carried it
//   pad       the boundary's pad sample
//
// Each lane is appended from its own thread under its own mutex. The census writes
// <harness-out>/present_times.csv (lane,ordinal,t_ns,frame) and the run.meta rows present_hist=,
// present_latency= and latency= (read by verify/judge_smooth.py, judge_pacing.py, judge_latency.py).
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r::title::sly2 {

class FrameTimeline {
public:
    enum class Lane : uint8_t { Boundary = 0, Drained = 1, Consumed = 2, Output = 3, Shown = 4, Input = 5, Pad = 6 };
    static constexpr int kLanes = 7;

    FrameTimeline();
    FrameTimeline(const FrameTimeline&) = delete;
    FrameTimeline& operator=(const FrameTimeline&) = delete;

    // Appends now to `lane`, tagged `frame` (0 = untagged). Any thread.
    void Note(Lane lane, uint64_t frame = 0);
    // Appends a given instant (e.g. the presenter's own timestamp). Any thread.
    void NoteAt(Lane lane, std::chrono::steady_clock::time_point at, uint64_t frame = 0);
    // Instants in ns since construction.
    std::vector<uint64_t> Instants(Lane lane) const;
    struct Mark {
        uint64_t t_ns = 0;
        uint64_t frame = 0;
    };
    std::vector<Mark> Marks(Lane lane) const;
    // Frame tag of the lane's last mark (0 if none).
    uint64_t LastFrame(Lane lane) const;

    // Inter-frame gap statistics of one lane.
    struct Stats {
        uint64_t n = 0, gaps = 0;
        double p50_ms = 0, p99_ms = 0, max_ms = 0;
        uint64_t over_33 = 0, over_50 = 0;
    };
    Stats StatsOf(Lane lane) const;
    static Stats StatsOfInstants(const std::vector<uint64_t>& t_ns);
    // "boundary=n:gaps:p50:p99:max:>33:>50;drained=...;consumed=...;output=...;shown=..." (ms, two decimals).
    std::string Census() const;

    // Per-frame latency from the boundary lane to `lane`, matched by frame tag. A frame with no mark in
    // `lane` (dropped, or still in flight) counts as missing.
    struct Latency {
        uint64_t matched = 0, missing = 0;
        double p50_ms = 0, p99_ms = 0, max_ms = 0;
    };
    Latency LatencyTo(Lane lane) const;
    // Latency from each `from` mark tagged N to the first `to` mark tagged N + offset.
    Latency LatencyBetween(Lane from, Lane to, uint64_t offset) const;
    // "field_ms=16.67;edges=N;input_to_pad=matched/missing:p50/p99/max;pad_to_output=...;output_to_shown=...;
    //  input_to_shown=...;boundary_to_output=...;boundary_to_shown=..." (ms, two decimals); "edges=0" alone when no
    // input edge was stamped (the stages from the pad lane still follow).
    std::string InputLatencyCensus() const;
    // "frames=N:drained=matched/missing:p50/p99/max;consumed=...;output=...;shown=..." (ms, two decimals);
    // "frames=0" when the boundary lane carries no tags.
    std::string LatencyCensus() const;

    // One `lane,ordinal,t_ns,frame` row per instant. False if the file cannot be written.
    bool WriteCsv(const std::string& path) const;

    static const char* LaneName(Lane lane);

private:
    const std::chrono::steady_clock::time_point t0_;
    mutable std::mutex mutex_[kLanes];
    std::vector<Mark> lanes_[kLanes];
};

}  // namespace ps2r::title::sly2
