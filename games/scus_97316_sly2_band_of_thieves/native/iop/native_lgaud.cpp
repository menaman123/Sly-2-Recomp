// native_lgaud.cpp -- the lgAud bodies (native_lgaud.h). Each body is the recompiled function at its address
// (the generated TU `iop/lgaud/iop_recompiled.cpp`, the ground truth) rewritten over the module's RAM words:
// the same library calls in the same order through IopServices, the same words written, v0 as the recompiled
// body leaves it. The guest stack frame is pushed with the recompiled prologue's size so every callee (an
// import or a guest function) sees the $sp the recompiled body would have shown it; the module's structs that
// a kernel service READS from the stack (CreateSema / CreateThread / ReferModuleStatus / printf's 5th argument,
// a format string the module copies to its frame) are written at the recompiled offsets.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_lgaud.h"

#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"

namespace ps2r::title::sly2 {

namespace {

NativeLgaud g_lgaud;

inline uint32_t A0(const IopContext& c) { return c.gpr[4]; }
inline uint32_t A1(const IopContext& c) { return c.gpr[5]; }
inline uint32_t A2(const IopContext& c) { return c.gpr[6]; }
inline uint32_t A3(const IopContext& c) { return c.gpr[7]; }
inline void Ret(IopContext& c, uint32_t v) { c.gpr[2] = v; }
inline void Ret(IopContext& c, int32_t v) { c.gpr[2] = static_cast<uint32_t>(v); }
inline int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
inline uint32_t U(int32_t v) { return static_cast<uint32_t>(v); }
inline uint32_t SignedByte(uint32_t addr) { return U(static_cast<int32_t>(static_cast<int8_t>(IopRam::R8(addr)))); }
inline uint32_t Bswap(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xff00u) | ((v & 0xff00u) << 8) | (v << 24);
}
// The module copies a .rodata string onto its stack with lwl/lwr/lb pairs: the same bytes, the same place.
void CopyBytes(uint32_t dst, uint32_t src, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) IopRam::W8(dst + i, IopRam::R8(src + i));
}
// A guest function of this module through its seam wrapper (native or recompiled), a0..a3 set.
uint32_t Guest(IopContext& ctx, uint32_t addr, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    ctx.gpr[6] = a2;
    ctx.gpr[7] = a3;
    IopServices::CallGuest(ctx, addr);
    return ctx.gpr[2];
}

}  // namespace

void NativeLgaud::Register(NativeIopModule& lgaud) {
    using M = NativeIopModule;
    lgaud.Add(kEntry, &M::Thunk<NativeLgaud, &NativeLgaud::Entry>, "entry", &g_lgaud);
    lgaud.Add(kUnload, &M::Thunk<NativeLgaud, &NativeLgaud::Unload>, "unload", &g_lgaud);
    lgaud.Add(kParseArg, &M::Thunk<NativeLgaud, &NativeLgaud::ParseArg>, "parse_arg", &g_lgaud);
    lgaud.Add(kStrncasecmp, &M::Thunk<NativeLgaud, &NativeLgaud::Strncasecmp>, "strncasecmp", &g_lgaud);
    lgaud.Add(kModuleStatus, &M::Thunk<NativeLgaud, &NativeLgaud::ModuleStatus>, "module_status", &g_lgaud);
    lgaud.Add(kHashReset, &M::Thunk<NativeLgaud, &NativeLgaud::HashReset>, "hash_reset", &g_lgaud);
    lgaud.Add(kHashStep, &M::Thunk<NativeLgaud, &NativeLgaud::HashStep>, "hash_step", &g_lgaud);
    lgaud.Add(kHashGet, &M::Thunk<NativeLgaud, &NativeLgaud::HashGet>, "hash_get", &g_lgaud);
    lgaud.Add(kAlloc, &M::Thunk<NativeLgaud, &NativeLgaud::Alloc>, "alloc", &g_lgaud);
    lgaud.Add(kFree, &M::Thunk<NativeLgaud, &NativeLgaud::Free>, "free", &g_lgaud);
    lgaud.Add(kMax, &M::Thunk<NativeLgaud, &NativeLgaud::Max>, "max", &g_lgaud);
    lgaud.Add(kMin, &M::Thunk<NativeLgaud, &NativeLgaud::Min>, "min", &g_lgaud);
    lgaud.Add(kMulDivRound, &M::Thunk<NativeLgaud, &NativeLgaud::MulDivRound>, "muldiv_round", &g_lgaud);
    lgaud.Add(kAbs, &M::Thunk<NativeLgaud, &NativeLgaud::Abs>, "abs", &g_lgaud);
    lgaud.Add(kGetDeviceState, &M::Thunk<NativeLgaud, &NativeLgaud::GetDeviceState>, "get_device_state", &g_lgaud);
}

