// spiop_native.cpp -- the bodies (spiop_native.h states the contract). Each function carries the address
// and size of the MIPS it replaces and a transliteration of that code read from the recompiled TU: the
// frame size, the saved-register slots, the stores in their order, the imports, the sibling calls. Where
// the original's arithmetic has a shape (the /1024 and %360 idioms, the 0xfff interleave masks) the C++
// spells the same 32-bit result.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_native.h"

#include <cstdio>

#include "engine/iop/abi/host_app/iop_stubs.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_state.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_libc.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_threads.h"

namespace ps2r::title::sly2::spiop {

namespace {

using Ctx = ps2r::iop::IopContext;
enum Reg : int { v0 = 2, v1 = 3, a0 = 4, a1 = 5, a2 = 6, a3 = 7, s0 = 16, s1, s2, s3, s4, s5, s6, s7, sp = 29, fp = 30, ra = 31 };

// The recompiled body's stack frame, mirrored: $sp moves by the same amount for the body's duration, the
// same registers are stored at the same slots (the values are the module's own: ctx.gpr), outgoing
// stack arguments and parameter blocks land where the original put them.
struct Frame {
    Ctx& c;
    uint32_t size;
    Frame(Ctx& ctx, uint32_t bytes) : c(ctx), size(bytes) { c.gpr[sp] -= bytes; }
    ~Frame() { c.gpr[sp] += size; }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    uint32_t at(uint32_t off) const { return c.gpr[sp] + off; }
    void save(uint32_t off, int reg) const { W32(at(off), c.gpr[reg]); }
    void put(uint32_t off, uint32_t v) const { W32(at(off), v); }
    uint32_t get(uint32_t off) const { return R32(at(off)); }
};

// A sibling call: through the resolver (native when installed, recompiled otherwise); $ra = the return
// address the original's JAL would have set (what the callee saves in its frame).
inline uint32_t Call(Ctx& c, uint32_t addr, uint32_t ret_pc) {
    c.gpr[ra] = ret_pc;
    const auto* fn = iop_stubs::iop_resolve(addr);
    if (fn) (*fn)(c);
    return c.gpr[v0];
}
inline uint32_t Import(Ctx& c, const char* lib, uint32_t idx) {
    iop_stubs::import_call(lib, idx, c);
    return c.gpr[v0];
}
inline void Args(Ctx& c, uint32_t x0) { c.gpr[a0] = x0; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1) { c.gpr[a0] = x0; c.gpr[a1] = x1; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1, uint32_t x2) { c.gpr[a0] = x0; c.gpr[a1] = x1; c.gpr[a2] = x2; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1, uint32_t x2, uint32_t x3) {
    c.gpr[a0] = x0; c.gpr[a1] = x1; c.gpr[a2] = x2; c.gpr[a3] = x3;
}

// The libraries (the import ordinals the module's stubs name: cdvdman 6 sceCdRead, 11 sceCdSync, 14 sceCdTrayReq;
// thbase 4 CreateThread, 5 DeleteThread, 6 StartThread, 14 ChangeThreadPriority, 33 DelayThread; thsemap 4 CreateSema,
// 5 DeleteSema, 6 SignalSema, 8 WaitSema; sysmem 4 AllocSysMemory, 5 FreeSysMemory; sysclib 5 longjmp, 14 memset,
// 40 wmemcopy; snd989 / lgaud: the other hosted modules' exports).
inline uint32_t WaitSema(Ctx& c, uint32_t id) { Args(c, id); return Import(c, "thsemap", 8); }
inline uint32_t SignalSema(Ctx& c, uint32_t id) { Args(c, id); return Import(c, "thsemap", 6); }
inline uint32_t DelayThread(Ctx& c, uint32_t us) { Args(c, us); return Import(c, "thbase", 33); }
inline void SignalIfValid(Ctx& c, uint32_t id) {
    if (static_cast<int32_t>(id) >= 0) SignalSema(c, id);
}
inline void WaitIfValid(Ctx& c, uint32_t id) {
    if (static_cast<int32_t>(id) >= 0) WaitSema(c, id);
}

// The context (fn_5a3bc: allocated on first use).
inline uint32_t Context(Ctx& c, uint32_t ret_pc) { return Call(c, 0x5a3bc, ret_pc); }

// The original's signed idioms: (x * y) / 1024 and x % 360 with C truncation (the mult/mflo + 0x3ff
// adjust and the 0xb60b60b7 reciprocal in the listing).
inline int32_t MulDiv1024(int32_t x, int32_t y) {
    const int32_t p = static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y));
    return p / 1024;
}
inline int32_t Mod360Positive(int32_t x) {
    int32_t r = x % 360;
    if (r < 0) r += 360;
    return r;
}


// ---------------------------------------------------------------------------------------------------------
// The 'SUKR' handlers (0x5a000..0x5a30c): op -> body, each fetching the context first.

void fn_5a000(Ctx& c) {   // op 0x00 init
    Frame f(c, 0x18);
    f.save(0x10, ra);
    const uint32_t ctx = Context(c, 0x5a010);
    Args(c, ctx);
    Call(c, 0x5a400, 0x5a018);
    c.gpr[v0] = 0;
}

void fn_5a028(Ctx& c) {   // op 0x01 open(size, args): size must be 0x18
    Frame f(c, 0x18);
    f.save(0x10, s0);
    const uint32_t args = c.gpr[a1];
    f.save(0x14, ra);
    if (c.gpr[a0] != 0x18) {
        c.gpr[v0] = 0;
        return;
    }
    const uint32_t ctx = Context(c, 0x5a048);
    Args(c, ctx, args);
    Call(c, 0x5a7f4, 0x5a054);
}

// ops 0x02 close, 0x03, 0x04: body(ctx, handle), result 0.
template <uint32_t kBody, uint32_t kRet1, uint32_t kRet2>
void HandleCtxArg0(Ctx& c) {
    Frame f(c, 0x18);
    f.save(0x10, s0);
    f.save(0x14, ra);
    const uint32_t h = c.gpr[a0];
    const uint32_t ctx = Context(c, kRet1);
    Args(c, ctx, h);
    Call(c, kBody, kRet2);
    c.gpr[v0] = 0;
}
void fn_5a070(Ctx& c) { HandleCtxArg0<0x5a8dc, 0x5a084, 0x5a090>(c); }
void fn_5a0a4(Ctx& c) { HandleCtxArg0<0x5a960, 0x5a0b8, 0x5a0c4>(c); }
void fn_5a0d8(Ctx& c) { HandleCtxArg0<0x5a9bc, 0x5a0ec, 0x5a0f8>(c); }

void fn_5a10c(Ctx& c) {   // op 0x05 (handle, flags, volume, pan, x5)
    Frame f(c, 0x30);
    f.save(0x1c, s1);
    const uint32_t x5 = f.get(0x40);   // the caller's 5th argument
    f.save(0x20, s2);
    const uint32_t h = c.gpr[a0];
    f.save(0x24, s3);
    const uint32_t flags = c.gpr[a1];
    f.save(0x28, s4);
    const uint32_t vol = c.gpr[a2];
    f.save(0x18, s0);
    f.save(0x2c, ra);
    const uint32_t pan = c.gpr[a3];
    const uint32_t ctx = Context(c, 0x5a140);
    Args(c, ctx, h, flags, vol);
    f.put(0x10, pan);
    f.put(0x14, x5);
    Call(c, 0x5aa18, 0x5a15c);
    c.gpr[v0] = 0;
}

void fn_5a180(Ctx& c) {   // op 0x06 set(handle, voice id, volume, pan)
    Frame f(c, 0x30);
    f.save(0x1c, s1);
    const uint32_t h = c.gpr[a0];
    f.save(0x20, s2);
    const uint32_t id = c.gpr[a1];
    f.save(0x24, s3);
    const uint32_t vol = c.gpr[a2];
    f.save(0x18, s0);
    f.save(0x28, ra);
    const uint32_t pan = c.gpr[a3];
    const uint32_t ctx = Context(c, 0x5a1ac);
    Args(c, ctx, h, id, vol);
    f.put(0x10, pan);
    Call(c, 0x5aa98, 0x5a1c4);
    c.gpr[v0] = 0;
}

// ops 0x07 start, 0x08 playing?, 0x09: body(ctx, handle), result = the body's.
template <uint32_t kBody, uint32_t kRet1, uint32_t kRet2>
void HandleCtxArg0Result(Ctx& c) {
    Frame f(c, 0x18);
    f.save(0x10, s0);
    f.save(0x14, ra);
    const uint32_t h = c.gpr[a0];
    const uint32_t ctx = Context(c, kRet1);
    Args(c, ctx, h);
    Call(c, kBody, kRet2);
}
void fn_5a1e4(Ctx& c) { HandleCtxArg0Result<0x5ab10, 0x5a1f8, 0x5a204>(c); }
void fn_5a214(Ctx& c) { HandleCtxArg0Result<0x5ab94, 0x5a228, 0x5a234>(c); }
void fn_5a244(Ctx& c) { HandleCtxArg0Result<0x5ac18, 0x5a258, 0x5a264>(c); }

void fn_5a274(Ctx& c) {   // op 0x0a close all, wait for the threads
    Frame f(c, 0x18);
    f.save(0x10, ra);
    const uint32_t ctx = Context(c, 0x5a284);
    Args(c, ctx);
    Call(c, 0x5aca0, 0x5a28c);
    c.gpr[v0] = 0;
}

void fn_5a29c(Ctx& c) {   // op 0x0b -> ctx->mic level
    Frame f(c, 0x18);
    f.save(0x10, ra);
    const uint32_t ctx = Context(c, 0x5a2ac);
    c.gpr[v1] = R32(ctx + CtxL::kMicLevel);
    c.gpr[v0] = c.gpr[v1];
}

void fn_5a2c0(Ctx& c) {   // op 0x0c -> ctx->tray count (THE per-frame poll)
    Frame f(c, 0x18);
    f.save(0x10, ra);
    const uint32_t ctx = Context(c, 0x5a2d0);
    c.gpr[v0] = R32(ctx + CtxL::kTrayCount);
}

void fn_5a2e4(Ctx& c) {   // op 0x0d shutdown
    Frame f(c, 0x18);
    f.save(0x10, ra);
    const uint32_t ctx = Context(c, 0x5a2f4);
    Args(c, ctx);
    Call(c, 0x5a678, 0x5a2fc);
    c.gpr[v0] = 0;
}

