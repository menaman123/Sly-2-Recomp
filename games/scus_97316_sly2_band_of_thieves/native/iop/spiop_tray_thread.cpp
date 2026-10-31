// spiop_tray_thread.cpp -- fn_5b438 as a step body.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop_tray_thread.h"

#include <memory>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"

namespace ps2r::title::sly2 {

namespace {

using Park = iop::IopKernel::Park;
using Resume = iop::IopKernel::Resume;

struct State {
    IopContext regs;     // the thread's register file across steps (sp already moved by the frame)
    IopContext initial;  // the thread's starting register file (the entry row)
    uint32_t ctx = 0;    // s0 = a0: the module context
    int phase = 0;       // 0 loop top, 1 after the disc-sema park, 2 after the delay, 3 the exit
    StepPark::Pending pending;   // the park being waited on (its J row + v0 at the resume)
};

}  // namespace

void SpiopTrayThread::Register(NativeIopModule& spiop) { spiop.AddThread(kEntry, &SpiopTrayThread::Make, "spiop_tray_poll"); }

iop::IopKernel::StepBody SpiopTrayThread::Make(const IopContext& initial) {
    auto st = std::make_shared<State>();
    st->regs = initial;
    st->ctx = initial.gpr[4];
    // The prologue: addiu sp, -0x20 (the frame the two stack words live in).
    st->regs.gpr[29] = static_cast<uint32_t>(static_cast<int32_t>(st->regs.gpr[29]) - 0x20);
    st->initial = initial;
    return [st](const Resume& resume) -> Park {
        IopContext& r = st->regs;
        const uint32_t sp = r.gpr[29];
        if (resume.first) StepPark::Entry(kEntry, st->initial);   // the E row the seam wrapper would have written
        StepPark::Resumed(r, st->pending, resume);                // v0 + the J row of the park just satisfied
        for (;;) {
            switch (st->phase) {
                case 0: {
                    if (IopRam::R32(st->ctx) != 0) {
                        // The shutdown exit (never reached on the lanes: the module is not unloaded). The coroutine
                        // form may yield inside ChangeThreadPriority when a higher-priority thread is Ready; the
                        // step form signals and exits first -- the one ordering this form does not reproduce.
                        IopServices::ChangeThreadPriority(r, 0, 9);
                        st->phase = 3;
                        continue;
                    }
                    const uint32_t sema = IopRam::R32(st->ctx + 0x30);
                    IopRam::W32(sp + 0x10, sema);
                    st->phase = 1;
                    if (static_cast<int32_t>(sema) >= 0) return StepPark::WaitSema(r, st->pending, static_cast<int32_t>(sema));
                    continue;
                }
                case 1: {
                    IopRam::W32(sp + 0x14, 0);
                    if (IopServices::Import(r, "cdvdman", 14, 2, sp + 0x14) != 0)   // sceCdTrayReq(2, &tray)
                        IopRam::W32(st->ctx + 0x2c, IopRam::R32(sp + 0x14));
                    const uint32_t sema = IopRam::R32(sp + 0x10);
                    if (static_cast<int32_t>(sema) >= 0) IopServices::SignalSema(r, static_cast<int32_t>(sema));
                    st->phase = 2;
                    return StepPark::DelayThread(r, st->pending, 100000);
                }
                case 2:
                    st->phase = 0;
                    continue;
                case 3:
                default:
                    IopServices::SignalSema(r, static_cast<int32_t>(IopRam::R32(st->ctx + 0x24)));
                    return Park::Exit();
            }
        }
    };
}

}  // namespace ps2r::title::sly2
