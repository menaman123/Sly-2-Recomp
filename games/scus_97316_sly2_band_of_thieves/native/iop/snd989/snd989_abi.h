// snd989_abi.h -- how a native 989snd body sits in the recompiled module's seat (STAGE NATIVE-3, track snd989).
//
// A ported function is a host body installed in the module's per-function seam (iop_snd989::iop_seam_install):
// it receives the SAME IopContext the recompiled body would (args in $a0..$a3 and the caller's stack from
// sp+0x10, result in $v0, the saved registers $s0..$s7/$fp/$gp/$sp preserved) and reads/writes the SAME IOP RAM
// (the module's .data/.bss at 0x50000.., its heap blocks, the voice table), so the two bodies are interchangeable
// at any call and the module's RAM stays the one state. Callees -- ported or not -- are reached by the same
// register-level call the recompiled caller makes (`Call(ctx, iop_snd989::iop_func_X, a0, ...)`: the callee's
// own seam routes it), library imports by the same `import_call("lib", index)` the recompiled JAL site makes
// (same ordinal, args, order: the libsd op stream is the per-function oracle).
//
// What a native body may differ in (not guest-observable, not an oracle): the stack bytes below the caller's
// $sp (a Frame is allocated for locals and outgoing stack args, its size need not equal the original frame),
// and the caller-saved registers ($at $v0 $v1 $a0..$a3 $t0..$t9 hi lo) after the call -- the MIPS o32 contract
// the compiled caller already assumes.
#pragma once

#include <cstdint>

#include "engine/iop/abi/iop_abi_surface.h"
#include "engine/iop/iop_context.h"
#include "snd989/iop_recompiled.h"

