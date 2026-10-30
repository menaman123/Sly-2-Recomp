// vu1_native_gen -- the BUILD-TIME generator of Sly 2's native VU1 microprograms.
//
//   vu1_native_gen --corpus <dir> [--corpus <dir>]... --out <gen dir> [--chain 0|1] [--mac-acc 0|1]
//                  [--region-blocks N] [--region-ops N] [--region-taken 0|1] [--images <images.txt>]
//
// Reads every Vu1Case record (engine/vu/vu1_case.h: the unit state at a kick -- the 16 KB micro memory
// IS the microprogram image, game data that never enters the repository) under the corpus directories,
// groups the cases by image hash (VuImageHash::Fast, the key every engine table uses), and for each
// image runs the engine's generic VU1 AOT generator ONCE, at full-program granularity:
//   1. Vu1AotVerifier::Verify(cases) -- the specialised block table vs the interpreter over the image's
//      cases (the first gate; a divergence refuses the image) and the block pcs the native legs built
//      (JR / JALR targets the structural walk cannot infer);
//   2. Vu1AotEmitter::ReachableBlocks over {the cases' entries, those pcs} -- the closure over fall-through,
//      static targets and the wrapped delay slot;
//   3. Vu1AotEmitter::Emit -> <out>/vu1_<hash>.cpp, one region function per block, the extern "C"
//      vu1_aot_exports_<hash>() table (engine/vu/vu1_aot/vu1_aot_exports.h, ABI v3).
// Then <out>/vu1_native_images.cpp (the table the title's NativeVu1 registry walks: hash -> exports fn),
// <out>/vu1_native_images.txt (hash, cases, entries, blocks, ops: the enumeration, no bytes) and
// <out>/MANIFEST.sha256 is recorded by the CMake step, not here.
//
// Deterministic: the TU text is a function of (image bytes, the case set's entries, the generator, the
// policy) -- no timestamps, no compiler name, no host path; the engine_build field is the fixed token
// "title-native". The --images list, when given, is the AUTHORITATIVE set: an image of the corpus that
// is not listed is skipped (reported), a listed image without cases is an error (the recipe is stale).
// Exit 0 = every listed image emitted; 1 = an image refused / missing; 2 = usage; 77 = no corpus.
//
// Portable: standard C++17 only (filesystem, fstream); no POSIX, no platform defines.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"

namespace {

namespace fs = std::filesystem;

using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;

struct Args {
    std::vector<std::string> corpus;
    std::string out;
    std::string images;  // the authoritative list (optional)
    Vu1AotBlockGen::Policy region;
};

int Usage() {
    std::fprintf(stderr,
                 "usage: vu1_native_gen --corpus <dir>... --out <gen dir> [--images <images.txt>]\n"
                 "                      [--chain 0|1] [--mac-acc 0|1] [--region-blocks N] [--region-ops N] [--region-taken 0|1]\n");
    return 2;
}

bool Parse(int argc, char** argv, Args* a) {
    a->region.chain = true;  // the recorded policy of the title's generated code (the engine's default engine.vu1_aot_chain=on)
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](std::string* dst) {
            if (i + 1 >= argc) return false;
            *dst = argv[++i];
            return true;
        };
        std::string v;
        if (s == "--corpus") { if (!next(&v)) return false; a->corpus.push_back(v); }
        else if (s == "--out") { if (!next(&a->out)) return false; }
        else if (s == "--images") { if (!next(&a->images)) return false; }
        else if (s == "--chain") { if (!next(&v)) return false; a->region.chain = std::atoi(v.c_str()) != 0; }
        else if (s == "--mac-acc") { if (!next(&v)) return false; a->region.mac_acc = std::atoi(v.c_str()) != 0; }
        else if (s == "--region-blocks") { if (!next(&v)) return false; a->region.block_cap = std::atoi(v.c_str()); }
        else if (s == "--region-ops") { if (!next(&v)) return false; a->region.op_cap = std::atoi(v.c_str()); }
        else if (s == "--region-taken") { if (!next(&v)) return false; a->region.taken_targets = std::atoi(v.c_str()) != 0; }
        else return false;
    }
    return !a->corpus.empty() && !a->out.empty();
}

