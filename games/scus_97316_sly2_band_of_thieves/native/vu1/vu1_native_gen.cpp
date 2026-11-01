// vu1_native_gen -- the BUILD-TIME generator of Sly 2's native VU1 microprograms.
//
//   vu1_native_gen --corpus <dir> [--corpus <dir>]... --out <gen dir> [--chain 0|1] [--mac-acc 0|1]
//                  [--region-blocks N] [--region-ops N] [--region-taken 0|1] [--store-back-inline 0|1]
//                  [--cycle-local 0|1] [--entry-clamp 0|1] [--cluster-ops N] [--clamp-mask 0|1] [--images <images.txt>]
//                  [--programs <disc_programs.txt>]
//
// CONTENT IDENTITY (--programs, STAGE NATIVE-2): the disc's VU programs (extract_images.py over the ELF's
// .vutext: id, size, bytes at the load address) are the authority. Every corpus case is assigned to the
// program whose bytes equal its micro memory's prefix (longest match); the cases of one program -- however
// many whole-memory RESIDUE variants they carry -- yield ONE TU named by the CONTENT hash (VuImageHash::Fast
// over the program's bytes), generated from the canonical image (the program's bytes, zero residue) with the
// block closure BOUNDED to the program's extent (a block starting or ending past the end is dropped and
// reported: it would depend on the residue). At run time the title identifies a kicked image by the same
// prefix hash and registers the whole-memory hash as an alias (Vu1AotTier::SetNativeExportsAlias), so a
// program loaded over any residue runs the same generated code. The table rows carry the program size.
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
#include <sstream>
#include <system_error>
#include <vector>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"

