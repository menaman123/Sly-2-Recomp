#include "games/scus_97316_sly2_band_of_thieves/hle/leg_trace.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "engine/cdvd/iso_fs.h"
#include "engine/core/log/log.h"
#include "engine/core/verify/run_meta.h"
#include "engine/ee/cpu/access_tracer.h"
#include "engine/gif/draw_attribution.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"

namespace ps2r::title::sly2 {

namespace {

constexpr uint32_t kSceCdRead = 0x00242840;   // hle/sdk_bindings.tsv libcdvd.a sceCdRead
constexpr size_t kReturnedPtrsMax = 1u << 20;
constexpr size_t kDistinctPtrsMax = 4096;

// "<head>[;k=v;...]" -> head and the k=v pairs.
std::string SplitSpec(const std::string& spec, std::map<std::string, std::string>& kv) {
    size_t semi = spec.find(';');
    const std::string head = spec.substr(0, semi);
    while (semi != std::string::npos) {
        const size_t next = spec.find(';', semi + 1);
        const std::string item = spec.substr(semi + 1, next == std::string::npos ? std::string::npos : next - semi - 1);
        const size_t eq = item.find('=');
        if (eq != std::string::npos) kv[item.substr(0, eq)] = item.substr(eq + 1);
        semi = next;
    }
    return head;
}

std::vector<uint32_t> HexList(const std::string& list) {
    std::vector<uint32_t> out;
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        if (comma == std::string::npos) comma = list.size();
        if (comma > pos) out.push_back(static_cast<uint32_t>(std::strtoul(list.substr(pos, comma - pos).c_str(), nullptr, 16)));
        pos = comma + 1;
    }
    return out;
}

bool InRam(uint64_t v) { return v >= 0x00100000ull && v < 0x02000000ull; }

}  // namespace

LegTrace::LegTrace(const LegKnobs& knobs, LegDisc& disc, const std::string& title_serial) : title_serial_(title_serial) {
    if (knobs.corpus) ArmCorpus(*knobs.corpus);
    if (knobs.fn_ring) ArmRing(*knobs.fn_ring);
    if (knobs.hook_census) {
        hook_census_file_ = *knobs.hook_census;
        FnHooks::ArmAll(FnHooks::kCount);
    }
    if (knobs.disc_attr) ArmDisc(*knobs.disc_attr, disc);
    if (knobs.draw_attr) {
        draw_file_ = *knobs.draw_attr;
        DrawAttribution::Enable(true);
    }
    if (knobs.alloc_census) {
        alloc_census_file_ = *knobs.alloc_census;
        observers_[1] = FnHooks::AddObserver(FnHooks::kAttrib, &LegTrace::OnAllocCensus, this);
        FnHooks::ArmAll(FnHooks::kAttrib);
    }
    if (knobs.alloc_log) ArmAllocLog(*knobs.alloc_log);
    if (FnHooks::ArmedCount())
        PS2R_LOG(HOST, Info, "trace: %u of %u functions armed in the hook table", FnHooks::ArmedCount(), FnHooks::FunctionCount());
}

void LegTrace::ArmCorpus(const std::string& spec) {
    std::map<std::string, std::string> kv;
    corpus_path_ = SplitSpec(spec, kv);
    std::vector<uint32_t> fns;
    if (kv.count("fns")) fns = HexList(kv["fns"]);
    const uint64_t max_distinct = kv.count("max") ? std::strtoull(kv["max"].c_str(), nullptr, 10) : 0;
    std::vector<std::pair<std::string, std::string>> meta;
    meta.emplace_back("title", "\"" + title_serial_ + "\"");
    std::string error;
    if (!AccessTracer::Start(corpus_path_, fns, meta, max_distinct, &error)) {
        corpus_error_ = error;
        std::fprintf(stderr, "nativeee: corpus: %s\n", error.c_str());
        corpus_path_.clear();
        return;
    }
    PS2R_LOG(HOST, Info, "corpus: recording %zu function(s) (0 = all) -> %s", fns.size(), corpus_path_.c_str());
}

