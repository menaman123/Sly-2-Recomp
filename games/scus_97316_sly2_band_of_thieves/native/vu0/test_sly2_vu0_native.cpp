// Sly2Vu0Native: the generated VU0 program bodies against the engine's VU tiers, on random
// register files. For every entry the generator emitted: N random COP2 register blocks (VF
// lanes drawn from the special patterns and random bits, random ACC / Q / I / flags / VI) are
// run through (a) the CACHED tier -- what the determinism lane runs, VuUnit::Execute with no
// hook -- (b) the interpreter reference, and (c) NativeVu0::Run; the three units must leave the
// same 608-byte register file (TPC included), the same clock delta and the same data memory.
// Two consecutive runs on the same unit are also compared (the clock and the flag state carry).
// Args: <ELF path> [iterations=2000]. SKIP (77) when the ELF is absent.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu0/native_vu0.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu0/vu0_native_programs.h"

namespace {

int g_fail = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            if (g_fail < 30) std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);      \
            ++g_fail;                                                                     \
        }                                                                                 \
    } while (0)

uint64_t g_rng = 0x2545F4914F6CDD1Dull;
uint32_t Rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<uint32_t>(g_rng >> 11);
}

const uint32_t kSpecial[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x00800000u, 0x80800000u,
    0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x3F800000u, 0xBF800000u,
    0x3F000000u, 0x40000000u, 0x4B800000u, 0x33800000u, 0x7F000000u, 0x00FFFFFFu, 0x01000000u, 0x42C80000u,
};

uint32_t RandomLane() {
    const uint32_t r = Rnd();
    switch (r & 7u) {
        case 0: return kSpecial[(r >> 3) % (sizeof(kSpecial) / sizeof(kSpecial[0]))];
        case 1: return (Rnd() & 0x807FFFFFu) | 0x3F800000u;   // around 1
        case 2: return (Rnd() & 0x807FFFFFu) | 0x7F000000u;   // near overflow: the clamps and O/S flags
        case 3: return (Rnd() & 0x80FFFFFFu);                 // denormal / tiny: U/Z flags
        case 4: return (Rnd() & 0x807FFFFFu) | 0x42000000u;   // ~32..64 (positions, radii)
        default: return Rnd();
    }
}

void RandomRegs(VUregs& r) {
    std::memset(&r, 0, sizeof(r));
    for (int i = 1; i < 32; ++i)
        for (int k = 0; k < 4; ++k) r.VF[i].I[k] = RandomLane();
    r.VF[0].x = r.VF[0].y = r.VF[0].z = 0.0f;
    r.VF[0].w = 1.0f;
    for (int k = 0; k < 4; ++k) r.ACC.I[k] = RandomLane();
    uint32_t q = RandomLane(), p = RandomLane();
    std::memcpy(&r.Q, &q, 4);
    std::memcpy(&r.P, &p, 4);
    r.I = RandomLane();
    r.R = Rnd();
    for (int i = 1; i < 16; ++i) r.VI[i] = static_cast<uint16_t>(Rnd());
    r.status_flag = static_cast<uint16_t>(Rnd() & 0xFFFu);
    r.mac_flag = static_cast<uint16_t>(Rnd());
    r.clip_flag = Rnd() & 0xFFFFFFu;
}

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool LoadImage(const char* elf_path, std::vector<uint8_t>& image) {
    std::ifstream f(elf_path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> elf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (elf.size() < 0x34) return false;
    const uint32_t phoff = Le32(&elf[0x1C]);
    const uint16_t phnum = static_cast<uint16_t>(elf[0x2C] | (elf[0x2D] << 8));
    const uint32_t vaddr = ps2r::title::sly2::vu0_native::kImage.vaddr, size = ps2r::title::sly2::vu0_native::kImage.size;
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + static_cast<size_t>(i) * 32;
        if (Le32(&elf[ph]) != 1) continue;
        const uint32_t p_offset = Le32(&elf[ph + 4]), p_vaddr = Le32(&elf[ph + 8]), p_filesz = Le32(&elf[ph + 16]);
        if (vaddr >= p_vaddr && vaddr + size <= p_vaddr + p_filesz) {
            const size_t off = p_offset + (vaddr - p_vaddr);
            image.assign(elf.begin() + static_cast<long>(off), elf.begin() + static_cast<long>(off + size));
            return true;
        }
    }
    return false;
}