// The module entry (0x5a30c): allocate the 0x38-byte record, fill {'SUKR', 14, <2 words untouched>,
// 14 words copied from the .data table at 0x5f4e8 -- the original copies 0x38 bytes from +0x10, i.e.
// 0x10 bytes past the allocation; the sysmem granule absorbs it}, register it with 989snd.
void fn_5a30c(Ctx& c) {
    Frame f(c, 0x18);
    f.save(0x10, s0);
    f.save(0x14, ra);
    if (R32(G::kRecordPtr) != 0) {
        c.gpr[v0] = 0;
        return;
    }
    Args(c, kRecordBytes);
    const uint32_t rec = Call(c, 0x5cbc0, 0x5a330);
    W32(G::kRecordPtr, rec);
    if (rec == 0) {
        c.gpr[v0] = 0;
        return;
    }
    W32(rec, kRecordTag);
    W32(rec + 4, G::kHandlerCount);
    uint32_t dst = rec + 0x10, src = G::kHandlers;
    for (; src != G::kHandlers + 0x30; src += 0x10, dst += 0x10) {
        const uint32_t w0 = R32(src), w1 = R32(src + 4), w2 = R32(src + 8), w3 = R32(src + 0xc);
        W32(dst, w0);
        W32(dst + 4, w1);
        W32(dst + 8, w2);
        W32(dst + 0xc, w3);
    }
    const uint32_t w0 = R32(src), w1 = R32(src + 4);
    W32(dst, w0);
    Args(c, R32(G::kRecordPtr));
    W32(dst + 4, w1);
    Import(c, "snd989", 93);   // snd_RegisterExternalModule(record)
    c.gpr[v0] = 0;
}

// fn_5a3bc: the context, allocated (0x5d0) and initialised on first use.
void fn_5a3bc(Ctx& c) {
    Frame f(c, 0x18);
    f.save(0x10, s0);
    if (R32(G::kContextPtr) == 0) {
        f.save(0x14, ra);
        Args(c, CtxL::kSize);
        const uint32_t p = Call(c, 0x5cb30, 0x5a3e0);
        Args(c, p);
        const uint32_t ctx = Call(c, 0x5aedc, 0x5a3e8);
        W32(G::kContextPtr, ctx);
    } else {
        f.save(0x14, ra);
    }
    c.gpr[v0] = R32(G::kContextPtr);
}

// fn_5a400(ctx): op 0x00's body -- 989snd setup, the buffers, four semaphores, three threads, the slots.
void fn_5a400(Ctx& c) {
    Frame f(c, 0x60);
    f.save(0x58, s2);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x5c, ra);
    f.save(0x54, s1);
    f.save(0x50, s0);
    if (R32s(ctx + CtxL::kDiscSema) >= 0) {
        c.gpr[v0] = 1;
        return;
    }
    auto fail = [&]() {
        Args(c, ctx);
        Call(c, 0x5a678, 0x5a65c);
        c.gpr[v0] = 0;
    };
    Args(c, 0xa, 0x2000, 0, 0);
    Import(c, "snd989", 48);
    Args(c, 0, 0, 3);
    Import(c, "snd989", 121);
    Args(c, 1, 4, 8);
    Import(c, "snd989", 121);
    Args(c, 2, 9, 9);
    Import(c, "snd989", 121);
    // the parameter block {2, -5, 0xc000, 0x4000, 0x10000, 0} at sp+0x10
    Args(c, 0, f.at(0x10));
    f.put(0x10, 2);
    f.put(0x14, static_cast<uint32_t>(-5));
    f.put(0x18, 0xc000);
    f.put(0x1c, 0x4000);
    f.put(0x20, 0x10000);
    f.put(0x24, 0);
    Import(c, "snd989", 119);
    Args(c, CtxL::kReadBufBytes);
    uint32_t p = Call(c, 0x5cbc0, 0x5a4b0);
    W32(ctx + CtxL::kReadBuf, p);
    if (p == 0) return fail();
    Args(c, CtxL::kSectorBufBytes);
    p = Call(c, 0x5cbc0, 0x5a4c0);
    W32(ctx + CtxL::kSectorBuf, p);
    if (p == 0) return fail();
    // the sema parameter block {attr 0, option 0, initial 1, max 1} at sp+0x28
    const uint32_t sema_param = f.at(0x28);
    Args(c, sema_param);
    f.put(0x28, 0);
    f.put(0x30, 1);
    f.put(0x34, 1);
    f.put(0x2c, 0);
    uint32_t sema = Import(c, "thsemap", 4);
    Args(c, sema_param);
    W32(ctx + CtxL::kDiscSema, sema);
    sema = Import(c, "thsemap", 4);
    Args(c, sema_param);
    W32(ctx + CtxL::kHeadsetSema, sema);
    sema = Import(c, "thsemap", 4);
    Args(c, sema_param);
    W32(ctx + CtxL::kSemaB, sema);
    const uint32_t sema_c = Import(c, "thsemap", 4);
    W32(ctx + CtxL::kSemaC, sema_c);
    if (R32s(ctx + CtxL::kDiscSema) < 0 || R32s(ctx + CtxL::kHeadsetSema) < 0 || R32s(ctx + CtxL::kSemaB) < 0 ||
        static_cast<int32_t>(sema_c) < 0)
        return fail();
    // the thread parameter block {attr 0x2000000, option 0, entry, stack 0x800, priority 0x50} at sp+0x38
    const uint32_t th_param = f.at(0x38);
    Args(c, th_param);
    f.put(0x38, 0x2000000);
    f.put(0x40, 0x5b2a0);
    f.put(0x48, 0x50);
    f.put(0x44, 0x800);
    f.put(0x3c, 0);
    uint32_t th = Import(c, "thbase", 4);
    W32(ctx + CtxL::kThreadA, th);
    if (static_cast<int32_t>(th) <= 0) return fail();
    Args(c, th_param);
    W32(ctx + CtxL::kShutdown, 0);
    f.put(0x40, 0x5b0cc);
    th = Import(c, "thbase", 4);
    W32(ctx + CtxL::kThreadB, th);
    if (static_cast<int32_t>(th) <= 0) return fail();
    W32(ctx + CtxL::kHeadsetDev, static_cast<uint32_t>(-1));
    WaitSema(c, R32(ctx + CtxL::kSemaB));
    Args(c, R32(ctx + CtxL::kThreadB), ctx);
    if (Import(c, "thbase", 6) != 0) {
        SignalSema(c, R32(ctx + CtxL::kSemaB));
        return fail();
    }
    Args(c, th_param);
    f.put(0x40, 0x5b438);
    th = Import(c, "thbase", 4);
    W32(ctx + CtxL::kThreadC, th);
    if (static_cast<int32_t>(th) <= 0) return fail();
    W32(ctx + CtxL::kTrayCount, 0);
    WaitSema(c, R32(ctx + CtxL::kSemaC));
    Args(c, R32(ctx + CtxL::kThreadC), ctx);
    if (Import(c, "thbase", 6) != 0) {
        SignalSema(c, R32(ctx + CtxL::kSemaC));
        return fail();
    }
    for (uint32_t i = 0; i < CtxL::kSlotCount; ++i) {
        Args(c, CtxL::Slot(ctx, i), i);
        if (Call(c, 0x5b568, 0x5a638) == 0) return fail();
    }
    c.gpr[v0] = 1;
}

// fn_5a678(ctx): op 0x0d's body -- tear every slot, thread, semaphore and buffer down.
void fn_5a678(Ctx& c) {
    Frame f(c, 0x30);
    f.save(0x28, s2);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x20, s0);
    f.save(0x24, s1);
    f.save(0x2c, ra);
    for (uint32_t i = 0; i < CtxL::kSlotCount; ++i) {
        Args(c, CtxL::Slot(ctx, i));
        Call(c, 0x5b658, 0x5a6a0);
    }
    const uint32_t sa = R32(ctx + CtxL::kHeadsetSema);
    W32(ctx + CtxL::kShutdown, 1);
    f.put(0x10, sa);
    WaitIfValid(c, sa);
    const uint32_t sb = R32(ctx + CtxL::kSemaB);
    f.put(0x14, sb);
    WaitIfValid(c, sb);
    const uint32_t sc = R32(ctx + CtxL::kSemaC);
    f.put(0x18, sc);
    WaitIfValid(c, sc);
    Args(c, R32(ctx + CtxL::kThreadA));
    Import(c, "thbase", 5);
    Args(c, R32(ctx + CtxL::kThreadB));
    Import(c, "thbase", 5);
    Args(c, R32(ctx + CtxL::kThreadC));
    Import(c, "thbase", 5);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    W32(ctx + CtxL::kThreadC, minus1);
    W32(ctx + CtxL::kThreadB, minus1);
    W32(ctx + CtxL::kThreadA, minus1);
    SignalIfValid(c, f.get(0x18));
    SignalIfValid(c, f.get(0x14));
    SignalIfValid(c, f.get(0x10));
    Args(c, R32(ctx + CtxL::kHeadsetSema));
    W32(ctx + CtxL::kShutdown, 0);
    W32(ctx + CtxL::kHeadsetDev, minus1);
    Import(c, "thsemap", 5);
    Args(c, R32(ctx + CtxL::kSemaB));
    Import(c, "thsemap", 5);
    Args(c, R32(ctx + CtxL::kSemaC));
    Import(c, "thsemap", 5);
    Args(c, R32(ctx + CtxL::kDiscSema));
    Import(c, "thsemap", 5);
    const uint32_t read_buf = R32(ctx + CtxL::kReadBuf);
    W32(ctx + CtxL::kDiscSema, minus1);
    W32(ctx + CtxL::kSemaC, minus1);
    W32(ctx + CtxL::kSemaB, minus1);
    W32(ctx + CtxL::kHeadsetSema, minus1);
    if (read_buf) {
        Args(c, read_buf);
        Call(c, 0x5cba0, 0x5a7c0);
    }
    const uint32_t sector_buf = R32(ctx + CtxL::kSectorBuf);
    W32(ctx + CtxL::kReadBuf, 0);
    if (sector_buf) {
        Args(c, sector_buf);
        Call(c, 0x5cba0, 0x5a7d8);
    }
    W32(ctx + CtxL::kSectorBuf, 0);
}

// fn_5a7f4(ctx, args): open -- slot args[3] must be free; lock it, fill it (fn_5b738), hand out a handle.
void fn_5a7f4(Ctx& c) {
    Frame f(c, 0x28);
    f.save(0x18, s0);
    const uint32_t args = c.gpr[a1];
    f.save(0x24, ra);
    f.save(0x20, s2);
    f.save(0x1c, s1);
    const uint32_t idx = R32(args + 0xc);
    const uint32_t ctx = c.gpr[a0];
    if (idx >= CtxL::kSlotCount) {
        c.gpr[v0] = 0;
        return;
    }
    const uint32_t handles = ctx + CtxL::kHandles;
    if (R32(handles + idx * 4) != 0) {
        c.gpr[v0] = 0;
        return;
    }
    const uint32_t lock = f.at(0x10);
    Args(c, lock, CtxL::Slot(ctx, idx));
    Call(c, 0x5b6e4, 0x5a860);
    Args(c, f.get(0x14), args);
    if (Call(c, 0x5b738, 0x5a86c) == 0) {
        c.gpr[v0] = 0;
        const uint32_t sema = f.get(0x10);
        if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
        c.gpr[v0] = 0;
        return;
    }
    const uint32_t h = R32(ctx + CtxL::kNextHandle) + 1;
    W32(ctx + CtxL::kNextHandle, h);
    const uint32_t sema = f.get(0x10);
    W32(handles + R32(args + 0xc) * 4, h);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
    c.gpr[v0] = h;
}