void RegisterLgaudNatives(NativeIopModule& lgaud) { NativeLgaud::Register(lgaud); }

// _start(argc, argv) at 0x299f4 (frame 0xf0). argc < 0 = the unload request.
void NativeLgaud::Entry(IopContext& ctx) {
    const int32_t argc = S(A0(ctx));
    const uint32_t argv = A1(ctx);
    IopStackFrame f(ctx, 0xf0);
    const uint32_t sp = f.Sp();
    if (argc < 0) {
        Guest(ctx, kUnload, A0(ctx));
        Ret(ctx, 1);
        return;
    }
    // The three option keys on the stack (lwl/lwr/lb copies of the .rodata strings).
    CopyBytes(sp + 0x18, kKeyThpri, 7);
    CopyBytes(sp + 0x20, kKeyRpc, 5);
    CopyBytes(sp + 0x28, kKeyMaxstream, 11);
    uint32_t no_rpc = 0;             // s5: rpc=0
    uint32_t max_stream = 0x10000;   // s3
    if (argc > 1) {
        uint32_t arg = argv + 4;
        for (int32_t left = argc - 1; left != 0; --left, arg += 4) {
            f.Word(0x10, 0x7b);
            uint32_t v = Guest(ctx, kParseArg, IopRam::R32(arg), sp + 0x18, IopRam::R32(kThreadPrio), 9);
            IopRam::W32(kThreadPrio, v);
            f.Word(0x10, 1);
            v = Guest(ctx, kParseArg, IopRam::R32(arg), sp + 0x20, no_rpc < 1 ? 1u : 0u, 0);
            no_rpc = v < 1 ? 1u : 0u;
            f.Word(0x10, 0x100000);
            v = Guest(ctx, kParseArg, IopRam::R32(arg), sp + 0x28, max_stream, 0);
            max_stream = v;
        }
    }
    // The module's own image: hash its text up to the export-table magic (the anti-tamper banner character).
    const uint32_t info = sp + 0x38;
    if (Guest(ctx, kModuleStatus, IopRam::R32(kModName), info) != 0) {
        int32_t text_size = S(IopRam::R32(info + 0x4c));
        uint32_t text = IopRam::R32(info + 0x48);
        Guest(ctx, kHashReset, A0(ctx));
        if (text_size > 0) {
            for (;;) {
                const uint32_t r = Guest(ctx, kHashStep, IopRam::R32(text), 0);
                text_size -= 4;
                text += 4;
                if (r == 0) break;
                if (text_size <= 0) break;
            }
        }
        const uint32_t h = Guest(ctx, kHashGet, A0(ctx));
        IopRam::W32(kHashExpect, IopRam::R32(kHashExpect) ^ h);
    }
    // The USB_driver module's version tag ("PsII" in its data) must read >= 2.4.3.0.
    if (Guest(ctx, kModuleStatus, kStrUsbDriver, info) != 0) {
        const uint32_t text = IopRam::R32(info + 0x48);
        const uint32_t text_size = IopRam::R32(info + 0x4c);
        int32_t left = S(IopRam::R32(info + 0x50));
        if (left > 0) {
            uint32_t p = text + text_size;
            for (;;) {
                const uint32_t w = IopRam::R32(p);
                left -= 4;
                if (w == kPsIITag) {
                    const uint32_t version = Bswap(IopRam::R32(p + 0xc));
                    if (!(kUsbdMinVersion < version)) {
                        const uint32_t t = p + 4;
                        f.Word(0x10, SignedByte(t + 0xb));
                        IopServices::Import(ctx, "stdio", 4, kFmtUsbdVersion, SignedByte(t + 8), SignedByte(t + 9), SignedByte(t + 10));
                        Ret(ctx, 1);
                        return;
                    }
                }
                if (left <= 0) break;
                p += 4;
            }
        }
    }
    if (no_rpc == 0) {
        IopRam::W32(kMaxStream, max_stream);
        const uint32_t size = Guest(ctx, kMax, max_stream + 0x30, 0x180);
        IopRam::W32(kRpcBufSize, size);
        const uint32_t buf = Guest(ctx, kAlloc, size);
        IopRam::W32(kRpcBuf, buf);
        if (buf == 0) {
            IopServices::Import(ctx, "stdio", 4, kFmtAllocFail, IopRam::R32(kModName));
            Ret(ctx, 1);
            return;
        }
    } else {
        IopRam::W32(kRpcBufSize, 0);
        IopRam::W32(kMaxStream, 0);
        IopRam::W32(kRpcBuf, 0);
    }
    const uint32_t mark = IopRam::R32(kHashExpect) == IopRam::R32(kHashSeed) ? 0x20u : 0x2eu;
    f.Word(0x10, IopRam::R32(kThreadPrio));
    IopServices::Import(ctx, "stdio", 4, kFmtBanner, IopRam::R32(kModName), IopRam::R32(kVersion), mark);
    if (no_rpc == 0)
        IopServices::Import(ctx, "stdio", 4, kFmtRpc, IopRam::R32(kMaxStream));
    else
        IopServices::Import(ctx, "stdio", 4, kFmtNoRpc);
    IopServices::CpuSuspendIntr(ctx, sp + 0xc0);
    const int32_t reg = IopServices::RegisterLibraryEntries(ctx, kExportTable);
    IopServices::CpuResumeIntr(ctx, f.Word(0xc0));
    if (reg != 0) {
        Guest(ctx, kFree, IopRam::R32(kRpcBuf));
        Ret(ctx, 1);
        return;
    }
    if (IopServices::Import(ctx, "usbd", 4, kLdd) != 0) {   // sceUsbdRegisterLdd
        Guest(ctx, kFree, IopRam::R32(kRpcBuf));
        IopServices::Import(ctx, "loadcore", 7, kExportTable);   // ReleaseLibraryEntries
        Ret(ctx, 1);
        return;
    }
    for (uint32_t i = 0; i < kSlotCount; ++i) IopRam::W32(kSlots + 4 * i, 0);
    f.Word(0xa4, 1);   // max
    f.Word(0x98, 1);   // attr
    f.Word(0xa0, 1);   // init
    f.Word(0x9c, 0);   // option
    IopRam::W32(kSema, U(IopServices::CreateSema(ctx, sp + 0x98)));
    IopRam::W32(kDeviceState, 0);
    if (no_rpc != 0) {
        Ret(ctx, 2);
        return;
    }
    f.Word(0xa8, 0x2000000);                  // attr
    f.Word(0xb0, kRpcThread);                 // entry
    f.Word(0xb8, IopRam::R32(kThreadPrio));   // priority
    f.Word(0xb4, 0x800);                      // stack size
    f.Word(0xac, 0);                          // option
    const int32_t tid = IopServices::CreateThread(ctx, sp + 0xa8);
    IopRam::W32(kRpcTid, U(tid));
    if (tid > 0) {
        IopServices::StartThread(ctx, tid, 0);
        Ret(ctx, 2);
        return;
    }
    Guest(ctx, kFree, IopRam::R32(kRpcBuf));
    IopServices::Import(ctx, "loadcore", 7, kExportTable);   // ReleaseLibraryEntries
    IopServices::Import(ctx, "usbd", 5, kLdd);               // sceUsbdUnregisterLdd
    Ret(ctx, 1);
}

