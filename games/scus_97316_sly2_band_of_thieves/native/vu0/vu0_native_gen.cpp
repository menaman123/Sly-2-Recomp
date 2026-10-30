// vu0_native_gen -- BUILD-TIME generator of Sly 2's VU0 microprograms as native C++
// (STAGE NATIVE, track gs-vu0). Reads the title ELF, takes the VU0 program image the
// game uploads (the .vutext bytes at a known vaddr: an ADDRESS, title data that may be
// named), decodes each requested entry with the engine's ONE decoder, and emits one
// host function per STRAIGHT-LINE program -- the exact transliteration of the cached /
// interpreter tiers' instruction bodies (engine/vu/tiers/vu_cached_tier_exec.inl
// ExecUpperFmac / ExecUpperAcc / the MINI-MAX and lower MOVE bodies, the FDIV unit's
// DoDiv / DoSqrt / DoRsqrt) with the two pipelines FOLDED statically:
//   * the MAC flag ring: a program that reads no flag mid-run and drains at its E bit
//     ends with MAC = the last FMAC's pack and STATUS Z/S/U/O = that nibble, the sticky
//     twins OR'd over every FMAC of the run (FlagPipeline::ApplyMacToStatus applied in
//     issue order == the eager ring == the lazy ring: VuLazyFlagsAlgebra);
//   * the Q latch and the clock: the stall schedule of a straight-line program is a
//     function of the program alone (StallModel: register dependencies and op kinds, no
//     values), so the generator RUNS the engine's StallModel + FlagPipeline over the
//     decoded slots once, symbolically, and learns at which bundle a pending Q becomes
//     visible (VisibleQ / WAITQ / a second FDIV op / the end-of-run flush) and how many
//     cycles the run takes -- the emitted code commits Q at exactly that bundle and
//     advances the unit's clock by the constant.
// Anything the generator cannot prove (a branch, a flag reader, an I-bit bundle, an
// unsupported kind, a same-bundle VF hazard, no E bit within the image) REFUSES the
// entry: it is left to the cached tier (the oracle) and reported.
//
// The output TU is game-derived: generated into the build dir like the recomp shards,
// pinned by native/vu0/MANIFEST.sha256, never committed. The build-time oracle is
// test_sly2_vu0_native (random register files through the generated body and the
// interpreter tier -> identical 608-byte register file + clock); the run-time oracle is
// the determinism lane (mem.mhash: every float the game computes lands in RAM).
//
// Usage: vu0_native_gen --elf PATH --image-vaddr HEX --image-size HEX --entry HEX[,HEX...]
//                       --out FILE.cpp [--report FILE.txt]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/vu/flag_pipeline.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/vu_decoder.h"

namespace {

struct Bundle {
    uint32_t pc = 0;
    uint32_t lower = 0, upper = 0;
    VuDecodedSlot slot;
    // the static plan
    bool q_commit_before_upper = false;  // the upper's Q read (or the stall step) commits the pending Q here
    bool q_push_after = false;           // the lower issues a DIV / SQRT / RSQRT (pending Q from here)
    int q_push_delay = 0;
    bool fdiv_wait = false;              // the stall step flushes a pending Q (WAITQ / a second FDIV op)
};

enum class FdivKind { None, Div, Sqrt, Rsqrt, Waitq };

FdivKind ClassifyFdiv(uint32_t lower) {
    if (((lower >> 25) & 0x7Fu) != 0x40u) return FdivKind::None;
    const uint32_t fn = lower & 0x3Fu, f2 = (lower >> 6) & 0x1Fu;
    if (f2 != 0x0Eu) return FdivKind::None;
    switch (fn) {
        case 0x3C: return FdivKind::Div;
        case 0x3D: return FdivKind::Sqrt;
        case 0x3E: return FdivKind::Rsqrt;
        case 0x3F: return FdivKind::Waitq;
        default: return FdivKind::None;
    }
}

uint64_t Fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string Hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%x", v);
    return b;
}

const char* LaneName(unsigned k) { return k == 0 ? "x" : k == 1 ? "y" : k == 2 ? "z" : "w"; }

