// games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.cpp -- FrameTimeline (frame_timeline.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_timeline.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace ps2r::title::sly2 {

FrameTimeline::FrameTimeline() : t0_(std::chrono::steady_clock::now()) {
    for (auto& lane : lanes_) lane.reserve(8192);
}

const char* FrameTimeline::LaneName(Lane lane) {
    switch (lane) {
        case Lane::Boundary: return "boundary";
        case Lane::Drained: return "drained";
        case Lane::Consumed: return "consumed";
        case Lane::Output: return "output";
        case Lane::Shown: return "shown";
        case Lane::Input: return "input";
        case Lane::Pad: return "pad";
    }
    return "?";
}

void FrameTimeline::Note(Lane lane, uint64_t frame) { NoteAt(lane, std::chrono::steady_clock::now(), frame); }

void FrameTimeline::NoteAt(Lane lane, std::chrono::steady_clock::time_point at, uint64_t frame) {
    const int i = static_cast<int>(lane);
    const uint64_t ns = at > t0_ ? static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(at - t0_).count()) : 0;
    std::lock_guard<std::mutex> lock(mutex_[i]);
    lanes_[i].push_back(Mark{ns, frame});
}

std::vector<FrameTimeline::Mark> FrameTimeline::Marks(Lane lane) const {
    const int i = static_cast<int>(lane);
    std::lock_guard<std::mutex> lock(mutex_[i]);
    return lanes_[i];
}

uint64_t FrameTimeline::LastFrame(Lane lane) const {
    const int i = static_cast<int>(lane);
    std::lock_guard<std::mutex> lock(mutex_[i]);
    return lanes_[i].empty() ? 0u : lanes_[i].back().frame;
}

std::vector<uint64_t> FrameTimeline::Instants(Lane lane) const {
    const std::vector<Mark> marks = Marks(lane);
    std::vector<uint64_t> t;
    t.reserve(marks.size());
    for (const Mark& m : marks) t.push_back(m.t_ns);
    return t;
}

namespace {

// p50 / p99 / max of a set of durations (ns), as ms.
void Percentiles(std::vector<uint64_t>& v, double* p50, double* p99, double* max) {
    *p50 = *p99 = *max = 0;
    if (v.empty()) return;
    std::sort(v.begin(), v.end());
    const auto pct = [&](double p) {
        size_t k = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
        if (k >= v.size()) k = v.size() - 1;
        return static_cast<double>(v[k]) / 1e6;
    };
    *p50 = pct(0.50);
    *p99 = pct(0.99);
    *max = static_cast<double>(v.back()) / 1e6;
}

}  // namespace

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
    Percentiles(gaps, &s.p50_ms, &s.p99_ms, &s.max_ms);
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

// Boundary frame k (tagged) -> the FIRST instant of `lane` tagged k. A later lane may carry a tag more
// than once (a picture re-posted) -- the first is the one the chain produced.
FrameTimeline::Latency FrameTimeline::LatencyTo(Lane lane) const {
    Latency l;
    const std::vector<Mark> from = Marks(Lane::Boundary);
    const std::vector<Mark> to = Marks(lane);
    std::unordered_map<uint64_t, uint64_t> at;
    at.reserve(to.size());
    for (const Mark& m : to)
        if (m.frame != 0) at.emplace(m.frame, m.t_ns);
    std::vector<uint64_t> d;
    d.reserve(from.size());
    for (const Mark& b : from) {
        if (b.frame == 0) continue;
        const auto it = at.find(b.frame);
        if (it == at.end() || it->second < b.t_ns) { ++l.missing; continue; }
        d.push_back(it->second - b.t_ns);
    }
    l.matched = d.size();
    Percentiles(d, &l.p50_ms, &l.p99_ms, &l.max_ms);
    return l;
}

FrameTimeline::Latency FrameTimeline::LatencyBetween(Lane from_lane, Lane to_lane, uint64_t offset) const {
    Latency l;
    const std::vector<Mark> from = Marks(from_lane);
    const std::vector<Mark> to = Marks(to_lane);
    std::unordered_map<uint64_t, uint64_t> at;
    at.reserve(to.size());
    for (const Mark& m : to)
        if (m.frame != 0) at.emplace(m.frame, m.t_ns);   // the FIRST mark of a tag
    std::vector<uint64_t> d;
    d.reserve(from.size());
    for (const Mark& b : from) {
        if (b.frame == 0) continue;
        const auto it = at.find(b.frame + offset);
        if (it == at.end() || it->second < b.t_ns) { ++l.missing; continue; }
        d.push_back(it->second - b.t_ns);
    }
    l.matched = d.size();
    Percentiles(d, &l.p50_ms, &l.p99_ms, &l.max_ms);
    return l;
}

std::string FrameTimeline::InputLatencyCensus() const {
    uint64_t edges = 0;
    for (const Mark& m : Marks(Lane::Input)) edges += m.frame != 0;
    std::string out = "field_ms=16.67;edges=" + std::to_string(edges);
    struct Stage { const char* name; Lane from; Lane to; uint64_t offset; };
    const Stage stages[] = {
        {"input_to_pad", Lane::Input, Lane::Pad, 0},         // the edge waits for the boundary's sample
        {"pad_to_output", Lane::Pad, Lane::Output, 1},       // the game's tick + the chain behind the EE
        {"output_to_shown", Lane::Output, Lane::Shown, 0},   // the presenter
        {"input_to_shown", Lane::Input, Lane::Shown, 1},     // end to end (the judge adds the game's extra depth)
        {"boundary_to_output", Lane::Boundary, Lane::Output, 0},
        {"boundary_to_shown", Lane::Boundary, Lane::Shown, 0},
    };
    for (const Stage& st : stages) {
        const Latency l = LatencyBetween(st.from, st.to, st.offset);
        char row[160];
        std::snprintf(row, sizeof(row), ";%s=%llu/%llu:%.2f/%.2f/%.2f", st.name, static_cast<unsigned long long>(l.matched),
                      static_cast<unsigned long long>(l.missing), l.p50_ms, l.p99_ms, l.max_ms);
        out += row;
    }
    return out;
}

std::string FrameTimeline::LatencyCensus() const {
    uint64_t tagged = 0;
    for (const Mark& b : Marks(Lane::Boundary)) tagged += b.frame != 0;
    std::string out = "frames=" + std::to_string(tagged);
    if (tagged == 0) return out;
    for (int i = 1; i <= static_cast<int>(Lane::Shown); ++i) {   // the chain lanes; input / pad are latency='s
        const Latency l = LatencyTo(static_cast<Lane>(i));
        char row[160];
        std::snprintf(row, sizeof(row), ";%s=%llu/%llu:%.2f/%.2f/%.2f", LaneName(static_cast<Lane>(i)),
                      static_cast<unsigned long long>(l.matched), static_cast<unsigned long long>(l.missing), l.p50_ms, l.p99_ms, l.max_ms);
        out += row;
    }
    return out;
}

bool FrameTimeline::WriteCsv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "lane,ordinal,t_ns,frame\n");
    for (int i = 0; i < kLanes; ++i) {
        const std::vector<Mark> t = Marks(static_cast<Lane>(i));
        for (size_t k = 0; k < t.size(); ++k)
            std::fprintf(f, "%s,%llu,%llu,%llu\n", LaneName(static_cast<Lane>(i)), static_cast<unsigned long long>(k),
                         static_cast<unsigned long long>(t[k].t_ns), static_cast<unsigned long long>(t[k].frame));
    }
    return std::fclose(f) == 0;
}

}  // namespace ps2r::title::sly2
