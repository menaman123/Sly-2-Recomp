// snd989_steps.cpp -- 989snd_Library's threads in the STEP form (NativeIopModule::AddThread; iop_step.h): each is the
// coroutine body of snd989_threads.cpp with its top-level blocking import returned as the park and the body re-entered
// after it:
//   * the tick thread fn_3a8d8: SleepThread = Park::Sleep; on the wake fn_4325c + the tick-done signal;
//   * the SPU transfer thread fn_49854 (one park: the transfer semaphore), the refill thread fn_49428 (two parks on the
//     refill-waiter semaphore), the stream reader fn_46520 (one park: the stream semaphore).
//   * the CD stream thread fn_47fec (two parks: the CD-read semaphore at the top of a pass, the EE-DMA completion wait).
// NOT in step form (the coroutine bodies stay): the two RPC servers. fn_34ba0 (0x123457): its fno 3 handler (the bank
// load) blocks INSIDE the handler (fn_3c308's reply-semaphore wait, fn_46d8c's SleepThread, fn_3d118's wait: boot.pad +
// the loader route). fn_349e4 ('SND '): its stop-all handler (fn_3384c -> fn_41c9c) DelayThread-loops inside the handler
// while sounds drain (the loader route: found when the step form spun 10.8M times on the kernel's "blocking kernel call
// from step thread" report). A step body cannot return a park taken inside a callee; iopsys's lock / wait pre-acquisition
// protocol is the design for it. RpcServerBody below is the ready step form for fn_349e4, not registered.
// On the boot.pad, Paris hub and loader-route lanes every other blocking import of the module is at the top level of the
// five registered bodies (per-thread analysis of the OFF traces; the nested WaitSema's of fn_47a88 / fn_42f58 / fn_4c1ac
// always take the count inline); a nested block from a step thread is reported by the kernel = the fail-loud oracle.
#include <cstdio>
#include <memory>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_native.h"

namespace ps2r::title::sly2::snd989 {

namespace {
using Park = iop::IopKernel::Park;
using Resume = iop::IopKernel::Resume;

constexpr uint32_t kRpcBuffer0 = G::kData + 0x638;
constexpr uint32_t kRpcHandler0 = 0x3497c;
constexpr uint32_t kTickDoneSema = G::kData + 0x18f0;
constexpr uint32_t kTransDone0 = G::kData + 0x1948, kTransDone1 = G::kData + 0x194c, kTransSema = G::kData + 0x1950;
constexpr uint32_t kTransBusy = G::kData + 0x1940, kTransSlot = G::kData + 0x2354;
constexpr uint32_t kRefillQueueHead = G::kData + 0x2344, kRefillQueueTail = G::kData + 0x2348;
constexpr uint32_t kStartQueueHead = G::kData + 0x233c, kStartQueueTail = G::kData + 0x2340;
constexpr uint32_t kRefillCurrent = G::kData + 0x234c;
constexpr uint32_t kCd = G::kData + 0x22f0;
constexpr uint32_t kReaderWaiter = G::kData + 0x235c;
constexpr uint32_t kSearchName = G::kData + 0x2368, kSearchFile = G::kData + 0x236c, kSearchSema = G::kData + 0x2370;
constexpr uint32_t kSearchResult = G::kData + 0x2374;
constexpr uint32_t kPositionDmaSema = G::kData + 0x62c, kPositionDmaCallback = 0x351c8;
constexpr uint32_t kVoiceTable = 0x593a8;
constexpr uint32_t kVagLE = 0x70474156u, kVagBE = 0x56414770u, kVpk = 0x56504b20u;
inline uint32_t Bswap(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }

void Prologue(IopContext& regs, uint32_t frame) {
    regs.gpr[29] -= frame;
    regs.FixZero();
}

struct ThreadState {
    IopContext regs;
    IopContext initial;
    int phase = 0;
    uint32_t status = 0;
    uint32_t fp = 0;        // fn_47fec's $fp (persists across passes)
    uint32_t dma_id = 0;    // fn_47fec's EE-DMA id across the completion wait
    StepPark::Pending pending;
};

// fn_349e4 (frame 0x88): the 'SND ' RPC server thread.
Park RpcServerBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(0x349e4, st.initial);
        Prologue(c, 0x88);
        const uint32_t sp = c.gpr[29];
        const uint32_t reply = Ld32(G::kReplyBufPtr);
        St32(reply, 0xffffffffu);
        Import(c, "sifcmd", 14, 0);
        const uint32_t tid = Import(c, "thbase", 20);
        Import(c, "sifcmd", 19, sp + 0x20, tid);
        St32(sp + 0x10, 0);
        St32(sp + 0x14, 0);
        St32(sp + 0x18, sp + 0x20);
        Import(c, "sifcmd", 17, sp + 0x38, 0x123456, kRpcHandler0, Ld32(kRpcBuffer0));
        return StepPark::RpcLoopEnter(c, sp + 0x20);
    }
    StepPark::RpcDrain();
    return Park::Sleep();
}