std::string DestName(unsigned d) {
    std::string s;
    if (d & 8) s += 'x';
    if (d & 4) s += 'y';
    if (d & 2) s += 'z';
    if (d & 1) s += 'w';
    return s.empty() ? "-" : s;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// vaddr -> file offset through the ELF32 program headers; false when unmapped.
bool ElfVaddrToOffset(const std::vector<uint8_t>& elf, uint32_t vaddr, uint32_t size, size_t& off) {
    static const uint8_t kElfMagic[4] = {0x7F, 'E', 'L', 'F'};
    if (elf.size() < 0x34 || std::memcmp(elf.data(), kElfMagic, 4) != 0) return false;
    const uint32_t phoff = Le32(&elf[0x1C]);
    const uint16_t phnum = static_cast<uint16_t>(elf[0x2C] | (elf[0x2D] << 8));
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + static_cast<size_t>(i) * 32;
        if (ph + 32 > elf.size()) return false;
        const uint32_t p_type = Le32(&elf[ph]), p_offset = Le32(&elf[ph + 4]), p_vaddr = Le32(&elf[ph + 8]);
        const uint32_t p_filesz = Le32(&elf[ph + 16]);
        if (p_type != 1) continue;
        if (vaddr >= p_vaddr && vaddr + size <= p_vaddr + p_filesz) {
            off = p_offset + (vaddr - p_vaddr);
            return off + size <= elf.size();
        }
    }
    return false;
}

// ---- the emitted bodies ------------------------------------------------------------
// Each is the tier body of vu_cached_tier_exec.inl for that kind with the registers
// named and the flag push replaced by the fold step.

std::string FmacResultExpr(uint8_t kind, uint8_t bc, const std::string& s1, const std::string& ft_reg) {
    // s1 = clamped VF[fs]; broadcast lane = ClampBits1(VF[ft].I[bc]); vec = ClampVec(VF[ft])
    const std::string bv = "VuFloat::Splat4(VuFloat::Bits2F(VuFloat::ClampBits1(" + ft_reg + ".I[" + std::to_string(bc) + "])))";
    const std::string s2 = "VuFloat::ClampVec(" + ft_reg + ")";
    switch (kind) {
        case VUK_U_ADD_BC: return "VuFloat::Add4(" + s1 + ", " + bv + ")";
        case VUK_U_SUB_BC: return "VuFloat::Sub4(" + s1 + ", " + bv + ")";
        case VUK_U_MUL_BC: return "VuFloat::Mul4(" + s1 + ", " + bv + ")";
        case VUK_U_MADD_BC: return "VuFloat::Fma4(" + s1 + ", " + bv + ", regs.ACC)";
        case VUK_U_MSUB_BC: return "VuFloat::Fms4(" + s1 + ", " + bv + ", regs.ACC)";
        case VUK_U_ADD: return "VuFloat::Add4(" + s1 + ", " + s2 + ")";
        case VUK_U_SUB: return "VuFloat::Sub4(" + s1 + ", " + s2 + ")";
        case VUK_U_MUL: return "VuFloat::Mul4(" + s1 + ", " + s2 + ")";
        case VUK_U_MADD: return "VuFloat::Fma4(" + s1 + ", " + s2 + ", regs.ACC)";
        case VUK_U_MSUB: return "VuFloat::Fms4(" + s1 + ", " + s2 + ", regs.ACC)";
        case VUK_U_MULQ: return "VuFloat::Mul4(" + s1 + ", VuFloat::Splat4(VuFloat::ClampFloat(regs.Q)))";
        case VUK_U_MUL_I: return "VuFloat::Mul4(" + s1 + ", VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))))";
        case VUK_U_ADD_I: return "VuFloat::Add4(" + s1 + ", VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))))";
        default: return "";
    }
}

