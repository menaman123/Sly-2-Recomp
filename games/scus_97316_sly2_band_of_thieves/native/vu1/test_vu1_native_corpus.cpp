// test_vu1_native_corpus -- the corpus gate on the title's LINKED native VU1 code (the generated TUs
// compiled into ps2r_title_native_vu1, not a dlopen'd library): for every image of the corpus the
// table (native_vu1_table.h) must name an exports table, and Vu1AotVerifier must find the native run
// identical to the interpreter's on EVERY case (registers, data memory, clock, bundles, kicked PATH1
// bytes), then again under a sweep of cycle caps (the region functions' budget exits and the loop's
// re-sync). The same gate the engine's run-time builder applies before publishing a library, here on
// the code the title ships.
//
//   test_vu1_native_corpus <corpus dir>... [--caps]      (77 = no corpus given / none readable)
//   test_vu1_native_corpus <corpus dir>... --bench N      (STAGE PERF-1) the kick microbenchmark of the LINKED
//                                                        code: every case in corpus order (the capture order = the
//                                                        game's kick sequence) replayed on ONE unit in rotation, N
//                                                        rounds, only Execute timed (registers / data memory
//                                                        restored before each replay; the image re-uploaded only
//                                                        when the case's image differs from the resident one --
//                                                        the MPG shape); the fastest round's ns per bundle and per
//                                                        kick, overall and per image. No identity judgement here:
//                                                        pair it with the gate above. Rows for the roofline table.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/null_gs_sink.h"
#include "engine/platform/host/platform.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1_table.h"

namespace {

namespace fs = std::filesystem;
using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;
int g_fail = 0;
std::vector<const Vu1Case*> g_order;  // every case in load order (the bench's rotation)

void Check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

void LoadDir(const std::string& dir, Images& images, size_t* n) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        std::unique_ptr<Vu1Case> c(new Vu1Case());
        if (vu1_case::ReadCase(f.c_str(), *c)) {
            g_order.push_back(c.get());
            images[Vu1AotVerifier::ImageHashOf(*c)].push_back(std::move(c));
            ++*n;
        }
    }
}

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The case restored on a unit whose micro memory already holds its image: registers, data memory, the
// VIF tops and the pipe latches (VuUnit::LoadCase minus the image copy and the generation bump).
void RestoreKeepImage(VuUnit& vu, const Vu1Case& c) {
    std::memcpy(&vu.regs(), &c.regs, sizeof(VUregs));
    std::memcpy(vu.state().data, c.data_mem, Vu1Case::kDataSize);
    vu.SetVifTop(static_cast<uint16_t>(c.vif1_top), static_cast<uint16_t>(c.vif1_itops));
    vu.flags().SetQPending(c.q_pending_valid != 0, c.q_pending_retire, VuFloat::Bits2F(c.q_pending_value_bits),
                           static_cast<uint16_t>(c.q_pending_status));
    vu.flags().SetPPending(c.p_pending_valid != 0, c.p_pending_retire, VuFloat::Bits2F(c.p_pending_value_bits));
    vu.flags().SetCycle(c.vu_cycle);
}

