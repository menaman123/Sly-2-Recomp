// iop_step.cpp -- StepPark (iop_step.h).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"

namespace ps2r::title::sly2 {

namespace {
std::function<void(const IopContext*)> g_rpc_loop_bind;
std::function<void()> g_rpc_drain;
}  // namespace

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

StepPark::Park StepPark::ReceiveMbx(IopContext& regs, Pending& pending, uint32_t out, int32_t mbx) {
    regs.gpr[4] = out;
    regs.gpr[5] = static_cast<uint32_t>(mbx);
    pending = Pending{"thmsgbx", 8, out};
    IopCallTrace::StepImport(pending.lib, pending.index, regs);
    return Park::Mbx(mbx - 1);
}

void StepPark::Resumed(IopContext& regs, Pending& pending, const Resume& r) {
    if (r.first || !pending.lib) return;
    if (pending.index == 8 && pending.lib[2] == 'm') {   // thmsgbx:8: the message's guest address at `out`, v0 = 0
        if (pending.out) IopRam::W32(pending.out, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.message)));
        regs.gpr[2] = static_cast<uint32_t>(r.result);
    } else {
        regs.gpr[2] = static_cast<uint32_t>(r.result);
    }
    IopCallTrace::StepImportExit(pending.lib, pending.index, regs.gpr[2]);
    pending = Pending{};
}

StepPark::Park StepPark::RpcLoopEnter(IopContext& regs, uint32_t qd) {
    regs.gpr[4] = qd;
    IopCallTrace::StepImport("sifcmd", 22, regs);
    if (g_rpc_loop_bind) g_rpc_loop_bind(&regs);
    return Park::Sleep();
}

void StepPark::RpcDrain() {
    if (g_rpc_drain) g_rpc_drain();
}

void StepPark::BindRpc(std::function<void(const IopContext*)> loop_bind, std::function<void()> drain) {
    g_rpc_loop_bind = std::move(loop_bind);
    g_rpc_drain = std::move(drain);
}

}  // namespace ps2r::title::sly2
