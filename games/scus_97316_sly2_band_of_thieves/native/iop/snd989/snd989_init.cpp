// snd989_init.cpp -- the module's boot-time path: the entry's external-command registration (fn_33000), the
// 0x123457 RPC handler (fn_34a68), the module init (fn_34c14: the argument shims, the library registration, the
// banner, SIF/RPC init, the reply / result buffers, the two RPC server threads), the banner (fn_34fd8), the sound
// library init (fn_3a250: the SPU setup, the tick thread + hard timer (fn_3a48c), the CD-read semaphore and the
// loader callbacks (fn_3aa9c)). Each runs once per boot. Ported from the recompiled 989snd_Library over the
// module's RAM.
#include <cstdio>
#include <thread>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kThreadBase = G::kData + 0x61c;        // base priority of the module's threads
constexpr uint32_t kThreadBase2 = G::kData + 0x620;       // base priority of the RPC server threads
constexpr uint32_t kBannerWord = G::kData + 0x624;
constexpr uint32_t kRpcServerTid = G::kData + 0x628;
constexpr uint32_t kPositionDmaSema = G::kData + 0x62c, kReplySema = G::kData + 0x630;
constexpr uint32_t kInitEnabled = G::kData + 0x634;
constexpr uint32_t kRpcBuffer0 = G::kData + 0x638;
constexpr uint32_t kLibInitState = G::kData + 0x1900;     // -1 not initialised, 1 initialised
constexpr uint32_t kTickPeriod = G::kData + 0x1904, kTickRate = G::kData + 0x1908;
constexpr uint32_t kLoudFlag = G::kData + 0x1910, kThirdFlag = G::kData + 0x1918;
constexpr uint32_t kTickDoneSema = G::kData + 0x18f0, kTickTid = G::kData + 0x18f4, kTickTimer = G::kData + 0x18f8;
constexpr uint32_t kTickCounter = G::kData + 0x191c;
constexpr uint32_t kTimerRecord = G::kData + 0x2530;      // {&kTickTid, the 240 Hz compare}
constexpr uint32_t kCdReadSema = G::kData + 0x1928;
constexpr uint32_t kLoadState = G::kData + 0x1920;        // 0x1920..0x1924, 0x1988..0x199c: the loader words
constexpr uint32_t kRpc1Reply = G::kData + 0x2678;
constexpr uint32_t kTickThread = 0x3a8d8, kTickHandler = 0x3a90c;
constexpr uint32_t kRpcThread0 = 0x349e4, kRpcThread1 = 0x34ba0;
constexpr uint32_t kBanner = G::kData - 0x5e8;
}  // namespace

// fn_33000(rec): register an external-command record (rec[0] = the 4-byte tag): a known tag (fn_33148) is reported
// (1) -> 0; else rec[8] = rec[12] = 0 and the record appended to kExtModuleList -> 1.
void fn_33000(IopContext& c) {
    const uint32_t rec = A0(c);
    Frame f(c, 0x20);
    if (Call(c, 0x33148, Ld32(rec)) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 1, Ld32(rec), Ld32(rec), 0);
        Ret(c, 0);
        return;
    }
    St32(rec + 8, 0);
    St32(rec + 0xc, 0);
    const uint32_t head = Ld32(G::kExtModuleList);
    if (head == 0) {
        St32(G::kExtModuleList, rec);
    } else {
        uint32_t n = head;
        while (Ld32(n + 8) != 0) n = Ld32(n + 8);
        St32(n + 8, rec);
    }
    Ret(c, 1);
}