// fn_5a8dc(ctx, handle): close -- stop the slot (state 4), free its handle.
void fn_5a8dc(Ctx& c) {
    Frame f(c, 0x28);
    f.save(0x1c, s1);
    const uint32_t ctx = c.gpr[a0];
    const uint32_t h = c.gpr[a1];
    f.save(0x20, ra);
    Args(c, f.at(0x10), ctx, h);
    f.save(0x18, s0);
    Call(c, 0x5af94, 0x5a900);
    const uint32_t slot = f.get(0x14);
    if (slot) {
        Args(c, slot);
        Call(c, 0x5b810, 0x5a91c);
        Args(c, f.get(0x14));
        const uint32_t idx = Call(c, 0x5b72c, 0x5a928);
        c.gpr[v0] = ctx + idx * 4;   // the original's leftover (sw zero, 0x38(v0))
        W32(ctx + CtxL::kHandles + idx * 4, 0);
    }
    const uint32_t sema = f.get(0x10);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
}

// ops 0x03 / 0x04 bodies: lookup + lock, per-slot body, unlock.
template <uint32_t kBody, uint32_t kRet1, uint32_t kRet2>
void SlotOpNoArgs(Ctx& c) {
    Frame f(c, 0x20);
    const uint32_t ctx = c.gpr[a0], h = c.gpr[a1];
    f.save(0x18, ra);
    Args(c, f.at(0x10), ctx, h);
    Call(c, 0x5af94, kRet1);
    const uint32_t slot = f.get(0x14);
    if (slot) {
        Args(c, slot);
        Call(c, kBody, kRet2);
    }
    const uint32_t sema = f.get(0x10);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
}
void fn_5a960(Ctx& c) { SlotOpNoArgs<0x5b834, 0x5a97c, 0x5a994>(c); }
void fn_5a9bc(Ctx& c) { SlotOpNoArgs<0x5b8ec, 0x5a9d8, 0x5a9f0>(c); }

// fn_5aa18(ctx, handle, flags, volume, pan@sp+0x40, x5@sp+0x44): op 0x05's body.
void fn_5aa18(Ctx& c) {
    Frame f(c, 0x30);
    const uint32_t ctx = c.gpr[a0], h = c.gpr[a1];
    f.save(0x24, s1);
    const uint32_t flags = c.gpr[a2];
    f.save(0x20, s0);
    f.save(0x28, ra);
    const uint32_t vol = c.gpr[a3];
    Args(c, f.at(0x18), ctx, h);
    Call(c, 0x5af94, 0x5aa48);
    const uint32_t slot = f.get(0x1c);
    if (slot) {
        const uint32_t x5 = f.get(0x44), pan = f.get(0x40);
        Args(c, slot, flags, vol, pan);
        f.put(0x10, x5);
        Call(c, 0x5b9a4, 0x5aa6c);
    }
    const uint32_t sema = f.get(0x18);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
}

// fn_5aa98(ctx, handle, voice id, volume, pan@sp+0x38): op 0x06's body.
void fn_5aa98(Ctx& c) {
    Frame f(c, 0x28);
    const uint32_t ctx = c.gpr[a0], h = c.gpr[a1];
    f.save(0x1c, s1);
    const uint32_t id = c.gpr[a2];
    f.save(0x18, s0);
    f.save(0x20, ra);
    const uint32_t vol = c.gpr[a3];
    Args(c, f.at(0x10), ctx, h);
    Call(c, 0x5af94, 0x5aac8);
    const uint32_t slot = f.get(0x14);
    if (slot) {
        Args(c, slot, id, vol, f.get(0x38));
        Call(c, 0x5bb38, 0x5aae4);
    }
    const uint32_t sema = f.get(0x10);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
}

// ops 0x07 / 0x08: lookup + lock; no slot -> `kNoSlot`; else the body's result.
template <uint32_t kBody, uint32_t kNoSlot, uint32_t kRet1, uint32_t kRet2>
void SlotOpResult(Ctx& c) {
    Frame f(c, 0x20);
    const uint32_t ctx = c.gpr[a0], h = c.gpr[a1];
    f.save(0x1c, ra);
    Args(c, f.at(0x10), ctx, h);
    f.save(0x18, s0);
    Call(c, 0x5af94, kRet1);
    const uint32_t slot = f.get(0x14);
    if (slot == 0) {
        const uint32_t sema = f.get(0x10);
        c.gpr[v0] = kNoSlot;
        if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
        c.gpr[v0] = kNoSlot;
        return;
    }
    Args(c, slot);
    const uint32_t r = Call(c, kBody, kRet2);
    const uint32_t sema = f.get(0x10);
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
    c.gpr[v0] = r;
}
void fn_5ab10(Ctx& c) { SlotOpResult<0x5bc6c, 1, 0x5ab30, 0x5ab68>(c); }
void fn_5ab94(Ctx& c) { SlotOpResult<0x5bc94, 0, 0x5abb4, 0x5abec>(c); }

// fn_5ac18(ctx, handle): op 0x09 -- the two 16-bit cursor positions (read, write) in 16-byte units.
void fn_5ac18(Ctx& c) {
    Frame f(c, 0x28);
    const uint32_t ctx = c.gpr[a0], h = c.gpr[a1];
    f.save(0x24, ra);
    Args(c, f.at(0x10), ctx, h);
    f.save(0x20, s0);
    Call(c, 0x5af94, 0x5ac38);
    const uint32_t slot = f.get(0x14);
    if (slot == 0) {
        const uint32_t sema = f.get(0x10);
        c.gpr[v0] = 0;
        if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
        c.gpr[v0] = 0;
        return;
    }
    Args(c, slot, f.at(0x18), f.at(0x1a));
    f.put(0x18, 0);
    Call(c, 0x5bca8, 0x5ac74);
    const uint32_t sema = f.get(0x10);
    const uint32_t r = f.get(0x18);
    c.gpr[v0] = r;
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
    c.gpr[v0] = r;
}

// fn_5aca0(ctx): op 0x0a -- close every slot, then wait (5 ms polls) until each has gone idle.
void fn_5aca0(Ctx& c) {
    Frame f(c, 0x38);
    f.save(0x28, s4);
    f.save(0x2c, s5);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x18, s0);
    f.save(0x1c, s1);
    f.save(0x30, ra);
    f.save(0x24, s3);
    f.save(0x20, s2);
    for (uint32_t i = 0; i < CtxL::kSlotCount; ++i) {
        Args(c, ctx, R32(CtxL::Handle(ctx, i)));
        Call(c, 0x5a8dc, 0x5ace0);
    }
    for (uint32_t i = 0; i < CtxL::kSlotCount;) {
        Args(c, f.at(0x10), CtxL::Slot(ctx, i));
        Call(c, 0x5b6e4, 0x5ad24);
        Args(c, f.get(0x14));
        const uint32_t busy = Call(c, 0x5bc94, 0x5ad30);
        const uint32_t sema = f.get(0x10);
        if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
        if (busy) {
            DelayThread(c, 0x1388);
            continue;
        }
        ++i;
    }
    c.gpr[v0] = 0;   // the original's leftover (slti v0, s3, 3 at the loop's end)
}

// fn_5ad80(ctx, slot, volume, flags): open the slot's single VAGp voice on the Logitech headset
// (lgaud). -1 without a device; otherwise the device handle.
void fn_5ad80(Ctx& c) {
    Frame f(c, 0x28);
    f.save(0x14, s1);
    const uint32_t vol = c.gpr[a2];
    f.save(0x10, s0);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x18, s2);
    const uint32_t slot = c.gpr[a1];
    f.save(0x1c, s3);
    const uint32_t flags = c.gpr[a3];
    f.save(0x20, ra);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    if (R32(ctx + CtxL::kHeadsetDev) == minus1 || R32(slot + Slot::kVoiceCount) != 1) {
        c.gpr[v0] = minus1;
        return;
    }
    Args(c, slot, 0);
    const uint32_t desc = Call(c, 0x5bcf4, 0x5add4);
    const uint32_t buf = R32(desc + 8);
    if (R32(buf) != kMagicVagp || Bswap32(R32(buf + 0x10)) != 0x5622) {
        c.gpr[v0] = minus1;
        return;
    }
    W32(desc + 8, buf + 0x30);
    WaitSema(c, R32(ctx + CtxL::kHeadsetSema));
    Args(c, R32(ctx + CtxL::kHeadsetDev));
    Import(c, "lgaud", 13);
    const int32_t level = static_cast<int32_t>(static_cast<uint32_t>(vol) * 80u) / 1024;
    Args(c, R32(ctx + CtxL::kHeadsetDev), 0, static_cast<uint32_t>(level) & 0xff);
    Import(c, "lgaud", 16);
    if (flags & 1) {
        Args(c, R32(ctx + CtxL::kHeadsetDev));
        Import(c, "lgaud", 14);
    }
    Args(c, R32(ctx + CtxL::kThreadA), ctx);
    W32(ctx + CtxL::kHeadsetSlot, slot);
    if (Import(c, "thbase", 6) == 0) {
        c.gpr[v0] = R32(ctx + CtxL::kHeadsetDev);
        return;
    }
    Args(c, R32(ctx + CtxL::kHeadsetSema));
    W32(ctx + CtxL::kHeadsetSlot, 0);
    Import(c, "thsemap", 6);
    c.gpr[v0] = minus1;
}

// fn_5aedc(ctx): the fresh context.
void fn_5aedc(Ctx& c) {
    Frame f(c, 0x28);
    f.save(0x14, s1);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x10, s0);
    f.save(0x18, s2);
    f.save(0x1c, s3);
    f.save(0x20, ra);
    for (uint32_t i = 0; i < CtxL::kSlotCount; ++i) {
        Args(c, CtxL::Slot(ctx, i));
        Call(c, 0x5b4ec, 0x5af0c);
    }
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    W32(ctx + CtxL::kHeadsetDev, minus1);
    W32(ctx + CtxL::kShutdown, 0);
    W32(ctx + CtxL::kMicLevel, 0);
    W32(ctx + CtxL::kHeadsetSema, minus1);
    W32(ctx + CtxL::kThreadA, minus1);
    W32(ctx + CtxL::kSemaB, minus1);
    W32(ctx + CtxL::kThreadB, minus1);
    W32(ctx + CtxL::kSemaC, minus1);
    W32(ctx + CtxL::kThreadC, minus1);
    W32(ctx + CtxL::kHeadsetSlot, 0);
    W32(ctx + CtxL::kSectorBuf, 0);
    W32(ctx + CtxL::kTrayCount, 0);
    W32(ctx + CtxL::kDiscSema, minus1);
    W32(ctx + CtxL::kReadBuf, 0);
    W32(CtxL::Handle(ctx, 2), 0);
    W32(CtxL::Handle(ctx, 1), 0);
    W32(CtxL::Handle(ctx, 0), 0);
    W32(ctx + CtxL::kNextHandle, 0);
    c.gpr[v0] = ctx;
}