namespace {

namespace fs = std::filesystem;

using Images = std::map<uint64_t, std::vector<std::unique_ptr<Vu1Case>>>;

struct Args {
    std::vector<std::string> corpus;
    std::string out;
    std::string images;    // the authoritative list (optional)
    std::string programs;  // disc_programs.txt (content identity; optional)
    Vu1AotBlockGen::Policy region;
};

std::vector<uint32_t> g_vu_addrs;  // disc_programs.txt "vuaddrs": VU addresses the EE code references

// One disc program (disc_programs.txt row + its bytes from <dir>/<id>.bin).
struct Program {
    std::string id;
    std::string unit;
    uint32_t size = 0;
    uint64_t content_hash = 0;
    std::vector<uint8_t> bytes;
};

int Usage() {
    std::fprintf(stderr,
                 "usage: vu1_native_gen --corpus <dir>... --out <gen dir> [--images <images.txt>]\n"
                 "                      [--chain 0|1] [--mac-acc 0|1] [--region-blocks N] [--region-ops N] [--region-taken 0|1]\n"
                 "                      [--store-back-inline 0|1] [--cycle-local 0|1] [--entry-clamp 0|1] [--cluster-ops N] [--clamp-mask 0|1]\n");
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
        else if (s == "--programs") { if (!next(&a->programs)) return false; }
        else if (s == "--chain") { if (!next(&v)) return false; a->region.chain = std::atoi(v.c_str()) != 0; }
        else if (s == "--mac-acc") { if (!next(&v)) return false; a->region.mac_acc = std::atoi(v.c_str()) != 0; }
        else if (s == "--region-blocks") { if (!next(&v)) return false; a->region.block_cap = std::atoi(v.c_str()); }
        else if (s == "--region-ops") { if (!next(&v)) return false; a->region.op_cap = std::atoi(v.c_str()); }
        else if (s == "--region-taken") { if (!next(&v)) return false; a->region.taken_targets = std::atoi(v.c_str()) != 0; }
        else if (s == "--store-back-inline") { if (!next(&v)) return false; a->region.store_back_inline = std::atoi(v.c_str()) != 0; }
        else if (s == "--cycle-local") { if (!next(&v)) return false; a->region.cycle_local = std::atoi(v.c_str()) != 0; }
        else if (s == "--entry-clamp") { if (!next(&v)) return false; a->region.entry_clamp = std::atoi(v.c_str()) != 0; }
        else if (s == "--cluster-ops") { if (!next(&v)) return false; a->region.cluster_ops = std::atoi(v.c_str()); }
        else if (s == "--clamp-mask") { if (!next(&v)) return false; a->region.clamp_mask = std::atoi(v.c_str()) != 0; }
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

// disc_programs.txt: "<id> <unit> <size hex> <content hash 16 hex> <sha256_16> <chunks>"; the bytes in <id>.bin beside it.
bool ReadPrograms(const std::string& path, std::vector<Program>* out) {
    std::ifstream in(path);
    if (!in) return false;
    const fs::path dir = fs::path(path).parent_path();
    std::string line;
    while (std::getline(in, line)) {
        const size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        Program pr;
        std::string size_hex, hash_hex;
        std::istringstream row(line);
        if (!(row >> pr.id >> pr.unit >> size_hex >> hash_hex)) continue;
        if (pr.id == "vuaddrs") {  // the EE code's VU address constants: extra closure entries for every program
            std::istringstream rest(line.substr(p + 7));
            std::string tok;
            while (rest >> tok) g_vu_addrs.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            continue;
        }
        pr.size = static_cast<uint32_t>(std::strtoul(size_hex.c_str(), nullptr, 16));
        pr.content_hash = std::strtoull(hash_hex.c_str(), nullptr, 16);
        std::ifstream bin((dir / (pr.id + ".bin")).string(), std::ios::binary);
        if (!bin) {
            std::fprintf(stderr, "vu1_native_gen: %s: no %s.bin beside it\n", path.c_str(), pr.id.c_str());
            return false;
        }
        pr.bytes.assign(std::istreambuf_iterator<char>(bin), std::istreambuf_iterator<char>());
        if (pr.bytes.size() != pr.size || VuImageHash::Fast(pr.bytes.data(), pr.bytes.size()) != pr.content_hash) {
            std::fprintf(stderr, "vu1_native_gen: %s.bin does not match its disc_programs.txt row (size / content hash)\n", pr.id.c_str());
            return false;
        }
        out->push_back(std::move(pr));
    }
    // longest first: the 2-bundle stub is a prefix of the main program
    std::sort(out->begin(), out->end(), [](const Program& x, const Program& y) { return x.size > y.size; });
    return true;
}

// The program whose bytes are the micro memory's prefix (longest match), or null.
const Program* Identify(const std::vector<Program>& programs, const uint8_t* micro) {
    for (const Program& p : programs)
        if (p.unit == "vu1" && std::memcmp(micro, p.bytes.data(), p.size) == 0) return &p;
    return nullptr;
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
        uint32_t size;    // program bytes (content mode; 16384 for a whole-image row)
        size_t cases;
        size_t entries;
        uint32_t blocks;
        uint64_t ops;
        uint64_t bundles;
        size_t bytes;
        std::string id;
        std::string variants;  // whole-memory image hashes seen in the corpus (content mode)
    };
    std::vector<Row> rows;
    int rc = 0;
    std::vector<Program> programs;
    if (!a.programs.empty() && !ReadPrograms(a.programs, &programs)) {
        std::fprintf(stderr, "vu1_native_gen: cannot read %s\n", a.programs.c_str());
        return 2;
    }
    if (!programs.empty()) {
        // CONTENT MODE: cases -> programs by prefix; one TU per program.
        std::map<uint64_t, std::vector<uint64_t>> by_program;  // content hash -> the whole-image hashes
        std::map<uint64_t, const Program*> prog_of;
        size_t unknown = 0;
        for (const auto& kv : images) {
            const Program* pr = Identify(programs, kv.second.front()->micro_mem);
            if (pr == nullptr) {
                std::printf("image=%s cases=%zu matches NO disc program (not generated)\n", Vu1AotEmitter::Hex16(kv.first).c_str(), kv.second.size());
                unknown += kv.second.size();
                rc = 1;
                continue;
            }
            by_program[pr->content_hash].push_back(kv.first);
            prog_of[pr->content_hash] = pr;
        }
        for (const Program& pr : programs) {
            if (pr.unit != "vu1") continue;
            const std::string hex = Vu1AotEmitter::Hex16(pr.content_hash);
            if (have_list && listed.count(pr.content_hash) == 0) {
                std::printf("program=%s id=%s size=%x SKIPPED (not in %s)\n", hex.c_str(), pr.id.c_str(), pr.size, a.images.c_str());
                continue;
            }
            auto it = by_program.find(pr.content_hash);
            if (it == by_program.end()) {
                std::printf("program=%s id=%s size=%x has NO corpus case: a disc program without a native body\n", hex.c_str(), pr.id.c_str(), pr.size);
                rc = 1;
                continue;
            }
            // Gate 1 per residue variant (the verifier wants one whole image per call); entries = the union.
            std::set<uint32_t> entry_set;
            std::vector<uint32_t> entries;
            size_t n_cases_prog = 0;
            uint64_t bundles = 0;
            bool refused = false;
            std::string variants;
            for (uint64_t ih : it->second) {
                std::vector<const Vu1Case*> ptrs;
                for (const auto& c : images[ih]) ptrs.push_back(c.get());
                const Vu1AotVerifier::Result pre = Vu1AotVerifier::Verify(ptrs);
                if (!pre.identical) {
                    std::printf("program=%s variant=%s cases=%zu REFUSED at the specialised-table gate: %s\n", hex.c_str(), Vu1AotEmitter::Hex16(ih).c_str(), ptrs.size(), pre.divergence.c_str());
                    refused = true;
                    break;
                }
                for (uint32_t pc : pre.block_pcs) entries.push_back(pc);
                for (const Vu1Case* c : ptrs) {
                    entries.push_back(c->start_pc & VuBlockTier::kPcMask);
                    entry_set.insert(c->start_pc & VuBlockTier::kPcMask);
                }
                n_cases_prog += ptrs.size();
                bundles += pre.bundles;
                variants += (variants.empty() ? "" : "|") + Vu1AotEmitter::Hex16(ih) + "x" + std::to_string(ptrs.size());
            }
            if (refused) {
                rc = 1;
                continue;
            }
            // The canonical image: the program's bytes, zero residue.
            // The EE code's VU address constants inside this program's extent: closure entries too (an MSCAL
            // the lanes never kicked still gets its blocks; a data address only adds an unreachable block).
            size_t static_added = 0;
            for (uint32_t v : g_vu_addrs)
                if (v < pr.size && entry_set.count(v) == 0) { entries.push_back(v); ++static_added; }
            std::vector<uint8_t> canon(Vu1Case::kMicroSize, 0);
            std::memcpy(canon.data(), pr.bytes.data(), pr.size);
            VuBlockProgram scratch;
            VuBlockTier::DecodeImage(canon.data(), Vu1Case::kMicroSize, scratch);
            scratch.hash = pr.content_hash;
            std::vector<uint32_t> in_extent;
            for (uint32_t pc : entries)
                if (pc < pr.size) in_extent.push_back(pc);
                else std::printf("program=%s entry %s is past the program's end (%x): dropped (a residue block)\n", hex.c_str(), Hex4(pc).c_str(), pr.size);
            const std::vector<uint32_t> all_pcs = Vu1AotEmitter::ReachableBlocks(scratch, in_extent, 8192);
            std::vector<uint32_t> pcs;
            size_t dropped = 0;
            for (uint32_t pc : all_pcs) {
                const VuBlock* blk = VuBlockTier::BuildBlock(scratch, pc >> 3);
                const uint32_t end = pc + 8u * static_cast<uint32_t>(blk->ops.size());
                if (pc < pr.size && end <= pr.size) pcs.push_back(pc);
                else ++dropped;
            }
            Vu1AotEmitter::Options opt;
            opt.engine_build = "title-native";
            opt.region = a.region;
            std::string ent;
            for (uint32_t e : entry_set) ent += (ent.empty() ? "" : ",") + Hex4(e);
            opt.note = "Sly 2 (SCUS_973.16) VU1 program " + pr.id + " (content hash " + hex + ", " + std::to_string(pr.size) +
                       " bytes at VU address 0; .vutext of the disc's ELF); corpus cases: " + std::to_string(n_cases_prog) +
                       " over " + std::to_string(it->second.size()) + " residue variant(s); kick entries: " + ent +
                       "; generated at build time by games/scus_97316_sly2_band_of_thieves/native/vu1 (vu1_native_gen, content identity)";
            const Vu1AotEmitter::Output out = Vu1AotEmitter::Emit(scratch, pcs, opt);
            const std::string path = a.out + "/vu1_" + hex + ".cpp";
            if (!WriteFile(path, out.source)) {
                std::fprintf(stderr, "vu1_native_gen: cannot write %s\n", path.c_str());
                return 1;
            }
            std::printf("program=%s id=%s size=%x variants=%zu cases=%zu entries=%zu (+%zu static vu addresses) blocks=%u (dropped past the end: %zu) ops=%llu bundles=%llu bytes=%zu -> %s\n",
                        hex.c_str(), pr.id.c_str(), pr.size, it->second.size(), n_cases_prog, entry_set.size(), static_added, out.blocks, dropped,
                        static_cast<unsigned long long>(out.ops), static_cast<unsigned long long>(bundles), out.source.size(), path.c_str());
            rows.push_back({pr.content_hash, pr.size, n_cases_prog, entry_set.size(), out.blocks, out.ops, bundles, out.source.size(), pr.id, variants});
            listed.erase(pr.content_hash);
        }
        if (unknown) std::printf("vu1_native_gen: %zu case(s) of images matching no disc program\n", unknown);
    } else {
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
        rows.push_back({hash, Vu1Case::kMicroSize, ptrs.size(), entry_set.size(), out.blocks, out.ops, pre.bundles, out.source.size(), hex, ""});
        listed.erase(hash);
    }
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
        tbl += "    {0x" + Vu1AotEmitter::Hex16(r.hash) + "ull, " + std::to_string(r.size) + "u, &vu1_aot_exports_" + Vu1AotEmitter::Hex16(r.hash) + ", " +
               std::to_string(r.blocks) + "u, " + std::to_string(r.ops) + "ull},\n";
    if (rows.empty()) tbl += "    {0ull, 0u, nullptr, 0u, 0ull},\n";
    tbl += "};\nconst size_t kNativeVu1ImageCount = " + std::to_string(rows.size()) + ";\n\n}  // namespace ps2r::title::sly2\n";
    if (!WriteFile(a.out + "/vu1_native_images.cpp", tbl)) return 1;
    std::string txt = "# vu1_native_gen -- the images generated (hash, program id, size, corpus cases, kick entries, blocks, ops, interpreter bundles over the corpus, TU bytes, residue variants)\n";
    txt += "# policy " + Vu1AotEmitter::PolicyName(a.region) + (programs.empty() ? "" : ":identity=content") + "\n";
    for (const Row& r : rows)
        txt += Vu1AotEmitter::Hex16(r.hash) + " id=" + r.id + " size=" + std::to_string(r.size) + " cases=" + std::to_string(r.cases) + " entries=" + std::to_string(r.entries) + " blocks=" + std::to_string(r.blocks) +
               " ops=" + std::to_string(r.ops) + " bundles=" + std::to_string(r.bundles) + " bytes=" + std::to_string(r.bytes) + (r.variants.empty() ? "" : " variants=" + r.variants) + "\n";
    if (!WriteFile(a.out + "/vu1_native_images.txt", txt)) return 1;
    std::printf("vu1_native_gen: %zu image(s) emitted into %s%s\n", rows.size(), a.out.c_str(), rc == 0 ? "" : " (INCOMPLETE)");
    return rc;
}