namespace ps2r::title::sly2::snd989 {

using IopContext = ps2r::iop::IopContext;
using IopFn = void (*)(IopContext&);

// IOP RAM through the bound surface (the same accessor the generated code uses).
inline uint32_t Ld32(uint32_t a) { return ps2r::iop::IopAbiSurface::Memory().Read<uint32_t>(a); }
inline uint16_t Ld16(uint32_t a) { return ps2r::iop::IopAbiSurface::Memory().Read<uint16_t>(a); }
inline uint8_t Ld8(uint32_t a) { return ps2r::iop::IopAbiSurface::Memory().Read<uint8_t>(a); }
inline int32_t Lds32(uint32_t a) { return static_cast<int32_t>(Ld32(a)); }
inline int32_t Lds16(uint32_t a) { return static_cast<int16_t>(Ld16(a)); }
inline int32_t Lds8(uint32_t a) { return static_cast<int8_t>(Ld8(a)); }
inline void St32(uint32_t a, uint32_t v) { ps2r::iop::IopAbiSurface::Memory().Write<uint32_t>(a, v); }
inline void St16(uint32_t a, uint32_t v) { ps2r::iop::IopAbiSurface::Memory().Write<uint16_t>(a, static_cast<uint16_t>(v)); }
inline void St8(uint32_t a, uint32_t v) { ps2r::iop::IopAbiSurface::Memory().Write<uint8_t>(a, static_cast<uint8_t>(v)); }

// The register file as the body sees it.
inline uint32_t A0(const IopContext& c) { return c.gpr[4]; }
inline uint32_t A1(const IopContext& c) { return c.gpr[5]; }
inline uint32_t A2(const IopContext& c) { return c.gpr[6]; }
inline uint32_t A3(const IopContext& c) { return c.gpr[7]; }
// The caller's stack argument n (n = 0 is the 5th argument, at sp+0x10).
inline uint32_t StackArg(const IopContext& c, unsigned n) { return Ld32(c.gpr[29] + 0x10 + 4 * n); }
inline void Ret(IopContext& c, uint32_t v) {
    c.gpr[2] = v;
    c.FixZero();
}

// A stack frame for the body's duration: locals (an out-parameter a kernel call writes, a struct passed by
// address) and the outgoing stack arguments of its calls live below the caller's $sp as a compiled body's would.
class Frame {
public:
    Frame(IopContext& c, uint32_t bytes) : c_(c), bytes_((bytes + 7) & ~7u) {
        c_.gpr[29] -= bytes_;
        c_.FixZero();
    }
    ~Frame() {
        c_.gpr[29] += bytes_;
        c_.FixZero();
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    // The IOP address of local byte `off` of this frame (0x10.. are the outgoing stack args).
    uint32_t At(uint32_t off) const { return c_.gpr[29] + off; }
    IopContext& ctx() const { return c_; }

private:
    IopContext& c_;
    uint32_t bytes_;
};

// A call to a module function (ported or recompiled: its seam decides) with the register arguments set.
// Stack arguments (the 5th on) are written by the caller into its Frame at 0x10.. before calling.
inline uint32_t Call(IopContext& c, IopFn fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    c.gpr[4] = a0;
    c.gpr[5] = a1;
    c.gpr[6] = a2;
    c.gpr[7] = a3;
    fn(c);
    c.FixZero();
    return c.gpr[2];
}
// A call through a function pointer held in module RAM (the recompiled jalr: iop_resolve over every hosted module +
// the import stubs). An unknown target is reported like the generated code does and returns $v0 unchanged.
// RULE (found on the lane, fn_3926c): pass $a0..$a3 exactly as the original has them at the jalr -- the callee's
// arity is not visible at the call site (the sequencer's event handlers take a third argument in $a2).
uint32_t CallAddr(IopContext& c, uint32_t target, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);
// A library import, exactly the recompiled JAL site's import_call("lib", index).
inline uint32_t Import(IopContext& c, const char* lib, uint32_t index, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0,
                       uint32_t a3 = 0) {
    c.gpr[4] = a0;
    c.gpr[5] = a1;
    c.gpr[6] = a2;
    c.gpr[7] = a3;
    ps2r::iop::IopAbiSurface::ImportCall(lib, index, c);
    return c.gpr[2];
}

// The imports 989snd uses, by the library's export index (stock_library_binder.cpp / the libsd pair table).
namespace imp {
inline uint32_t CpuSuspendIntr(IopContext& c, uint32_t state_ptr) { return Import(c, "intrman", 17, state_ptr); }
inline uint32_t CpuResumeIntr(IopContext& c, uint32_t state) { return Import(c, "intrman", 18, state); }
inline uint32_t GetThreadId(IopContext& c) { return Import(c, "thbase", 20); }
inline uint32_t WaitSema(IopContext& c, uint32_t sema) { return Import(c, "thsemap", 8, sema); }
inline uint32_t PollSema(IopContext& c, uint32_t sema) { return Import(c, "thsemap", 9, sema); }
inline uint32_t SignalSema(IopContext& c, uint32_t sema) { return Import(c, "thsemap", 6, sema); }
inline uint32_t iSignalSema(IopContext& c, uint32_t sema) { return Import(c, "thsemap", 7, sema); }
// stdio:4 printf(fmt, ...): a1..a3 + the caller's stack from sp+0x10 (a Frame with the words written).
inline uint32_t Printf(IopContext& c, uint32_t fmt, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    return Import(c, "stdio", 4, fmt, a1, a2, a3);
}
// libsd by export index (libsd_module.h ordinals via the binder's pair table).
inline uint32_t SdSetParam(IopContext& c, uint32_t entry, uint32_t value) { return Import(c, "libsd", 5, entry, value); }
inline uint32_t SdGetParam(IopContext& c, uint32_t entry) { return Import(c, "libsd", 6, entry); }
inline uint32_t SdSetSwitch(IopContext& c, uint32_t entry, uint32_t value) { return Import(c, "libsd", 7, entry, value); }
inline uint32_t SdGetSwitch(IopContext& c, uint32_t entry) { return Import(c, "libsd", 8, entry); }
inline uint32_t SdSetAddr(IopContext& c, uint32_t entry, uint32_t value) { return Import(c, "libsd", 9, entry, value); }
inline uint32_t SdGetAddr(IopContext& c, uint32_t entry) { return Import(c, "libsd", 10, entry); }
inline uint32_t SdSetCoreAttr(IopContext& c, uint32_t entry, uint32_t value) { return Import(c, "libsd", 11, entry, value); }
// SdVoiceTrans(chan, mode, iop_addr, spu_addr, size): the 5th argument is the caller's stack word at sp+0x10 (a Frame).
inline uint32_t SdVoiceTrans(IopContext& c, uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr) {
    return Import(c, "libsd", 17, chan, mode, iop_addr, spu_addr);
}
inline uint32_t SdVoiceTransStatus(IopContext& c, uint32_t chan, uint32_t flag) { return Import(c, "libsd", 19, chan, flag); }
inline uint32_t SceCdReadIOPm(IopContext& c) { return Import(c, "cdvdman", 39); }
}  // namespace imp

// 989snd's two-word (per core) voice masks: voice v (0..47) is core v / 24, bit v % 24.
inline int VoiceCore(int v) { return v / 24; }
inline int VoiceBit(int v) { return v - VoiceCore(v) * 24; }

}  // namespace ps2r::title::sly2::snd989
