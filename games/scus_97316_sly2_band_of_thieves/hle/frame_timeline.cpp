// games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.cpp -- FrameTimeline (frame_timeline.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ps2r::title::sly2 {

FrameTimeline::FrameTimeline() : t0_(std::chrono::steady_clock::now()) {
    for (auto& lane : lanes_) lane.reserve(8192);
}

const char* FrameTimeline::LaneName(Lane lane) {
    switch (lane) {
        case Lane::Boundary: return "boundary";
        case Lane::Output: return "output";
        case Lane::Shown: return "shown";
    }
    return "?";
}

void FrameTimeline::Note(Lane lane) { NoteAt(lane, std::chrono::steady_clock::now()); }

void FrameTimeline::NoteAt(Lane lane, std::chrono::steady_clock::time_point at) {
    const int i = static_cast<int>(lane);
    const uint64_t ns = at > t0_ ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(at - t0_).count()) : 0;
    {   // TEMP-PIPE protocol trace (removed before the report)
        static const bool proto_trace = std::getenv("NATIVEEE_PROTO_TRACE") != nullptr;
        if (proto_trace)
            std::fprintf(stderr, "proto: t=%llu lane=%s\n",
                         (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(at.time_since_epoch()).count(), LaneName(lane));
    }
    std::lock_guard<std::mutex> lock(mutex_[i]);
    lanes_[i].push_back(ns);
}

std::vector<uint64_t> FrameTimeline::Instants(Lane lane) const {
    const int i = static_cast<int>(lane);
    std::lock_guard<std::mutex> lock(mutex_[i]);
    return lanes_[i];
}

FrameTimeline::Stats FrameTimeline::StatsOfInstants(const std::vector<uint64_t>& t) {
    Stats s;
    s.n = t.size();
    if (t.size() < 2) return s;
    std::vector<uint64_t> gaps;
    gaps.reserve(t.size() - 1);
    for (size_t k = 1; k < t.size(); ++k) gaps.push_back(t[k] >= t[k - 1] ? t[k] - t[k - 1] : 0);
    s.gaps = gaps.size();
    for (uint64_t g : gaps) {
        if (g > 33000000) ++s.over_33;
        if (g > 50000000) ++s.over_50;
    }
    std::sort(gaps.begin(), gaps.end());
    const auto pct = [&](double p) {
        size_t k = static_cast<size_t>(p * static_cast<double>(gaps.size() - 1) + 0.5);
        if (k >= gaps.size()) k = gaps.size() - 1;
        return static_cast<double>(gaps[k]) / 1e6;
    };
    s.p50_ms = pct(0.50);
    s.p99_ms = pct(0.99);
    s.max_ms = static_cast<double>(gaps.back()) / 1e6;
    return s;
}

FrameTimeline::Stats FrameTimeline::StatsOf(Lane lane) const { return StatsOfInstants(Instants(lane)); }

std::string FrameTimeline::Census() const {
    std::string out;
    for (int i = 0; i < kLanes; ++i) {
        const Stats s = StatsOf(static_cast<Lane>(i));
        char row[192];
        std::snprintf(row, sizeof(row), "%s%s=%llu:%llu:%.2f:%.2f:%.2f:%llu:%llu", i ? ";" : "", LaneName(static_cast<Lane>(i)),
                      static_cast<unsigned long long>(s.n), static_cast<unsigned long long>(s.gaps), s.p50_ms, s.p99_ms, s.max_ms,
                      static_cast<unsigned long long>(s.over_33), static_cast<unsigned long long>(s.over_50));
        out += row;
    }
    return out;
}

bool FrameTimeline::WriteCsv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "lane,ordinal,t_ns\n");
    for (int i = 0; i < kLanes; ++i) {
        const std::vector<uint64_t> t = Instants(static_cast<Lane>(i));
        for (size_t k = 0; k < t.size(); ++k)
            std::fprintf(f, "%s,%llu,%llu\n", LaneName(static_cast<Lane>(i)), static_cast<unsigned long long>(k),
                         static_cast<unsigned long long>(t[k]));
    }
    return std::fclose(f) == 0;
}

}  // namespace ps2r::title::sly2