// fn_5af94(out, ctx, handle): find the slot with this handle; if it is in use, lock it and hand the lock
// over in out {sema, slot}; else out = {-1, 0}.
void fn_5af94(Ctx& c) {
    Frame f(c, 0x38);
    f.save(0x24, s3);
    const uint32_t out = c.gpr[a0];
    f.save(0x2c, s5);
    const uint32_t h = c.gpr[a2];
    f.save(0x20, s2);
    f.save(0x28, s4);
    const uint32_t ctx = c.gpr[a1];
    f.save(0x1c, s1);
    f.save(0x30, ra);
    f.save(0x18, s0);
    for (uint32_t i = 0; i < CtxL::kSlotCount; ++i) {
        if (R32(CtxL::Handle(ctx, i)) != h) continue;
        Args(c, f.at(0x10), CtxL::Slot(ctx, i));
        Call(c, 0x5b6e4, 0x5afe8);
        Args(c, f.get(0x14));
        if (Call(c, 0x5bc94, 0x5aff4) != 0) {
            const uint32_t sema = f.get(0x10);
            f.put(0x10, static_cast<uint32_t>(-1));
            const uint32_t slot = f.get(0x14);
            f.put(0x14, 0);
            W32(out, sema);
            W32(out + 4, slot);
            c.gpr[v0] = out;
            return;
        }
        const uint32_t sema = f.get(0x10);
        if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
    }
    W32(out, static_cast<uint32_t>(-1));
    W32(out + 4, 0);
    c.gpr[v0] = out;
}

// fn_5b074(ctx, slot): the slot's handle, under its lock.
void fn_5b074(Ctx& c) {
    Frame f(c, 0x20);
    f.save(0x18, s0);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x1c, ra);
    Args(c, f.at(0x10), c.gpr[a1]);
    Call(c, 0x5b6e4, 0x5b08c);
    Args(c, f.get(0x14));
    const uint32_t idx = Call(c, 0x5b72c, 0x5b098);
    const uint32_t sema = f.get(0x10);
    const uint32_t h = R32(ctx + CtxL::kHandles + idx * 4);
    c.gpr[v0] = h;
    if (static_cast<int32_t>(sema) >= 0) SignalSema(c, sema);
    c.gpr[v0] = h;
}

// ---------------------------------------------------------------------------------------------------------
// The three threads op 0x00 starts.

// fn_5b0cc(ctx): thread B -- the Logitech headset monitor. Without a device (lgaud:18 reports state != 1)
// it sleeps 500 ms and looks again; with one it opens the capture pipe and computes the mic level (an RMS
// over 0x400 samples in single-precision soft float: fn_5d8e0 int->float, fn_5d078 add, fn_5d134 mul,
// fn_5f0f0 sqrt -- called through the resolver, recompiled until ported).
void fn_5b0cc(Ctx& c) {
    Frame f(c, 0x1a0);
    f.save(0x18c, s3);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x19c, ra);
    f.save(0x198, s6);
    f.save(0x194, s5);
    f.save(0x190, s4);
    f.save(0x188, s2);
    f.save(0x184, s1);
    f.save(0x180, s0);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    while (R32(ctx + CtxL::kShutdown) == 0) {
        Args(c, f.at(0x174));
        Import(c, "lgaud", 18);
        const uint32_t state = f.get(0x174);
        if (state != 1) {
            DelayThread(c, 0x7a120);
            continue;
        }
        Args(c, 0, f.at(0x10));
        if (Import(c, "lgaud", 4) != 0) {
            DelayThread(c, 0x7a120);
            continue;
        }
        // the capture descriptor at sp+0x164
        Args(c, 0, f.at(0x164), f.at(0x178));
        W8(f.at(0x164), 3);
        W16(f.at(0x16e), 0x5622);
        W8(f.at(0x16c), static_cast<uint8_t>(state));
        W8(f.at(0x16d), 0x10);
        W16(f.at(0x170), 0x64);
        W8(f.at(0x166), static_cast<uint8_t>(state));
        W8(f.at(0x167), 0x10);
        W16(f.at(0x168), 0x2b11);
        W16(f.at(0x16a), 0x64);
        Import(c, "lgaud", 5);
        const uint32_t dev = f.get(0x178);
        if (dev == minus1) continue;
        W32(ctx + CtxL::kHeadsetDev, dev);
        Args(c, dev);
        Import(c, "lgaud", 11);
        f.put(0x17c, 0x800);
        for (;;) {
            if (R32(ctx + CtxL::kShutdown) != 0) break;
            Args(c, f.get(0x178), 0, R32(ctx + CtxL::kSectorBuf), f.at(0x17c));
            if (Import(c, "lgaud", 7) != 0) break;
            // the RMS of the captured block
            uint32_t acc = 0;
            uint32_t p = R32(ctx + CtxL::kSectorBuf);
            for (int32_t n = 0x3ff; n >= 0; --n) {
                const int32_t s = R16s(p);
                p += 2;
                Args(c, static_cast<uint32_t>(s * s));
                const uint32_t fs = Call(c, 0x5d8e0, 0x5b1dc);
                Args(c, acc, fs);
                acc = Call(c, 0x5d078, 0x5b1e8);
            }
            Args(c, acc, 0x3a800000);
            uint32_t r = Call(c, 0x5d134, 0x5b1fc);
            Args(c, r);
            r = Call(c, 0x5f0f0, 0x5b204);
            Args(c, r, 0x38000000);
            r = Call(c, 0x5d134, 0x5b210);
            W32(ctx + CtxL::kMicLevel, r);
        }
        Args(c, f.get(0x178));
        Import(c, "lgaud", 6);
        W32(ctx + CtxL::kHeadsetDev, minus1);
        W32(ctx + CtxL::kMicLevel, 0);
    }
    Args(c, 0, 9);
    Import(c, "thbase", 14);
    SignalSema(c, R32(ctx + CtxL::kSemaB));
}

// fn_5b2a0(ctx): thread A -- feeds the headset from the slot ctx->0x18 points at, 5 ms polls.
void fn_5b2a0(Ctx& c) {
    Frame f(c, 0x38);
    f.save(0x30, s4);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x34, ra);
    f.save(0x2c, s3);
    f.save(0x28, s2);
    f.save(0x24, s1);
    f.save(0x20, s0);
    const uint32_t lock = f.at(0x10);
    for (;;) {
        Args(c, lock, R32(ctx + CtxL::kHeadsetSlot));
        Call(c, 0x5b6e4, 0x5b3c4);
        const uint32_t slot = f.get(0x14);
        bool exit_now = true;
        if (slot) {
            Args(c, slot);
            if (Call(c, 0x5bc94, 0x5b3dc) != 0) exit_now = false;
        }
        if (exit_now) {
            SignalIfValid(c, f.get(0x10));
            break;
        }
        Args(c, slot, 0);
        const uint32_t desc = Call(c, 0x5bcf4, 0x5b2d0);
        if (desc == 0) {
            SignalIfValid(c, f.get(0x10));
            break;
        }
        const uint32_t end = R32(desc + 4) + R32(desc);
        Args(c, slot);
        const uint32_t avail = Call(c, 0x5bd4c, 0x5b2ec);
        const uint32_t to_end = end - R32(desc + 8);
        if (avail < to_end) {
            Args(c, slot);
            f.put(0x18, Call(c, 0x5bd4c, 0x5b310));
        } else {
            f.put(0x18, to_end);
        }
        if (static_cast<int32_t>(f.get(0x18)) > 0) {
            Args(c, R32(ctx + CtxL::kHeadsetDev), 1, R32(desc + 8), f.at(0x18));
            if (Import(c, "lgaud", 22) != 0) {
                SignalIfValid(c, f.get(0x10));
                break;
            }
            const uint32_t n = f.get(0x18);
            const uint32_t cur = R32(desc + 8) + n;
            const uint32_t total = R32(desc + 0xc) + f.get(0x18);
            W32(desc + 8, cur);
            W32(desc + 0xc, total);
            if (!(cur < end)) W32(desc + 8, R32(desc + 4));
        }
        SignalIfValid(c, f.get(0x10));
        DelayThread(c, 0x1388);
    }
    Args(c, 0, 9);
    W32(ctx + CtxL::kHeadsetSlot, 0);
    Import(c, "thbase", 14);
    SignalSema(c, R32(ctx + CtxL::kHeadsetSema));
}

// fn_5b438(ctx): thread C -- the tray poll (sceCdTrayReq every 100 ms under the disc lock) -> ctx->0x2c.
void fn_5b438(Ctx& c) {
    Frame f(c, 0x20);
    f.save(0x18, s0);
    const uint32_t ctx = c.gpr[a0];
    f.save(0x1c, ra);
    while (R32(ctx + CtxL::kShutdown) == 0) {
        const uint32_t sema = R32(ctx + CtxL::kDiscSema);
        f.put(0x10, sema);
        WaitIfValid(c, sema);
        f.put(0x14, 0);
        Args(c, 2, f.at(0x14));
        if (Import(c, "cdvdman", 14) != 0) W32(ctx + CtxL::kTrayCount, f.get(0x14));
        SignalIfValid(c, f.get(0x10));
        DelayThread(c, 0x186a0);
    }
    Args(c, 0, 9);
    Import(c, "thbase", 14);
    SignalSema(c, R32(ctx + CtxL::kSemaC));
}

// ---------------------------------------------------------------------------------------------------------
// Slots.

void fn_5b4ec(Ctx& c) {   // the slot's one-time init
    Frame f(c, 0x18);
    f.save(0x10, s0);
    const uint32_t slot = c.gpr[a0];
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    f.save(0x14, ra);
    W32(slot + Slot::kState, minus1);
    W32(slot + Slot::kSema, minus1);
    W32(slot + Slot::kThread, minus1);
    Args(c, slot);
    W32(slot + Slot::kRing, 0);
    Call(c, 0x5b528, 0x5b514);
    c.gpr[v0] = slot;
}

void fn_5b528(Ctx& c) {   // the slot's per-stream reset (leaf; $v0 = -1 is the original's leftover)
    const uint32_t slot = c.gpr[a0];
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    c.gpr[v0] = minus1;
    W32(slot + Slot::kLba, 0);
    W32(slot + Slot::kVolume, 0);
    W32(slot + Slot::kPan, 0);
    W32(slot + Slot::kIndex, minus1);
    W32(slot + Slot::kLoop, 0);
    W32(slot + Slot::kHeadset, 0);
    W32(slot + Slot::kFlags, 0);
    W32(slot + Slot::kVoiceCount, 0);
    W32(slot + Slot::kHeadsetStream, minus1);
    W32(slot + Slot::kStreamPos, 0);
    W32(slot + Slot::kDataStart, 0);
    W32(slot + Slot::kDataEnd, 0);
    W32(slot + Slot::kLoopStart, 0);
    W32(slot + Slot::kWriteCursor, 0);
}