std::string AccResultExpr(uint8_t kind, const std::string& bv) {
    switch (kind) {
        case VUK_U_ADDA_BC: return "VuFloat::Add4(s1, " + bv + ")";
        case VUK_U_SUBA_BC: return "VuFloat::Sub4(s1, " + bv + ")";
        case VUK_U_MULA_BC: return "VuFloat::Mul4(s1, " + bv + ")";
        case VUK_U_MADDA_BC: return "VuFloat::Fma4(s1, " + bv + ", acc_c)";
        case VUK_U_MSUBA_BC: return "VuFloat::Fms4(s1, " + bv + ", acc_c)";
        case VUK_U_MULA: return "VuFloat::Mul4(s1, s2)";
        case VUK_U_MADDA: return "VuFloat::Fma4(s1, s2, acc_c)";
        default: return "";
    }
}

bool IsFmacKind(uint8_t k) {
    switch (k) {
        case VUK_U_ADD_BC: case VUK_U_SUB_BC: case VUK_U_MUL_BC: case VUK_U_MADD_BC: case VUK_U_MSUB_BC:
        case VUK_U_ADD: case VUK_U_SUB: case VUK_U_MUL: case VUK_U_MADD: case VUK_U_MSUB:
        case VUK_U_MULQ: case VUK_U_MUL_I: case VUK_U_ADD_I:
            return true;
        default: return false;
    }
}
bool IsAccKind(uint8_t k) {
    switch (k) {
        case VUK_U_ADDA_BC: case VUK_U_SUBA_BC: case VUK_U_MULA_BC: case VUK_U_MADDA_BC: case VUK_U_MSUBA_BC:
        case VUK_U_MULA: case VUK_U_MADDA:
            return true;
        default: return false;
    }
}

// The upper word of one bundle; returns false (with `why`) for a kind outside the set.
bool EmitUpper(std::ostringstream& o, const Bundle& b, std::string& why) {
    const VuDecodedSlot& s = b.slot;
    const uint8_t k = s.upper_kind;
    const std::string fd = "regs.VF[" + std::to_string(s.u_fd) + "]";
    const std::string fs = "regs.VF[" + std::to_string(s.u_fs) + "]";
    const std::string ft = "regs.VF[" + std::to_string(s.u_ft) + "]";
    const unsigned dest = s.u_dest;
    if (k == VUK_U_NOP) {
        o << "        VuFloat::ClampStoreACC(regs.ACC);\n";
        return true;
    }
    if (IsFmacKind(k)) {
        // ExecUpperFmac: s1 = clamped fs; ACC re-clamped; result raw; fd dest lanes clamped (VF00 protected); flags on the raw result.
        o << "        {\n";
        o << "            const VR_reg s1 = VuFloat::ClampVec(" << fs << ");\n";
        o << "            VuFloat::ClampStoreACC(regs.ACC);\n";
        o << "            const VR_reg result = " << FmacResultExpr(k, s.u_bc, "s1", ft) << ";\n";
        if (s.u_fd != 0) o << "            VuFloat::WriteMaskClamp(" << fd << ", result.I, " << dest << ");\n";
        o << "            FOLD_MAC(result, " << dest << ");\n";
        o << "        }\n";
        return true;
    }
    if (IsAccKind(k)) {
        o << "        {\n";
        o << "            VuFloat::ClampStoreACC(regs.ACC);\n";
        o << "            const VR_reg acc_in = regs.ACC;\n";
        o << "            const VR_reg s1 = VuFloat::ClampVec(" << fs << ");\n";
        o << "            const VR_reg s2 = VuFloat::ClampVec(" << ft << ");\n";
        o << "            const VR_reg acc_c = VuFloat::ClampVec(acc_in);\n";
        o << "            const VR_reg bv = VuFloat::Splat4(s2.F[" << unsigned(s.u_bc) << "]);\n";
        o << "            (void)bv; (void)s2; (void)acc_c;\n";
        o << "            VR_reg acc = " << AccResultExpr(k, "bv") << ";\n";
        for (int lane = 0; lane < 4; ++lane) {
            if (!(dest & (8u >> lane))) o << "            acc.I[" << lane << "] = acc_in.I[" << lane << "];\n";
        }
        o << "            FOLD_MAC(acc, " << dest << ");\n";
        o << "            VuFloat::WriteMaskClamp(regs.ACC, acc.I, " << dest << ");\n";
        o << "        }\n";
        return true;
    }
    if (k == VUK_U_MAX || k == VUK_U_MINI) {
        o << "        VuFloat::ClampStoreACC(regs.ACC);\n";
        o << "        VuFloat::ApplyMinMax4(" << fd << ", " << fs << ", " << ft << ", " << dest << ", " << (s.u_fd == 0 ? "true" : "false")
          << ", " << (k == VUK_U_MAX ? "true" : "false") << ");\n";
        return true;
    }
    if (k == VUK_U_MAX_BC || k == VUK_U_MINI_BC) {
        o << "        {\n";
        o << "            VuFloat::ClampStoreACC(regs.ACC);\n";
        o << "            VR_reg b4; b4.I[0] = b4.I[1] = b4.I[2] = b4.I[3] = " << ft << ".I[" << unsigned(s.u_bc) << "];\n";
        o << "            VuFloat::ApplyMinMax4(" << fd << ", " << fs << ", b4, " << dest << ", " << (s.u_fd == 0 ? "true" : "false")
          << ", " << (k == VUK_U_MAX_BC ? "true" : "false") << ");\n";
        o << "        }\n";
        return true;
    }
    why = "upper kind " + std::to_string(k) + " (word " + Hex(b.upper) + ") not in the straight-line set";
    return false;
}