// Every *.bin under `dir`, sorted by name (the order fixes the corpus hash and the entry order).
void LoadDir(const std::string& dir, Images& images, size_t* n_cases) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::fprintf(stderr, "vu1_native_gen: not a directory: %s\n", dir.c_str());
        return;
    }
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
            ++*n_cases;
        }
    }
}

// images.txt: one image per row, "<16 hex>" first, the rest a comment; '#' lines ignored.
bool ReadImageList(const std::string& path, std::set<uint64_t>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        const std::string tok = line.substr(p, 16);
        if (tok.size() != 16) continue;
        out->insert(std::strtoull(tok.c_str(), nullptr, 16));
    }
    return true;
}

bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::string Hex4(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04x", v);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!Parse(argc, argv, &a)) return Usage();
    Images images;
    size_t n_cases = 0;
    for (const std::string& d : a.corpus) LoadDir(d, images, &n_cases);
    if (images.empty()) {
        std::printf("vu1_native_gen: no readable Vu1Case under the corpus directories (nothing generated)\n");
        return 77;
    }
    std::set<uint64_t> listed;
    const bool have_list = !a.images.empty();
    if (have_list && !ReadImageList(a.images, &listed)) {
        std::fprintf(stderr, "vu1_native_gen: cannot read %s\n", a.images.c_str());
        return 2;
    }
    std::error_code ec;
    fs::create_directories(a.out, ec);
    std::printf("vu1_native_gen: corpus %zu cases, %zu images; policy %s\n", n_cases, images.size(), Vu1AotEmitter::PolicyName(a.region).c_str());

    struct Row {
        uint64_t hash;
        size_t cases;
        size_t entries;
        uint32_t blocks;
        uint64_t ops;
        uint64_t bundles;
        size_t bytes;
    };
    std::vector<Row> rows;
    int rc = 0;
    for (const auto& kv : images) {
        const uint64_t hash = kv.first;
        const std::string hex = Vu1AotEmitter::Hex16(hash);
        if (have_list && listed.count(hash) == 0) {
            std::printf("image=%s cases=%zu SKIPPED (not in %s)\n", hex.c_str(), kv.second.size(), a.images.c_str());
            continue;
        }
        std::vector<const Vu1Case*> ptrs;
        for (const auto& c : kv.second) ptrs.push_back(c.get());
        // Gate 1: the specialised table == the interpreter over the image's cases; its native legs' blocks.
        const Vu1AotVerifier::Result pre = Vu1AotVerifier::Verify(ptrs);
        if (!pre.identical) {
            std::printf("image=%s cases=%zu REFUSED at the specialised-table gate: %s\n", hex.c_str(), ptrs.size(), pre.divergence.c_str());
            rc = 1;
            continue;
        }
        std::set<uint32_t> entry_set;
        std::vector<uint32_t> entries(pre.block_pcs);
        for (const Vu1Case* c : ptrs) {
            entries.push_back(c->start_pc & VuBlockTier::kPcMask);
            entry_set.insert(c->start_pc & VuBlockTier::kPcMask);
        }
        VuBlockProgram scratch;
        VuBlockTier::DecodeImage(ptrs[0]->micro_mem, Vu1Case::kMicroSize, scratch);
        scratch.hash = hash;
        const std::vector<uint32_t> pcs = Vu1AotEmitter::ReachableBlocks(scratch, entries, 8192);
        Vu1AotEmitter::Options opt;
        opt.engine_build = "title-native";  // fixed: the TU text must not depend on the build
        opt.region = a.region;
        std::string ent;
        for (uint32_t e : entry_set) ent += (ent.empty() ? "" : ",") + Hex4(e);
        opt.note = "Sly 2 (SCUS_973.16) VU1 image " + hex + "; corpus cases: " + std::to_string(ptrs.size()) + "; kick entries: " + ent +
                   "; generated at build time by games/scus_97316_sly2_band_of_thieves/native/vu1 (vu1_native_gen)";
        const Vu1AotEmitter::Output out = Vu1AotEmitter::Emit(scratch, pcs, opt);
        const std::string path = a.out + "/vu1_" + hex + ".cpp";
        if (!WriteFile(path, out.source)) {
            std::fprintf(stderr, "vu1_native_gen: cannot write %s\n", path.c_str());
            return 1;
        }
        std::printf("image=%s cases=%zu entries=%zu blocks=%u ops=%llu bundles=%llu bytes=%zu -> %s\n", hex.c_str(), ptrs.size(),
                    entry_set.size(), out.blocks, static_cast<unsigned long long>(out.ops), static_cast<unsigned long long>(pre.bundles),
                    out.source.size(), path.c_str());
        rows.push_back({hash, ptrs.size(), entry_set.size(), out.blocks, out.ops, pre.bundles, out.source.size()});
        listed.erase(hash);
    }
    if (have_list && !listed.empty()) {
        for (uint64_t h : listed) std::printf("image=%s listed in %s but NO corpus case carries it (stale recipe)\n", Vu1AotEmitter::Hex16(h).c_str(), a.images.c_str());
        rc = 1;
    }
    // The registry table: the title walks it at startup (native_vu1.cpp).
    std::string tbl;
    tbl += "// GENERATED by vu1_native_gen (games/scus_97316_sly2_band_of_thieves/native/vu1) -- DO NOT EDIT.\n";
    tbl += "// The native VU1 images of the title: image hash -> the exports table of its generated TU.\n";
    tbl += "#include \"games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1_table.h\"\n\n";
    for (const Row& r : rows) tbl += "extern \"C\" const Vu1AotImageExports* vu1_aot_exports_" + Vu1AotEmitter::Hex16(r.hash) + "();\n";
    tbl += "\nnamespace ps2r::title::sly2 {\n\nconst NativeVu1Image kNativeVu1Images[] = {\n";
    for (const Row& r : rows)
        tbl += "    {0x" + Vu1AotEmitter::Hex16(r.hash) + "ull, &vu1_aot_exports_" + Vu1AotEmitter::Hex16(r.hash) + ", " + std::to_string(r.blocks) + "u, " +
               std::to_string(r.ops) + "ull},\n";
    if (rows.empty()) tbl += "    {0ull, nullptr, 0u, 0ull},\n";
    tbl += "};\nconst size_t kNativeVu1ImageCount = " + std::to_string(rows.size()) + ";\n\n}  // namespace ps2r::title::sly2\n";
    if (!WriteFile(a.out + "/vu1_native_images.cpp", tbl)) return 1;
    std::string txt = "# vu1_native_gen -- the images generated (hash, corpus cases, kick entries, blocks, ops, interpreter bundles over the corpus, TU bytes)\n";
    txt += "# policy " + Vu1AotEmitter::PolicyName(a.region) + "\n";
    for (const Row& r : rows)
        txt += Vu1AotEmitter::Hex16(r.hash) + " cases=" + std::to_string(r.cases) + " entries=" + std::to_string(r.entries) + " blocks=" + std::to_string(r.blocks) +
               " ops=" + std::to_string(r.ops) + " bundles=" + std::to_string(r.bundles) + " bytes=" + std::to_string(r.bytes) + "\n";
    if (!WriteFile(a.out + "/vu1_native_images.txt", txt)) return 1;
    std::printf("vu1_native_gen: %zu image(s) emitted into %s%s\n", rows.size(), a.out.c_str(), rc == 0 ? "" : " (INCOMPLETE)");
    return rc;
}