void fn_5b568(Ctx& c) {   // (slot, index): the ring buffers, the lock, the thread (not started)
    Frame f(c, 0x40);
    f.save(0x38, s0);
    const uint32_t slot = c.gpr[a0], idx = c.gpr[a1];
    f.save(0x3c, ra);
    if (R32(slot + Slot::kRing) != 0) {
        c.gpr[v0] = 1;
        return;
    }
    if (idx >= CtxL::kSlotCount) {
        c.gpr[v0] = 0;
        return;
    }
    Args(c, Slot::RingBytesForIndex(idx));
    const uint32_t ring = Call(c, 0x5cbc0, 0x5b5d8);
    W32(slot + Slot::kRing, ring);
    if (ring == 0) {
        c.gpr[v0] = 0;
        return;
    }
    Args(c, f.at(0x10));
    f.put(0x10, 0);
    f.put(0x18, 1);
    f.put(0x1c, 1);
    f.put(0x14, 0);
    const uint32_t sema = Import(c, "thsemap", 4);
    W32(slot + Slot::kSema, sema);
    if (static_cast<int32_t>(sema) < 0) {
        c.gpr[v0] = 0;
        return;
    }
    Args(c, f.at(0x20));
    f.put(0x20, 0x2000000);
    f.put(0x28, 0x5be48);
    f.put(0x30, 0x50);
    f.put(0x2c, 0x800);
    f.put(0x24, 0);
    const uint32_t th = Import(c, "thbase", 4);
    c.gpr[v1] = th;
    c.gpr[v0] = static_cast<int32_t>(th) > 0 ? 1u : 0u;
    W32(slot + Slot::kThread, th);
}

void fn_5b658(Ctx& c) {   // (slot): stop, wait idle, delete the thread, the lock, the rings
    Frame f(c, 0x20);
    f.save(0x10, s0);
    const uint32_t slot = c.gpr[a0];
    f.save(0x18, ra);
    Args(c, slot);
    f.save(0x14, s1);
    Call(c, 0x5b810, 0x5b670);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    while (R32(slot + Slot::kState) != minus1) DelayThread(c, 0x1388);
    Args(c, R32(slot + Slot::kThread));
    Import(c, "thbase", 5);
    Args(c, R32(slot + Slot::kSema));
    Import(c, "thsemap", 5);
    const uint32_t ring = R32(slot + Slot::kRing);
    c.gpr[v0] = minus1;   // the original's leftover when there is nothing to free
    W32(slot + Slot::kSema, minus1);
    W32(slot + Slot::kThread, minus1);
    if (ring) {
        Args(c, ring);
        Call(c, 0x5cba0, 0x5b6cc);
    }
    W32(slot + Slot::kRing, 0);
}

void fn_5b6e4(Ctx& c) {   // (out, slot): take the slot lock; out = {sema, slot}
    Frame f(c, 0x20);
    f.save(0x14, s1);
    const uint32_t slot = c.gpr[a1];
    f.save(0x18, ra);
    f.save(0x10, s0);
    const uint32_t out = c.gpr[a0];
    const uint32_t sema = R32(slot + Slot::kSema);
    W32(out, sema);
    if (static_cast<int32_t>(sema) >= 0) WaitSema(c, sema);
    W32(out + 4, slot);
    c.gpr[v0] = out;
}

void fn_5b72c(Ctx& c) { c.gpr[v0] = R32(c.gpr[a0] + Slot::kIndex); }   // (slot) -> index

void fn_5b738(Ctx& c) {   // (slot, args): fill the slot from the open's 0x18-byte block, start its thread
    Frame f(c, 0x20);
    f.save(0x10, s0);
    const uint32_t slot = c.gpr[a0];
    f.save(0x14, s1);
    f.save(0x18, ra);
    const uint32_t args = c.gpr[a1];
    if (Call(c, 0x5bc94, 0x5b754) != 0 || R32(slot + Slot::kRing) == 0) {
        c.gpr[v0] = 0;
        return;
    }
    W32(slot + Slot::kLba, R32(args));
    W32(slot + Slot::kVolume, R32(args + 4));
    W32(slot + Slot::kPan, R32(args + 8));
    W32(slot + Slot::kIndex, R32(args + 0xc));
    W32(slot + Slot::kLoop, (R32(args + 0x10) >> 2) & 1);
    W32(slot + Slot::kHeadset, R32(args + 0x14));
    Args(c, slot, 0, 1);
    W32(slot + Slot::kFlags, R32(args + 0x10));
    Call(c, 0x5bff8, 0x5b7cc);
    Args(c, R32(slot + Slot::kThread), slot);
    if (Import(c, "thbase", 6) == 0) {
        c.gpr[v0] = 1;
        return;
    }
    Args(c, slot);
    Call(c, 0x5b528, 0x5b7e8);
    Args(c, slot, static_cast<uint32_t>(-1), 1);
    Call(c, 0x5bff8, 0x5b7f8);
    c.gpr[v0] = 0;
}

void fn_5b810(Ctx& c) {   // (slot): request the stop (state 4, the lock already held)
    Frame f(c, 0x18);
    Args(c, c.gpr[a0], 4, 1);
    f.save(0x10, ra);
    Call(c, 0x5bff8, 0x5b824);
}

// ops 0x03 / 0x04 on a playing slot: headset -> lgaud:14 / lgaud:15; voices -> snd989:27 / snd989:28.
template <uint32_t kLgaud, uint32_t kSnd>
void PerVoiceOp(Ctx& c) {
    Frame f(c, 0x20);
    f.save(0x18, s2);
    const uint32_t slot = c.gpr[a0];
    f.save(0x1c, ra);
    f.save(0x14, s1);
    f.save(0x10, s0);
    c.gpr[v0] = 3;   // the leftovers below are the original's at each exit
    if (R32(slot + Slot::kState) != 3) return;
    if (R32(slot + Slot::kHeadset) != 0) {
        const uint32_t hs = R32(slot + Slot::kHeadsetStream);
        c.gpr[v0] = static_cast<uint32_t>(-1);
        if (hs == static_cast<uint32_t>(-1)) return;
        Args(c, hs);
        Import(c, "lgaud", kLgaud);
        return;
    }
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    c.gpr[v0] = static_cast<uint32_t>(n);
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t h = R32(Slot::Voice(slot, static_cast<uint32_t>(i)));
        if (h) {
            Args(c, h);
            Import(c, "snd989", kSnd);
        }
        c.gpr[v0] = (i + 1 < R32s(slot + Slot::kVoiceCount)) ? 1u : 0u;
    }
}
void fn_5b834(Ctx& c) { PerVoiceOp<14, 27>(c); }
void fn_5b8ec(Ctx& c) { PerVoiceOp<15, 28>(c); }

// fn_5b9a4(slot, flags, volume, pan, x5@sp+0x40): op 0x05 -- the master volume (flag 1) / pan (flag 4),
// pushed to the headset (lgaud:16) or to every voice (snd989:126 with the voice's own volume * master / 1024
// and (pan + master pan) mod 360).
void fn_5b9a4(Ctx& c) {
    Frame f(c, 0x30);
    f.save(0x1c, s1);
    const uint32_t slot = c.gpr[a0];
    f.save(0x24, s3);
    const uint32_t flags = c.gpr[a1];
    f.save(0x2c, ra);
    f.save(0x28, s4);
    f.save(0x20, s2);
    f.save(0x18, s0);
    const uint32_t state = R32(slot + Slot::kState);
    c.gpr[v0] = state;   // the leftovers below are the original's at each exit
    if (state != 3) return;
    if (flags & 1) W32(slot + Slot::kVolume, c.gpr[a2]);
    if (flags & 4) W32(slot + Slot::kPan, c.gpr[a3]);
    if (R32(slot + Slot::kHeadset) != 0) {
        const uint32_t hs = R32(slot + Slot::kHeadsetStream);
        c.gpr[v0] = static_cast<uint32_t>(-1);
        if (hs == static_cast<uint32_t>(-1) || !(flags & 1)) return;
        const int32_t level = static_cast<int32_t>(static_cast<uint32_t>(R32(slot + Slot::kVolume)) * 80u) / 1024;
        Args(c, hs, 0, static_cast<uint32_t>(level) & 0xff);
        Import(c, "lgaud", 16);
        return;
    }
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    c.gpr[v0] = static_cast<uint32_t>(n);
    if (n <= 0) return;
    const int32_t x5 = static_cast<int16_t>(f.get(0x40));
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t voice = Slot::Voice(slot, static_cast<uint32_t>(i));
        const uint32_t h = R32(voice + Voice::kHandle);
        if (h != 0) {
            const int32_t vol = MulDiv1024(R32s(voice + Voice::kVolume), R32s(slot + Slot::kVolume));
            const int32_t pan = Mod360Positive(R32s(voice + Voice::kPan) + R32s(slot + Slot::kPan));
            Args(c, h, flags, static_cast<uint32_t>(vol), static_cast<uint32_t>(pan));
            f.put(0x10, static_cast<uint32_t>(x5));
            f.put(0x14, 0);
            Import(c, "snd989", 126);
        }
        c.gpr[v0] = (i + 1 < R32s(slot + Slot::kVoiceCount)) ? 1u : 0u;
    }
}

// fn_5bb38(slot, voice id, volume, pan): op 0x06 "set" -- every voice with this id gets the volume / pan;
// snd989:32 (snd_SetSoundVolPan) with volume * master / 1024 and (pan + master pan) mod 360.
void fn_5bb38(Ctx& c) {
    Frame f(c, 0x30);
    f.save(0x18, s2);
    const uint32_t slot = c.gpr[a0];
    f.save(0x24, s5);
    const uint32_t id = c.gpr[a1];
    f.save(0x20, s4);
    const uint32_t vol = c.gpr[a2];
    f.save(0x1c, s3);
    const uint32_t pan = c.gpr[a3];
    f.save(0x28, ra);
    f.save(0x14, s1);
    f.save(0x10, s0);
    c.gpr[v0] = 3;   // the leftovers below are the original's at each exit
    if (R32(slot + Slot::kState) != 3) return;
    const uint32_t headset = R32(slot + Slot::kHeadset);
    c.gpr[v0] = headset;
    if (headset != 0) return;
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    c.gpr[v0] = static_cast<uint32_t>(n);
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t voice = Slot::Voice(slot, static_cast<uint32_t>(i));
        const uint32_t h = R32(voice + Voice::kHandle);
        if (h != 0 && R32(voice + Voice::kId) == id) {
            W32(voice + Voice::kVolume, vol);
            W32(voice + Voice::kPan, pan);
            const int32_t evol = MulDiv1024(static_cast<int32_t>(vol), R32s(slot + Slot::kVolume));
            const int32_t epan = Mod360Positive(static_cast<int32_t>(pan) + R32s(slot + Slot::kPan));
            Args(c, h, static_cast<uint32_t>(evol), static_cast<uint32_t>(epan));
            Import(c, "snd989", 32);
        }
        c.gpr[v0] = (i + 1 < R32s(slot + Slot::kVoiceCount)) ? 1u : 0u;
    }
}