// fn_3a8d8 (frame 0x18): the tick thread.
Park TickBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(0x3a8d8, st.initial);
        Prologue(c, 0x18);
        return StepPark::SleepThread(c, st.pending);
    }
    StepPark::Resumed(c, st.pending, r);
    Call(c, 0x4325c);
    imp::SignalSema(c, Ld32(kTickDoneSema));
    return StepPark::SleepThread(c, st.pending);
}

// fn_49854 (frame 0x30): the SPU transfer thread (one park: the transfer semaphore).
Park TransferBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(0x49854, st.initial);
        Prologue(c, 0x30);
        st.status = 0;
    } else {
        StepPark::Resumed(c, st.pending, r);
        st.status = c.gpr[2];
        goto body;
    }
    for (;;) {
        if (Ld32(kTransDone0) == 0 && Ld32(kTransDone1) == 0) return StepPark::WaitSema(c, st.pending, S32(Ld32(kTransSema)));
    body:
        if (st.status != 0) {
            St32(c.gpr[29] + 0x10, 0);
            Call(c, 0x35140, 0x47, st.status, 0, 0);
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

// fn_49428 (frame 0x48): the refill thread (two parks on the refill-waiter semaphore: phase 1 = the first wait, 2 = the
// second).
Park RefillBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(0x49428, st.initial);
        Prologue(c, 0x48);
        st.status = 0;
        st.phase = 0;
    } else {
        StepPark::Resumed(c, st.pending, r);
        st.status = c.gpr[2];
    }
    const uint32_t intr = c.gpr[29] + 0x18;
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
        if (st.status != 0) {
            St32(c.gpr[29] + 0x10, 0);
            Call(c, 0x35140, 0x46, st.status, 0, 0);
        }
    };
    if (st.phase == 1) goto after_wait0;
    if (st.phase == 2) goto after_wait2;
top:
    if (Ld32(kRefillQueueHead) == 0) goto wait0;
    if (Ld32(kTransBusy) == 0) goto after_wait0;
    if (Ld32(kTransBusy + 4) == 0) goto after_wait0;
wait0:
    st.phase = 1;
    return StepPark::WaitSema(c, st.pending, S32(Ld32(G::kRampWaiterSema)));
after_wait0:
    report();
    if (Ld32(kTransBusy) == 0 && Ld32(kTransSlot) != 0 && Ld32(kTransBusy + 4) == 0 && Ld32(kTransSlot + 4) != 0) {
        st.phase = 2;
        return StepPark::WaitSema(c, st.pending, S32(Ld32(G::kRampWaiterSema)));
    }
after_wait2:
    st.phase = 0;
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

// fn_46520 (frame 0x48): the stream reader thread (one park: the stream semaphore).
Park ReaderBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(0x46520, st.initial);
        Prologue(c, 0x48);
        st.status = 0;
    } else {
        StepPark::Resumed(c, st.pending, r);
        st.status = c.gpr[2];
    }
    const uint32_t intr = c.gpr[29] + 0x18;
    uint32_t suspended;
    if (!r.first) goto after_wait;
top:
    if (Ld32(kRefillCurrent) == 0) goto idle_check;
wait:
    return StepPark::WaitSema(c, st.pending, S32(Ld32(G::kStreamSema)));
after_wait:
    if (Ld32(kRefillCurrent) != 0) goto wait;