// fn_34a68(fno, args): the 0x123457 RPC handler: 2 -> fn_3b1b4(args+4, args[0], 0, 0), 3 -> fn_3c5d8(args[0], args[1],
// 0, 0) (the bank load), 4 -> fn_3c9a8(args+4, args[0]), 5 -> fn_3cc04(args[0], args[1]), 0x57 -> fn_3c664(args[0], 0, 0),
// 0x59 -> fn_3c71c(args[0], 0, 0), 0x6a -> fn_3b2c8(args[0..3]); anything else -> 0. The result goes to kRpc1Reply,
// whose address is returned.
void fn_34a68(IopContext& c) {
    const uint32_t fno = A0(c), args = A1(c);
    Frame f(c, 0x18);
    uint32_t r = 0;
    switch (fno) {
        case 2: r = Call(c, 0x3b1b4, args + 4, Ld32(args), 0, 0); break;
        case 3: r = Call(c, 0x3c5d8, Ld32(args), Ld32(args + 4), 0, 0); break;
        case 4: r = Call(c, 0x3c9a8, args + 4, Ld32(args)); break;
        case 5: r = Call(c, 0x3cc04, Ld32(args), Ld32(args + 4)); break;
        case 0x57: r = Call(c, 0x3c664, Ld32(args), 0, 0); break;
        case 0x59: r = Call(c, 0x3c71c, Ld32(args), 0, 0); break;
        case 0x6a: r = Call(c, 0x3b2c8, Ld32(args), Ld32(args + 4), Ld32(args + 8), Ld32(args + 0xc)); break;
        default: break;
    }
    St32(kRpc1Reply, r);
    Ret(c, kRpc1Reply);
}

// fn_34fd8(): the banner (stdio printf of the module's strings; the thread priorities and the banner word).
void fn_34fd8(IopContext& c) {
    Frame f(c, 0x28);
    const uint32_t s1 = kBanner, s2 = kBanner + 4;
    imp::Printf(c, s1);
    imp::Printf(c, s2);
    imp::Printf(c, G::kData - 0x5a8);
    St32(f.At(0x10), 0x61);
    imp::Printf(c, G::kData - 0x56c, 3, 1, 3);
    imp::Printf(c, G::kData - 0x52c, G::kData - 0x500, G::kData - 0x4f4, 3);
    imp::Printf(c, s1, G::kData - 0x500, G::kData - 0x4f4, 3);
    imp::Printf(c, G::kData - 0x4e8, G::kData - 0x500, G::kData - 0x4f4, 3);
    imp::Printf(c, G::kData - 0x4b0, G::kData - 0x500, G::kData - 0x4f4, 3);
    imp::Printf(c, s1, G::kData - 0x500, G::kData - 0x4f4, 3);
    imp::Printf(c, G::kData - 0x484, 3, 0, 1);
    imp::Printf(c, s2, 3, 0, 1);
    imp::Printf(c, G::kData - 0x45c, 3, 0, 1);
    imp::Printf(c, G::kData - 0x444, Ld32(kThreadBase), 0, 1);
    imp::Printf(c, G::kData - 0x418, Ld32(kThreadBase2), 0, 1);
    imp::Printf(c, G::kData - 0x3ec, Ld32(kThreadBase2) + 1, 0, 1);
    const uint32_t w = Ld32(kBannerWord);
    St32(f.At(0x10), w + 3);
    imp::Printf(c, G::kData - 0x3c0, w, w + 1, w + 2);
    imp::Printf(c, s2, w, w + 1, w + 2);
    imp::Printf(c, s1, w, w + 1, w + 2);
}

