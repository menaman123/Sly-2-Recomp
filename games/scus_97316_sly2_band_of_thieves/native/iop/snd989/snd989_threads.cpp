// snd989_threads.cpp -- the module's thread bodies in the coroutine form (each loops forever; its blocking imports park
// the hosted coroutine where the recompiled body did): the two RPC servers (fn_349e4 'SND ' 0x123456 -> fn_3497c,
// fn_34ba0 0x123457 -> fn_34a68), the 240 Hz tick thread (fn_3a8d8), the SPU transfer thread (fn_49854), the stream
// reader thread (fn_46520); plus the loader open / close (fn_3ac34 / fn_3aca0) and the library registration
// (fn_39f0c). Ported from the recompiled 989snd_Library over the module's RAM.
//
// The RPC servers keep their sifcmd queue / server records ON THE THREAD STACK (sp + 0x20 / sp + 0x38 of a 0x88
// frame): the EE's bind reply carries the server record's address, so the frame here is the original's size and the
// records sit at the original's offsets.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kRpcBuffer0 = G::kData + 0x638, kRpcBuffer1 = G::kData + 0x2578;
constexpr uint32_t kRpcHandler0 = 0x3497c, kRpcHandler1 = 0x34a68;
constexpr uint32_t kTickDoneSema = G::kData + 0x18f0;
constexpr uint32_t kTransDone0 = G::kData + 0x1948, kTransDone1 = G::kData + 0x194c, kTransSema = G::kData + 0x1950;
constexpr uint32_t kRefillQueueHead = G::kData + 0x2344;
constexpr uint32_t kStartQueueHead = G::kData + 0x233c, kStartQueueTail = G::kData + 0x2340;
constexpr uint32_t kRefillCurrent = G::kData + 0x234c;
constexpr uint32_t kCd = G::kData + 0x22f0;              // kCd + 0x1c max sectors, + 0x28 failed, + 0x60 busy
constexpr uint32_t kReaderWaiter = G::kData + 0x235c;    // a thread to wake when the reader idles
constexpr uint32_t kSearchName = G::kData + 0x2368, kSearchFile = G::kData + 0x236c, kSearchSema = G::kData + 0x2370;
constexpr uint32_t kSearchResult = G::kData + 0x2374;
constexpr uint32_t kLoadFd = G::kData + 0x1958, kLoadFromCd = G::kData + 0x195c, kLoadBase = G::kData + 0x1960;
constexpr uint32_t kLoadSector = G::kData + 0x1964, kLoadOffset = G::kData + 0x1968, kLoadSector2 = G::kData + 0x196c;
constexpr uint32_t kLoadBase2 = G::kData + 0x1970, kLoadFromMem = G::kData + 0x1978;
constexpr uint32_t kLibRegistered = G::kData + 0x18ec, kExportTable = G::kData - 0xc98;
}  // namespace

// fn_349e4: the 'SND ' RPC server thread (0x123456): the reply buffer's first word = -1, the queue bound to this
// thread, the server registered with fn_3497c over kRpcBuffer0, then sceSifRpcLoop forever.
void fn_349e4(IopContext& c) {
    Frame f(c, 0x88);
    const uint32_t reply = Ld32(G::kReplyBufPtr);
    St32(reply, 0xffffffffu);
    Import(c, "sifcmd", 14, 0);
    const uint32_t tid = Import(c, "thbase", 20);
    Import(c, "sifcmd", 19, f.At(0x20), tid);
    St32(f.At(0x10), 0);
    St32(f.At(0x14), 0);
    St32(f.At(0x18), f.At(0x20));
    Import(c, "sifcmd", 17, f.At(0x38), 0x123456, kRpcHandler0, Ld32(kRpcBuffer0));
    Import(c, "sifcmd", 22, f.At(0x20));
    Ret(c, 0);
}

// fn_34ba0: the second RPC server thread (0x123457 -> fn_34a68 over kRpcBuffer1).
void fn_34ba0(IopContext& c) {
    Frame f(c, 0x88);
    Import(c, "sifcmd", 14, 0);
    const uint32_t tid = Import(c, "thbase", 20);
    Import(c, "sifcmd", 19, f.At(0x20), tid);
    St32(f.At(0x10), 0);
    St32(f.At(0x14), 0);
    St32(f.At(0x18), f.At(0x20));
    Import(c, "sifcmd", 17, f.At(0x38), 0x123457, kRpcHandler1, kRpcBuffer1);
    Import(c, "sifcmd", 22, f.At(0x20));
    Ret(c, 0);
}