// The lower word; `fd` kinds (DIV/SQRT/RSQRT/WAITQ) are classified from the raw word
// (RSQRT / SQRT have no fast kind in the decoder).
bool EmitLower(std::ostringstream& o, const Bundle& b, std::string& why) {
    const VuDecodedSlot& s = b.slot;
    const FdivKind fk = ClassifyFdiv(b.lower);
    const unsigned fsf = (b.lower >> 21) & 3u, ftf = (b.lower >> 23) & 3u;
    const std::string fs = "regs.VF[" + std::to_string(s.l_fs) + "]";
    const std::string ft = "regs.VF[" + std::to_string(s.l_ft) + "]";
    switch (fk) {
        case FdivKind::Waitq:
            // the stall step already flushed the pending Q (fdiv_pipe_op); the body's FlushQPipe is then a no-op
            o << "        // WAITQ (the Q commit is in the stall step above)\n";
            return true;
        case FdivKind::Div:
            o << "        {   // DIV Q, " << fs << "." << LaneName(fsf) << ", " << ft << "." << LaneName(ftf) << " (DivLanes)\n";
            o << "            uint16_t st;\n";
            o << "            const float q = VuInterpreterTier::DivQuotient(VuFloat::ClampFloat(" << fs << ".F[" << fsf << "]), VuFloat::ClampFloat(" << ft << ".F[" << ftf << "]), st);\n";
            o << "            Q_PUSH(q, st);\n";
            o << "        }\n";
            return true;
        case FdivKind::Sqrt:
            o << "        {   // SQRT Q, " << ft << "." << LaneName(ftf) << " (DoSqrt)\n";
            o << "            const float v = VuFloat::ClampFloat(" << ft << ".F[" << ftf << "]);\n";
            o << "            const uint16_t st = (v < 0.0f) ? static_cast<uint16_t>(vu_status::I | vu_status::IS) : 0;\n";
            o << "            Q_PUSH(std::sqrt(std::fabs(v)), st);\n";
            o << "        }\n";
            return true;
        case FdivKind::Rsqrt:
            o << "        {   // RSQRT Q, " << fs << "." << LaneName(fsf) << ", " << ft << "." << LaneName(ftf) << " (DoRsqrt)\n";
            o << "            const float num = VuFloat::ClampFloat(" << fs << ".F[" << fsf << "]);\n";
            o << "            const float d = VuFloat::ClampFloat(" << ft << ".F[" << ftf << "]);\n";
            o << "            const uint32_t sign = (VuFloat::F2Bits(num) ^ VuFloat::F2Bits(d)) & VuFloat::kSignBit;\n";
            o << "            uint16_t st = 0; float q;\n";
            o << "            if (d == 0.0f) {\n";
            o << "                st |= static_cast<uint16_t>(vu_status::D | vu_status::DS);\n";
            o << "                if (num != 0.0f) { q = VuFloat::Bits2F(sign | VuFloat::kMaxFloatBits); }\n";
            o << "                else { st |= static_cast<uint16_t>(vu_status::I | vu_status::IS); q = VuFloat::Bits2F(sign); }\n";
            o << "            } else {\n";
            o << "                if (d < 0.0f) st |= static_cast<uint16_t>(vu_status::I | vu_status::IS);\n";
            o << "                q = num / std::sqrt(std::fabs(d));\n";
            o << "            }\n";
            o << "            Q_PUSH(q, st);\n";
            o << "        }\n";
            return true;
        case FdivKind::None:
            break;
    }
    switch (s.lower_kind) {
        case VUK_L_NOP:
            return true;
        case VUK_L_MOVE:
            if (s.l_ft != 0) {
                for (int lane = 0; lane < 4; ++lane) {
                    if (s.l_dest & (8u >> lane)) o << "        " << ft << ".I[" << lane << "] = " << fs << ".I[" << lane << "];\n";
                }
            }
            return true;
        case VUK_L_MR32:
            if (s.l_ft != 0) {
                o << "        { const VR_reg src = " << fs << "; VR_reg v = {}; v.x = src.y; v.y = src.z; v.z = src.w; v.w = src.x;\n";
                for (int lane = 0; lane < 4; ++lane) {
                    if (s.l_dest & (8u >> lane)) o << "          " << ft << ".I[" << lane << "] = v.I[" << lane << "];\n";
                }
                o << "        }\n";
            }
            return true;
        default:
            why = "lower kind " + std::to_string(s.lower_kind) + " (word " + Hex(b.lower) + ") not in the straight-line set";
            return false;
    }
}