// fn_29938 (frame 0x18): the unload arm -- export 21 (0x2aeb4), the LDD, the RPC thread/server, the library, the sema.
void NativeLgaud::Unload(IopContext& ctx) {
    IopStackFrame f(ctx, 0x18);
    IopServices::CallGuest(ctx, 0x2aeb4);
    IopServices::Import(ctx, "usbd", 5, kLdd);   // sceUsbdUnregisterLdd
    if (IopRam::R32(kRpcBuf) != 0) {
        IopServices::TerminateThread(ctx, S(IopRam::R32(kRpcTid)));
        IopServices::DeleteThread(ctx, S(IopRam::R32(kRpcTid)));
        IopRam::W32(kRpcTid, 0);
        IopServices::Import(ctx, "sifcmd", 24, kRpcServer, kRpcQueue);   // sceSifRemoveRpc
        IopServices::Import(ctx, "sifcmd", 25, kRpcQueue);               // sceSifRemoveRpcQueue
        Guest(ctx, kFree, IopRam::R32(kRpcBuf));
        IopRam::W32(kRpcBuf, 0);
    }
    IopServices::Import(ctx, "loadcore", 7, kExportTable);   // ReleaseLibraryEntries
    IopServices::DeleteSema(ctx, S(IopRam::R32(kSema)));
    Ret(ctx, 1);
}