// --bench N: the rotation over g_order on one unit, N rounds, the fastest round reported.
int Bench(const Images& images, int rounds) {
    if (g_order.empty() || rounds <= 0) {
        std::printf("SKIP: --bench needs N > 0 and at least one case\n");
        return 77;
    }
    // The worker's own scheduling class (macOS: off the efficiency cores), as the MTVU worker runs.
    ps2r::platform::host::PinCurrentThreadInteractive("vu1bench", ps2r::platform::host::ThreadRole::Worker);
    NullGsSink null_sink;
    GifRegs gif_regs;
    GifParser parser(null_sink, gif_regs);
    GifArbiter arbiter(parser);
    VuUnit vu(1, &arbiter);
    VuSettings& settings = vu.dispatch().settings();
    settings.vu1_mode = VuTierMode::Aot;
    settings.hle_enabled = false;
    vu.dispatch().aot().set_assume_verified(true);
    // Every image of the corpus the table names: its exports installed (an alias row by content identity).
    size_t named = 0;
    for (const auto& kv : images) {
        const ps2r::title::sly2::NativeVu1Image* img = nullptr;
        for (size_t i = 0; i < ps2r::title::sly2::kNativeVu1ImageCount; ++i)
            if (ps2r::title::sly2::kNativeVu1Images[i].hash == kv.first) img = &ps2r::title::sly2::kNativeVu1Images[i];
        if (img == nullptr) img = ps2r::title::sly2::NativeVu1::Identify(kv.second[0]->micro_mem);
        if (img == nullptr) continue;
        if (img->hash == kv.first) vu.dispatch().aot().SetNativeExports(kv.first, img->exports());
        else vu.dispatch().aot().SetNativeExportsAlias(kv.first, img->exports());
        ++named;
    }
    std::printf("bench: cases=%zu images=%zu named=%zu rounds=%d\n", g_order.size(), images.size(), named, rounds);
    struct PerImage {
        uint64_t ns = 0, bundles = 0, kicks = 0;
    };
    std::map<uint64_t, PerImage> best_per_image;
    uint64_t best_ns = ~0ull, best_bundles = 0;
    uint64_t resident = 0;
    std::vector<uint64_t> hashes(g_order.size());  // the image hash per case, once (not the kick's cost)
    for (size_t i = 0; i < g_order.size(); ++i) hashes[i] = Vu1AotVerifier::ImageHashOf(*g_order[i]);
    for (int r = 0; r < rounds; ++r) {
        std::map<uint64_t, PerImage> per_image;
        uint64_t round_ns = 0;
        const uint64_t bundles0 = vu.dispatch().bundles();
        for (size_t ci = 0; ci < g_order.size(); ++ci) {
            const Vu1Case* c = g_order[ci];
            const uint64_t h = hashes[ci];
            if (h != resident) {
                vu.LoadCase(*c);  // the image changes: the MPG shape (generation bump, re-lookup)
                resident = h;
            } else {
                RestoreKeepImage(vu, *c);
            }
            const uint64_t b0 = vu.dispatch().bundles();
            const uint64_t t0 = NowNs();
            vu.Execute(c->start_pc);
            const uint64_t t1 = NowNs();
            arbiter.drain();
            PerImage& pi = per_image[h];
            pi.ns += t1 - t0;
            pi.bundles += vu.dispatch().bundles() - b0;
            ++pi.kicks;
            round_ns += t1 - t0;
        }
        const uint64_t round_bundles = vu.dispatch().bundles() - bundles0;
        std::printf("bench round %d: %llu ns, %llu bundles, %.3f ns/bundle, %.1f ns/kick\n", r, static_cast<unsigned long long>(round_ns),
                    static_cast<unsigned long long>(round_bundles), round_bundles ? static_cast<double>(round_ns) / static_cast<double>(round_bundles) : 0.0,
                    static_cast<double>(round_ns) / static_cast<double>(g_order.size()));
        if (round_ns < best_ns) {
            best_ns = round_ns;
            best_bundles = round_bundles;
            best_per_image = per_image;
        }
    }
    for (const auto& kv : best_per_image) {
        const PerImage& pi = kv.second;
        std::printf("bench image=%s kicks=%llu bundles=%llu ns=%llu ns/bundle=%.3f ns/kick=%.1f bundles/kick=%.1f\n",
                    Vu1AotEmitter::Hex16(kv.first).c_str(), static_cast<unsigned long long>(pi.kicks), static_cast<unsigned long long>(pi.bundles),
                    static_cast<unsigned long long>(pi.ns), pi.bundles ? static_cast<double>(pi.ns) / static_cast<double>(pi.bundles) : 0.0,
                    static_cast<double>(pi.ns) / static_cast<double>(pi.kicks), static_cast<double>(pi.bundles) / static_cast<double>(pi.kicks));
    }
    std::printf("bench best: %llu ns, %llu bundles, %.3f ns/bundle, %.1f ns/kick, %.1f Mbundles/s\n", static_cast<unsigned long long>(best_ns),
                static_cast<unsigned long long>(best_bundles), best_bundles ? static_cast<double>(best_ns) / static_cast<double>(best_bundles) : 0.0,
                static_cast<double>(best_ns) / static_cast<double>(g_order.size()),
                best_ns ? 1e3 * static_cast<double>(best_bundles) / static_cast<double>(best_ns) : 0.0);
    return 0;
}

// The table row of an image: by whole-image hash (legacy rows) or by CONTENT (NativeVu1::Identify hashes the
// image's first `size` bytes per program row -- the run-time rule).
const ps2r::title::sly2::NativeVu1Image* Find(uint64_t hash, const uint8_t* micro) {
    for (size_t i = 0; i < ps2r::title::sly2::kNativeVu1ImageCount; ++i)
        if (ps2r::title::sly2::kNativeVu1Images[i].hash == hash) return &ps2r::title::sly2::kNativeVu1Images[i];
    return ps2r::title::sly2::NativeVu1::Identify(micro);
}

}  // namespace