void fn_5bc6c(Ctx& c) {   // (slot) "start": 0 while the stream is in states 0..2, 1 otherwise (leaf)
    const int32_t st = R32s(c.gpr[a0] + Slot::kState);
    c.gpr[v0] = (st >= 3 || st < 0) ? 1u : 0u;
}

void fn_5bc94(Ctx& c) { c.gpr[v0] = R32(c.gpr[a0] + Slot::kState) != static_cast<uint32_t>(-1) ? 1u : 0u; }   // in use?

void fn_5bca8(Ctx& c) {   // (slot, int16* read, int16* write): the ring cursors in 16-byte units (leaf)
    const uint32_t slot = c.gpr[a0];
    const int32_t ring = R32s(slot + Slot::kRing);
    W16(c.gpr[a1], static_cast<uint16_t>((R32s(Slot::Voice(slot, 0) + Voice::kDescReadCursor) - ring) / 16));
    const int32_t w = (R32s(slot + Slot::kWriteCursor) - ring) / 16;
    c.gpr[v0] = static_cast<uint32_t>(w);   // the original's leftover
    W16(c.gpr[a2], static_cast<uint16_t>(w));
}

void fn_5bcf4(Ctx& c) {   // (slot, i) -> voice i's 989snd descriptor, or 0 (leaf)
    const uint32_t slot = c.gpr[a0];
    const int32_t i = static_cast<int32_t>(c.gpr[a1]);
    const uint32_t st = R32(slot + Slot::kState);
    if (i < 0 || i >= R32s(slot + Slot::kVoiceCount) || st == static_cast<uint32_t>(-1) || st == 4) {
        c.gpr[v0] = 0;
        return;
    }
    c.gpr[v0] = Slot::Voice(slot, static_cast<uint32_t>(i)) + Voice::kDesc;
}

void fn_5bd4c(Ctx& c) {   // (slot) -> bytes buffered ahead of voice 0's read cursor (leaf)
    const uint32_t slot = c.gpr[a0];
    const uint32_t st = R32(slot + Slot::kState);
    if (R32(slot + Slot::kVoiceCount) == 0 || st == static_cast<uint32_t>(-1) || st == 4) {
        c.gpr[v0] = 0;
        return;
    }
    int32_t d = R32s(slot + Slot::kWriteCursor) - R32s(Slot::Voice(slot, 0) + Voice::kDescReadCursor);
    if (d < 0) d += Slot::kRingBytes;
    c.gpr[v0] = static_cast<uint32_t>(d);
}

void fn_5bda8(Ctx& c) {   // (slot) -> free ring bytes (capped by the data left unless looping) (leaf)
    const uint32_t slot = c.gpr[a0];
    if (R32(slot + Slot::kVoiceCount) == 0) {
        c.gpr[v0] = 0;
        return;
    }
    int32_t free_bytes = R32s(Slot::Voice(slot, 0) + Voice::kDescReadCursor) - R32s(slot + Slot::kWriteCursor);
    if (free_bytes < 0) free_bytes += Slot::kRingBytes;
    if (R32(slot + Slot::kLoop) != 0) {
        c.gpr[v0] = static_cast<uint32_t>(free_bytes);
        return;
    }
    const uint32_t remaining = R32(slot + Slot::kDataEnd) - R32(slot + Slot::kStreamPos);
    c.gpr[v0] = static_cast<uint32_t>(free_bytes) < remaining ? static_cast<uint32_t>(free_bytes) : remaining;
}

void fn_5be0c(Ctx& c) {   // (slot) -> the refill threshold: 0x6000, or the data left when less (leaf)
    const uint32_t slot = c.gpr[a0];
    uint32_t r = Slot::kReadCap;
    if (R32(slot + Slot::kLoop) == 0) {
        const uint32_t remaining = R32(slot + Slot::kDataEnd) - R32(slot + Slot::kStreamPos);
        if (!(Slot::kReadCap < remaining)) r = remaining;
    }
    c.gpr[v0] = r;
}

// fn_5be48(slot): THE STREAM THREAD. A state machine polled every 5 ms: 0 read the header (fn_5c0c4) -> 1
// open the 989snd voices (fn_5c410) -> 2 wait for them to arm (fn_5c560) -> 3 playing: refill when the ring
// has room (fn_5c794), stop when the voices end or the data runs dry -> 4 stop: close the voices, free the
// handle, reset, back to -1 and exit.
void fn_5be48(Ctx& c) {
    Frame f(c, 0x30);
    f.save(0x14, s1);
    const uint32_t slot = c.gpr[a0];
    f.save(0x28, ra);
    f.save(0x24, s5);
    f.save(0x20, s4);
    f.save(0x1c, s3);
    f.save(0x18, s2);
    f.save(0x10, s0);
    const uint32_t ctx = Context(c, 0x5be70);
    for (;;) {
        const uint32_t st = R32(slot + Slot::kState);
        switch (st) {
            case 0:
                Args(c, slot);
                if (Call(c, 0x5c0c4, 0x5beac) != 0) {
                    Args(c, slot, 1, 0);
                    Call(c, 0x5bff8, 0x5bf60);
                }
                break;
            case 1:
                Args(c, slot);
                if (Call(c, 0x5c410, 0x5bec4) != 0) {
                    Args(c, slot, 2, 0);
                    Call(c, 0x5bff8, 0x5bf60);
                }
                break;
            case 2:
                Args(c, slot);
                if (Call(c, 0x5c560, 0x5bedc) != 0) {
                    Args(c, slot, 3, 0);
                    Call(c, 0x5bff8, 0x5bf60);
                }
                break;
            case 3: {
                bool stop = false;
                Args(c, slot);
                if (Call(c, 0x5c628, 0x5bef8) == 0) {
                    stop = true;
                } else {
                    Args(c, slot);
                    const uint32_t room = Call(c, 0x5bda8, 0x5bf10);
                    if (static_cast<int32_t>(room) > 0) {
                        Args(c, slot);
                        const uint32_t threshold = Call(c, 0x5be0c, 0x5bf24);
                        if (!(room < threshold)) {
                            Args(c, slot, 0);
                            Call(c, 0x5c794, 0x5bf38);
                            Args(c, slot);
                            const uint32_t after = Call(c, 0x5bda8, 0x5bf40);
                            stop = (Slot::kRingBytes - after) < 0x2000;
                        }
                    }
                }
                if (stop) {
                    Args(c, slot, 4, 0);
                    Call(c, 0x5bff8, 0x5bf60);
                }
                break;
            }
            case 4: {
                Args(c, slot);
                Call(c, 0x5c6f0, 0x5bf70);
                Args(c, ctx, slot);
                const uint32_t h = Call(c, 0x5b074, 0x5bf7c);
                Args(c, ctx, h);
                Call(c, 0x5a8dc, 0x5bf88);
                Args(c, slot);
                Call(c, 0x5b528, 0x5bf90);
                Args(c, 0, 9);
                Import(c, "thbase", 14);
                Args(c, slot, static_cast<uint32_t>(-1), 0);
                Call(c, 0x5bff8, 0x5bfac);
                return;
            }
            default:
                break;
        }
        if (R32(slot + Slot::kState) == st) DelayThread(c, 0x1388);
    }
}

// fn_5bff8(slot, state, nolock): the state setter under the slot lock (nolock == 1: the caller holds it).
// Refused: no change; going backwards unless to -1; 4 (stop) on an idle slot.
void fn_5bff8(Ctx& c) {
    Frame f(c, 0x28);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    f.save(0x1c, s1);
    const uint32_t slot = c.gpr[a0];
    f.save(0x18, s0);
    const int32_t next = static_cast<int32_t>(c.gpr[a1]);
    uint32_t sema = minus1;
    f.save(0x20, ra);
    if (c.gpr[a2] != 1) sema = R32(slot + Slot::kSema);
    f.put(0x10, sema);
    if (static_cast<int32_t>(sema) >= 0) WaitSema(c, sema);
    const int32_t cur = R32s(slot + Slot::kState);
    bool refuse = next == cur;
    c.gpr[v0] = static_cast<uint32_t>(cur);   // the original's leftovers at each decision
    if (!refuse && next < cur) {
        c.gpr[v0] = minus1;
        if (next != -1) refuse = true;
    }
    if (!refuse && (next >= cur || next == -1) && !(next == cur)) {
        c.gpr[v0] = 4;
        if (cur == -1 && next == 4) refuse = true;
    }
    if (!refuse) W32(slot + Slot::kState, static_cast<uint32_t>(next));
    SignalIfValid(c, f.get(0x10));
}

