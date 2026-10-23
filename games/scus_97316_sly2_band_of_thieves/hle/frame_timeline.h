// games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h -- FrameTimeline: the wall instants of the
// frame trains of a play run, the record "it does not feel smooth" is judged on (presented-frame
// STEADINESS, not the p50), and -- every instant tagged with its EE frame ordinal -- the per-frame
// LATENCY of the chain behind the EE (pipeline2: the chain runs a frame behind the EE; what matters is
// that no stage waits for another and no picture is lost):
//   boundary  the EE frame boundary (the game tick; the game thread)          -- game-rate hitches
//   drained   the GIF drain thread pushed the frame's VSync record into the DrawIR queue (the frame's
//             packets are all parsed: the drain stage is done with the frame)
//   consumed  the GS consumer finished the frame record (its draws rendered, its picture's grab queued)
//   output    the GS consumer's present: a new picture posted to the window   -- consumer hitches
//   shown     the window draw that showed a NEW picture (the presenter)       -- what the player sees
//   input     (round 5) an input EDGE's arrival on the host (a key / button down or up the pump drained, a
//             harness HoldKey), tagged with the boundary whose pad sample FIRST carried it
//   pad       (round 5) the boundary's pad SAMPLE (PlatformInput's main-side read, after the TickAdvance
//             park: right after the wall edge), tagged with its boundary
// Each lane is a vector of (steady_clock ns since this object's construction, frame tag), appended from
// its own thread under its own mutex (five 60 Hz appends: no contention). The tag is the EE frame
// ordinal the instant belongs to (JobRec::src_frame of the frame's VSync job; 0 = untagged, the lanes
// before this census). Written at the census as <harness-out>/present_times.csv
// (`lane,ordinal,t_ns,frame`; verify/judge_smooth.py and judge_pacing.py read the first three columns),
// plus the run.meta rows present_hist= (per lane: n, p50/p99/max of the inter-frame gaps in ms, gaps
// > 33 ms, gaps > 50 ms) and present_latency= (per frame, matched by tag: boundary -> drained /
// consumed / output / shown in ms, p50 / p99 / max, and how many frames of the boundary lane found
// each later lane), and latency= (round 5: the INPUT -> PHOTON budget per stage, matched by tag:
// input(N) -> pad(N) [the edge waits for the boundary], pad(N) -> output(N+1) [the first picture whose
// boundary followed the sample: the game's tick + the chain behind the EE], output(N) -> shown(N) [the
// presenter], input(N) -> shown(N+1) [end to end, minus the game's own extra depth the judge adds];
// ms p50 / p99 / max and matched / missing per stage; verify/judge_latency.py judges it in fields).
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

    // The instant now (steady_clock) appended to `lane`, tagged `frame` (0 = untagged); any thread.
    void Note(Lane lane, uint64_t frame = 0);
    // The given instant (the presenter's own stamp) appended to `lane`; any thread.
    void NoteAt(Lane lane, std::chrono::steady_clock::time_point at, uint64_t frame = 0);
    // The lane's instants (ns since construction), a copy.
    std::vector<uint64_t> Instants(Lane lane) const;
    // The lane's (instant, frame tag) pairs, a copy.
    struct Mark {
        uint64_t t_ns = 0;
        uint64_t frame = 0;
    };
    std::vector<Mark> Marks(Lane lane) const;
    // The frame tag of the lane's LAST mark (0 = none / untagged); any thread.
    uint64_t LastFrame(Lane lane) const;

    // The gap statistics of one lane: n = instants, gaps = n - 1 inter-frame gaps.
    struct Stats {
        uint64_t n = 0, gaps = 0;
        double p50_ms = 0, p99_ms = 0, max_ms = 0;
        uint64_t over_33 = 0, over_50 = 0;
    };
    Stats StatsOf(Lane lane) const;
    static Stats StatsOfInstants(const std::vector<uint64_t>& t_ns);
    // "boundary=n:gaps:p50:p99:max:>33:>50;drained=...;consumed=...;output=...;shown=..." (ms, two decimals).
    std::string Census() const;

    // The per-frame latency from the boundary lane to each later lane, matched by frame tag
    // (a boundary frame with no instant in a later lane -- a dropped picture, a frame still in
    // flight at the census -- is counted missing, never guessed from order).
    struct Latency {
        uint64_t matched = 0, missing = 0;
        double p50_ms = 0, p99_ms = 0, max_ms = 0;
    };
    Latency LatencyTo(Lane lane) const;
    // The per-mark latency from `from`(N) to the FIRST mark of `to` tagged N + `offset` (round 5's stages).
    Latency LatencyBetween(Lane from, Lane to, uint64_t offset) const;
    // "field_ms=16.67;edges=N;input_to_pad=matched/missing:p50/p99/max;pad_to_output=...;output_to_shown=...;
    //  input_to_shown=...;boundary_to_output=...;boundary_to_shown=..." (ms, two decimals); "edges=0" alone when no
    // input edge was stamped (the stages from the pad lane still follow).
    std::string InputLatencyCensus() const;
    // "frames=N:drained=matched/missing:p50/p99/max;consumed=...;output=...;shown=..." (ms, two decimals);
    // "frames=0" when the boundary lane carries no tags.
    std::string LatencyCensus() const;

    // `lane,ordinal,t_ns,frame` rows, one per instant, lanes in enum order. False = could not open.
    bool WriteCsv(const std::string& path) const;

    static const char* LaneName(Lane lane);

private:
    const std::chrono::steady_clock::time_point t0_;
    mutable std::mutex mutex_[kLanes];
    std::vector<Mark> lanes_[kLanes];
};

}  // namespace ps2r::title::sly2