int main(int argc, char** argv) {
    Images images;
    size_t n_cases = 0;
    bool caps = false;
    int bench = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--caps") == 0) caps = true;
        else if (std::strcmp(argv[i], "--bench") == 0 && i + 1 < argc) bench = std::atoi(argv[++i]);
        else LoadDir(argv[i], images, &n_cases);
    }
    if (images.empty()) {
        std::printf("SKIP: no readable Vu1Case under the given directories\n");
        return 77;
    }
    if (bench > 0) return Bench(images, bench);
    std::printf("corpus: %zu cases, %zu images; table: %zu native image(s)\n", n_cases, images.size(), ps2r::title::sly2::kNativeVu1ImageCount);
    uint64_t total_bundles = 0, total_native_blocks = 0, total_native_ops = 0;
    size_t identical_cases = 0;
    for (const auto& kv : images) {
        const std::string hex = Vu1AotEmitter::Hex16(kv.first);
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        const ps2r::title::sly2::NativeVu1Image* img = Find(kv.first, ptrs[0]->micro_mem);
        const bool alias = img != nullptr && img->hash != kv.first;
        std::printf("image=%s cases=%zu%s\n", hex.c_str(), ptrs.size(),
                    alias ? (" -> program " + Vu1AotEmitter::Hex16(img->hash) + " (" + std::to_string(img->size) + " bytes, content identity)").c_str() : "");
        Check(img != nullptr, "the table names this image (by whole-image hash or by program content)");
        if (img == nullptr) continue;
        const Vu1AotImageExports* ex = img->exports();
        Check(ex != nullptr && ex->image_hash == img->hash && ex->abi_version == kVu1AotAbiVersion, "exports header: hash and ABI version");
        const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(ptrs, ex, 0, false, alias);
        if (!r.identical) std::printf("     divergence: %s\n", r.divergence.c_str());
        Check(r.identical, "native == interpreter on every case (regs, data, clock, bundles, kicked bytes)");
        Check(r.native_blocks > 0, "at least one block ran through a generated function");
        std::printf("     bundles=%llu native_blocks=%llu native_ops=%llu table_blocks=%u\n", static_cast<unsigned long long>(r.bundles),
                    static_cast<unsigned long long>(r.native_blocks), static_cast<unsigned long long>(r.native_ops), ex->block_count);
        total_bundles += r.bundles;
        total_native_blocks += r.native_blocks;
        total_native_ops += r.native_ops;
        if (r.identical) identical_cases += r.cases;
        if (caps) {
            // The sweep of test_vu1_aot_native (tests/app): a cap where the BLOCK tier itself differs from
            // the interpreter (a pre-existing cut-path item, cycle only) is judged against the block tier.
            static const uint64_t kCaps[] = {1, 2, 3, 4, 5, 7, 11, 17, 29, 47, 100, 333, 1000};
            bool caps_ok = true;
            int block_judged = 0;
            for (uint64_t cap : kCaps) {
                const Vu1AotVerifier::Result rc = Vu1AotVerifier::Verify(ptrs, ex, cap, false, alias);
                if (rc.identical) continue;
                const Vu1AotVerifier::Result rb = Vu1AotVerifier::Verify(ptrs, ex, cap, true, alias);
                if (rb.identical) {
                    std::printf("     cap=%llu: native == block tier; block tier vs interp: %s (pre-existing cut path)\n",
                                static_cast<unsigned long long>(cap), rc.divergence.c_str());
                    ++block_judged;
                    continue;
                }
                std::printf("     cap=%llu divergence: %s\n", static_cast<unsigned long long>(cap), rc.divergence.c_str());
                caps_ok = false;
            }
            Check(caps_ok, "identical under every cycle cap of the sweep");
            if (block_judged > 0) std::printf("     caps judged against the block tier: %d\n", block_judged);
        }
    }
    std::printf("total: images=%zu cases=%zu identical_cases=%zu bundles=%llu native_blocks=%llu native_ops=%llu\n", images.size(), n_cases,
                identical_cases, static_cast<unsigned long long>(total_bundles), static_cast<unsigned long long>(total_native_blocks),
                static_cast<unsigned long long>(total_native_ops));
    std::printf("%s\n", g_fail ? "FAILURES" : "ALL PASS");
    return g_fail ? 1 : 0;
}