// fn_297e4(arg, key, default, min, [sp+0x10] max) (frame 0x48): "key=<decimal>" clamped to [min, max] with the
// "%s: adjusting %s to %d\n" report (the format copied to the frame first, as the module does); `default` when
// the argument does not start with the key (case-insensitive).
void NativeLgaud::ParseArg(IopContext& ctx) {
    const uint32_t arg = A0(ctx), key = A1(ctx), dflt = A2(ctx);
    const int32_t min = S(A3(ctx));
    const int32_t max = S(IopRam::R32(ctx.gpr[29] + 0x10));
    IopStackFrame f(ctx, 0x48);
    const uint32_t key_len = IopServices::Import(ctx, "sysclib", 27, key);   // strlen
    const uint32_t cmp = Guest(ctx, kStrncasecmp, arg, key, key_len);
    const uint32_t value_str = arg + key_len;
    if (cmp != 0) {
        Ret(ctx, dflt);
        return;
    }
    CopyBytes(f.Sp() + 0x10, kFmtAdjust, 24);
    int32_t value = S(IopServices::Import(ctx, "sysclib", 36, value_str, 0, 10));   // strtol
    if (max < value) {
        value = max;
        IopServices::Import(ctx, "stdio", 4, f.Sp() + 0x10, IopRam::R32(kModName), key, U(value));
        Ret(ctx, value);
        return;
    }
    if (value < min) {
        value = min;
        IopServices::Import(ctx, "stdio", 4, f.Sp() + 0x10, IopRam::R32(kModName), key, U(value));
        Ret(ctx, value);
        return;
    }
    Ret(ctx, value);
}

// fn_296ac(a, b, n) (frame 0x28): strncasecmp over sysclib's tolower; -1 / 0 / 1.
void NativeLgaud::Strncasecmp(IopContext& ctx) {
    uint32_t a = A0(ctx), b = A1(ctx);
    int32_t n = S(A2(ctx));
    IopStackFrame f(ctx, 0x28);
    if (b == 0) {
        Ret(ctx, 1);
        return;
    }
    if (a == 0) {
        Ret(ctx, -1);
        return;
    }
    for (;;) {
        if (n == 0) {
            Ret(ctx, 0);
            return;
        }
        const uint32_t ca = SignedByte(a);
        if (ca == 0) break;
        if (SignedByte(b) == 0) break;
        const int32_t la = S(IopServices::Import(ctx, "sysclib", 7, ca));               // tolower
        const int32_t lb = S(IopServices::Import(ctx, "sysclib", 7, SignedByte(b)));   // tolower
        --n;
        if (lb < la) {
            Ret(ctx, 1);
            return;
        }
        if (la < lb) {
            Ret(ctx, -1);
            return;
        }
        ++a;
        ++b;
    }
    if (n == 0) {
        Ret(ctx, 0);
        return;
    }
    const uint32_t ca = SignedByte(a), cb = SignedByte(b);
    if (ca == cb) {
        Ret(ctx, 0);
        return;
    }
    Ret(ctx, ca == 0 ? -1 : 1);
}

// fn_29794(name, info) (frame 0x20): GetModuleIdListByName(name, &id, 1, 0) then ReferModuleStatus(id, info); 1 = found.
void NativeLgaud::ModuleStatus(IopContext& ctx) {
    const uint32_t name = A0(ctx), info = A1(ctx);
    IopStackFrame f(ctx, 0x20);
    const int32_t n = S(IopServices::Import(ctx, "modload", 18, name, f.Sp() + 0x10, 1, 0));
    if (n <= 0) {
        Ret(ctx, 0);
        return;
    }
    const uint32_t r = IopServices::Import(ctx, "modload", 17, f.Word(0x10), info);
    Ret(ctx, r < 1 ? 1u : 0u);
}

void NativeLgaud::HashReset(IopContext& /*ctx*/) { IopRam::W32(kTextHash, 0); }