void LegTrace::ArmRing(const std::string& spec) {
    std::map<std::string, std::string> kv;
    ring_fns_ = HexList(SplitSpec(spec, kv));
    if (kv.count("n")) FnHooks::SetRingCapacity(static_cast<uint32_t>(std::strtoul(kv["n"].c_str(), nullptr, 10)));
    ring_file_ = kv.count("file") ? kv["file"] : std::string();
    for (uint32_t fn : ring_fns_)
        if (!FnHooks::Arm(fn, FnHooks::kRing)) std::fprintf(stderr, "nativeee: fn-ring: 0x%08x is not a function start\n", fn);
}

void LegTrace::ArmDisc(const std::string& path, LegDisc& disc) {
    disc_file_ = path;
    if (disc.iso_open) {
        // lbn -> file: every entry of the volume, walked from the root.
        std::vector<std::pair<IsoFs::Entry, std::string>> todo{{disc.iso_fs.Root(), ""}};
        while (!todo.empty()) {
            auto [dir, prefix] = todo.back();
            todo.pop_back();
            std::vector<IsoFs::Entry> entries;
            if (!disc.iso_fs.List(dir, entries)) continue;
            for (const IsoFs::Entry& e : entries) {
                const std::string full = prefix + "/" + e.name;
                if (e.directory) todo.emplace_back(e, full);
                else disc_files_[e.lba] = {(e.size + IsoFs::kSectorSize - 1) / IsoFs::kSectorSize, full};
            }
        }
    }
    observers_[0] = FnHooks::AddObserver(FnHooks::kAttrib, &LegTrace::OnDisc, this);
    if (!FnHooks::Arm(kSceCdRead, FnHooks::kAttrib)) std::fprintf(stderr, "nativeee: disc-attr: sceCdRead 0x%08x is not a function start\n", kSceCdRead);
}

void LegTrace::ArmAllocLog(const std::string& spec) {
    std::map<std::string, std::string> kv;
    alloc_log_file_ = SplitSpec(spec, kv);
    if (kv.count("alloc")) alloc_fn_ = static_cast<uint32_t>(std::strtoul(kv["alloc"].c_str(), nullptr, 16));
    if (kv.count("free")) free_fn_ = static_cast<uint32_t>(std::strtoul(kv["free"].c_str(), nullptr, 16));
    if (!alloc_fn_) {
        std::fprintf(stderr, "nativeee: alloc-log: no allocator identified for this title yet; pass ;alloc=<hex>[;free=<hex>]\n");
        alloc_log_file_.clear();
        return;
    }
    alloc_log_ = std::fopen(alloc_log_file_.c_str(), "w");
    if (!alloc_log_) { alloc_log_file_.clear(); return; }
    std::fprintf(alloc_log_, "# kind frame caller fn a0 a1 v0\n");
    observers_[2] = FnHooks::AddObserver(FnHooks::kAttrib, &LegTrace::OnAllocLog, this);
    FnHooks::Arm(alloc_fn_, FnHooks::kAttrib);
    if (free_fn_) FnHooks::Arm(free_fn_, FnHooks::kAttrib);
}

std::string LegTrace::FileOf(uint32_t lbn) const {
    auto it = disc_files_.upper_bound(lbn);
    if (it == disc_files_.begin()) return "?";
    --it;
    if (lbn >= it->first + it->second.first) return "?";
    return it->second.second + "+" + std::to_string((lbn - it->first) * IsoFs::kSectorSize);
}

void LegTrace::OnDisc(void* user, const FnHooks::Call& call, bool exit) {
    if (!exit || call.fn != kSceCdRead) return;
    auto* self = static_cast<LegTrace*>(user);
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->disc_rows_.size() < 1000000)
        self->disc_rows_.push_back(DiscRow{call.frame, call.caller, static_cast<uint32_t>(call.args[0]), static_cast<uint32_t>(call.args[1]),
                                           static_cast<uint32_t>(call.args[2]), static_cast<uint32_t>(call.ret[0])});
}