struct Program {
    uint32_t entry = 0;
    std::vector<Bundle> bundles;
    uint32_t tpc = 0;
    uint64_t cycles = 0;
    bool q_commit_at_end = false;  // DrainAll's FlushQPipe commits a still-pending Q (with its clock jump folded in `cycles`)
    std::string body;
    std::string refused;  // non-empty = not emitted
};

// Decode the straight-line run from `entry`, plan the pipelines, emit the body.
Program Generate(const std::vector<uint8_t>& image, uint32_t entry) {
    Program p;
    p.entry = entry;
    const uint32_t size = static_cast<uint32_t>(image.size());
    uint32_t pc = entry;
    bool end_pending = false;
    while (true) {
        if (pc + 8 > size) { p.refused = "runs past the image (no E bit)"; return p; }
        Bundle b;
        b.pc = pc;
        b.lower = Le32(&image[pc]);
        b.upper = Le32(&image[pc + 4]);
        b.slot = VuDecoder::DecodeVuSlot(b.lower, b.upper);
        if (b.slot.i_bit) { p.refused = "I-bit bundle at " + Hex(pc); return p; }
        if (VuDecoder::IsBranchKind(b.slot.lower_kind) || VuDecoder::IsRunTerminalLowerKind(b.slot.lower_kind)) {
            if (b.slot.lower_kind != VUK_L_GENERIC || ClassifyFdiv(b.lower) == FdivKind::None) {
                p.refused = "branch / run-terminal lower at " + Hex(pc) + " (word " + Hex(b.lower) + ")";
                return p;
            }
        }
        if (b.slot.discard_lower || b.slot.hide_upper) { p.refused = "same-bundle VF hazard at " + Hex(pc); return p; }
        if (b.slot.efu_pipe_op) { p.refused = "EFU op at " + Hex(pc); return p; }
        p.bundles.push_back(b);
        if (end_pending) break;                 // the bundle after the E bit ran: the program ends
        if (b.slot.e_bit) end_pending = true;
        pc += 8;
        if (p.bundles.size() > 512) { p.refused = "no E bit within 512 bundles"; return p; }
    }
    p.tpc = (pc + 8) & 0x3FFFu;

    // ---- the static plan: the interpreter loop's order over a scratch unit ----
    // ++cycle; stalls.Apply (FMAC read stalls, FDIV wait = FlushQPipe); upper (VisibleQ for a Q
    // reader); lower (PushQ for an FDIV op); ... DrainAll at the end.
    StallModel sm;
    FlagPipeline fp;
    VUregs scratch{};
    const uint64_t c0 = fp.cycle();
    for (Bundle& b : p.bundles) {
        ++fp.cycle();
        const bool q_was_pending = fp.q_pending();
        sm.Apply(b.slot, scratch, fp);           // a WAITQ / second FDIV op commits the pending Q here (clock jump)
        b.fdiv_wait = q_was_pending && !fp.q_pending();
        if (b.slot.upper_kind == VUK_U_MULQ) {   // the upper's VisibleQ: commit if retired
            if (fp.q_pending() && fp.q_pending_retire() <= fp.cycle()) {
                b.q_commit_before_upper = true;
                fp.VisibleQ(scratch);
            }
        } else {
            // a GENERIC upper Q reader (ADDq/SUBq/MADDq/...) would be refused by EmitUpper below
        }
        const FdivKind fk = ClassifyFdiv(b.lower);
        if (fk == FdivKind::Div || fk == FdivKind::Sqrt || fk == FdivKind::Rsqrt) {
            b.q_push_after = true;
            b.q_push_delay = fk == FdivKind::Rsqrt ? FlagPipeline::kRsqrtDelay : FlagPipeline::kDivDelay;
            fp.PushQ(scratch, 0.0f, b.q_push_delay, 0);
        }
    }
    // DrainAll: FlushQPipe (clock jump to the retire), the FMAC writebacks pass.
    if (fp.q_pending()) {
        p.q_commit_at_end = true;
        fp.FlushQPipe(scratch);
    }
    sm.FlushFmacWb(fp);
    p.cycles = fp.cycle() - c0;

    // ---- the text ----
    std::ostringstream o;
    o << "// VU0 program at micro " << Hex(entry) << ": " << p.bundles.size() << " bundles, TPC " << Hex(p.tpc) << ", "
      << p.cycles << " cycles\n";
    o << "static void Prog_" << std::hex << entry << std::dec << "(VuUnit& unit) {\n";
    o << "    VUregs& regs = unit.regs();\n";
    o << "    FlagPipeline& pipes = unit.flags();\n";
    o << "    uint16_t mac_last = 0, mac_or = 0; bool any_mac = false;\n";
    o << "    bool q_pending = false; float q_value = 0.0f; uint16_t q_status = 0;\n";
    o << "    (void)q_pending; (void)q_value; (void)q_status;\n";
    o << "    regs.VF[0].x = 0.0f; regs.VF[0].y = 0.0f; regs.VF[0].z = 0.0f; regs.VF[0].w = 1.0f;\n";
    for (const Bundle& b : p.bundles) {
        o << "    // " << Hex(b.pc) << "  upper " << Hex(b.upper) << " lower " << Hex(b.lower) << "  dest " << DestName(b.slot.u_dest) << "\n";
        o << "    {\n";
        if (b.fdiv_wait) o << "        Q_COMMIT();   // the stall step's FlushQPipe (WAITQ / second FDIV op)\n";
        if (b.q_commit_before_upper) o << "        Q_COMMIT();   // VisibleQ: the latch retired before this bundle\n";
        std::string why;
        if (!EmitUpper(o, b, why)) { p.refused = why; return p; }
        if (!EmitLower(o, b, why)) { p.refused = why; return p; }
        o << "    }\n";
    }
    o << "    // end of run (DrainAll): the MAC ring folds, a pending Q commits, VF0/VI0 pinned, TPC, the clock\n";
    if (p.q_commit_at_end) o << "    Q_COMMIT();\n";
    o << "    if (any_mac) {\n";
    o << "        regs.mac_flag = mac_last;\n";
    o << "        uint16_t status = static_cast<uint16_t>(regs.status_flag & 0xFF0u);\n";
    o << "        if (mac_last & 0x000Fu) status |= vu_status::Z;\n";
    o << "        if (mac_last & 0x00F0u) status |= vu_status::S;\n";
    o << "        if (mac_last & 0x0F00u) status |= vu_status::U;\n";
    o << "        if (mac_last & 0xF000u) status |= vu_status::O;\n";
    o << "        if (mac_or & 0x000Fu) status |= vu_status::ZS;\n";
    o << "        if (mac_or & 0x00F0u) status |= vu_status::SS;\n";
    o << "        if (mac_or & 0x0F00u) status |= vu_status::US;\n";
    o << "        if (mac_or & 0xF000u) status |= vu_status::OS;\n";
    o << "        regs.status_flag = status;\n";
    o << "    }\n";
    o << "    regs.VF[0].x = 0.0f; regs.VF[0].y = 0.0f; regs.VF[0].z = 0.0f; regs.VF[0].w = 1.0f; regs.VI[0] = 0;\n";
    o << "    regs.TPC = " << Hex(p.tpc) << "u;\n";
    o << "    pipes.cycle() += " << p.cycles << "u;\n";
    o << "}\n\n";
    p.body = o.str();
    return p;
}

}  // namespace

