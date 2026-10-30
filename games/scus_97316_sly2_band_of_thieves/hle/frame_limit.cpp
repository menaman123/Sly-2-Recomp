#include "games/scus_97316_sly2_band_of_thieves/hle/frame_limit.h"

#include <algorithm>

namespace ps2r::title::sly2 {

void FrameLimit::Apply(int limit) {
    if (!paced_) return;   // headless and NATIVEEE_UNPACED keep their own pacing
    if (limit < 0) limit = kConsole;
    limit_ = limit;
    if (movie_probe_) {
        const uint64_t count = movie_probe_();
        if (count != movie_count_) {
            movie_count_ = count;
            movie_hold_ = kMovieHold;
        } else if (movie_hold_ > 0) {
            --movie_hold_;
        }
    }
    const bool uncapped = limit != kConsole && movie_hold_ == 0;
    if (vu1_backlog_) {
        const uint64_t backlog = vu1_backlog_();
        vu1_backlog_bytes_ = backlog;
        if (!vu1_throttled_ && uncapped && backlog > kVu1HighBytes) {
            vu1_throttled_ = true;
            ++vu1_throttles_;
        } else if (vu1_throttled_ && (backlog < kVu1LowBytes || !uncapped)) {
            vu1_throttled_ = false;
        }
    }
    const uint32_t k = !uncapped || vu1_throttled_ ? 1
                       : limit == 0 ? kUnlimitedK
                                    : std::clamp<uint32_t>(static_cast<uint32_t>((limit + kConsole / 2) / kConsole),
                                                           TickSource::kTurboKMin, TickSource::kTurboKMax);
    if (uncapped == uncapped_ && k == k_) return;
    if (uncapped != uncapped_) {
        ++switches_;
        clock_.SetUncapped(uncapped, core_.clocks.Cop0Count());
        next_quantum_ = Clock::now();
    }
    uncapped_ = uncapped;
    k_ = k;
    if (k > 1) core_.tick_source.SetMode(TickSource::Mode::Turbo, k);
    else core_.tick_source.SetMode(TickSource::Mode::Realtime);
}

bool FrameLimit::TakeQuantum() {
    if (!uncapped_) return quantum_ = true;
    const Clock::time_point now = Clock::now();
    quantum_ = now >= next_quantum_;
    if (quantum_) {
        next_quantum_ += kField;
        if (next_quantum_ < now) next_quantum_ = now;   // a slow frame advances one quantum, never a burst
    }
    return quantum_;
}

}  // namespace ps2r::title::sly2
