// ClockTick: Sly 2's frame clock under an uncapped frame rate.
//
// FUN_0014d0b8 (the Sly engine's MarkClockTick) measures each frame's dt from the COP0 Count register
// (FUN_0014d240 extends it to 64 bits; 294.912 MHz) and clamps it to [1/60, 1/30] s, so on its own the
// game never takes a step shorter than one field: above 60 frames a second it would run fast. With the
// frame rate uncapped, this class
//   * serves the guest's Count from wall time (continuous across a switch in either direction), and
//   * runs the clock body with a kUncappedMinDt floor instead of 1/60.
// Capped (the default), the generated body runs unchanged and Count follows the emulated clock.
//
// The clock (a0, at 0x2dded0 for the game clock):
//   +0x00 t, +0x04 dt   the second time line, advanced when a1 == 0, with FUN_00161478 called after
//   +0x08 its rate      +0x0c the main rate     +0x10 dt (scaled)   +0x14 t
//   +0x24 the real rate +0x3c real t            +0x40 real dt       +0x48 the tick of the last frame
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/main_memory.h"

namespace ps2r::title::sly2 {

class ClockTick {
public:
    static constexpr uint32_t kMarkClockTick = 0x0014d0b8;
    static constexpr uint32_t kTickNow = 0x0014d240;
    static constexpr uint32_t kAfterTick = 0x00161478;
    static constexpr uint32_t kAfterTickArg = 0x002deb30;
    static constexpr float kUncappedMinDt = 1.0f / 1000.0f;
    static constexpr double kCountHz = 294912000.0;

    explicit ClockTick(MainMemory& ram) : ram_(ram) {}

    // Wraps the generated clock body; false when it is not a generated function start.
    bool Install();
    // Game thread, at a boundary.
    void SetUncapped(bool on, uint64_t emulated_count);
    bool Uncapped() const { return uncapped_.load(std::memory_order_relaxed); }
    // The Count the guest reads, given the emulated clock's value.
    uint64_t Count(uint64_t emulated_count) const;
    // run.meta clock_tick=: calls, uncapped calls, frames shorter than a field, mean dt (ms).
    std::string Census() const;

private:
    void Tick(CpuContext& ctx);
    float LoadF(uint32_t addr) const;
    void StoreF(uint32_t addr, float v);
    uint64_t Load64(uint32_t addr) const;
    void Store64(uint32_t addr, uint64_t v);
    uint64_t WallCount() const;

    MainMemory& ram_;
    std::atomic<bool> uncapped_{false};
    // Count = offset_ + (emulated, or wall counts since the switch); set at a switch.
    std::atomic<int64_t> offset_{0};
    std::chrono::steady_clock::time_point wall_origin_;
    uint64_t calls_ = 0, uncapped_calls_ = 0, short_frames_ = 0;
    double dt_sum_ = 0.0;
};

}  // namespace ps2r::title::sly2