// fn_3a8d8: the tick thread: sleep (the 240 Hz timer beat wakes it), tick (fn_4325c), signal the tick-done
// semaphore; forever.
void fn_3a8d8(IopContext& c) {
    Frame f(c, 0x18);
    for (;;) {
        Import(c, "thbase", 24);
        Call(c, 0x4325c);
        imp::SignalSema(c, Ld32(kTickDoneSema));
    }
}

// fn_49854: the SPU transfer thread: wait on the transfer semaphore while neither channel's done flag is set (a failed
// wait is reported (0x47) on every pass until a wait succeeds), step the channels whose flag is set (fn_49c58), and
// wake the refill waiter when the refill queue is non-empty; forever.
void fn_49854(IopContext& c) {
    Frame f(c, 0x30);
    uint32_t status = 0;
    for (;;) {
        if (Ld32(kTransDone0) == 0 && Ld32(kTransDone1) == 0) status = imp::WaitSema(c, Ld32(kTransSema));
        if (status != 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x47, status, 0, 0);
        }
        if (Ld32(kTransDone0) != 0) {
            St32(kTransDone0, 0);
            Call(c, 0x49c58, 0);
        }
        if (Ld32(kTransDone1) != 0) {
            St32(kTransDone1, 0);
            Call(c, 0x49c58, 1);
        }
        if (Ld32(kRefillQueueHead) != 0) imp::SignalSema(c, Ld32(G::kRampWaiterSema));
    }
}

// fn_46520: the stream reader thread. Waits on the stream semaphore while a refill is current, the reader busy, or
// nothing is pending (start queue, CD request, file search); a failed wait is reported (0x38). A failed CD read is
// finished (fn_493b0). Under suspended interrupts the next start-queue record becomes current (a stopping handle's
// is dropped; one already playing / ended (flags 0x21) is dropped) and refilled (fn_471a0(0)); else a pending CD
// request is issued (fn_46ed0, busy set); else a pending file search runs (sceCdSearchFile, the result stored, the
// search semaphore signalled). When everything is idle the waiting thread (kReaderWaiter) is woken. Forever.
void fn_46520(IopContext& c) {
    Frame f(c, 0x48);
    const uint32_t intr = f.At(0x18);
    uint32_t status = 0;
    uint32_t suspended;
top:
    if (Ld32(kRefillCurrent) == 0) goto idle_check;
wait:
    status = imp::WaitSema(c, Ld32(G::kStreamSema));
    if (Ld32(kRefillCurrent) != 0) goto wait;
idle_check:
    if (Ld32(kCd + 0x60) != 0) goto wait;
    if (Ld32(kStartQueueHead) == 0 && Ld32(kCd + 0x1c) == 0 && Ld32(kSearchName) == 0) goto wait;
    if (status != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x38, status, 0, 0);
    }
    if (Ld32(kCd + 0x28) != 0 && Ld32(kCd + 0x60) == 0) Call(c, 0x493b0);
    suspended = imp::CpuSuspendIntr(c, intr);
    if (Ld32(kRefillCurrent) != 0 || Ld32(kCd + 0x60) != 0) goto resume;
    if (Ld32(kStartQueueHead) != 0) {
        for (;;) {
            const uint32_t cur = Ld32(kStartQueueHead);
            const uint32_t next = Ld32(cur + 0xc);
            St32(kRefillCurrent, cur);
            St32(kStartQueueHead, next);
            if (next == 0) St32(kStartQueueTail, 0);
            if (Ld32(kRefillCurrent) != 0) {
                const uint32_t h = Ld32(Ld32(Ld32(kRefillCurrent) + 0x10) + 0xd0);
                if (Ld8(h + 0x16) & 4) St32(kRefillCurrent, 0);
                if (Ld32(kRefillCurrent) != 0) break;
            }
            if (Ld32(kStartQueueHead) != 0) continue;
            if (Ld32(kRefillCurrent) == 0) goto drop;
            break;
        }
        {
            const uint32_t st = Ld32(Ld32(kRefillCurrent) + 0x10);
            if (Ld32(st) & 0x21) goto drop;
        }
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        Call(c, 0x471a0, 0);
        goto resuspend;
    drop:
        St32(kRefillCurrent, 0);
        goto resume;
    }
    if (Ld32(kCd + 0x1c) != 0) {
        St32(kCd + 0x60, 1);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        Call(c, 0x46ed0);
        goto resuspend;
    }
    if (Ld32(kSearchName) != 0) {
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        const uint32_t r = Import(c, "cdvdman", 10, Ld32(kSearchFile), Ld32(kSearchName));
        const uint32_t sema = Ld32(kSearchSema);
        St32(kSearchResult, r);
        St32(kSearchName, 0);
        St32(kSearchFile, 0);
        imp::SignalSema(c, sema);
        goto resuspend;
    }
    goto resume;
