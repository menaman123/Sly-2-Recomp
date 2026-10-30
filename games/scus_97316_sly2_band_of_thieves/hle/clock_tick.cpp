#include "games/scus_97316_sly2_band_of_thieves/hle/clock_tick.h"

#include <algorithm>
#include <cstring>

#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/ps2_float.h"

namespace ps2r::title::sly2 {

namespace {
// The generated body's constants (lui/ori pairs in FUN_0014d0b8).
constexpr uint32_t kSecondsPerCount = 0x31690453;   // 1 / 294.912 MHz
constexpr uint32_t kMinDt = 0x3c888889;             // 1/60
constexpr uint32_t kMaxDt = 0x3d088889;             // 1/30

float Bits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}
}  // namespace

bool ClockTick::Install() {
    DispatchTable::Body original = DispatchTable::Capture(kMarkClockTick);
    if (!original) return false;
    // TraceBody keeps the generated body's continuation entries (savestate capture and restore).
    return DispatchTable::Install(kMarkClockTick, DispatchTable::TraceBody(kMarkClockTick, original, [this, original](CpuContext& ctx, uint32_t entry) {
        ++calls_;
        // entry is the function's own address (or 0) for a call; anything else resumes inside the generated body.
        if (!uncapped_.load(std::memory_order_relaxed) || (entry != 0 && entry != kMarkClockTick)) {
            original(ctx, entry);
            return;
        }
        Tick(ctx);
    }));
}

float ClockTick::LoadF(uint32_t addr) const {
    float v;
    std::memcpy(&v, ram_.Data() + (addr & 0x1ffffff), sizeof v);
    return v;
}

void ClockTick::StoreF(uint32_t addr, float v) { std::memcpy(ram_.Data() + (addr & 0x1ffffff), &v, sizeof v); }

uint64_t ClockTick::Load64(uint32_t addr) const {
    uint64_t v;
    std::memcpy(&v, ram_.Data() + (addr & 0x1ffffff), sizeof v);
    return v;
}

void ClockTick::Store64(uint32_t addr, uint64_t v) { std::memcpy(ram_.Data() + (addr & 0x1ffffff), &v, sizeof v); }

// FUN_0014d0b8 with the uncapped floor; the order of operations is the generated body's.
void ClockTick::Tick(CpuContext& ctx) {
    ++uncapped_calls_;
    const uint32_t clock = ctx.cpuRegs.GPR.r[4].UL[0];
    const bool second_line = ctx.cpuRegs.GPR.r[5].UL[0] == 0;
    DispatchTable::Invoke(kTickNow, ctx);   // a0 is still the clock
    const uint64_t tick = ctx.cpuRegs.GPR.r[2].UD[0];
    const uint64_t delta = tick - Load64(clock + 0x48);
    const float raw = ps2_float_clamp(ps2_float_clamp(static_cast<float>(delta)) * Bits(kSecondsPerCount));
    const float dt = raw < kUncappedMinDt ? kUncappedMinDt : (raw > Bits(kMaxDt) ? Bits(kMaxDt) : raw);
    if (raw < Bits(kMinDt)) ++short_frames_;
    dt_sum_ += dt;
    const float scaled = ps2_float_clamp(dt * LoadF(clock + 0x0c));
    const float real = ps2_float_clamp(dt * LoadF(clock + 0x24));
    StoreF(clock + 0x10, scaled);
    StoreF(clock + 0x40, real);
    StoreF(clock + 0x14, ps2_float_clamp(LoadF(clock + 0x14) + scaled));
    if (second_line) {
        const float step = ps2_float_clamp(real * LoadF(clock + 0x08));
        StoreF(clock + 0x04, step);
        StoreF(clock + 0x00, ps2_float_clamp(LoadF(clock + 0x00) + step));
        ctx.cpuRegs.GPR.r[4].UD[0] = kAfterTickArg;
        DispatchTable::Invoke(kAfterTick, ctx);
    }
    StoreF(clock + 0x3c, ps2_float_clamp(LoadF(clock + 0x3c) + LoadF(clock + 0x40)));
    Store64(clock + 0x48, tick);
}

uint64_t ClockTick::WallCount() const {
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_origin_).count();
    return static_cast<uint64_t>(s * kCountHz);
}

void ClockTick::SetUncapped(bool on, uint64_t emulated_count) {
    if (on == uncapped_.load(std::memory_order_relaxed)) return;
    const uint64_t now = Count(emulated_count);
    if (on) {
        wall_origin_ = std::chrono::steady_clock::now();
        offset_.store(static_cast<int64_t>(now), std::memory_order_relaxed);
    } else {
        offset_.store(static_cast<int64_t>(now) - static_cast<int64_t>(emulated_count), std::memory_order_relaxed);
    }
    uncapped_.store(on, std::memory_order_relaxed);
}

uint64_t ClockTick::Count(uint64_t emulated_count) const {
    const int64_t off = offset_.load(std::memory_order_relaxed);
    if (uncapped_.load(std::memory_order_relaxed)) return static_cast<uint64_t>(off) + WallCount();
    return static_cast<uint64_t>(off + static_cast<int64_t>(emulated_count));
}

std::string ClockTick::Census() const {
    const double mean_ms = uncapped_calls_ ? dt_sum_ / static_cast<double>(uncapped_calls_) * 1000.0 : 0.0;
    char buf[160];
    std::snprintf(buf, sizeof buf, "calls=%llu:uncapped=%llu:short_frames=%llu:mean_dt_ms=%.2f",
                  static_cast<unsigned long long>(calls_), static_cast<unsigned long long>(uncapped_calls_),
                  static_cast<unsigned long long>(short_frames_), mean_ms);
    return buf;
}

}  // namespace ps2r::title::sly2