// fn_34c14(argc, argv): the module init: each argument through fn_34e34, the library registered (fn_39f0c), the
// banner, SIF initialised when idle, the RPC layer; when enabled (kInitEnabled): under suspended interrupts the 4 KiB
// RPC buffer (filled 0xab; a failure -> 5, 1) and the 0x420-byte reply buffer (failure -> 6, 1) allocated; the
// position-DMA and reply semaphores created (the reply one signalled); the two RPC server threads created and started
// (a failure -> 1). Returns 0.
void fn_34c14(IopContext& c) {
    const uint32_t argc = A0(c), argv = A1(c);
    Frame f(c, 0x50);
    if (1 < S32(argc)) {
        uint32_t p = argv + 4;
        for (uint32_t n = argc - 1; n != 0; --n, p += 4) Call(c, 0x34e34, Ld32(p));
    }
    Call(c, 0x39f0c);
    Call(c, 0x34fd8);
    if (Import(c, "sifman", 29) == 0) Import(c, "sifman", 5);
    Import(c, "sifcmd", 14, 0);
    if (Ld32(kInitEnabled) == 0) {
        Ret(c, 0);
        return;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x3c));
    const uint32_t buf = Import(c, "sysmem", 4, 0, 0x1000, 0);
    St32(kRpcBuffer0, buf);
    if (buf == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 5, 0, 0, 0);
        Ret(c, 1);
        return;
    }
    for (uint32_t i = 0; i < 0x1000; ++i) St8(buf + i, 0xab);
    const uint32_t reply = Import(c, "sysmem", 4, 0, 0x420, 0);
    St32(G::kReplyBufPtr, reply);
    if (reply == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 6, 0, 0, 0);
        Ret(c, 1);
        return;
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x3c)));
    St32(f.At(0x24), 1);
    St32(f.At(0x18), 0);
    St32(f.At(0x20), 0);
    St32(G::kResultPtr, reply + 4);
    St32(kPositionDmaSema, Import(c, "thsemap", 4, f.At(0x18)));
    const uint32_t rs = Import(c, "thsemap", 4, f.At(0x18));
    St32(kReplySema, rs);
    imp::SignalSema(c, rs);
    St32(f.At(0x28), 0x2000000);
    St32(f.At(0x2c), 0);
    St32(f.At(0x30), kRpcThread0);
    St32(f.At(0x34), 0x1000);
    St32(f.At(0x38), Ld32(kThreadBase2));
    const uint32_t tid = Import(c, "thbase", 4, f.At(0x28));
    St32(kRpcServerTid, tid);
    if (S32(tid) <= 0) {
        Ret(c, 1);
        return;
    }
    Import(c, "thbase", 6, tid, 0);
    St32(f.At(0x28), 0x2000000);
    St32(f.At(0x2c), 0);
    St32(f.At(0x30), kRpcThread1);
    St32(f.At(0x34), 0x800);
    St32(f.At(0x38), Ld32(kThreadBase2) + 1);
    const uint32_t tid2 = Import(c, "thbase", 4, f.At(0x28));
    if (S32(tid2) <= 0) {
        Ret(c, 1);
        return;
    }
    Import(c, "thbase", 6, tid2, 0);
    Ret(c, 0);
}

// fn_3aa9c(): the CD-read semaphore (a failure is reported (0x73) and the module spins forever, as the original), the
// loader words cleared, the loader callbacks (fn_3deec / fn_3e0c0) set, the 0x38-byte load record zeroed.
void fn_3aa9c(IopContext& c) {
    Frame f(c, 0x30);
    St32(f.At(0x18), 0);
    St32(f.At(0x20), 0);
    St32(f.At(0x24), 1);
    St32(f.At(0x1c), 0);
    const uint32_t s = Import(c, "thsemap", 4, f.At(0x18));
    St32(kCdReadSema, s);
    if (S32(s) < 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x73, 0, 0, 0);
        std::printf("[snd989] fn_3aa9c: CreateSema failed -- the original spins at 0x3aae4\n");
        for (;;) std::this_thread::yield();
    }
    St32(kLoadState, 0);
    St32(kLoadState + 4, 0);
    St32(G::kData + 0x1994, 0);
    St32(G::kData + 0x1998, 0);
    St32(G::kData + 0x199c, 0);
    St32(G::kData + 0x1988, 0x3deec);
    St32(G::kData + 0x198c, 0x3e0c0);
    Import(c, "sysclib", 14, G::kData + 0x2718, 0, 0x38);
}

// fn_3a48c(): the tick machinery: the tick-done semaphore (0/1), the global lock semaphore (1/1, signalled), the
// tick thread created (fn_3a8d8, 4 KiB, kThreadBase), the 240 Hz compare from USec2SysClock(1 s) / 240, a hard timer
// (AllocHardTimer(1, 32, 1)) with fn_3a90c over kTimerRecord, set up and started; the tick thread started. Returns 0.
void fn_3a48c(IopContext& c) {
    Frame f(c, 0x50);
    St32(f.At(0x24), 1);
    St32(f.At(0x2c), 0);
    St32(f.At(0x30), 1);
    St32(kTickDoneSema, Import(c, "thsemap", 4, f.At(0x24)));
    St32(f.At(0x24), 1);
    St32(f.At(0x2c), 1);
    St32(f.At(0x30), 1);
    const uint32_t lock = Import(c, "thsemap", 4, f.At(0x24));
    St32(G::kLockSema, lock);
    imp::SignalSema(c, lock);
    St32(f.At(0x10), 0x2000000);
    St32(f.At(0x18), kTickThread);
    St32(f.At(0x1c), 0x1000);
    St32(f.At(0x14), 0);
    St32(f.At(0x20), Ld32(kThreadBase));
    St32(kTickTid, Import(c, "thbase", 4, f.At(0x10)));
    St32(kTickCounter, 0);
    Import(c, "thbase", 39, 0xf4240, f.At(0x34));
    St32(kTimerRecord, kTickTid);
    St32(kTimerRecord + 4, MulHiU(Ld32(f.At(0x34)), 0x88888889u) >> 7);
    St32(kTickTimer, Import(c, "timrman", 4, 1, 0x20, 1));
    Import(c, "timrman", 20, Ld32(kTickTimer), Ld32(kTimerRecord + 4), kTickHandler, kTimerRecord);
    Import(c, "timrman", 22, Ld32(kTickTimer), 1, 0, 1);
    Import(c, "timrman", 23, Ld32(kTickTimer));
    const uint32_t tid = Ld32(kTickTid);
    if (S32(tid) > 0) Import(c, "thbase", 6, tid, 0);
    Ret(c, 0);
}

