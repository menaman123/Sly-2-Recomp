// test_spiop_libc.cpp -- the differential test of spiop's libc half (spiop_libc.cpp): every ported function run
// through the recompiled body (the generated TU, no native installed) and through the native body, on the same
// IOP RAM image and register file, for random and edge inputs -- the register results (v0; v1 for the unpack/add
// leftovers are not compared: callers never read them) and the whole RAM image must be identical. No game data:
// the inputs are synthesized bit patterns; the module's words these functions touch are its static NaN record
// at 0x5f6a0 (read only) and the stack.
//
// WHY A SEPARATE TEST: no lane reaches these functions (the headset arm needs a USB device the engine never
// reports), so the lane oracle (NATIVEEE_IOP_TRACE compare) cannot judge them; this is their oracle.
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "engine/iop/abi/iop_abi_surface.h"
#include "engine/iop/iop_memory.h"
#include "iop_recompiled.h"   // the generated spiop TU (ps2r_title_iop_spiop's include dir)
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_libc.h"

using ps2r::iop::IopContext;
using ps2r::iop::IopMemory;

namespace {

constexpr uint32_t kStackTop = 0x7f000;    // a scratch stack inside the 2 MiB image, away from the module (0x5a000..0x5f700)
constexpr uint32_t kModuleLo = 0x5a000, kModuleHi = 0x5f700;
constexpr uint32_t kScratchLo = 0x70000, kScratchHi = 0x80000;   // the stack (refilled per arm)
constexpr uint32_t kInputLo = 0x68000, kInputHi = 0x68400;       // the inputs and the fp records (snapshot per case, restored per arm)
constexpr uint32_t kTheNan = 0x5f6a0;

IopMemory g_mem;
int g_failures = 0;
uint64_t g_cases = 0;

const ps2r::iop::IopAbiSurface::GuestFunction* Resolve(uint32_t addr) {
    auto it = iop_spiop::iop_recompiled_functions.find(addr);
    return it == iop_spiop::iop_recompiled_functions.end() ? nullptr : &it->second;
}

void InstallNatives(bool on) {
    size_t n = 0;
    const auto* fns = ps2r::title::sly2::spiop::LibcFunctions(n);
    for (size_t i = 0; i < n; ++i) {
        iop_seam::IopFnSlot* s = iop_spiop::iop_fn_slot(fns[i].addr);
        if (!s) {
            std::printf("FAIL: no slot for 0x%x\n", fns[i].addr);
            ++g_failures;
            continue;
        }
        if (on) iop_seam::InstallPlain(*s, fns[i].body);
        else iop_seam::Uninstall(*s);
    }
}

// The fp-bit static NaN record (the module's .data: class QNAN, sign 0, exp 0, fraction 0x100000 -- what the IRX
// carries; its exact words do not matter to the compare, both arms read the same).
void SeedModuleData() {
    g_mem.Write<uint32_t>(kTheNan + 0, 1);
    g_mem.Write<uint32_t>(kTheNan + 4, 0);
    g_mem.Write<uint32_t>(kTheNan + 8, 0);
    g_mem.Write<uint32_t>(kTheNan + 12, 0x100000);
}

struct Arm {
    IopContext ctx;
    std::vector<uint8_t> scratch;   // kScratchLo..kScratchHi after the run
    std::vector<uint8_t> module;    // kModuleLo..kModuleHi after the run
    std::vector<uint8_t> input;     // kInputLo..kInputHi after the run (the records the body wrote)
};

std::vector<uint8_t> Snap(uint32_t lo, uint32_t hi) {
    const uint8_t* p = g_mem.Pointer(lo, hi - lo);
    return std::vector<uint8_t>(p, p + (hi - lo));
}

Arm Run(uint32_t addr, const IopContext& in, bool native, const std::vector<uint8_t>& input_pre) {
    // A fresh image region for the stack each arm: the same bytes (0xa5 fill) so uninitialized reads agree; the
    // input region restored to the case's pre-state.
    std::memset(g_mem.Pointer(kScratchLo, kScratchHi - kScratchLo), 0xa5, kScratchHi - kScratchLo);
    std::memcpy(g_mem.Pointer(kInputLo, kInputHi - kInputLo), input_pre.data(), input_pre.size());
    InstallNatives(native);
    Arm a;
    a.ctx = in;
    const auto* fn = Resolve(addr);
    if (!fn) {
        std::printf("FAIL: 0x%x not in the recompiled map\n", addr);
        ++g_failures;
        return a;
    }
    (*fn)(a.ctx);
    a.scratch = Snap(kScratchLo, kScratchHi);
    a.module = Snap(kModuleLo, kModuleHi);
    a.input = Snap(kInputLo, kInputHi);
    return a;
}

IopContext Ctx(uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0) {
    IopContext c = IopContext::ForEntry(kStackTop, 0);
    c.gpr[4] = a0;
    c.gpr[5] = a1;
    c.gpr[6] = a2;
    c.gpr[31] = 0x5a000;   // a return address inside the module (never jumped to)
    // Callee-saved registers the bodies spill: recognizable values.
    for (int r = 16; r <= 23; ++r) c.gpr[r] = 0x60000u + static_cast<uint32_t>(r);
    return c;
}

bool Compare(const char* what, uint32_t addr, const IopContext& in, const Arm& r, const Arm& n, bool check_v0 = true) {
    ++g_cases;
    bool ok = true;
    if (check_v0 && r.ctx.gpr[2] != n.ctx.gpr[2]) {
        std::printf("FAIL %s 0x%x a0=%08x a1=%08x: v0 recompiled=%08x native=%08x\n", what, addr, in.gpr[4], in.gpr[5], r.ctx.gpr[2], n.ctx.gpr[2]);
        ok = false;
    }
    if (r.ctx.gpr[29] != n.ctx.gpr[29]) {
        std::printf("FAIL %s 0x%x: sp recompiled=%08x native=%08x\n", what, addr, r.ctx.gpr[29], n.ctx.gpr[29]);
        ok = false;
    }
    for (int reg = 16; reg <= 23; ++reg) {
        if (r.ctx.gpr[reg] != n.ctx.gpr[reg]) {
            std::printf("FAIL %s 0x%x: s%d recompiled=%08x native=%08x\n", what, addr, reg - 16, r.ctx.gpr[reg], n.ctx.gpr[reg]);
            ok = false;
        }
    }
    if (r.module != n.module) {
        for (size_t i = 0; i < r.module.size(); ++i)
            if (r.module[i] != n.module[i]) {
                std::printf("FAIL %s 0x%x: module byte 0x%zx recompiled=%02x native=%02x\n", what, addr, kModuleLo + i, r.module[i], n.module[i]);
                break;
            }
        ok = false;
    }
    if (r.input != n.input) {
        for (size_t i = 0; i < r.input.size(); ++i)
            if (r.input[i] != n.input[i]) {
                std::printf("FAIL %s 0x%x a0=%08x a1=%08x: record byte 0x%zx recompiled=%02x native=%02x\n", what, addr, in.gpr[4], in.gpr[5],
                            kInputLo + i, r.input[i], n.input[i]);
                break;
            }
        ok = false;
    }
    if (r.scratch != n.scratch) {
        // The stack: only the words the recompiled body WROTE are judged (the callee-saved spills whose values are
        // the register file's are mirrored by the port; a byte the recompiled run left at the fill is not compared
        // when the native run wrote it -- the native frames spill the same registers, so this never triggers).
        size_t first = 0;
        int diffs = 0;
        for (size_t i = 0; i < r.scratch.size(); ++i)
            if (r.scratch[i] != n.scratch[i]) {
                if (diffs == 0) first = i;
                ++diffs;
            }
        std::printf("FAIL %s 0x%x a0=%08x a1=%08x: %d scratch bytes differ, first at 0x%zx (recompiled %02x native %02x)\n", what, addr,
                    in.gpr[4], in.gpr[5], diffs, kScratchLo + first, r.scratch[first], n.scratch[first]);
        ok = false;
    }
    if (!ok) ++g_failures;
    return ok;
}

void Check(const char* what, uint32_t addr, const IopContext& in, bool check_v0 = true) {
    const std::vector<uint8_t> pre = Snap(kInputLo, kInputHi);
    const Arm r = Run(addr, in, false, pre);
    const Arm n = Run(addr, in, true, pre);
    Compare(what, addr, in, r, n, check_v0);
}

std::vector<uint32_t> EdgeFloats() {
    return {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x00800000u, 0x3f800000u, 0xbf800000u,
            0x3f000000u, 0x40000000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7f800001u, 0x7fc00000u,
            0x7f900000u, 0xffc00000u, 0x3fffffffu, 0x40490fdbu, 0x3a800000u, 0x38000000u, 0x4b000000u, 0x4f000000u,
            0x00400000u, 0x33800000u, 0x7e800000u, 0x3f7fffffu, 0x3f800001u, 0x41200000u, 0xc1200000u, 0x00000002u};
}

uint32_t RandomFloat(std::mt19937& rng) {
    const uint32_t k = rng() % 8;
    switch (k) {
        case 0: return rng();                                                   // any bits
        case 1: return (rng() & 0x807fffffu) | ((100u + rng() % 60) << 23);      // near 1.0
        case 2: return (rng() & 0x807fffffu);                                   // denormals / zero
        case 3: return 0x7f800000u | (rng() & 0x807fffffu);                     // inf / NaN
        case 4: return (rng() & 0x807fffffu) | ((rng() % 255) << 23);           // finite
        case 5: return static_cast<uint32_t>(rng() % 0x100000);                 // small ints as bits
        default: return (rng() & 0x80ffffffu) | ((120u + rng() % 16) << 23);
    }
}

// An fp_number record at `at` built by the recompiled unpack_d of `bits` (both arms see the same record).
void UnpackInto(uint32_t at, uint32_t bits) {
    g_mem.Write<uint32_t>(kInputLo + 0x100, bits);
    InstallNatives(false);
    IopContext c = Ctx(kInputLo + 0x100, at);
    (*Resolve(0x5cd64))(c);
}

}  // namespace