// fn_5c0c4(slot): state 0 -- under the disc lock read the first sector of the file into the ring, parse the
// header ("VAGp": one voice, 0x30-byte header, data size at +0xc big-endian; " KPV": the container -- data
// end at +4, data start at +8, 0x2000 at +0xc, voice count at +0x14, per voice {?, id, pan} at +0x18, loop
// start at +0x7fc; a 0x30-byte VAGp header is synthesised into every voice's ring), then the first refill.
void fn_5c0c4(Ctx& c) {
    Frame f(c, 0x60);
    f.save(0x50, s2);
    const uint32_t slot = c.gpr[a0];
    f.save(0x58, ra);
    f.save(0x54, s3);
    f.save(0x4c, s1);
    const uint32_t ctx = Context(c, 0x5c0e4);
    f.save(0x48, s0);
    const uint32_t lock = R32(ctx + CtxL::kDiscSema);
    f.put(0x10, lock);
    WaitIfValid(c, lock);
    const uint32_t ring = R32(slot + Slot::kRing);
    Args(c, R32(slot + Slot::kLba), 1, ring, G::kReadMode);
    W32(slot + Slot::kStreamPos, 0);
    W32(slot + Slot::kLoopStart, 0);
    W32(slot + Slot::kWriteCursor, ring);
    const uint32_t issued = Import(c, "cdvdman", 6);
    Args(c, 0);
    Import(c, "cdvdman", 11);
    auto fail = [&]() {
        c.gpr[v0] = 0;
        SignalIfValid(c, f.get(0x10));
        c.gpr[v0] = 0;
    };
    if (issued == 0) return fail();
    const uint32_t hdr = R32(slot + Slot::kWriteCursor);
    const uint32_t magic = R32(hdr);
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    if (magic == kMagicVpk) {
        if (R32(hdr + 0xc) != 0x2000 || R32(hdr + 0x14) >= 6) return fail();
        W32(slot + Slot::kDataEnd, R32(hdr + 4));
        W32(slot + Slot::kDataStart, R32(hdr + 8));
        const int32_t n = R32s(hdr + 0x14);
        W32(slot + Slot::kVoiceCount, static_cast<uint32_t>(n));
        W32(slot + Slot::kLoopStart, R32(hdr + 0x7fc));
        uint32_t ring_off = 0;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t voice = Slot::Voice(slot, static_cast<uint32_t>(i));
            const uint32_t rec = hdr + 0x18 + static_cast<uint32_t>(i) * 4;
            W32(voice + Voice::kHandle, 0);
            W32(voice + Voice::kVolume, 0x400);
            W32(voice + Voice::kId, static_cast<uint32_t>(static_cast<int32_t>(R8s(rec + 1))));
            W32(voice + Voice::kDescRingBytes, Slot::kRingBytes);
            W32(voice + Voice::kPan, static_cast<uint32_t>(static_cast<int32_t>(R16s(rec + 2))));
            const uint32_t vring = R32(slot + Slot::kRing) + ring_off;
            ring_off += Slot::kRingBytes;
            W32(voice + Voice::kDesc1c, 0);
            W32(voice + Voice::kDesc20, 0);
            W32(voice + Voice::kDesc24, 0);
            W32(voice + Voice::kDesc28, minus1);
            W32(voice + Voice::kDescRing, vring);
            W32(voice + Voice::kDescReadCursor, vring);
        }
        const uint32_t vag = f.at(0x14);
        Args(c, vag, 0, 0x30);
        Import(c, "sysclib", 14);   // memset
        W32(vag, kMagicVagp);
        W32(vag + 0x10, Bswap32(R32(hdr + 0x10)));
        if (R32(slot + Slot::kLoop) != 0) W32(vag + 0xc, 0xffffff7f);
        else W32(vag + 0xc, Bswap32(R32(hdr + 4)));
        for (int32_t i = 0; i < R32s(slot + Slot::kVoiceCount); ++i) {
            Args(c, R32(Slot::Voice(slot, static_cast<uint32_t>(i)) + Voice::kDescRing), vag, 0x30);
            Import(c, "sysclib", 40);   // wmemcopy
        }
    } else if (magic == kMagicVagp) {
        W32(slot + Slot::kDataStart, 0x30);
        W32(slot + Slot::kVoiceCount, 1);
        W32(slot + Slot::kDataEnd, Bswap32(R32(hdr + 0xc)));
        if (R32(slot + Slot::kLoop) != 0) W32(hdr + 0xc, 0xffffff7f);
        const uint32_t voice = Slot::Voice(slot, 0);
        W32(voice + Voice::kHandle, 0);
        W32(voice + Voice::kVolume, 0x400);
        W32(voice + Voice::kId, 0);
        W32(voice + Voice::kPan, 0);
        W32(voice + Voice::kDescRingBytes, Slot::kRingBytes);
        W32(voice + Voice::kDescRing, R32(slot + Slot::kRing));
        const uint32_t vring = R32(slot + Slot::kRing);
        W32(voice + Voice::kDesc1c, 0);
        W32(voice + Voice::kDesc20, 0);
        W32(voice + Voice::kDesc24, 0);
        W32(voice + Voice::kDesc28, minus1);
        W32(voice + Voice::kDescReadCursor, vring);
    } else {
        return fail();
    }
    Args(c, slot, 1);
    W32(slot + Slot::kWriteCursor, R32(slot + Slot::kWriteCursor) + 0x30);
    const uint32_t r = Call(c, 0x5c794, 0x5c3d8);
    SignalIfValid(c, f.get(0x10));
    c.gpr[v0] = r;
}

// fn_5c410(slot): state 1 -- the headset path (fn_5ad80) or one 989snd stream per voice (snd989:49 with the
// voice's descriptor; two consecutive voices with the same id are opened as a stereo pair).
void fn_5c410(Ctx& c) {
    Frame f(c, 0x40);
    f.save(0x34, s3);
    const uint32_t slot = c.gpr[a0];
    f.save(0x3c, ra);
    f.save(0x38, s4);
    f.save(0x30, s2);
    f.save(0x2c, s1);
    const uint32_t ctx = Context(c, 0x5c434);
    f.save(0x28, s0);
    if (R32(slot + Slot::kHeadset) != 0) {
        Args(c, ctx, slot, R32(slot + Slot::kVolume), R32(slot + Slot::kFlags));
        W32(slot + Slot::kHeadsetStream, Call(c, 0x5ad80, 0x5c454));
    }
    if (R32(slot + Slot::kHeadsetStream) != static_cast<uint32_t>(-1)) {
        c.gpr[v0] = 1;
        return;
    }
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    W32(slot + Slot::kHeadset, 0);
    if (n <= 0) {
        c.gpr[v0] = 1;
        return;
    }
    for (int32_t i = 0; i < n;) {
        const uint32_t voice = Slot::Voice(slot, static_cast<uint32_t>(i));
        uint32_t pair = 0;
        if (i < n - 1) {
            const uint32_t next = Slot::Voice(slot, static_cast<uint32_t>(i + 1));
            if (R32(next + Voice::kId) == R32(voice + Voice::kId)) {
                pair = next;
                ++i;
            }
        }
        Args(c, voice + Voice::kDesc, pair ? pair + Voice::kDesc : 0, 0, 0);
        f.put(0x1c, 0);
        f.put(0x20, 0);
        f.put(0x10, R32(slot + Slot::kVolume));
        f.put(0x14, R32(slot + Slot::kPan));
        f.put(0x18, R32(slot + Slot::kIndex));
        f.put(0x24, R32(slot + Slot::kFlags) | 0x10000);
        const uint32_t h = Import(c, "snd989", 49);
        W32(voice + Voice::kHandle, h);
        if (R32(voice + Voice::kHandle) == 0) {
            Args(c, slot);
            Call(c, 0x5c6f0, 0x5c520);
            c.gpr[v0] = 0;
            return;
        }
        ++i;
    }
    c.gpr[v0] = 1;
}

// states 2 and 3: every voice with a handle answers snd989:99 (armed) / snd989:25 (playing) -> 1 when all do.
template <uint32_t kSnd>
void AllVoices(Ctx& c) {
    Frame f(c, 0x30);
    f.save(0x24, s3);
    const uint32_t slot = c.gpr[a0];
    f.save(0x2c, ra);
    f.save(0x28, s4);
    f.save(0x20, s2);
    f.save(0x1c, s1);
    f.save(0x18, s0);
    if (R32(slot + Slot::kHeadset) != 0) {
        Args(c, R32(slot + Slot::kHeadsetStream), f.at(0x10));
        f.put(0x10, 0);
        Import(c, "lgaud", 19);
        c.gpr[v0] = f.get(0x10) != 0 ? 1u : 0u;
        return;
    }
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    uint32_t with_handle = 0, answered = 0;
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t h = R32(Slot::Voice(slot, static_cast<uint32_t>(i)));
        if (h == 0) continue;
        Args(c, h);
        ++with_handle;
        if (Import(c, "snd989", kSnd) != 0) ++answered;
    }
    c.gpr[v0] = with_handle == answered ? 1u : 0u;
}
void fn_5c560(Ctx& c) { AllVoices<99>(c); }
void fn_5c628(Ctx& c) { AllVoices<25>(c); }

void fn_5c6f0(Ctx& c) {   // (slot): close the voices (snd989:29) or forget the headset stream
    Frame f(c, 0x20);
    f.save(0x18, s2);
    const uint32_t slot = c.gpr[a0];
    f.save(0x1c, ra);
    f.save(0x14, s1);
    f.save(0x10, s0);
    if (R32(slot + Slot::kHeadset) != 0) {
        const uint32_t minus1 = static_cast<uint32_t>(-1);
        const uint32_t hs = R32(slot + Slot::kHeadsetStream);
        c.gpr[v0] = hs;   // the original's leftover
        if (hs != minus1) W32(slot + Slot::kHeadsetStream, minus1);
        return;
    }
    const int32_t n = R32s(slot + Slot::kVoiceCount);
    c.gpr[v0] = static_cast<uint32_t>(n);
    for (int32_t i = 0; i < n; ++i) {
        const uint32_t voice = Slot::Voice(slot, static_cast<uint32_t>(i));
        const uint32_t h = R32(voice);
        if (h != 0) {
            Args(c, h);
            Import(c, "snd989", 29);
            W32(voice, 0);
        }
        c.gpr[v0] = (i + 1 < R32s(slot + Slot::kVoiceCount)) ? 1u : 0u;
    }
}