int main(int argc, char** argv) {
    std::string elf_path, out_path, report_path, entries_arg;
    uint32_t image_vaddr = 0, image_size = 0;
    for (int i = 1; i < argc; ++i) {
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "vu0_native_gen: missing value for %s\n", flag); std::exit(2); }
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--elf")) elf_path = need("--elf");
        else if (!std::strcmp(argv[i], "--out")) out_path = need("--out");
        else if (!std::strcmp(argv[i], "--report")) report_path = need("--report");
        else if (!std::strcmp(argv[i], "--entry")) entries_arg = need("--entry");
        else if (!std::strcmp(argv[i], "--image-vaddr")) image_vaddr = static_cast<uint32_t>(std::strtoul(need("--image-vaddr"), nullptr, 16));
        else if (!std::strcmp(argv[i], "--image-size")) image_size = static_cast<uint32_t>(std::strtoul(need("--image-size"), nullptr, 16));
        else { std::fprintf(stderr, "vu0_native_gen: unknown argument %s\n", argv[i]); return 2; }
    }
    if (elf_path.empty() || out_path.empty() || entries_arg.empty() || image_size == 0) {
        std::fprintf(stderr, "usage: vu0_native_gen --elf PATH --image-vaddr HEX --image-size HEX --entry HEX[,HEX...] --out FILE.cpp [--report FILE]\n");
        return 2;
    }
    std::vector<uint8_t> elf;
    if (!ReadFile(elf_path, elf)) { std::fprintf(stderr, "vu0_native_gen: cannot read %s\n", elf_path.c_str()); return 1; }
    size_t off = 0;
    if (!ElfVaddrToOffset(elf, image_vaddr, image_size, off)) {
        std::fprintf(stderr, "vu0_native_gen: image 0x%x+0x%x is not mapped by the ELF\n", image_vaddr, image_size);
        return 1;
    }
    std::vector<uint8_t> image(elf.begin() + static_cast<long>(off), elf.begin() + static_cast<long>(off + image_size));
    const uint64_t image_fnv = Fnv1a64(image.data(), image.size());

    std::vector<uint32_t> entries;
    {
        std::stringstream ss(entries_arg);
        std::string tok;
        while (std::getline(ss, tok, ',')) entries.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16)));
    }

    std::ostringstream tu, rep;
    tu << "// GENERATED by vu0_native_gen (games/scus_97316_sly2_band_of_thieves/native/vu0/) -- do not edit, do not commit.\n";
    tu << "// Source: the title ELF's VU0 program image at vaddr 0x" << std::hex << image_vaddr << ", 0x" << image_size
       << " bytes, FNV-1a64 0x" << image_fnv << std::dec << ".\n";
    tu << "// Each body is the straight-line transliteration of the engine tiers' instruction bodies with the MAC ring and\n";
    tu << "// the Q latch folded statically (vu0_native_gen.cpp explains the fold); the interpreter tier is the oracle.\n";
    tu << "#include <cmath>\n#include <cstdint>\n\n";
    tu << "#include \"engine/vu/flag_pipeline.h\"\n#include \"engine/vu/tiers/vu_interpreter_tier.h\"\n";
    tu << "#include \"engine/vu/vu_decoder.h\"\n#include \"engine/vu/vu_float.h\"\n#include \"engine/vu/vu_unit.h\"\n";
    tu << "#include \"games/scus_97316_sly2_band_of_thieves/native/vu0/vu0_native_programs.h\"\n\n";
    tu << "namespace ps2r::title::sly2::vu0_native {\nnamespace {\n\n";
    tu << "// The fold step of one FMAC result (the ring entry the tier would push: pack of the raw result over the dest lanes).\n";
    tu << "#define FOLD_MAC(res, dest) do { const uint16_t m_ = VuFloat::PackMacBits((res).I, static_cast<uint8_t>(dest)); mac_last = m_; mac_or = static_cast<uint16_t>(mac_or | m_); any_mac = true; } while (0)\n";
    tu << "// The FDIV unit: PushQ clamps the value; a still-pending Q is committed first (back-to-back FDIV).\n";
    tu << "#define Q_PUSH(val, st) do { if (q_pending) Q_COMMIT(); q_pending = true; q_value = VuFloat::ClampFloat(val); q_status = (st); } while (0)\n";
    tu << "#define Q_COMMIT() do { if (q_pending) { regs.Q = q_value; VuFloat::CommitQStatus(regs, q_status); q_pending = false; } } while (0)\n\n";
    int emitted = 0;
    std::vector<std::pair<uint32_t, bool>> table;
    for (uint32_t e : entries) {
        Program p = Generate(image, e);
        if (!p.refused.empty()) {
            rep << "refused 0x" << std::hex << e << std::dec << ": " << p.refused << "\n";
            tu << "// entry 0x" << std::hex << e << std::dec << " REFUSED (left to the cached tier): " << p.refused << "\n\n";
            table.emplace_back(e, false);
            continue;
        }
        rep << "emitted 0x" << std::hex << e << std::dec << ": " << p.bundles.size() << " bundles, tpc 0x" << std::hex << p.tpc << std::dec
            << ", " << p.cycles << " cycles" << (p.q_commit_at_end ? ", Q commits at the end" : "") << "\n";
        tu << p.body;
        table.emplace_back(e, true);
        ++emitted;
    }
    tu << "#undef FOLD_MAC\n#undef Q_PUSH\n#undef Q_COMMIT\n\n";
    tu << "const Vu0NativeProgram kPrograms[] = {\n";
    for (const auto& t : table) {
        if (t.second) tu << "    {0x" << std::hex << t.first << std::dec << "u, &Prog_" << std::hex << t.first << std::dec << "},\n";
    }
    tu << "};\n\n}  // namespace\n\n";
    tu << "const Vu0NativeImage kImage = {\n    0x" << std::hex << image_vaddr << "u, 0x" << image_size << "u, 0x" << image_fnv << "ull,\n" << std::dec
       << "    kPrograms, " << emitted << "u,\n};\n\n";
    tu << "}  // namespace ps2r::title::sly2::vu0_native\n";

    std::ofstream out(out_path, std::ios::binary);
    if (!out) { std::fprintf(stderr, "vu0_native_gen: cannot write %s\n", out_path.c_str()); return 1; }
    out << tu.str();
    out.close();
    if (!report_path.empty()) {
        std::ofstream r(report_path, std::ios::binary);
        r << rep.str();
    }
    std::fprintf(stdout, "vu0_native_gen: image 0x%x+0x%x fnv %016llx, %d of %zu entries emitted -> %s\n%s", image_vaddr, image_size,
                 static_cast<unsigned long long>(image_fnv), emitted, entries.size(), out_path.c_str(), rep.str().c_str());
    return 0;
}
