// iop_step.cpp -- StepPark (iop_step.h).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"

namespace ps2r::title::sly2 {

void StepPark::Entry(uint32_t entry, const IopContext& initial) { IopCallTrace::StepEntry(entry, initial); }

StepPark::Park StepPark::WaitSema(IopContext& regs, Pending& pending, int32_t sid) {
    regs.gpr[4] = static_cast<uint32_t>(sid);
    pending = Pending{"thsemap", 8};
    IopCallTrace::StepImport(pending.lib, pending.index, regs);
    return Park::Sema(sid);
}

StepPark::Park StepPark::DelayThread(IopContext& regs, Pending& pending, uint32_t usec) {
    regs.gpr[4] = usec;
    pending = Pending{"thbase", 33};
    IopCallTrace::StepImport(pending.lib, pending.index, regs);
    return Park::Delay(usec);
}

StepPark::Park StepPark::SleepThread(IopContext& regs, Pending& pending) {
    pending = Pending{"thbase", 24};
    IopCallTrace::StepImport(pending.lib, pending.index, regs);
    return Park::Sleep();
}

void StepPark::Resumed(IopContext& regs, Pending& pending, const Resume& r) {
    if (r.first || !pending.lib) return;
    regs.gpr[2] = static_cast<uint32_t>(r.result);
    IopCallTrace::StepImportExit(pending.lib, pending.index, regs.gpr[2]);
    pending = Pending{};
}

}  // namespace ps2r::title::sly2
