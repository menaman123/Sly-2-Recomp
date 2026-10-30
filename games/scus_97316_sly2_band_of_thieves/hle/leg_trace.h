// LegTrace: the NATIVEEE_* knobs over the engine's runtime hook table (FnHooks), the tracing build's
// per-call corpus (AccessTracer), the draw / disc attribution tables and the allocator census. Every
// knob is a debugging or recording aid: with none set this phase arms nothing and the dispatch
// pays only the probe byte test the generated code always has. Outputs are written at the census
// (LegCensus::Write), so they land on every exit path before _Exit.
//
//   NATIVEEE_CORPUS=<file>[;fns=<hex>[,<hex>...]][;max=<distinct>]   RACORP1 per-call corpus (tracing
//       build only; refused with a message otherwise). The file holds game memory: data store only.
//   NATIVEEE_FN_RING=<hex>[,<hex>...][;n=<N>][;file=<path>]          ring of the last N calls per function
//   NATIVEEE_HOOK_CENSUS=<file>                                      call counts through the hook table
//                                                                    (`%08x count` lines, NATIVEEE_FN_CENSUS's format)
//   NATIVEEE_DISC_ATTR=<file>        every sceCdRead: frame, caller, lbn, sectors, buffer, result, file
//   NATIVEEE_DRAW_ATTR=<file>        GIF packets delivered per submitting function (DrawAttribution census)
//   NATIVEEE_ALLOC_CENSUS=<file>     the allocator census: per function, pointer-like returns and how many
//                                    of them were later passed as a0 (or stored) -- the evidence tool
//   NATIVEEE_ALLOC_LOG=<file>[;alloc=<hex>][;free=<hex>]   alloc/free log (frame, caller, size, address)
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ee/cpu/fn_hooks.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"

namespace ps2r::verify { class RunMeta; }

namespace ps2r::title::sly2 {

class LegDisc;

class LegTrace {
public:
    LegTrace(const LegKnobs& knobs, LegDisc& disc, const std::string& title_serial);
    LegTrace(const LegTrace&) = delete;
    LegTrace& operator=(const LegTrace&) = delete;

    // Writes every armed output and stops the corpus recording; run.meta rows trace_*.
    void Write(ps2r::verify::RunMeta* meta);

private:
    struct DiscRow {
        uint32_t frame, caller, lbn, sectors, buf, result;
    };
    struct AllocRow {
        uint64_t calls = 0;
        uint64_t ptr_returns = 0;     // v0 in main RAM, nonzero
        uint64_t ptr_used_as_a0 = 0;  // such a v0 later the a0 of some call
        uint64_t size_like_a0 = 0;    // a0 < 0x100000 at entry
        uint64_t distinct_ptrs = 0;
    };

    static void OnDisc(void* user, const FnHooks::Call& call, bool exit);
    static void OnAllocCensus(void* user, const FnHooks::Call& call, bool exit);
    static void OnAllocLog(void* user, const FnHooks::Call& call, bool exit);
    void ArmCorpus(const std::string& spec);
    void ArmRing(const std::string& spec);
    void ArmDisc(const std::string& path, LegDisc& disc);
    void ArmAllocLog(const std::string& spec);
    std::string FileOf(uint32_t lbn) const;
    void WriteRings();
    void WriteHookCensus();
    void WriteDisc();
    void WriteDraw();
    void WriteAllocCensus();

    const std::string title_serial_;
    std::string corpus_path_, corpus_error_;
    std::vector<uint32_t> ring_fns_;
    std::string ring_file_;
    std::string hook_census_file_;
    std::string disc_file_;
    std::map<uint32_t, std::pair<uint32_t, std::string>> disc_files_;   // lbn -> (sectors, path)
    std::string draw_file_;
    std::string alloc_census_file_;
    std::string alloc_log_file_;
    uint32_t alloc_fn_ = 0, free_fn_ = 0;
    FILE* alloc_log_ = nullptr;

    std::mutex mutex_;
    std::vector<DiscRow> disc_rows_;
    std::unordered_map<uint32_t, AllocRow> alloc_rows_;           // by fn
    std::unordered_map<uint32_t, uint32_t> returned_ptrs_;         // ptr -> returning fn (bounded)
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> ptr_sets_;   // fn -> distinct ptrs (bounded)
    uint32_t observers_[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
};

}  // namespace ps2r::title::sly2