idle_check:
    if (Ld32(kCd + 0x60) != 0) goto wait;
    if (Ld32(kStartQueueHead) == 0 && Ld32(kCd + 0x1c) == 0 && Ld32(kSearchName) == 0) goto wait;
    if (st.status != 0) {
        St32(c.gpr[29] + 0x10, 0);
        Call(c, 0x35140, 0x38, st.status, 0, 0);
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
            const uint32_t s = Ld32(Ld32(kRefillCurrent) + 0x10);
            if (Ld32(s) & 0x21) goto drop;
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
        const uint32_t res = Import(c, "cdvdman", 10, Ld32(kSearchFile), Ld32(kSearchName));
        const uint32_t sema = Ld32(kSearchSema);
        St32(kSearchResult, res);
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

// fn_47fec (frame 0x68): the CD stream thread -- snd989_cdthread.cpp's body with its two waits as parks (phase 1 = the
// CD-read semaphore at the top of a pass, 2 = the EE-DMA completion inside the CD-finished pass; $fp and the DMA id live
// in the thread state, the pass's locals on the thread stack as before).
Park CdStreamBody(ThreadState& ts, const Resume& r) {
    IopContext& c = ts.regs;
    uint32_t suspended, status = 0, cur, st, hdl, hdr, first = 0, err, n;
    if (r.first) {
        StepPark::Entry(0x47fec, ts.initial);
        Prologue(c, 0x68);
        ts.fp = 0;                     // $fp: a start-queue refusal -> the handle is stopped at the end of the pass
        St32(c.gpr[29] + 0x2c, 0);     // wake the refill waiter at the end of the pass
    } else {
        StepPark::Resumed(c, ts.pending, r);
        status = c.gpr[2];
    }
    const uint32_t sp = c.gpr[29];
    const uint32_t intr = sp + 0x28;
    if (!r.first && ts.phase == 2) goto dma_resume;
    if (!r.first) goto after_top;
top:
    ts.phase = 1;
    return StepPark::WaitSema(c, ts.pending, S32(Ld32(G::kCdReadSema)));
after_top:
    if (status != 0) {
        St32((sp + 0x10), 0);
        Call(c, 0x35140, 0x3f, status, 0, 0);
    }
    if (Ld32(kCd + 0x60) == 0 && Ld32(kRefillCurrent) == 0) goto signal_stream;
    if (Ld32(kCd + 0x60) != 0) {
        if (Ld32(G::kCdError) == 1) {
            St32(kCd + 0x30, 0xffffffffu);
            St32(G::kCdError, 0);
        } else {
            Import(c, "cdvdman", 11, 0);
            St32(kCd + 0x30, Import(c, "cdvdman", 8, 0));
        }
    } else {
        if (Ld32(G::kCdError) == 1) {
            St32(kCd + 0x2c, 0xffffffffu);
            St32(G::kCdError, 0);
        } else if ((Ld32(Ld32(Ld32(kRefillCurrent) + 0x10)) & 0x1f800u) == 0) {
            Import(c, "cdvdman", 11, 0);
            St32(kCd + 0x2c, Import(c, "cdvdman", 8, 0));
        }
        err = Ld32(kCd + 0x2c);
        if (err != 0) {
            St32((sp + 0x10), Ld32(kCd + 0x44));
            Call(c, 0x35140, 0x40, err, Ld32(kCd + 0x3c), Ld32(kCd + 0x40));
        }
    }
    if (Ld32(kCd + 0x60) == 0) goto refill;
    // ---- the CD request finished (0x48164)
    err = Ld32(kCd + 0x30);
    if (err != 0) {
        St32(sp + 0x10, Ld32(kCd + 0x44));
        Call(c, 0x35140, 0x41, err, Ld32(kCd + 0x3c), Ld32(kCd + 0x40));
        St32(kCd + 0x1c, 0);
        goto cd_done;
    }
    if (Ld32(kCd + 0x28) != 0) {
        St32(kCd + 0x1c, 0);
        goto cd_done;
    }
    if (Ld32(kCd + 0x60) == 0xffffffffu) goto cd_done;
    if (Ld32(kCd + 0x38) == 0 || Ld32(kCd + 0x1c) == 0) goto cd_advance;
    // memory source: the read sectors DMA'd to the EE
    St32(sp + 0x18, Ld32(kCd + 0x14));
    St32(sp + 0x1c, Ld32(kCd + 0x18));
    St32(sp + 0x24, 0);
    St32(sp + 0x20, Ld32(kCd + 0x20) << 11);
    suspended = imp::CpuSuspendIntr(c, intr);
    ts.dma_id = Import(c, "sifman", 32, sp + 0x18, 1, kPositionDmaCallback, kPositionDmaSema);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
    if (ts.dma_id == 0) {
        St32(sp + 0x10, 0);
        Call(c, 0x35140, 0x42, 0, 0, 0);
        St32(kCd + 0x1c, 0);
        St32(kCd + 0x30, 0xffffffffu);
        goto cd_advance;
    }
dma_loop:
    if (S32(Import(c, "sifman", 8, ts.dma_id)) < 0) goto cd_advance;
    ts.phase = 2;
    return StepPark::WaitSema(c, ts.pending, S32(Ld32(kPositionDmaSema)));
dma_resume:   // the EE-DMA completion wait returned (status = its result)
    if (status != 0) {
        St32(sp + 0x10, 0);
        Call(c, 0x35140, 0xfa3, 0, 0, 0);
    }
    goto dma_loop;
cd_advance:
    n = Ld32(kCd + 0x20);
    St32(kCd + 0x1c, Ld32(kCd + 0x1c) - n);
    if (Ld32(kCd + 0x1c) != 0) {
        St32(kCd + 0x24, Ld32(kCd + 0x24) + n);
        St32(kCd + 0x18, Ld32(kCd + 0x18) + (n << 11));
    }
cd_done:
    St32(kCd + 0x60, 0);
    if (Ld32(kCd + 0x1c) == 0 || Ld32(kCd + 0x28) == 1) Call(c, 0x493b0);
    imp::SignalSema(c, Ld32(G::kStreamSema));
    goto signal_stream;

refill:  // ---- the current refill (0x48364)
    cur = Ld32(kRefillCurrent);
    if (cur == 0) goto clear_signal;
    st = Ld32(cur + 0x10);
    {
        const uint32_t flags = Ld32(st);
        hdl = Ld32(st + 0xd0);
        if (flags & 1) goto mark_stopping;
        if (Ld8(hdl + 0x16) & 4) goto mark_stopping;
        if (Ld32(kCd + 0x2c) != 0) {
            St32(kRefillCurrent, 0);
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto signal_stream;
        }
        const uint32_t curflags = Ld32(cur);
        if (curflags & 0x10) {
        // ---- the header pass (0x483e8)
        hdr = Ld32(cur + 4) + Ld32(st + 0x8c);
        const uint32_t magic = Ld32(hdr);
        if (magic == kVagLE || magic == kVagBE) {
            uint32_t rate = Ld32(hdr + 0x10);
            if (magic == kVagLE) rate = Bswap(rate);
            St32(st + 0xb8, rate);
            St32(st + 8, MulHiU(rate << 12, 0x057619f1u) >> 10);
            uint32_t size = Ld32(hdr + 0xc);
            if (Ld32(hdr) == kVagLE) size = Bswap(size);
            St32(st + 0xb0, (size >> 4) * 28);
            const uint32_t data = size + Ld32(st + 0x8c) + 0x30;
            const uint32_t bufsize = Ld32(st + 0xc);
            St32(st + 0x10, 9);
            St32(st + 0xb4, 0);
            St32(st + 0xac, data);
            St32(st + 0xa8, data);
            St32(st + 0x3c, 1);
            if (data < (bufsize >> 1) + 1) {
                St32(st + 0x10, (flags & 0x400) ? 0x80d : 0xd);
            } else if (data < bufsize + 1) {
                St32(st + 0x3c, 5);
            } else {
                St32(st + 0x10, 0x29);
            }
            const uint32_t pair = Ld32(st + 0xc8);
            if (pair == 0) {
                St8(st + 4, 0x7f);
                St8(st + 5, 0);
                St16(st + 6, 0);
            } else {
                St32(pair + 0xac, size + Ld32(pair + 0x8c) + 0x30);
                St32(pair + 0xa8, Ld32(pair + 0xac));
                St32(pair + 8, Ld32(st + 8));
                St32(pair + 0xb8, Ld32(st + 0xb8));
                St32(pair + 0x10, 9);
                St32(pair + 0x3c, 1);
                St32(pair + 0x10, Ld32(pair + 0x10) | (Ld32(st + 0x10) & 0x800));
                St32(pair + 0xb4, 0);
                St32(pair + 0xb0, Ld32(st + 0xb0));
                if (Call(c, 0x4aa70, pair + 0x10) == 0) ts.fp = 1;
                St8(st + 4, 0x7f);
                St8(st + 5, 0);
                St16(st + 6, 0x10e);
                St8(pair + 4, 0x7f);
                St8(pair + 5, 0);
                St16(pair + 6, 0x5a);
                if (Ld32(st + 0x10) & 4) {
                    St32(st + 0x10, Ld32(st + 0x10) & ~4u);
                    St32(pair + 0x10, Ld32(pair + 0x10) | 0x1000);
                }
            }
            if (ts.fp == 0) {
                if ((Ld32(st + 0xc) >> 1) < Ld32(st + 0xac)) {
                    const uint32_t p = Ld32(st + 0xc8);
                    if (p != 0 && Call(c, 0x4aa70, p + 0x3c) == 0) ts.fp = 1;
                    if (ts.fp == 0 && Call(c, 0x4aa70, st + 0x3c) == 0) ts.fp = 1;
                }
                if (ts.fp == 0) {
                    Call(c, 0x471a0, 0);
                    goto signal_stream;
                }
            }
            hdl = Ld32(st + 0xd0);
            St32(kRefillCurrent, 0);
            ts.fp = 0;
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto signal_stream;
        }
        if (magic != kVpk) {
            // 0x48ce0: not a header we know
            St32((sp + 0x10), 0);
            St32(kRefillCurrent, 0);
            Call(c, 0x35140, 0x45, Ld32(st + 0x88), Ld32(st + 0x8c), 0);
            St8(Ld32(st + 0xd0) + 0x16, 4);
            goto signal_stream;
        }
        // ---- a VPK header: the voice chain (0x486e8)
        St32((sp + 0x30), Ld32(hdl + 0x6c) == st ? 1 : 0);
        if (Ld32(hdl + 0x38) != 0xffffffffu) {
            St32((sp + 0x10), 0);
            Call(c, 0x35140, 0x6a, 0, 0, 0);
            Call(c, 0x4dec0, Ld32(Ld32(st + 0xd0) + 0x38));
            St32(Ld32(st + 0xd0) + 0x38, 0xffffffffu);
        }
        if (Ld32(hdr + 0xc) != Ld32(st + 0xc)) {
            St32((sp + 0x10), 0);
            Call(c, 0x35140, 0x43, Ld32(st + 0xc), Ld32(hdr + 0xc), 0);
            goto mark_stopping;
        }
        Call(c, 0x42f58, 0x58);
        Call(c, 0x4c1ac, 1, 0x5c);
        suspended = imp::CpuSuspendIntr(c, intr);
        first = Call(c, 0x4ba00, st, Ld32(hdr + 0x14));
        if (first == 0) {
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
            St32((sp + 0x10), 0);
            Call(c, 0x35140, 0x44, 0, 0, 0);
            St32(kRefillCurrent, 0);
            hdl = Ld32(st + 0xd0);
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto unlock_signal;
        }
        St32((sp + 0x34), 1);
        St32((sp + 0x38), 0);
        if (Ld32(hdr + 0x14) != 0) {
            uint32_t rec = first, i = 0;
            do {
                St32(rec, flags | 2);
                if (rec == st) St32((sp + 0x34), 0);
                ++i;
                rec = Ld32(rec + 0xcc);
            } while (i < Ld32(hdr + 0x14));
        }
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (Ld32(hdr + 0x14) != 0) {
            uint32_t rec = first, prev = first, i = 0, hp = hdr;
            do {
                hdl = Ld32(st + 0xd0);
                St32(rec + 0xd0, hdl);
                if (Ld8(hdl + 0x16) & 2) St32(rec, Ld32(rec) | 0x10);
                St32(rec + 0xc8, 0);
                St32(rec + 0x88, Ld32(st + 0x88));
                if (i == 0) {
                    St32(rec + 0x7c, Ld32(st + 0x7c));
                    if (rec != st) St32(st + 0x7c, 0xffffffffu);
                } else {
                    St32(prev + 0xc8, rec);
                    prev = rec;
                    St32(rec + 0x7c, Call(c, 0x4c750, U32(Lds8(Ld32(st + 0xd0) + 0x17)), 0x7f, 1));
                }
                const uint32_t voice = Ld32(rec + 0x7c);
                if (voice == 0xffffffffu) {
                    St32((sp + 0x10), 0);
                    Call(c, 0x35140, 0x7a, Ld32(hdr + 0x14), 0, 0);
                    if (first != rec) {
                        uint32_t r = first;
                        do {
                            const uint32_t v = Ld32(r + 0x7c);
                            if (v != 0xffffffffu) {
                                Call(c, 0x4dec0, v);
                                St32(r + 0x7c, 0xffffffffu);
                            }
                            r = Ld32(r + 0xcc);
                        } while (r != rec);
                    }
                    hdl = Ld32(st + 0xd0);
                    St32(kRefillCurrent, 0);
                    St32((sp + 0x38), 1);
                    St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
                    break;
                }
                const uint32_t vt = kVoiceTable + voice * 56;
                St32(vt + 8, 4);
                St32(vt + 0xc, Ld32(rec + 0xd0));
                const uint32_t rate = Ld32(hdr + 0x10);
                St32(rec + 0xb8, rate);
                St32(rec + 8, U32(DivMagic(S32(rate << 12), 0x057619f1u, 10)));
                const uint32_t size = Ld32(hdr + 4);
                St32(rec + 0xb4, 0);
                St32(rec + 0x10, 0x109);
                St32(rec + 0x3c, 0x101);
                St32(rec + 0xb0, (size >> 4) * 28);
                St32(rec + 0xac, size);
                St32(rec + 0xa8, size);
                if (i == 0) {
                    St32(rec + 0x10, 0x309);
                    St32(rec + 0x3c, 0x301);
                }
                if (Ld32(rec + 0xac) < (Ld32(rec + 0xc) >> 1) + 1) {
                    St32(rec + 0x10, Ld32(rec + 0x10) | 4);
                } else if (Ld32(st + 0xac) < Ld32(st + 0xc) + 1) {
                    St32(rec + 0x3c, Ld32(rec + 0x3c) | 4);
                } else {
                    St32(rec + 0x10, Ld32(rec + 0x10) | 0x20);
                }
                St8(rec + 4, Ld8(hp + 0x18));
                St16(rec + 6, Ld16(hdr + 0x1a + i * 4));
                St8(rec + 5, Ld8(hp + 0x19));
                St32(rec + 0x68, 0);
                St32(rec + 0x24, 0);
                St32(rec + 0x50, 0);
                St32(rec + 0x6c, 0);
                St32(rec + 0x74, 0);
                St32(rec + 0x70, 0);
                St32(rec + 0xbc, 0);
                St32(rec + 0xc0, 0);
                St32(rec + 0x8c, 0);
                if (i == 0) {
                    St32(rec + 0x84, Ld32(st + 0x88) + 1);
                    St32(rec + 0x88, Ld32(st + 0x88));
                    if (Ld32(rec) & 0x1c000u) {
                        const uint32_t pos = Ld32(rec + 0xa0), base = Ld32(rec + 0x9c), half = Ld32(rec + 0xc) >> 1;
                        if (half == 0) {
                            std::printf("[IOP-RECOMP] BREAK at 0x48b24\n");
                            return Park::Exit();
                        }
                        const uint32_t rem = (pos - base) % half;
                        const uint32_t adv = (rem == 0 ? 0 : half - rem) + 0x800;
                        const uint32_t len = Ld32(rec + 0x98);
                        St32(rec + 0xa0, pos + adv);
                        if (!(pos + adv < base + len)) St32(rec + 0xa0, pos + adv - len);
                        Call(c, 0x47950, rec, adv, 0);
                    }
                }
                ++i;
                rec = Ld32(rec + 0xcc);
                hp += 4;
            } while (i < Ld32(hdr + 0x14));
        }
        if (Ld32((sp + 0x38)) != 0) goto unlock_signal;
        suspended = imp::CpuSuspendIntr(c, intr);
        if (Ld32((sp + 0x34)) != 0) {
            St32(st, 1);
            St32(st + 0x7c, 0xffffffffu);
        }
        if (Ld32((sp + 0x30)) == 0) {
            St32(Ld32(first + 0xd0) + 4, first);
            St32(Ld32(first + 0xd0) + 0x34, Ld32(hdr + 0x14));
        }
        for (uint32_t r = first; r != 0; r = Ld32(r + 0xc8)) {
            const int32_t v = Lds32(r + 0x7c);
            const int32_t q = Div24(v);
            const uint32_t bit = U32(v - q * 24);
            const uint32_t word = Ld32(first + 0xd0) + 0x18 + U32(q) * 4;
            St32(word, Ld32(word) | (1u << (bit & 31)));
        }
        St32(kRefillCurrent, first + 0x10);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (ts.fp == 0) {
            if ((Ld32(first + 0xc) >> 1) < Ld32(first + 0xac)) {
                if (Call(c, 0x4aa70, first + 0x3c) == 0) ts.fp = 1;
            }
            if (ts.fp == 0) {
                Call(c, 0x471a0, 0);
                goto unlock_signal;
            }
        }
        hdl = Ld32(first + 0xd0);
        St32(kRefillCurrent, 0);
        ts.fp = 0;
        St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
    unlock_signal:
        Call(c, 0x4c2f8);
        Call(c, 0x430dc);
        goto signal_stream;
        }
        // ---- the data pass (0x48d08)
        if (curflags & 0x100) goto chunk_sizes;
        if (curflags & 0x400) {
            St32(cur, curflags & ~0x400u);
            Import(c, "sysclib", 12, Ld32(cur + 0x20), Ld32(cur + 0x1c), Ld32(cur + 0x24));
            cur = Ld32(kRefillCurrent);
            St32(cur + 0x18, Ld32(cur + 0x18) - (Ld32(cur + 0x1c) - Ld32(cur + 0x20)));
        }
        cur = Ld32(kRefillCurrent);
        if (Ld32(cur + 0x14) != 0 && (Ld32(Ld32(cur + 0x10)) & 0x400)) {
            if (Ld32(cur) & 0x800) goto tail_800;
            // the stream's end inside this half (0x48d9c)
            uint32_t got = Ld32(cur + 0x18);
            uint32_t st2 = Ld32(cur + 0x10);
            St8(Ld32(cur + 4) + got - 0xf, 0);
            const int32_t left = S32(((Ld32(st2 + 0xc) >> 1) - got) + 0x10);
            cur = Ld32(kRefillCurrent);
            got = Ld32(cur + 0x18);
            if (!(got < 0x11)) St8(Ld32(cur + 4) + got - 0x1f, 0);
            cur = Ld32(kRefillCurrent);
            st2 = Ld32(cur + 0x10);
            St32(cur + 0x14, 0);
            St32(st2, Ld32(st2) & ~0x20u);
            St32(st2 + 0xac, Ld32(st2 + 0xa8));
            St32(st2 + 0x84, Ld32(st2 + 0x88));
            if (Ld32(st2) & 0x1c000u) {
                uint32_t adv = 0;
                const uint32_t pos = Ld32(st2 + 0xa0), half = Ld32(st2 + 0xc) >> 1;
                if (half == 0) {
                    std::printf("[IOP-RECOMP] BREAK at 0x48e68\n");
                    return Park::Exit();
                }
                const uint32_t rem = (pos - Ld32(st2 + 0x9c)) % half;
                if (rem != 0) {
                    adv = half - rem;
                    St32(st2 + 0xa0, pos + adv);
                }
                st2 = Ld32(Ld32(kRefillCurrent) + 0x10);
                const uint32_t p = Ld32(st2 + 0xa0), len = Ld32(st2 + 0x98);
                if (!(p < Ld32(st2 + 0x9c) + len)) St32(st2 + 0xa0, p - len);
                if (adv != 0) Call(c, 0x47950, Ld32(Ld32(kRefillCurrent) + 0x10), adv, 0);
            }
            if (!(left < 0x801)) {
                cur = Ld32(kRefillCurrent);
                const uint32_t t = Ld32(cur + 0x18), base = Ld32(cur + 4), s3 = Ld32(cur + 0x10);
                St32(cur, Ld32(cur) | 0x400);
                St32(cur + 0x20, base + t - 0x10);
                St32(cur + 0x1c, base + Ld32(s3 + 0x8c) + t + 0x30);
                St32(cur + 0x24, U32(left) - 0x40 - Ld32(s3 + 0x8c));
                Call(c, 0x471a0, t - 0x10);
                goto signal_stream;
            }
            cur = Ld32(kRefillCurrent);
            {
                const uint32_t s3 = Ld32(cur + 0x10);
                uint32_t other = s3 + 0x10;
                St32(cur + 0x18, Ld32(cur + 0x18) - 0x10);
                if (cur == other) other = s3 + 0x3c;
                const uint32_t so = Ld32(other + 0x10);
                St32(other, Ld32(other) | 0x400);
                St32(other + 0x20, Ld32(other + 4));
                St32(other + 0x1c, Ld32(other + 4) + Ld32(so + 0x8c) + 0x40);
                St32(other + 0x24, (Ld32(Ld32(cur + 0x10) + 0xc) >> 1) - 0x40 - Ld32(so + 0x8c));
            }
        }
        cur = Ld32(kRefillCurrent);
        {
            uint32_t buf;
            if (Ld32(cur) & 0x800) {
            tail_800:
                cur = Ld32(kRefillCurrent);
                const uint32_t s3 = Ld32(cur + 0x10), hoff = Ld32(s3 + 0x8c);
                buf = Ld32(cur + 4) + hoff + 0x30;
                St32(cur + 0x18, Ld32(cur + 0x18) - 0x40 - hoff);
            } else {
                buf = Ld32(cur + 4);
            }
            cur = Ld32(kRefillCurrent);
            St32(Ld32(kRefillCurrent) + 0x18, Call(c, 0x4a7c4, buf, Ld32(cur + 0x18), Ld32(cur + 0x14), Ld32(cur) & 0x800));
            goto finish;
        }
    chunk_sizes:  // 0x4901c: the chain's per-record ADPCM fix
        {
            const uint32_t off = (cur != st + 0x10) ? 44 : 0;
            for (uint32_t r = st; r != 0; r = Ld32(r + 0xc8)) {
                cur = Ld32(kRefillCurrent);
                St32(r + off + 0x28, Call(c, 0x4a7c4, Ld32(r + off + 0x14), Ld32(cur + 0x18), Ld32(cur + 0x14), 0));
            }
        }
    finish:  // 0x49074
        cur = Ld32(kRefillCurrent);
        {
            const uint32_t v1 = Ld32(cur) | 0x80;
            St32(cur, v1);
            if (v1 & 1) {
                Call(c, 0x4a9d4, cur);
                St32((sp + 0x2c), 1);
            } else if (v1 & 0x40) {
                const uint32_t h = Ld32(Ld32(cur + 0x10) + 0xd0);
                St8(h + 0x16, Ld8(h + 0x16) | 8);
                Call(c, 0x4a050, Ld32(Ld32(kRefillCurrent) + 0x10), 1);
            }
        }
        {
            const uint32_t wake = Ld32((sp + 0x2c));
            St32(kRefillCurrent, 0);
            if (wake != 0) {
                St32((sp + 0x2c), 0);
                imp::SignalSema(c, Ld32(G::kRampWaiterSema));
            }
        }
        goto signal_stream;
    }
mark_stopping:  // 0x4876c
    hdl = Ld32(st + 0xd0);
    St32(kRefillCurrent, 0);
    St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
    goto signal_stream;
clear_signal:  // 0x49110
    St32(kRefillCurrent, 0);
signal_stream:  // 0x49118
    imp::SignalSema(c, Ld32(G::kStreamSema));
    if (Ld32(kRefillCurrent) != 0 || Ld32(kCd + 0x60) != 0 || Ld32(kStartQueueHead) != 0 || Ld32(kSearchName) != 0) goto top;
    {
        const uint32_t waiter = Ld32(kReaderWaiter);
        if (waiter == 0) goto top;
        Import(c, "thbase", 25, waiter);
        St32(kReaderWaiter, 0);
    }
    goto top;
}


template <Park (*Body)(ThreadState&, const Resume&)>
iop::IopKernel::StepBody Make(const IopContext& initial) {
    auto st = std::make_shared<ThreadState>();
    st->regs = initial;
    st->initial = initial;
    return [st](const Resume& r) -> Park { return Body(*st, r); };
}

}  // namespace

void RegisterSnd989Threads(NativeIopModule& m) {
    // fn_349e4: NOT registered -- see the header (its stop-all handler fn_41c9c delays inside the handler on the loader
    // route). The body stays here as the step form to use once nested parks are possible.
    (void)&Make<&RpcServerBody>;
    m.AddThread(0x3a8d8, &Make<&TickBody>, "snd989_tick");
    m.AddThread(0x49854, &Make<&TransferBody>, "snd989_transfer");
    m.AddThread(0x49428, &Make<&RefillBody>, "snd989_refill");
    m.AddThread(0x46520, &Make<&ReaderBody>, "snd989_reader");
    m.AddThread(0x47fec, &Make<&CdStreamBody>, "snd989_cd_stream");
}

}  // namespace ps2r::title::sly2::snd989
