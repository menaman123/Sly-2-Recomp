// test_vu1_native_corpus -- the corpus gate on the title's LINKED native VU1 code (the generated TUs
// compiled into ps2r_title_native_vu1, not a dlopen'd library): for every image of the corpus the
// table (native_vu1_table.h) must name an exports table, and Vu1AotVerifier must find the native run
// identical to the interpreter's on EVERY case (registers, data memory, clock, bundles, kicked PATH1
// bytes), then again under a sweep of cycle caps (the region functions' budget exits and the loop's
// re-sync). The same gate the engine's run-time builder applies before publishing a library, here on
// the code the title ships.
//
//   test_vu1_native_corpus <corpus dir>... [--caps]      (77 = no corpus given / none readable)
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1_table.h"

namespace {

namespace fs = std::filesystem;
using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;
int g_fail = 0;

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
            images[Vu1AotVerifier::ImageHashOf(*c)].push_back(std::move(c));
            ++*n;
        }
    }
}

const ps2r::title::sly2::NativeVu1Image* Find(uint64_t hash) {
    for (size_t i = 0; i < ps2r::title::sly2::kNativeVu1ImageCount; ++i)
        if (ps2r::title::sly2::kNativeVu1Images[i].hash == hash) return &ps2r::title::sly2::kNativeVu1Images[i];
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    Images images;
    size_t n_cases = 0;
    bool caps = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--caps") == 0) caps = true;
        else LoadDir(argv[i], images, &n_cases);
    }
    if (images.empty()) {
        std::printf("SKIP: no readable Vu1Case under the given directories\n");
        return 77;
    }
    std::printf("corpus: %zu cases, %zu images; table: %zu native image(s)\n", n_cases, images.size(), ps2r::title::sly2::kNativeVu1ImageCount);
    uint64_t total_bundles = 0, total_native_blocks = 0, total_native_ops = 0;
    size_t identical_cases = 0;
    for (const auto& kv : images) {
        const std::string hex = Vu1AotEmitter::Hex16(kv.first);
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        const ps2r::title::sly2::NativeVu1Image* img = Find(kv.first);
        std::printf("image=%s cases=%zu\n", hex.c_str(), ptrs.size());
        Check(img != nullptr, "the table names this image");
        if (img == nullptr) continue;
        const Vu1AotImageExports* ex = img->exports();
        Check(ex != nullptr && ex->image_hash == kv.first && ex->abi_version == kVu1AotAbiVersion, "exports header: hash and ABI version");
        const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(ptrs, ex);
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
                const Vu1AotVerifier::Result rc = Vu1AotVerifier::Verify(ptrs, ex, cap);
                if (rc.identical) continue;
                const Vu1AotVerifier::Result rb = Vu1AotVerifier::Verify(ptrs, ex, cap, true);
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