resuspend:
    suspended = imp::CpuSuspendIntr(c, intr);
resume:
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
    if (Ld32(kRefillCurrent) != 0) goto wait;
    if (Ld32(kCd + 0x60) != 0 || Ld32(kStartQueueHead) != 0 || Ld32(kSearchName) != 0) goto top;
    {
        const uint32_t waiter = Ld32(kReaderWaiter);
        if (waiter == 0) goto top;
        Import(c, "thbase", 25, waiter);
        St32(kReaderWaiter, 0);
    }
    goto top;
}

// fn_3ac34(sector, offset): open the loader on the CD: the base / current / second sector+offset pairs, fd = 1.
void fn_3ac34(IopContext& c) {
    const uint32_t sector = A0(c), offset = A1(c);
    St32(kLoadFromCd, sector);
    St32(kLoadBase, offset);
    St32(kLoadSector, sector);
    St32(kLoadOffset, offset);
    St32(kLoadSector2, sector);
    St32(kLoadBase2, offset);
    St32(kLoadFd, 1);
    Ret(c, 1);
}

// fn_3aca0(): close the loader: the CD words cleared, or the memory flag, or ioman close(fd); fd = -1. Returns -1
// (the module base word when nothing was open).
void fn_3aca0(IopContext& c) {
    Frame f(c, 0x18);
    const uint32_t fd = Ld32(kLoadFd);
    if (fd == 0xffffffffu) {
        Ret(c, G::kData);
        return;
    }
    if (Ld32(kLoadFromCd) != 0) {
        St32(kLoadFromCd, 0);
        St32(kLoadBase, 0);
        St32(kLoadSector, 0);
        St32(kLoadOffset, 0);
    } else if (Ld32(kLoadFromMem) != 0) {
        St32(kLoadFromMem, 0);
    } else {
        Import(c, "ioman", 5, fd);
    }
    St32(kLoadFd, 0xffffffffu);
    Ret(c, 0xffffffffu);
}

// fn_39f0c(): register the module's export table once (RegisterLibraryEntries; a failure returns 1 without marking).
void fn_39f0c(IopContext& c) {
    Frame f(c, 0x18);
    if (Ld32(kLibRegistered) == 0 && Import(c, "loadcore", 6, kExportTable) != 0) {
        Ret(c, 1);
        return;
    }
    St32(kLibRegistered, 1);
    Ret(c, 0);
}

// fn_49428: the refill thread. Waits on the refill-waiter semaphore while nothing is queued or both channels are
// busy (a failed wait is reported, 0x46, and again after the second wait); for each channel in turn (0 then 1): when
// its slot is free, it is not busy and the queue is non-empty -- under suspended interrupts the channel is marked
// busy and the next queued half record (skipping a stopping handle's) becomes its slot; a slot taken arms the libsd
// transfer handler (fn_3aa3c) and starts the upload (fn_49788), else the channel is freed again. Forever.
void fn_49428(IopContext& c) {
    Frame f(c, 0x48);
    const uint32_t intr = f.At(0x18);
    uint32_t status = 0;
    constexpr uint32_t kTransBusy = G::kData + 0x1940, kTransSlot = G::kData + 0x2354;
    constexpr uint32_t kRefillQueueTail = G::kData + 0x2348;
    auto take = [&](uint32_t slot) {
        for (;;) {
            const uint32_t cur = Ld32(kRefillQueueHead);
            St32(slot, cur);
            const uint32_t next = Ld32(cur + 0xc);
            St32(kRefillQueueHead, next);
            if (next == 0) St32(kRefillQueueTail, 0);
            if (Ld32(slot) != 0) {
                const uint32_t h = Ld32(Ld32(Ld32(slot) + 0x10) + 0xd0);
                if (Ld8(h + 0x16) & 4) St32(slot, 0);
                if (Ld32(slot) != 0) return;
            }
            if (Ld32(kRefillQueueHead) == 0) return;
        }
    };
    auto report = [&]() {
        if (status != 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x46, status, 0, 0);
        }
    };