void LegTrace::OnAllocCensus(void* user, const FnHooks::Call& call, bool exit) {
    auto* self = static_cast<LegTrace*>(user);
    if (call.entry != call.fn) return;
    std::lock_guard<std::mutex> lock(self->mutex_);
    AllocRow& row = self->alloc_rows_[call.fn];
    if (!exit) {
        row.calls++;
        if (call.args[0] && call.args[0] < 0x100000ull) row.size_like_a0++;
        auto it = self->returned_ptrs_.find(static_cast<uint32_t>(call.args[0]));
        if (it != self->returned_ptrs_.end() && it->second != call.fn) {
            self->alloc_rows_[it->second].ptr_used_as_a0++;
            self->returned_ptrs_.erase(it);   // credited once per returned pointer
        }
        return;
    }
    if (call.unwound || !InRam(call.ret[0])) return;
    row.ptr_returns++;
    auto& set = self->ptr_sets_[call.fn];
    if (set.size() < kDistinctPtrsMax) set.insert(static_cast<uint32_t>(call.ret[0]));
    row.distinct_ptrs = set.size();
    if (self->returned_ptrs_.size() < kReturnedPtrsMax) self->returned_ptrs_[static_cast<uint32_t>(call.ret[0])] = call.fn;
}

void LegTrace::OnAllocLog(void* user, const FnHooks::Call& call, bool exit) {
    auto* self = static_cast<LegTrace*>(user);
    if (call.entry != call.fn || !self->alloc_log_) return;
    const bool is_alloc = call.fn == self->alloc_fn_;
    if (is_alloc != exit) return;   // alloc rows at exit (the address is v0), free rows at entry
    std::lock_guard<std::mutex> lock(self->mutex_);
    std::fprintf(self->alloc_log_, "%s %u %08x %08x %llx %llx %llx\n", is_alloc ? "alloc" : "free", call.frame, call.caller, call.fn,
                 static_cast<unsigned long long>(call.args[0]), static_cast<unsigned long long>(call.args[1]),
                 static_cast<unsigned long long>(call.ret[0]));
}

void LegTrace::Write(ps2r::verify::RunMeta* meta) {
    if (!corpus_path_.empty()) {
        const AccessTracer::Stats s = AccessTracer::CurrentStats();
        std::string error;
        const bool ok = AccessTracer::Stop(&error);
        std::string row = std::string("file=") + corpus_path_ + ":calls=" + std::to_string(s.calls) + ":distinct=" + std::to_string(s.distinct) +
                          ":dropped=" + std::to_string(s.dropped) + ":accesses=" + std::to_string(s.accesses) +
                          ":unattributed=" + std::to_string(s.unattributed) + ":bytes=" + std::to_string(s.bytes) +
                          ":truncated=" + (s.truncated ? "1" : "0") + ":frames=" + std::to_string(s.first_frame) + "-" + std::to_string(s.last_frame) +
                          ":written=" + (ok ? "1" : "0");
        if (!ok) { row += ":error=" + error; std::fprintf(stderr, "nativeee: corpus: %s\n", error.c_str()); }
        if (meta) meta->Set("trace_corpus", row);
        corpus_path_.clear();
    } else if (!corpus_error_.empty() && meta) {
        meta->Set("trace_corpus", "error=" + corpus_error_);
    }
    WriteRings();
    WriteHookCensus();
    WriteDisc();
    WriteDraw();
    WriteAllocCensus();
    if (alloc_log_) { std::fclose(alloc_log_); alloc_log_ = nullptr; }
    if (meta && FnHooks::ProbedCalls())
        meta->Set("trace_hooks", "probed_calls=" + std::to_string(FnHooks::ProbedCalls()) + ":unwinds=" + std::to_string(FnHooks::Unwinds()) +
                                     ":probe_stack_overflows=" + std::to_string(FnHooks::ProbeStackOverflows()) + ":armed=" + std::to_string(FnHooks::ArmedCount()));
}