int main() {
    ps2r::iop::IopAbiSurface::BindMemory(&g_mem);
    ps2r::iop::IopAbiSurface::BindResolver(&Resolve);
    ps2r::iop::IopAbiSurface::SetUnresolvedImport([](std::string_view lib, uint32_t index, IopContext&) {
        std::printf("FAIL: unexpected import %.*s:%u\n", static_cast<int>(lib.size()), lib.data(), index);
        ++g_failures;
    });
    iop_spiop::initialize_iop_recompiled_functions();
    SeedModuleData();
    std::mt19937 rng(0x5a000);
    const std::vector<uint32_t> edges = EdgeFloats();

    // Unary: unpack_d (bits -> record), floatsisf (int), sqrtf (bits), pack_d (record -> bits, records from unpack).
    std::vector<uint32_t> unary = edges;
    for (int i = 0; i < 4000; ++i) unary.push_back(RandomFloat(rng));
    for (uint32_t x : unary) {
        g_mem.Write<uint32_t>(kInputLo + 0x100, x);
        Check("unpack_d", 0x5cd64, Ctx(kInputLo + 0x100, kInputLo + 0x200));
        Check("floatsisf", 0x5d8e0, Ctx(x));
        Check("sqrtf", 0x5f0f0, Ctx(x));
        UnpackInto(kInputLo + 0x200, x);
        Check("pack_d", 0x5cc50, Ctx(kInputLo + 0x200));
    }
    for (int i = 0; i < 2000; ++i) {
        // pack_d over arbitrary records (classes 0..5, exponents around the range, fractions random).
        const uint32_t at = kInputLo + 0x200;
        g_mem.Write<uint32_t>(at + 0, rng() % 6);
        g_mem.Write<uint32_t>(at + 4, rng() % 2);
        g_mem.Write<uint32_t>(at + 8, static_cast<uint32_t>(static_cast<int32_t>(rng() % 320) - 160));
        g_mem.Write<uint32_t>(at + 12, rng());
        Check("pack_d(record)", 0x5cc50, Ctx(at));
    }
    // Binary: add, sub, mul, div, gt, ge over bit pairs; _fpadd_parts and __fpcmp_parts over records.
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (uint32_t a : edges)
        for (uint32_t b : edges) pairs.emplace_back(a, b);
    for (int i = 0; i < 6000; ++i) pairs.emplace_back(RandomFloat(rng), RandomFloat(rng));
    for (const auto& [a, b] : pairs) {
        Check("addsf3", 0x5d078, Ctx(a, b));
        Check("subsf3", 0x5d0d0, Ctx(a, b));
        Check("mulsf3", 0x5d134, Ctx(a, b));
        Check("divsf3", 0x5d324, Ctx(a, b));
        Check("gtsf2", 0x5d710, Ctx(a, b));
        Check("gesf2", 0x5d784, Ctx(a, b));
        UnpackInto(kInputLo + 0x200, a);
        UnpackInto(kInputLo + 0x210, b);
        Check("fpadd_parts", 0x5ce40, Ctx(kInputLo + 0x200, kInputLo + 0x210, kInputLo + 0x220));
        Check("fpcmp_parts", 0x5d484, Ctx(kInputLo + 0x200, kInputLo + 0x210));
    }
    std::printf("spiop libc differential: %" PRIu64 " cases, %d failures\n", g_cases, g_failures);
    return g_failures == 0 ? 0 : 1;
}
