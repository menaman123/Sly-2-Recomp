#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"

#include <cstdio>
#include <vector>

#include "engine/core/threads/park.h"
#include "engine/dmac/dmac_handler_registry.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gs/hle/sce/gs_sync.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/vu/tiers/mtvu_sync.h"

namespace ps2r::title::sly2 {

LegInterrupts::LegInterrupts(const LegKnobs& knobs, LegCore& core, LegGs& gs, LegDevices& devices, LegMtvu& mtvu)
    : devices_(devices) {
    Dmac& dmac = devices.dmac;
    std::unique_ptr<MtvuStack>& mtvu_stack = mtvu.mtvu;
    Intc& intc = core.intc;
    // The deferred VIF1 kick's observation points at the frame boundary and sceGsSyncPath. Under MTVU the
    // final Sync(SceGsSyncV) finishes every queued span before the boundary runs.
    GsSync::BindDmac(
        [&dmac, &mtvu_stack] {
            dmac.ChaseVif1Tail();
            if (mtvu_stack) mtvu_stack->FrameSync(GsSync::SyncVCount(), ps2r::Park::NowNs());
        },
        [&dmac, &mtvu_stack] {
            // "Is the path idle": a real wait when boundaries are not deferred. Unbound in Sly 2's manifest.
            if (mtvu_stack) mtvu_stack->sync().Sync(SyncSite::SceGsSyncPath);
            dmac.FlushDeferredVif1();
        });
    // GS interrupt: SIGNAL/FINISH are counted on GsPrivRegs when the register is written, on the kicking
    // thread. The mask is the kernel's GsPutIMR word (syscall 113; bit 8 SIGMSK, bit 9 FINISHMSK).
    // Delivered at the next dispatch point: a syscall entry, a back-edge yield, or the boundary.
    // NATIVEEE_ISR_TRACE=1 prints one line per event.
    const bool isr_trace = knobs.isr_trace;
    GifParser& gif_parser = devices.gif_parser;
    GifArbiter& gif_arbiter = devices.gif_arbiter;
    gs.gs_state.Priv().SetInterruptObserver([this, isr_trace, &gif_parser, &gif_arbiter, &intc](uint32_t csr_event) {
        ++gs_irq_events;
        {
            const int kind = (csr_event & ps2r::gs::GsPrivRegs::kCsrSignal) ? 1 : 0;
            int path = 0;
            switch (gif_parser.ActivePath()) {
                case GifPath::Path1: path = 1; break;
                case GifPath::Path2: path = 2; break;
                case GifPath::Path3: path = 3; break;
                default: break;
            }
            ++gs_irq_kind_path[kind][path];
            if (gif_arbiter.OnDrainThread()) ++gs_irq_on_drain; else ++gs_irq_on_ee;
        }
        const bool masked = (SyscallTable::Instance().GsImr() & (static_cast<uint64_t>(csr_event) << 8)) != 0;
        if (isr_trace)
            std::fprintf(stderr, "gs-irq: t=%llu tid=%d event=0x%x imr=0x%llx %s path=%d p1=%llu p2=%llu p3=%llu\n",
                         (unsigned long long)ps2r::Park::NowNs() / 1000, EeRunToken::Instance().CurrentTid(), csr_event,
                         static_cast<unsigned long long>(SyscallTable::Instance().GsImr()), masked ? "masked" : "raise",
                         static_cast<int>(gif_parser.ActivePath()),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path1)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path2)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path3)));
        if (masked) {
            ++gs_irq_masked;
            return;
        }
        // Deferred: the parser runs behind the VU1 worker on the drain thread, so the cause is latched and
        // enters INTC_STAT on the token holder at its next scheduling point, never on the thread it woke.
        intc.RaiseDeferred(Intc::kGs);
        ++gs_irq_raises;
    });
    // Syscall entry: pending enabled INTC causes (and DMAC INT1 handlers) are dispatched on the calling
    // thread before the syscall runs, so a GS FINISH raised by a kick reaches the game's handler before its
    // WaitSema could block forever. Intc's in-dispatch guard stops an ISR's own kernel calls recursing.
    //
    // A blocking wait is also a DMA observation point: the parked chain is walked and the PATH3 backlog
    // reaches the GS. If another thread is READY, the waiter blocks and the ISR runs at that thread's next
    // dispatch point, so DMA latency shows up as other threads' run time, as on the console. Taking it here
    // instead would finish the kick instantly; the high-priority render/pacing pair would then never both
    // wait, and the main thread starves (Sly 2's level loader after the intro movie). Only when nothing else
    // is READY (the console's idle loop) is the interrupt taken here.
    //
    // The entry loops: an ISR may hand the token to the thread it readied, and when this thread is
    // re-elected, causes raised meanwhile are its to take.
    //
    // Under MTVU the chain is staged to the worker and its FINISH arrives later via the GIF drain: each
    // entry drains the sealed prefix first, and a blocking wait with nothing else READY idles on the
    // worker's progress instead of blocking with nothing left to deliver the interrupt.
    // engine.mtvu_wait_probe: first ask the kernel object whether the wait would park at all; without it, a
    // WaitSema whose FINISH was already signalled still waits out the whole replay.
    const bool wait_probe = mtvu.wait_probe;
    SyscallTable::Instance().SetPreDispatch([this, isr_trace, wait_probe, &dmac, &mtvu_stack, &intc](int num, CpuContext& ctx) {
        if (num == 4 || num == 35 || num == 37 || num == 38) {
            // Exit / ExitThread / TerminateThread / iTerminateThread: print the call site, shadow frames
            // innermost first. tid 1 exiting ends the run.
            ShadowCallStack::Frame f0{}, f1{}, f2{};
            ShadowCallStack::Top(0, f0);
            ShadowCallStack::Top(1, f1);
            ShadowCallStack::Top(2, f2);
            std::fprintf(stderr, "thread-exit: syscall %d tid=%d a0=0x%08x ra=0x%08x frames=0x%08x/ra0x%08x<-0x%08x<-0x%08x\n", num,
                         ThreadManager::Instance().CurrentTid(), ctx.cpuRegs.GPR.r[4].UL[0], ctx.cpuRegs.GPR.r[31].UL[0],
                         f0.func_addr, f0.ra, f1.func_addr, f2.func_addr);
        }
        const bool blocking = SyscallTable::IsBlockingWait(num);
        // A wall field is due and a thread sleeps on the vblank: ready it now. As the higher-priority vsync
        // thread it runs its boundary, so the next frame's VIF1 kick overlaps this thread's logic tick, as the
        // console's vblank interrupt would.
        EeYieldPoint::Instance().WakeVblankIfWallFieldDue();
        if (blocking) dmac.ObserveAtKernelWait();
        // The observation point: a blocking wait with no other READY guest thread, i.e. the console's idle
        // loop. It first waits for the pipeline to owe nothing, so the interrupt is taken on the thread the
        // console would take it on. A wait that returns at once is not a block, and neither is one while a
        // thread sleeps in WaitVblank (that sleeper wakes the moment this thread blocks).
        const auto observation_point = [&] {
            if (!blocking || EeRunToken::Instance().AnyReadyBesides(EeRunToken::Instance().CurrentTid())) return false;
            if (wait_probe && !SyscallTable::WouldBlockNow(num, ctx)) {
                ++mtvu_idle_wait_skipped;
                return false;
            }
            if (wait_probe && EeRunToken::Instance().VblankWaiters() != 0) {
                ++mtvu_idle_wait_vblank_skipped;
                return false;
            }
            return true;
        };
        // Why the idle wait happened (census mtvu_waits=): would the body park now (not if a FINISH
        // handler at this entry already signalled the semaphore), and would the park wake a vblank sleeper.
        const auto idle_cause = [&] {
            if (!SyscallTable::WouldBlockNow(num, ctx)) return WaitCause::IdleWaitNonBlocking;
            if (EeRunToken::Instance().VblankWaiters() != 0) return WaitCause::IdleWaitVblankWaiter;
            return WaitCause::IdleWaitBlocking;
        };
        for (;;) {
            // The inline drain parses the sealed prefix here (its GS events latch deferred).
            if (mtvu_stack) mtvu_stack->sync().TryDrainPending();
            EeEventQueue::Instance().DrainIfPendingOnHolder();   // foreign wakes
            // Every entry is a scheduling point: causes the pipeline latched so far enter STAT here.
            intc.CollectDeferred();
            if (dmac.CheckInterrupt()) {
                InstallDmacHandlers();
                dmac.DispatchInterrupt(ctx);
            }
            if (!intc.Pending()) {
                // No "is the worker busy" test: the worker publishes idle after its last seal, so such a test
                // could block with that seal undrained. WaitProgress returns at once when nothing is owed.
                if (mtvu_stack && observation_point()) {
                    ++mtvu_idle_wait_entries;
                    if (!mtvu_stack->sync().WaitProgress(idle_cause())) continue;   // progress made, pipeline still running
                    EeEventQueue::Instance().DrainIfPendingOnHolder();
                    intc.CollectDeferred();                             // a FINISH the pipeline latched meanwhile
                    if (intc.Pending()) continue;                       // the last drain raised a cause
                }
                break;
            }
            EeRunToken& token = EeRunToken::Instance();
            const bool defer = blocking && token.AnyReadyBesides(token.CurrentTid());
            if (isr_trace)
                std::fprintf(stderr, "isr-trace: tid=%d syscall=%d a0=%u stat=0x%x mask=0x%x %s\n", token.CurrentTid(), num,
                             ctx.cpuRegs.GPR.r[4].UL[0], intc.Stat(), intc.Mask(), defer ? "deferred" : "dispatch");
            if (defer) {
                ++syscall_entry_isrs_deferred;
                break;
            }
            const int ran = intc.DispatchPending(ctx, Intc::kSiteSyscallEntry);
            syscall_entry_isrs += static_cast<uint64_t>(ran);
            if (ran == 0) break;  // pending but refused (nested) or without a handler: nothing more to do here
        }
    });
}

void LegInterrupts::InstallDmacHandlers() {
    std::vector<DmacHandler> records;
    int next_id = 1;
    for (const SyscallTable::KernelHandler& k : SyscallTable::Instance().DmacHandlers()) {
        DmacHandler h;
        h.id = k.id;
        h.cause = k.cause;
        h.pc = k.pc;
        h.gp = k.gp;
        h.arg = k.arg;
        h.flag = k.flag;
        h.active = k.active;
        records.push_back(h);
        if (k.id >= next_id) next_id = k.id + 1;
    }
    devices_.dmac.handlers().Install(records, next_id);
}

}  // namespace ps2r::title::sly2