void LegTrace::WriteRings() {
    if (ring_fns_.empty()) return;
    FILE* f = ring_file_.empty() ? stderr : std::fopen(ring_file_.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# fn frame seq depth caller ra sp a0 a1 a2 a3 t0 t1 t2 t3 [sp+10] [sp+14] [sp+18] [sp+1c] v0 v1\n");
    for (uint32_t fn : ring_fns_) {
        for (const FnHooks::Call& c : FnHooks::Recent(fn, UINT32_MAX)) {
            std::fprintf(f, "%08x %u %llu %u %08x %08x %08x", c.fn, c.frame, static_cast<unsigned long long>(c.seq), c.depth, c.caller, c.ra, c.sp);
            for (uint32_t i = 0; i < FnHooks::kArgs; i++) std::fprintf(f, " %llx", static_cast<unsigned long long>(c.args[i]));
            std::fprintf(f, " %llx %llx%s\n", static_cast<unsigned long long>(c.ret[0]), static_cast<unsigned long long>(c.ret[1]), c.unwound ? " unwound" : "");
        }
    }
    if (f != stderr) std::fclose(f);
    ring_fns_.clear();
}

void LegTrace::WriteHookCensus() {
    if (hook_census_file_.empty()) return;
    FILE* f = std::fopen(hook_census_file_.c_str(), "w");
    if (!f) return;
    for (const auto& [id, count] : FnHooks::Counts()) std::fprintf(f, "%08x %llu\n", FnHooks::AddressOfId(id), static_cast<unsigned long long>(count));
    std::fclose(f);
    hook_census_file_.clear();
}

void LegTrace::WriteDisc() {
    if (disc_file_.empty()) return;
    FILE* f = std::fopen(disc_file_.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# frame caller lbn sectors buf result file+offset\n");
    std::lock_guard<std::mutex> lock(mutex_);
    for (const DiscRow& r : disc_rows_)
        std::fprintf(f, "%u %08x %u %u %08x %u %s\n", r.frame, r.caller, r.lbn, r.sectors, r.buf, r.result, FileOf(r.lbn).c_str());
    std::fclose(f);
    disc_file_.clear();
}

void LegTrace::WriteDraw() {
    if (draw_file_.empty()) return;
    FILE* f = std::fopen(draw_file_.c_str(), "w");
    if (!f) return;
    std::vector<DrawAttribution::CensusRow> rows = DrawAttribution::Census();
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.packets > b.packets; });
    std::fprintf(f, "# delivered=%llu unattributed=%llu\n# fn packets bytes\n", static_cast<unsigned long long>(DrawAttribution::Delivered()),
                 static_cast<unsigned long long>(DrawAttribution::Unattributed()));
    for (const auto& r : rows) std::fprintf(f, "%08x %llu %llu\n", r.fn, static_cast<unsigned long long>(r.packets), static_cast<unsigned long long>(r.bytes));
    std::fclose(f);
    draw_file_.clear();
}

void LegTrace::WriteAllocCensus() {
    if (alloc_census_file_.empty()) return;
    FILE* f = std::fopen(alloc_census_file_.c_str(), "w");
    if (!f) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<uint32_t, AllocRow>> rows(alloc_rows_.begin(), alloc_rows_.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.ptr_used_as_a0 > b.second.ptr_used_as_a0; });
    std::fprintf(f, "# fn calls ptr_returns distinct_ptrs ptr_used_as_a0 size_like_a0\n");
    for (const auto& [fn, r] : rows) {
        if (!r.ptr_returns) continue;
        std::fprintf(f, "%08x %llu %llu %llu %llu %llu\n", fn, static_cast<unsigned long long>(r.calls), static_cast<unsigned long long>(r.ptr_returns),
                     static_cast<unsigned long long>(r.distinct_ptrs), static_cast<unsigned long long>(r.ptr_used_as_a0),
                     static_cast<unsigned long long>(r.size_like_a0));
    }
    std::fclose(f);
    alloc_census_file_.clear();
}

}  // namespace ps2r::title::sly2
