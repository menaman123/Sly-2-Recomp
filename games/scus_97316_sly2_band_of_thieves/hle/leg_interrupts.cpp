// games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.cpp -- LegInterrupts (leg_interrupts.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_interrupts.h"

#include <cstdio>
#include <vector>

#include "engine/core/threads/park.h"
#include "engine/dmac/dmac_handler_registry.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/sched/ee_run_token.h"
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
            std::fprintf(stderr, "gs-irq: tid=%d event=0x%x imr=0x%llx %s path=%d p1=%llu p2=%llu p3=%llu\n",
                         EeRunToken::Instance().CurrentTid(), csr_event,
                         static_cast<unsigned long long>(SyscallTable::Instance().GsImr()), masked ? "masked" : "raise",
                         static_cast<int>(gif_parser.ActivePath()),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path1)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path2)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path3)));
        if (masked) {
            ++gs_irq_masked;
            return;
        }
        intc.Raise(Intc::kGs);
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
        const bool blocking = SyscallTable::IsBlockingWait(num);
        if (blocking) dmac.ObserveAtKernelWait();
        for (;;) {
            if (mtvu_stack) mtvu_stack->sync().TryDrainPending();
            if (dmac.CheckInterrupt()) {
                InstallDmacHandlers();
                dmac.DispatchInterrupt(ctx);
            }
            if (!intc.Pending()) {
                // No "is the worker busy" test here: the worker publishes idle AFTER its last seal,
                // so a busy test that read idle could block with that marker undrained; WaitProgress
                // answers at once (after one drain) when nothing is outstanding.
                if (blocking && mtvu_stack && !EeRunToken::Instance().AnyReadyBesides(EeRunToken::Instance().CurrentTid())) {
                    ++mtvu_idle_wait_entries;
                    if (!mtvu_stack->sync().WaitProgress()) continue;   // progress delivered, the worker still runs
                    if (intc.Pending()) continue;                       // the last drain raised a cause
                }
                break;
            }
            EeRunToken& token = EeRunToken::Instance();
            const bool defer = blocking && token.AnyReadyBesides(token.CurrentTid());
            if (isr_trace)
                std::fprintf(stderr, "isr-trace: tid=%d syscall=%d stat=0x%x mask=0x%x %s\n", token.CurrentTid(), num,
                             intc.Stat(), intc.Mask(), defer ? "deferred" : "dispatch");
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
