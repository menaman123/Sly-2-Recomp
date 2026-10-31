// snd989_steps.cpp -- 989snd_Library's threads in the STEP form (NativeIopModule::AddThread; iop_step.h): each is the
// coroutine body of snd989_threads.cpp with its top-level blocking import returned as the park and the body re-entered
// after it:
//   * the 'SND ' RPC server fn_349e4: the frame (0x88; the sifcmd queue / server records at sp+0x20 / sp+0x38 -- the EE
//     holds the server record's address, so the frame is allocated once and never moves), the registration, then
//     sceSifRpcLoop = StepPark::RpcLoopEnter + Park::Sleep; every wake drains the RPC queue (RpcDrain) and sleeps;
//   * the tick thread fn_3a8d8: SleepThread = Park::Sleep; on the wake fn_4325c + the tick-done signal;
//   * the SPU transfer thread fn_49854 (one park: the transfer semaphore), the refill thread fn_49428 (two parks on the
//     refill-waiter semaphore), the stream reader fn_46520 (one park: the stream semaphore).
// NOT in step form (coroutine bodies stay): the 0x123457 RPC server fn_34ba0 -- its fno 3 handler (the bank load) blocks
// INSIDE the handler on the boot lane (fn_3c308's reply-semaphore wait, fn_46d8c's SleepThread, fn_3d118's wait), which a
// step body cannot return (iopsys's lock / wait pre-acquisition protocol is the design for it); the CD stream thread
// fn_47fec -- its EE-DMA completion wait (fn_47fec @0x48284) is a second park inside the pass (not converted here).
// On the boot.pad and Paris hub lanes every other blocking import of the module is at the top level of these five bodies
// (OFF traces: the nested WaitSema's of fn_47a88 / fn_42f58 / fn_4c1ac always take the count inline); a nested block
// from a step thread is reported by the kernel ("iop: blocking kernel call from step thread") = the fail-loud oracle.
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

void Prologue(IopContext& regs, uint32_t frame) {
    regs.gpr[29] -= frame;
    regs.FixZero();
}

struct ThreadState {
    IopContext regs;
    IopContext initial;
    int phase = 0;
    uint32_t status = 0;
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

template <Park (*Body)(ThreadState&, const Resume&)>
iop::IopKernel::StepBody Make(const IopContext& initial) {
    auto st = std::make_shared<ThreadState>();
    st->regs = initial;
    st->initial = initial;
    return [st](const Resume& r) -> Park { return Body(*st, r); };
}

}  // namespace

void RegisterSnd989Threads(NativeIopModule& m) {
    m.AddThread(0x349e4, &Make<&RpcServerBody>, "snd989_rpc_server");
    m.AddThread(0x3a8d8, &Make<&TickBody>, "snd989_tick");
    m.AddThread(0x49854, &Make<&TransferBody>, "snd989_transfer");
    m.AddThread(0x49428, &Make<&RefillBody>, "snd989_refill");
    m.AddThread(0x46520, &Make<&ReaderBody>, "snd989_reader");
}

}  // namespace ps2r::title::sly2::snd989
