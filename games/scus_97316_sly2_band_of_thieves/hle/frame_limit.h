// FrameLimit: the play session's frame-rate policy (video.frame_limit, the Display page's "Frame rate").
//
//   60          the console's: the pacer's 60 Hz wall edge, one vblank per field. The default and the only
//               policy outside paced play.
//   0 or > 60   uncapped: the pacer runs at the limit (TickSource Turbo, K = limit / 60; 0 = K 16, 960 Hz),
//               so the game renders a new frame at every edge it is ready for. The boundary keeps the paced
//               path (the park serves the VU1 worker's interrupts and bounds how far the game runs ahead of
//               the worker and the renderer); only the edges come faster. Game speed stays real time because
//               Sly's clock measures each frame from Count (ClockTick: wall Count, a 1 ms dt floor). Nothing
//               is interpolated: every picture is a real game frame.
//
// The IOP and the SPU stay on real time in both: TakeQuantum() is true once per 16.7 ms of wall time (and at
// most once per boundary), so music, streams and the device ring keep their rate at any frame rate.
//
// The VU1 worker can fall behind a game running several hundred frames a second (its queue grew by GBs);
// above kVu1HighBytes queued the pacer drops to 60 Hz edges, whose parks serve the worker, until the queue is
// under kVu1LowBytes. The frame rate then follows what the worker sustains.
//
// Movies always play at the console's pacing: the game's movie player counts vblanks, and uncapped every
// edge is a vblank. While the IPU decodes (the movie probe's count moves), and for kMovieHold boundaries
// after, the policy holds 60.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

#include "games/scus_97316_sly2_band_of_thieves/hle/clock_tick.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"

namespace ps2r::title::sly2 {

class FrameLimit {
public:
    static constexpr int kConsole = 60;
    static constexpr int kMovieHold = 30;
    static constexpr uint32_t kUnlimitedK = 16;
    static constexpr uint64_t kVu1HighBytes = 64ull << 20;
    static constexpr uint64_t kVu1LowBytes = 8ull << 20;

    // `paced`: a play session whose boundaries are locked to the wall clock (the pacer bound at start).
    FrameLimit(LegCore& core, ClockTick& clock, bool paced) : core_(core), clock_(clock), paced_(paced) {}

    // A count that moves while a movie decodes (the IPU's picture decode commands).
    void SetMovieProbe(std::function<uint64_t()> probe) { movie_probe_ = std::move(probe); }
    // The bytes queued ahead of the VU1 worker (unset = none).
    void SetVu1Backlog(std::function<uint64_t()> backlog) { vu1_backlog_ = std::move(backlog); }
    // Game thread, at a boundary: the limit in force (0 = unlimited, 60 = console, > 60 uncapped at that rate).
    void Apply(int limit);
    int Limit() const { return limit_; }
    bool Uncapped() const { return uncapped_; }
    // Game thread, once per boundary before the IOP's step: whether this boundary advances the IOP and the
    // SPU one quantum. Always true while capped.
    bool TakeQuantum();
    bool QuantumThisBoundary() const { return quantum_; }
    uint64_t Switches() const { return switches_; }
    uint64_t Vu1Throttles() const { return vu1_throttles_; }
    // The pacer's edges per 60 Hz field in force (1 = the console's), the VU1 throttle, the last backlog read.
    uint32_t K() const { return k_; }
    bool Vu1Throttled() const { return vu1_throttled_; }
    uint64_t Vu1Backlog() const { return vu1_backlog_bytes_; }

private:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::nanoseconds kField{16666667};

    LegCore& core_;
    ClockTick& clock_;
    const bool paced_;
    int limit_ = kConsole;
    uint32_t k_ = 1;
    bool uncapped_ = false;
    bool quantum_ = true;
    std::function<uint64_t()> movie_probe_;
    std::function<uint64_t()> vu1_backlog_;
    bool vu1_throttled_ = false;
    uint64_t vu1_backlog_bytes_ = 0;
    uint64_t vu1_throttles_ = 0;
    uint64_t movie_count_ = 0;
    int movie_hold_ = 0;
    Clock::time_point next_quantum_{};
    uint64_t switches_ = 0;
};

}  // namespace ps2r::title::sly2