// fn_5c794(slot, first): THE REFILL. Under the disc lock (unless `first`: the caller holds it) while the ring
// has room: take min(room, data left, 0x6000) bytes per voice; the file interleaves the voices in 0x1000-byte
// chunks, so the read covers n * chunk-aligned bytes from the voice-0 position (sector-aligned, through the
// 0x22800-byte read buffer); then per 0x1000-boundary-bounded piece copy each voice's bytes into its ring
// (wrapping at 0xc000), advance the stream position (loop start when looping past the end) and the write
// cursor. sceCdRead / sceCdSync are the engine's cdvdman (its completion timing untouched).
void fn_5c794(Ctx& c) {
    Frame f(c, 0x48);
    f.save(0x38, s6);
    const uint32_t slot = c.gpr[a0];
    f.save(0x20, s0);
    const uint32_t first = c.gpr[a1];
    f.save(0x44, ra);
    f.save(0x40, fp);
    f.save(0x3c, s7);
    f.save(0x34, s5);
    f.save(0x30, s4);
    f.save(0x2c, s3);
    f.save(0x28, s2);
    const uint32_t ctx = Context(c, 0x5c7cc);
    f.save(0x24, s1);
    uint32_t lock = static_cast<uint32_t>(-1);
    f.put(0x14, ctx);
    if (first == 0) lock = R32(ctx + CtxL::kDiscSema);
    f.put(0x10, lock);
    WaitIfValid(c, lock);
    Args(c, slot);
    int32_t want = static_cast<int32_t>(Call(c, 0x5bda8, 0x5c7f8));
    while (want > 0) {
        const uint32_t pos = R32(slot + Slot::kStreamPos);
        uint32_t take = R32(slot + Slot::kDataEnd) - pos;
        if (static_cast<uint32_t>(want) < take) take = static_cast<uint32_t>(want);
        if (!(take < 0x6001)) take = Slot::kReadCap;
        const uint32_t n = R32(slot + Slot::kVoiceCount);
        const uint32_t sub = pos & 0xfff;
        const uint32_t tail = (Slot::kInterleave - (sub + take)) & 0xfff;
        const uint32_t file_off = R32(slot + Slot::kDataStart) + n * (pos & ~0xfffu) + sub;
        const uint32_t in_sector = file_off & 0x7ff;
        uint32_t bytes = take * n + in_sector + (n - 1) * (sub + tail);
        bytes = (bytes + 0x7ff) & ~0x7ffu;
        Args(c, R32(slot + Slot::kLba) + ((file_off - in_sector) >> 11), bytes >> 11, R32(ctx + CtxL::kReadBuf), G::kReadMode);
        const uint32_t issued = Import(c, "cdvdman", 6);
        Args(c, 0);
        Import(c, "cdvdman", 11);
        if (issued == 0) {
            c.gpr[v0] = 0;
            SignalIfValid(c, f.get(0x10));
            c.gpr[v0] = 0;
            return;
        }
        uint32_t src = R32(ctx + CtxL::kReadBuf) + in_sector;
        if (static_cast<int32_t>(take) > 0) {
            uint32_t left = take;   // s3
            uint32_t piece = left;  // s0
            do {
                if (!(piece < 0x1001)) piece = Slot::kInterleave;
                const uint32_t p = R32(slot + Slot::kStreamPos);
                const uint32_t p_sub = p & 0xfff;
                if (p_sub != 0) {
                    const uint32_t to_boundary = (Slot::kInterleave - p) & 0xfff;
                    if (!(piece < to_boundary)) piece = to_boundary;
                }
                uint32_t rewind;
                uint32_t voices;
                if (((Slot::kInterleave - (p + piece)) & 0xfff) != 0) {
                    voices = R32(slot + Slot::kVoiceCount);
                    rewind = voices << 12;
                } else {
                    rewind = p_sub;
                    voices = R32(slot + Slot::kVoiceCount);
                }
                f.put(0x18, rewind);
                uint32_t dst = R32(slot + Slot::kWriteCursor);   // s2
                const uint32_t ring_off = dst - R32(slot + Slot::kRing);
                left -= piece;
                f.put(0x1c, left);
                const int32_t want_after = want - static_cast<int32_t>(piece);
                if (Slot::kRingBytes < ring_off + piece) {
                    if (static_cast<int32_t>(voices) > 0) {
                        const uint32_t head = Slot::kRingBytes - ring_off;
                        uint32_t base = dst - ring_off;
                        for (uint32_t j = 0; j < R32(slot + Slot::kVoiceCount); ++j) {
                            Args(c, dst, src, head);
                            Import(c, "sysclib", 40);
                            Args(c, base, src + head, piece - head);
                            Import(c, "sysclib", 40);
                            src += Slot::kInterleave;
                            base += Slot::kRingBytes;
                            dst += Slot::kRingBytes;
                        }
                    }
                } else if (static_cast<int32_t>(voices) > 0) {
                    for (uint32_t j = 0; j < R32(slot + Slot::kVoiceCount); ++j) {
                        Args(c, dst, src, piece);
                        Import(c, "sysclib", 40);
                        src += Slot::kInterleave;
                        dst += Slot::kRingBytes;
                    }
                }
                src -= f.get(0x18);
                left = f.get(0x1c);
                const uint32_t new_pos = R32(slot + Slot::kStreamPos) + piece;
                const uint32_t loop = R32(slot + Slot::kLoop);
                want = want_after;
                W32(slot + Slot::kStreamPos, new_pos);
                W32(slot + Slot::kWriteCursor, R32(slot + Slot::kWriteCursor) + piece);
                if (loop != 0 && !(new_pos < R32(slot + Slot::kDataEnd))) W32(slot + Slot::kStreamPos, R32(slot + Slot::kLoopStart));
                const uint32_t ring_end = R32(slot + Slot::kRing) + Slot::kRingBytes;
                const uint32_t cur = R32(slot + Slot::kWriteCursor);
                if (!(cur < ring_end)) W32(slot + Slot::kWriteCursor, cur - Slot::kRingBytes);
                piece = left;
            } while (static_cast<int32_t>(left) > 0);
        }
    }
    c.gpr[v0] = 1;
    SignalIfValid(c, f.get(0x10));
    c.gpr[v0] = 1;
}

// ---------------------------------------------------------------------------------------------------------
// The allocator wrappers (a C++ runtime's operator new / delete over sysmem).

void fn_5cb30(Ctx& c) {   // new(size): AllocSysMemory(0, size, 0) until it succeeds; the failure handler in between
    Frame f(c, 0x20);
    f.save(0x14, s1);
    uint32_t handler = R32(G::kAllocFailHandler);
    f.save(0x10, s0);
    uint32_t size = c.gpr[a0];
    f.save(0x18, ra);
    if (handler == 0) handler = 0x5cc08;
    if (size == 0) size = 1;
    for (;;) {
        Args(c, size);
        const uint32_t p = Call(c, 0x5db24, 0x5cb78);
        if (p != 0) {
            c.gpr[v0] = p;
            return;
        }
        Call(c, handler, 0x5cb70);
    }
}

void fn_5cba0(Ctx& c) {   // delete(p)
    Frame f(c, 0x18);
    f.save(0x10, ra);
    Call(c, 0x5cc20, 0x5cbb0);
}

void fn_5cbc0(Ctx& c) {   // new(size)
    Frame f(c, 0x18);
    f.save(0x10, ra);
    Call(c, 0x5cb30, 0x5cbd0);
}

void fn_5cbe0(Ctx& c) {   // set_new_handler(fn) -> previous (leaf)
    const uint32_t prev = R32(G::kAllocFailHandler);
    uint32_t fn = c.gpr[a0];
    if (fn == 0) fn = 0x5cc08;
    c.gpr[a0] = fn;
    W32(G::kAllocFailHandler, fn);
    c.gpr[v0] = prev;
}

void fn_5cc20(Ctx& c) {   // free(p): FreeSysMemory when p != 0
    Frame f(c, 0x18);
    const uint32_t p = c.gpr[a0];
    f.save(0x10, ra);
    if (p != 0) Call(c, 0x5db04, 0x5cc34);
}

void fn_5db04(Ctx& c) {   // FreeSysMemory(p)
    Frame f(c, 0x18);
    f.save(0x10, ra);
    Import(c, "sysmem", 5);
}

void fn_5db24(Ctx& c) {   // AllocSysMemory(0, size, 0)
    Frame f(c, 0x18);
    const uint32_t size = c.gpr[a0];
    f.save(0x10, ra);
    Args(c, 0, size, 0);
    Import(c, "sysmem", 4);
}

// ---------------------------------------------------------------------------------------------------------

#define FN(hex, size, name) {0x##hex, size, &fn_##hex, name}
const NativeFn kFunctions[] = {
    FN(5a000, 0x28, "SUKR op 0x00 init"),
    FN(5a028, 0x48, "SUKR op 0x01 open"),
    FN(5a070, 0x34, "SUKR op 0x02 close"),
    FN(5a0a4, 0x34, "SUKR op 0x03 pause"),
    FN(5a0d8, 0x34, "SUKR op 0x04 resume"),
    FN(5a10c, 0x74, "SUKR op 0x05 master volume/pan"),
    FN(5a180, 0x64, "SUKR op 0x06 set voice volume/pan"),
    FN(5a1e4, 0x30, "SUKR op 0x07 start"),
    FN(5a214, 0x30, "SUKR op 0x08 in use"),
    FN(5a244, 0x30, "SUKR op 0x09 cursors"),
    FN(5a274, 0x28, "SUKR op 0x0a close all"),
    FN(5a29c, 0x24, "SUKR op 0x0b mic level"),
    FN(5a2c0, 0x24, "SUKR op 0x0c tray count"),
    FN(5a2e4, 0x28, "SUKR op 0x0d shutdown"),
    FN(5a30c, 0xb0, "module entry: register 'SUKR'"),
    FN(5a3bc, 0x44, "context (lazy)"),
    FN(5a400, 0x278, "init body"),
    FN(5a678, 0x17c, "shutdown body"),
    FN(5a7f4, 0xe8, "open body"),
    FN(5a8dc, 0x84, "close body"),
    FN(5a960, 0x5c, "pause body"),
    FN(5a9bc, 0x5c, "resume body"),
    FN(5aa18, 0x80, "master volume/pan body"),
    FN(5aa98, 0x78, "set body"),
    FN(5ab10, 0x84, "start body"),
    FN(5ab94, 0x84, "in-use body"),
    FN(5ac18, 0x88, "cursors body"),
    FN(5aca0, 0xe0, "close-all body"),
    FN(5ad80, 0x15c, "headset stream open"),
    FN(5aedc, 0xb8, "context init"),
    FN(5af94, 0xe0, "slot by handle + lock"),
    FN(5b074, 0x58, "slot handle"),
    FN(5b0cc, 0x1d4, "thread B: headset monitor"),
    FN(5b2a0, 0x198, "thread A: headset feeder"),
    FN(5b438, 0xb4, "thread C: tray poll"),
    FN(5b4ec, 0x3c, "slot init"),
    FN(5b528, 0x40, "slot reset"),
    FN(5b568, 0xf0, "slot create"),
    FN(5b658, 0x8c, "slot destroy"),
    FN(5b6e4, 0x48, "slot lock"),
    FN(5b72c, 0xc, "slot index"),
    FN(5b738, 0xd8, "slot open"),
    FN(5b810, 0x24, "slot stop"),
    FN(5b834, 0xb8, "slot pause"),
    FN(5b8ec, 0xb8, "slot resume"),
    FN(5b9a4, 0x194, "slot master volume/pan"),
    FN(5bb38, 0x134, "slot set voice"),
    FN(5bc6c, 0x28, "slot start"),
    FN(5bc94, 0x14, "slot in use"),
    FN(5bca8, 0x4c, "slot cursors"),
    FN(5bcf4, 0x58, "voice descriptor"),
    FN(5bd4c, 0x5c, "buffered bytes"),
    FN(5bda8, 0x64, "free ring bytes"),
    FN(5be0c, 0x3c, "refill threshold"),
    FN(5be48, 0x1b0, "stream thread"),
    FN(5bff8, 0xcc, "state setter"),
    FN(5c0c4, 0x34c, "state 0: header read"),
    FN(5c410, 0x150, "state 1: open voices"),
    FN(5c560, 0xc8, "state 2: voices armed"),
    FN(5c628, 0xc8, "state 3: voices playing"),
    FN(5c6f0, 0xa4, "close voices"),
    FN(5c794, 0x39c, "refill read"),
    FN(5cb30, 0x70, "operator new"),
    FN(5cba0, 0x20, "operator delete"),
    FN(5cbc0, 0x20, "new wrapper"),
    FN(5cbe0, 0x28, "set_new_handler"),
    FN(5cc20, 0x30, "free"),
    FN(5db04, 0x20, "FreeSysMemory"),
    FN(5db24, 0x28, "AllocSysMemory"),
};
#undef FN
constexpr size_t kFunctionCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

}  // namespace

const NativeFn* Functions(size_t& count) {
    count = kFunctionCount;
    return kFunctions;
}

const NativeFn* Find(uint32_t addr) {
    for (const NativeFn& f : kFunctions)
        if (f.addr == addr) return &f;
    return nullptr;
}

namespace {
void Thunk(Ctx& ctx, void* self) { static_cast<const NativeFn*>(self)->body(ctx); }
}  // namespace

void Register(NativeIopModule& spiop) {
    for (size_t i = 0; i < kFunctionCount; ++i)
        spiop.Add(kFunctions[i].addr, &Thunk, kFunctions[i].name, const_cast<NativeFn*>(&kFunctions[i]));
    RegisterLibc(spiop);             // the soft-float / runtime half (spiop_libc.cpp): ported, unexercised on the lanes
    SpiopThreads::Register(spiop);   // the headset threads in the step form (the tray thread: spiop_tray_thread.cpp)
}

}  // namespace ps2r::title::sly2::spiop
