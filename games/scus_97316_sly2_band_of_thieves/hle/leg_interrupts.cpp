// games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.cpp -- LegInterrupts (leg_interrupts.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

namespace ps2r::title::sly2 {

LegInterrupts::LegInterrupts(const LegKnobs& knobs, LegCore& core, LegGs& gs, LegDevices& devices, LegMtvu& mtvu)
    : devices_(devices) {
    Dmac& dmac = devices.dmac;
    std::unique_ptr<MtvuStack>& mtvu_stack = mtvu.mtvu;
    Intc& intc = core.intc;
    // The deferred VIF1 kick's observation points at the frame boundary / sceGsSyncPath.
    // Under MTVU the chase is followed by the frame sync: the frame's VIF1 feed is complete; the
    // terminal Sync(SceGsSyncV) finishes every queued span (the chase's resumed END slots included)
    // before the boundary runs. Absent the stack both hooks are exactly the chase / the flush.
    GsSync::BindDmac(
        [&dmac, &mtvu_stack] {
            dmac.ChaseVif1Tail();
            if (mtvu_stack) mtvu_stack->FrameSync(GsSync::SyncVCount(), ps2r::Park::NowNs());
        },
        [&dmac, &mtvu_stack] {
            // "Is the path idle": with boundaries not deferred this is a real wait (the stack's
            // SiteSync would skip it under the deferral rule); unbound in this title's manifest.
            if (mtvu_stack) mtvu_stack->sync().Sync(SyncSite::SceGsSyncPath);
            dmac.FlushDeferredVif1();
        });
    // The GS interrupt: SIGNAL / FINISH count on GsPrivRegs as the general register is written
    // (on the kicking guest thread, so STAT is set in place); the IMR view is the kernel's
    // GsPutIMR word (syscall 113; bit 8 SIGMSK, bit 9 FINISHMSK). Delivered at the next
    // dispatch point: the syscall-entry hook below, a back-edge yield, or the boundary.
    // NATIVEEE_ISR_TRACE=1 (boot triage): one stderr line per GS SIGNAL / FINISH event -- the
    // kicking tid, the kernel's IMR word and whether the cause was raised or masked.
    const bool isr_trace = knobs.isr_trace;
    GifParser& gif_parser = devices.gif_parser;
    gs.gs_state.Priv().SetInterruptObserver([this, isr_trace, &gif_parser, &intc](uint32_t csr_event) {
        ++gs_irq_events;
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
        // Deferred (E422): the parser that reaches the register runs behind the VU1 worker (and
        // on the drain thread); the cause enters INTC_STAT at the holder's next OBSERVATION point
        // (below: a blocking wait with no other READY thread; the kernel idle; a field tick; the
        // boundary; a completed sync) -- never at a busy scheduling point, where the host's
        // pipeline timing would pick the guest instruction the interrupt lands behind.
        intc.RaiseDeferred(Intc::kGs);
        ++gs_irq_raises;
    });
    // The interrupt taken at the next kernel call: every syscall's entry dispatches the pending
    // enabled INTC causes (and the DMAC's INT1 handlers) on the calling thread before the
    // syscall's body runs -- a GS FINISH raised in-line by the kick above reaches the game's GS
    // handler before its WaitSema would block forever. Intc's in-dispatch guard keeps an ISR's own
    // kernel calls (iSignalSema) from recursing into the dispatch.
    // A BLOCKING kernel wait is also a DMA observation point (Dmac::ObserveAtKernelWait): the
    // thread that kicked a VIF1 chain / PATH3 upload and now sleeps for the FINISH handler's
    // semaphore gives the bus to the DMA; the parked chain is walked and the arbiter's PATH3
    // backlog reaches the GS. The FINISH the observer raises is then pending in INTC_STAT, and
    // an interrupt runs on WHICHEVER thread is running when it lands: if another thread is READY,
    // the waiter blocks and the ISR is taken at that thread's next dispatch point (its next
    // kernel call, back-edge yield or boundary) -- the DMA's latency shows up as the other
    // threads' run time, as on the console. Taking it here instead would complete the kick
    // instantaneously and return the waiter before it ever blocked: a high-priority render /
    // frame-pacing thread pair then never both wait and the lower-priority main thread starves
    // (Sly 2's level loader after the intro FMV). Only when nothing else is READY -- the kernel's
    // idle loop would take the interrupt -- is it dispatched here, before the block.
    // The entry LOOPS: a handler's exit (EeRunToken::IsrEnd) may hand the token to the thread it
    // readied; when this thread is re-elected it is still at this entry, about to block, and the
    // other threads counted it READY while they deferred -- so the causes raised meanwhile are its
    // to take now. Without the re-check they would stay pending with every thread asleep.
    // NATIVEEE_ISR_TRACE also prints one line per syscall entry that finds an INTC cause
    // pending -- tid, syscall, the pending/mask words and the decision.
    // Under MTVU the chain the observer flushes is STAGED to the worker and its FINISH arrives later,
    // through the GIF drain on this thread: every entry drains the sealed prefix first (the packets
    // the worker finished since the last entry reach the GS, their FINISH / SIGNAL the INTC), and a
    // BLOCKING wait that finds no other READY thread while the worker still runs idles on the
    // worker's progress (MtvuSync::WaitProgress: seal / idle, drain, re-check) instead of blocking
    // with nothing left to deliver the interrupt -- the console's idle loop taking the GS interrupt.
    SyscallTable::Instance().SetPreDispatch([this, isr_trace, &dmac, &mtvu_stack, &intc](int num, CpuContext& ctx) {
        if (num == 4 || num == 35 || num == 37 || num == 38) {
            // Exit / ExitThread / TerminateThread / iTerminateThread: the site (a thread exit ends the leg when
            // it is tid 1's) -- one line each, shadow frames innermost first.
            ShadowCallStack::Frame f0{}, f1{}, f2{};
            ShadowCallStack::Top(0, f0);
            ShadowCallStack::Top(1, f1);
            ShadowCallStack::Top(2, f2);
            std::fprintf(stderr, "thread-exit: syscall %d tid=%d a0=0x%08x ra=0x%08x frames=0x%08x/ra0x%08x<-0x%08x<-0x%08x\n", num,
                         ThreadManager::Instance().CurrentTid(), ctx.cpuRegs.GPR.r[4].UL[0], ctx.cpuRegs.GPR.r[31].UL[0],
                         f0.func_addr, f0.ra, f1.func_addr, f2.func_addr);
        }
        const bool blocking = SyscallTable::IsBlockingWait(num);
        // A wall field is due (Play: the pacer ticked while this thread ran) and a thread sleeps on
        // the vblank: it is readied here and, being the higher-priority vsync thread, runs its
        // boundary now -- the next frame's VIF1 kick overlaps the rest of this thread's logic
        // tick, as the console's vblank interrupt would have it (E421; census ee_yield wall_wakes).
        EeYieldPoint::Instance().WakeVblankIfWallFieldDue();
        // TEMP-PIPE protocol trace (removed before the report)
        static const bool proto_trace = std::getenv("NATIVEEE_PROTO_TRACE") != nullptr;
        static uint64_t proto_n = 0;
        const bool proto_line = proto_trace && proto_n < 200000 && (num == 66 || num == 67 || num == 68 || num == 69 || num == 32 || num == 33);
        if (proto_line) {
            ++proto_n;
            std::fprintf(stderr, "proto: t=%llu tid=%d sc=%d a0=%u seals=%llu idle=%d\n",
                         (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                         EeRunToken::Instance().CurrentTid(), num, ctx.cpuRegs.GPR.r[4].UL[0],
                         mtvu_stack ? (unsigned long long)mtvu_stack->worker().SealCount() : 0ull,
                         mtvu_stack ? (mtvu_stack->worker().WorkerIdle() ? 1 : 0) : -1);
        }
        if (blocking) dmac.ObserveAtKernelWait();
        if (proto_line && blocking) {
            std::fprintf(stderr, "proto: t=%llu tid=%d observed sc=%d a0=%u seals=%llu idle=%d\n",
                         (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                         EeRunToken::Instance().CurrentTid(), num, ctx.cpuRegs.GPR.r[4].UL[0],
                         mtvu_stack ? (unsigned long long)mtvu_stack->worker().SealCount() : 0ull,
                         mtvu_stack ? (mtvu_stack->worker().WorkerIdle() ? 1 : 0) : -1);
        }
        // The observation point (E422): a blocking wait that finds no other READY guest thread
        // -- the console's idle loop. Only here (and at the kernel idle, a field tick, the
        // boundary, a completed sync) do the GS causes the pipeline latched enter INTC_STAT: at a
        // busy entry the pipeline's host timing would otherwise pick the guest instruction the
        // FINISH lands behind (the inline lane's own race: a frame's three FINISHes landed on
        // tid 1's WaitSema(7) in one run and on tid 3's WaitSema(6) in the next).
        const auto observation_point = [&] {
            return blocking && !EeRunToken::Instance().AnyReadyBesides(EeRunToken::Instance().CurrentTid());
        };
        {   // TEMP-PIPE trace
            static const bool tr = std::getenv("NATIVEEE_PIPE_TRACE") != nullptr;
            if (tr && intc.DeferredPending())
                std::fprintf(stderr, "pipe: t=%lld entry-latched tid=%d sc=%d a0=%u blocking=%d others=%d\n",
                    (long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                    EeRunToken::Instance().CurrentTid(), num, ctx.cpuRegs.GPR.r[4].UL[0], blocking ? 1 : 0,
                    EeRunToken::Instance().AnyReadyBesides(EeRunToken::Instance().CurrentTid()) ? 1 : 0);
        }
        for (;;) {
            // The inline drain parses the sealed prefix here (its GS events latch deferred).
            if (mtvu_stack) mtvu_stack->sync().TryDrainPending();
            EeEventQueue::Instance().DrainIfPendingOnHolder();   // foreign wakes
            if (observation_point()) intc.CollectDeferred();
            if (dmac.CheckInterrupt()) {
                InstallDmacHandlers();
                dmac.DispatchInterrupt(ctx);
            }
            if (!intc.Pending()) {
                // No "is the worker busy" test here: the worker publishes idle AFTER its last seal,
                // so a busy test that read idle could block with that marker undrained; WaitProgress
                // answers at once (after one drain) when nothing is outstanding.
                if (mtvu_stack && observation_point()) {
                    ++mtvu_idle_wait_entries;
                    if (!mtvu_stack->sync().WaitProgress()) continue;   // progress delivered, the pipeline still runs
                    EeEventQueue::Instance().DrainIfPendingOnHolder();
                    intc.CollectDeferred();                             // a FINISH the pipeline latched meanwhile
                    if (intc.Pending()) continue;                       // the last drain raised a cause
                    {   // TEMP-PIPE trace
                        static const bool tr = std::getenv("NATIVEEE_PIPE_TRACE") != nullptr;
                        if (tr) std::fprintf(stderr, "pipe: t=%lld spinner-exit tid=%d sc=%d a0=%u worker_idle=%d arb_pending=%d drain_working=%d\n",
                            (long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                            EeRunToken::Instance().CurrentTid(), num, ctx.cpuRegs.GPR.r[4].UL[0], mtvu_stack->worker().WorkerIdle() ? 1 : 0,
                            devices_.gif_arbiter.hasPending() ? 1 : 0, mtvu_stack->drain().Working() ? 1 : 0);
                    }
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
