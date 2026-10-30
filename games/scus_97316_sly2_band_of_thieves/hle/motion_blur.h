// MotionBlur: Sly 2's full-screen motion blur on or off (video.motion_blur, the Display page's "Motion blur").
//
// FUN_00160f98, run from the render loop (FUN_00160e68), blends the previous picture into the new one. It
// returns at once while the word at kSkipFlag is non-zero (the load the community "no motion blur" patch
// edits). Off, a fresh call takes that early return without reading the word: the body is skipped and the
// registers the early return leaves (v0 = the word's lui half, v1 = non-zero) are set. On is the game as
// shipped.
#pragma once

#include <atomic>
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

namespace ps2r::title::sly2 {

class MotionBlur {
public:
    static constexpr uint32_t kBlend = 0x00160f98;
    static constexpr uint32_t kSkipFlag = 0x002deb24;

    // Wraps the generated body; false when it is not a generated function start.
    bool Install();
    // Any thread: on (the game's blur) or off, from the next frame.
    void Set(bool on) { on_.store(on, std::memory_order_relaxed); }
    bool On() const { return on_.load(std::memory_order_relaxed); }
    uint64_t Skipped() const { return skipped_; }

private:
    std::atomic<bool> on_{true};
    uint64_t skipped_ = 0;
};

}  // namespace ps2r::title::sly2