// fn_302b0(word, mode): rotate the running hash left by one; XOR in `word` unless (mode 0) it is a relocatable
// instruction class -- j/jal (op 2,3), addiu with rs != 0 (op 9), lui (op 15), any op >= 0x20 (loads/stores/
// coprocessor) -- or a syscall (0xc), which XOR in nothing; returns 2 for those classes, 1 otherwise; and (mode 0)
// 0 at the export-table magic 0x41c00000 without touching the hash.
void NativeLgaud::HashStep(IopContext& ctx) {
    uint32_t word = A0(ctx);
    const uint32_t mode = A1(ctx);
    const uint32_t op = word >> 26;
    uint32_t ret = 1;
    if (mode == 0 && word == kExportMagic) {
        Ret(ctx, 0);
        return;
    }
    uint32_t h = IopRam::R32(kTextHash);
    h = (h << 1) | (h >> 31);
    IopRam::W32(kTextHash, h);
    if (mode == 0) {
        bool skip = false;
        if (op == 9) {
            skip = ((word >> 21) & 0x1f) != 0;
        } else if (op >= 10) {
            skip = op == 15 || op >= 0x20;
        } else {
            skip = op == 2 || op == 3;
        }
        if (!skip && word == 0xc) skip = true;
        if (skip) {
            word = 0;
            ret = 2;
        }
    }
    IopRam::W32(kTextHash, IopRam::R32(kTextHash) ^ word);
    Ret(ctx, ret);
}

void NativeLgaud::HashGet(IopContext& ctx) { Ret(ctx, IopRam::R32(kTextHash)); }

// fn_2c1a0(size) (frame 0x20): AllocSysMemory(0, size, 0) between CpuSuspendIntr / CpuResumeIntr.
void NativeLgaud::Alloc(IopContext& ctx) {
    const uint32_t size = A0(ctx);
    IopStackFrame f(ctx, 0x20);
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    const uint32_t p = IopServices::AllocSysMemory(ctx, 0, size, 0);
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
    Ret(ctx, p);
}

// fn_2c1e8(ptr) (frame 0x20): FreeSysMemory(ptr) under the same bracket; a null pointer is a no-op (v0 untouched).
void NativeLgaud::Free(IopContext& ctx) {
    const uint32_t p = A0(ctx);
    IopStackFrame f(ctx, 0x20);
    if (p == 0) return;
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    IopServices::FreeSysMemory(ctx, p);
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
}

// fn_2c288(a, b) -> max (signed); fn_2c270(a, b) -> min (signed).
void NativeLgaud::Max(IopContext& ctx) { Ret(ctx, S(A1(ctx)) < S(A0(ctx)) ? A0(ctx) : A1(ctx)); }
void NativeLgaud::Min(IopContext& ctx) { Ret(ctx, S(A0(ctx)) < S(A1(ctx)) ? A0(ctx) : A1(ctx)); }

// fn_2c22c(a, b, c): (a*b + (a < 0 ? -c/2 : c/2)) / c in 32-bit signed arithmetic (lo/hi as the mult/div leave them);
// c == 0 is the module's `break 7` (the recompiled body prints and returns with v0 = the unrounded sum).
void NativeLgaud::MulDivRound(IopContext& ctx) {
    const int32_t a = S(A0(ctx)), b = S(A1(ctx)), c = S(A2(ctx));
    const int64_t product = static_cast<int64_t>(a) * static_cast<int64_t>(b);
    ctx.lo = static_cast<uint32_t>(product);
    ctx.hi = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32);
    int32_t half = S(U(c) + (U(c) >> 31)) >> 1;
    if (a < 0) half = -half;
    const int32_t sum = S(static_cast<uint32_t>(ctx.lo) + U(half));
    if (c == 0) {
        std::printf("[IOP-RECOMP] BREAK at 0x2c258\n");
        Ret(ctx, sum);
        return;
    }
    ctx.lo = U(sum / c);
    ctx.hi = U(sum % c);
    Ret(ctx, ctx.lo);
}

void NativeLgaud::Abs(IopContext& ctx) {
    const int32_t a = S(A0(ctx));
    Ret(ctx, a < 0 ? U(-a) : U(a));
}

// export 18 = lgaud:18(int* state) at 0x2a3b8 (frame 0x18): under the sema, *state = the device state; a 1 ("just
// connected") is consumed to 0. 0x80000004 for a null pointer (no sema taken).
void NativeLgaud::GetDeviceState(IopContext& ctx) {
    const uint32_t out = A0(ctx);
    IopStackFrame f(ctx, 0x18);
    if (out == 0) {
        Ret(ctx, 0x80000004u);
        return;
    }
    IopServices::WaitSema(ctx, S(IopRam::R32(kSema)));
    IopRam::W32(out, IopRam::R32(kDeviceState));
    if (IopRam::R32(kDeviceState) == 1) IopRam::W32(kDeviceState, 0);
    IopServices::SignalSema(ctx, S(IopRam::R32(kSema)));
    Ret(ctx, 0);
}

}  // namespace ps2r::title::sly2