void Diff(const char* tag, const VUregs& a, const VUregs& b) {
    for (int i = 0; i < 32; ++i)
        if (std::memcmp(a.VF[i].I, b.VF[i].I, 16) != 0)
            std::printf("  %s VF%02d %08x %08x %08x %08x | %08x %08x %08x %08x\n", tag, i, a.VF[i].I[0], a.VF[i].I[1], a.VF[i].I[2],
                        a.VF[i].I[3], b.VF[i].I[0], b.VF[i].I[1], b.VF[i].I[2], b.VF[i].I[3]);
    if (std::memcmp(a.ACC.I, b.ACC.I, 16) != 0) std::printf("  %s ACC differs\n", tag);
    uint32_t qa, qb;
    std::memcpy(&qa, &a.Q, 4);
    std::memcpy(&qb, &b.Q, 4);
    if (qa != qb) std::printf("  %s Q %08x | %08x\n", tag, qa, qb);
    if (a.status_flag != b.status_flag) std::printf("  %s status %04x | %04x\n", tag, a.status_flag, b.status_flag);
    if (a.mac_flag != b.mac_flag) std::printf("  %s mac %04x | %04x\n", tag, a.mac_flag, b.mac_flag);
    if (a.clip_flag != b.clip_flag) std::printf("  %s clip %06x | %06x\n", tag, a.clip_flag, b.clip_flag);
    if (a.TPC != b.TPC) std::printf("  %s TPC %x | %x\n", tag, a.TPC, b.TPC);
    for (int i = 0; i < 16; ++i)
        if (a.VI[i] != b.VI[i]) std::printf("  %s VI%d %04x | %04x\n", tag, i, a.VI[i], b.VI[i]);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ps2r::title::sly2;
    if (argc < 2) {
        std::printf("usage: test_sly2_vu0_native <ELF> [iterations]\n");
        return 2;
    }
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 2000;
    std::vector<uint8_t> image;
    if (!LoadImage(argv[1], image)) {
        std::printf("Sly2Vu0Native: SKIP (no ELF at %s)\n", argv[1]);
        return 77;
    }

    NativeKnobs knobs;
    knobs.vu0 = true;
    NativeVu0 native;

    // Three units, the image loaded into each (a WriteMicroMem bumps micro_generation).
    VuUnit cached(0), interp(0), nat(0);
    cached.WriteMicroMem(0, image.data(), image.size());
    interp.WriteMicroMem(0, image.data(), image.size());
    nat.WriteMicroMem(0, image.data(), image.size());
    native.Install(knobs, nat);   // the hook on `nat` only: nat.Execute -> native body; cached.Execute -> the tier
    CHECK(native.installed());

    // A wrong fingerprint must decline (the hook returns false, the tier runs).
    {
        VuUnit other(0);
        std::vector<uint8_t> bad = image;
        bad[0x10] ^= 0x40u;
        other.WriteMicroMem(0, bad.data(), bad.size());
        CHECK(!native.Run(other, vu0_native::kImage.programs[0].entry, other.micro_generation()));
    }

    const uint32_t count = vu0_native::kImage.count;
    std::printf("Sly2Vu0Native: %u native programs, %d register files each\n", count, iterations);
    for (uint32_t pi = 0; pi < count; ++pi) {
        const uint32_t entry = vu0_native::kImage.programs[pi].entry;
        int bad = 0;
        std::vector<std::pair<uint64_t, int>> paths;   // (clock delta, count): one delta per control-flow path
        for (int it = 0; it < iterations && bad < 3; ++it) {
            VUregs r;
            RandomRegs(r);
            cached.regs() = r;
            interp.regs() = r;
            nat.regs() = r;
            const uint64_t c0 = cached.flags().cycle(), i0 = interp.flags().cycle(), n0 = nat.flags().cycle();
            cached.Execute(entry);
            interp.dispatch().interp().Run(interp, entry);
            nat.Execute(entry);
            const bool same_cn = std::memcmp(&cached.regs(), &nat.regs(), sizeof(VUregs)) == 0;
            const bool same_ci = std::memcmp(&cached.regs(), &interp.regs(), sizeof(VUregs)) == 0;
            const bool same_clock = (cached.flags().cycle() - c0) == (nat.flags().cycle() - n0) &&
                                    (cached.flags().cycle() - c0) == (interp.flags().cycle() - i0);
            const bool same_data = std::memcmp(cached.state().data, nat.state().data, Vu1State::kMemSize) == 0;
            {
                const uint64_t d = cached.flags().cycle() - c0;
                bool found = false;
                for (auto& pc : paths) if (pc.first == d) { ++pc.second; found = true; }
                if (!found) paths.emplace_back(d, 1);
            }
            if (!same_cn || !same_ci || !same_clock || !same_data) {
                ++bad;
                ++g_fail;
                std::printf("FAIL entry 0x%x iteration %d: regs cached==native %d cached==interp %d clock %d data %d (cycles c %llu i %llu n %llu)\n",
                            entry, it, same_cn, same_ci, same_clock, same_data,
                            static_cast<unsigned long long>(cached.flags().cycle() - c0),
                            static_cast<unsigned long long>(interp.flags().cycle() - i0),
                            static_cast<unsigned long long>(nat.flags().cycle() - n0));
                Diff("cached|native", cached.regs(), nat.regs());
            }
        }
        std::printf("  entry 0x%x: %s (%d files; paths by clock delta:", entry, bad == 0 ? "identical" : "DIVERGES", iterations);
        for (const auto& pc : paths) std::printf(" %llu cycles x%d", static_cast<unsigned long long>(pc.first), pc.second);
        std::printf(")\n");
    }
    // Back-to-back runs with the register file carried (the second run sees the first's flags / Q / clock).
    for (uint32_t pi = 0; pi < count && g_fail == 0; ++pi) {
        const uint32_t entry = vu0_native::kImage.programs[pi].entry;
        VUregs r;
        RandomRegs(r);
        cached.regs() = r;
        nat.regs() = r;
        for (int k = 0; k < 4; ++k) {
            cached.Execute(entry);
            nat.Execute(entry);
            CHECK(std::memcmp(&cached.regs(), &nat.regs(), sizeof(VUregs)) == 0);
        }
    }
    std::printf("Sly2Vu0Native: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