// fn_3a250(flags): the sound library init, once (kLibInitState != -1 -> -1): the loud / quiet / third flags from
// bits 0..1, the periodic timer pool, the CD-read semaphore + loader (fn_3aa9c), the voice table (fn_4bf1c), the
// handle pools (fn_410c0), the group volumes (fn_4e0b0); unless bit 2: SdInit and the core parameters (master
// volumes, effect off, the core attributes with the bit-3 / bit-4 options); the tick rate (fn_3a8d0) -> kTickRate and
// kTickPeriod = 1e8 / (999 * rate / 10); the tick machinery (fn_3a48c: a failure undoes the init state); the master
// volume (fn_4e23c(0)) and the group volumes 0..14 and 16 at 0x400; the lock depth cleared. Returns 0.
void fn_3a250(IopContext& c) {
    const uint32_t flags = A0(c);
    Frame f(c, 0x20);
    if (Ld32(kLibInitState) != 0xffffffffu) {
        Ret(c, 0xffffffffu);
        return;
    }
    St32(kLoudFlag, (flags & 1) ? 0 : 1);
    St32(G::kQuiet, (flags & 2) ? 1 : 0);
    St32(kThirdFlag, 1);
    Call(c, 0x426dc);
    Call(c, 0x3aa9c);
    Call(c, 0x4bf1c);
    Call(c, 0x410c0);
    Call(c, 0x4e0b0);
    if (!(flags & 4)) {
        Import(c, "libsd", 4, 0);
        imp::SdSetParam(c, 0x800, 0xf00);
        imp::SdSetParam(c, 0x801, 0xf0c);
        imp::SdSetParam(c, 0x980, 0x3fff);
        imp::SdSetParam(c, 0xa80, 0x3fff);
        imp::SdSetParam(c, 0xb80, 0);
        imp::SdSetParam(c, 0xc80, 0);
        imp::SdSetParam(c, 0xb81, 0);
        imp::SdSetParam(c, 0xc81, 0);
        imp::SdSetParam(c, 0xd81, 0x7fff);
        imp::SdSetParam(c, 0xe81, 0x7fff);
        uint32_t attr = (flags << 8) & 0x800;
        if (!(flags & 0x10)) attr = (attr | 0x80) & 0xffff;
        imp::SdSetCoreAttr(c, 0xa, attr);
        imp::SdSetCoreAttr(c, 2, 0);
        imp::SdSetCoreAttr(c, 3, 0);
    }
    St32(kLibInitState, 1);
    const int32_t rate = S32(Call(c, 0x3a8d0));
    const int32_t scaled = DivMagic(S32(U32(rate) * 999u), 0x66666667u, 2);
    St32(kTickRate, U32(rate));
    if (scaled == 0) {
        std::printf("[IOP-RECOMP] BREAK at 0x3a414\n");
        return;
    }
    St32(kTickPeriod, U32(MipsDiv(0x05f5e100, scaled)));
    const uint32_t r = Call(c, 0x3a48c);
    if (r != 0) {
        St32(kLibInitState, 0xffffffffu);
        Ret(c, r);
        return;
    }
    Call(c, 0x4e23c, 0);
    for (uint32_t i = 0; i < 15; ++i) Call(c, 0x4e100, i, 0x400);
    Call(c, 0x4e100, 0x10, 0x400);
    St32(G::kLockDepth, 0);
    Ret(c, 0);
}

}  // namespace ps2r::title::sly2::snd989