top:
    if (Ld32(kRefillQueueHead) == 0) goto wait0;
    if (Ld32(kTransBusy) == 0) goto after_wait0;
    if (Ld32(kTransBusy + 4) == 0) goto after_wait0;
wait0:
    status = imp::WaitSema(c, Ld32(G::kRampWaiterSema));
after_wait0:
    report();
    if (Ld32(kTransBusy) == 0 && Ld32(kTransSlot) != 0 && Ld32(kTransBusy + 4) == 0 && Ld32(kTransSlot + 4) != 0)
        status = imp::WaitSema(c, Ld32(G::kRampWaiterSema));
    report();
    if (Ld32(kTransSlot) == 0 && Ld32(kTransBusy) == 0 && Ld32(kRefillQueueHead) != 0) {
        const uint32_t suspended = imp::CpuSuspendIntr(c, intr);
        St32(kTransBusy, 1);
        if (Ld32(kTransSlot) == 0 && Ld32(kRefillQueueHead) != 0) take(kTransSlot);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (Ld32(kTransSlot) == 0) {
            St32(kTransBusy, 0);
        } else {
            Import(c, "libsd", 26, 0, 0x3aa3c, 0);
            Call(c, 0x49788, 0);
        }
    }
    if (Ld32(kTransSlot + 4) != 0 || Ld32(kTransBusy + 4) != 0) goto top;
    if (Ld32(kRefillQueueHead) == 0) goto wait0;
    {
        const uint32_t suspended = imp::CpuSuspendIntr(c, intr);
        St32(kTransBusy + 4, 1);
        if (Ld32(kTransSlot + 4) == 0 && Ld32(kRefillQueueHead) != 0) take(kTransSlot + 4);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (Ld32(kTransSlot + 4) == 0) {
            St32(kTransBusy + 4, 0);
        } else {
            Import(c, "libsd", 26, 1, 0x3aa3c, 0);
            Call(c, 0x49788, 1);
        }
    }
    goto top;
}

// fn_411f8(def, volume, by_origin, *out): may a sequence of `def` start? def[5] (the polyphony) == 0 -> 0. The live
// sequence handles of this definition (rec[4] == def, or rec[0x120] == def when `by_origin`) are counted and the one
// to supersede chosen (def[6] & 0x10: the quietest rec[0x3a]; & 0x20: the oldest rec[0x11c]); under the polyphony ->
// 1; else the candidate goes to *out when it is quieter than `volume` (& 0x10) or by age (& 0x20) -> 1; else 0.
void fn_411f8(IopContext& c) {
    const uint32_t def = A0(c), out = A3(c), by_origin = A2(c);
    const int32_t vol = S32(A1(c));
    if (Lds8(def + 5) == 0) {
        Ret(c, 0);
        return;
    }
    if (out != 0) St32(out, 0);
    const int32_t poly = Lds8(def + 5);
    int32_t count = 0;
    uint32_t pick = 0;
    for (uint32_t rec = Ld32(G::kSoundList); rec != 0; rec = Ld32(rec + 0x24)) {
        if ((Ld8(rec + 3) & 0x1f) != 5) continue;
        if (!(by_origin != 0 && Ld32(rec + 0x120) == def) && Ld32(rec + 4) != def) continue;
        ++count;
        if (pick == 0) {
            pick = rec;
            continue;
        }
        const uint32_t fl = Ld16(def + 6);
        if ((fl & 0x10) && Lds16(rec + 0x3a) < Lds16(pick + 0x3a)) {
            pick = rec;
            continue;
        }
        if ((fl & 0x20) && Ld32(rec + 0x11c) < Ld32(pick + 0x11c)) pick = rec;
    }
    if (!(poly - 1 < count)) {
        Ret(c, 1);
        return;
    }
    if (pick == 0) {
        Ret(c, 0);
        return;
    }
    const uint32_t fl = Ld16(def + 6);
    if (((fl & 0x10) && Lds16(pick + 0x3a) < vol) || (fl & 0x20)) {
        St32(out, pick);
        Ret(c, 1);
        return;
    }
    Ret(c, 0);
}

}  // namespace ps2r::title::sly2::snd989
