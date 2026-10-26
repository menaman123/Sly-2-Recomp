#include "games/scus_97316_sly2_band_of_thieves/hle/leg_watchdog.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/core/verify/run_meta.h"
#include "engine/ee/sched/ee_run_token.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegWatchdog::LegWatchdog(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegPark& park, LegCensus& census) {
    if (knobs.watchdog_s > 0) thread_ = std::thread([this, &host, &knobs, &core, &park, &census] { Run(host, knobs, core, park, census); });
}

void LegWatchdog::Join() {
    if (thread_.joinable()) thread_.join();
}

void LegWatchdog::Run(ps2r::verify::VerifyHost& host, const LegKnobs& knobs, LegCore& core, LegPark& park, LegCensus& census) {
    const uint64_t watchdog_s = knobs.watchdog_s;
    uint64_t last = 0;
    auto since = std::chrono::steady_clock::now();
    while (core.running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (park.boundaries != last) {
            last = park.boundaries;
            since = std::chrono::steady_clock::now();
            continue;
        }
        const auto waited = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since);
        if (static_cast<uint64_t>(waited.count()) < watchdog_s) continue;
        CpuContext* ctx = park.main_ctx.load();
        const uint32_t pc = ctx ? ctx->cpuRegs.pc : 0;
        const std::string detail = "no boundary for " + std::to_string(watchdog_s) + "s; last syscall #" +
                                   std::to_string(core.syscall_count);
        census.Write();
        if (auto* meta = host.Meta()) {
            if (ctx) {
                meta->Set("watchdog_ra", Hex(ctx->cpuRegs.GPR.r[31].UL[0]));
                meta->Set("watchdog_sp", Hex(ctx->cpuRegs.GPR.r[29].UL[0]));
                meta->Set("watchdog_a0", Hex(ctx->cpuRegs.GPR.r[4].UL[0]));
            }
            meta->Set("threads", LegPark::ThreadCensus());
            meta->Set("token_tid", std::to_string(EeRunToken::Instance().OwnerForTest()));
        }
        park.closer.Close("watchdog", pc, 1, detail.c_str());
        if (knobs.ram_dump) {
            if (std::FILE* f = std::fopen(knobs.ram_dump->c_str(), "wb")) {
                std::fwrite(core.ram.Data(), 1, core.ram.Size(), f);
                std::fclose(f);
                std::fprintf(stderr, "watchdog: RAM dumped to %s\n", knobs.ram_dump->c_str());
            }
        }
        std::fflush(stdout);
        std::_Exit(0);
    }
}

}  // namespace ps2r::title::sly2
