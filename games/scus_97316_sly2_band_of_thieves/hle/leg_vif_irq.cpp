// games/scus_97316_sly2_band_of_thieves/hle/leg_vif_irq.cpp -- LegVifIrq (leg_vif_irq.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_vif_irq.h"

#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/intc/intc.h"
#include "engine/vif/vif.h"
#include "engine/vu/tiers/mtvu_sync.h"
#include "engine/vu/tiers/mtvu_worker.h"

namespace ps2r::title::sly2 {

LegVifIrq::LegVifIrq(LegCore& core, LegDevices& devices, LegMtvu& mtvu) : core_(core), devices_(devices), mtvu_(mtvu) {
    devices.vif.SetIrqHooks(&LegVifIrq::OnIrq, &LegVifIrq::OnStc, this);
    if (mtvu.mtvu) mtvu.mtvu->sync().SetIrqStallService(&LegVifIrq::Service, this);
}

LegVifIrq::~LegVifIrq() {
    devices_.vif.SetIrqHooks(nullptr, nullptr, nullptr);
    if (mtvu_.mtvu) mtvu_.mtvu->sync().SetIrqStallService(nullptr, nullptr);
}

// The running guest thread's context, or null off the guest's threads.
namespace {
CpuContext* CurrentGuestContext() {
    const std::shared_ptr<ThreadInfo> info = ThreadManager::Instance().Current();
    return info ? info->ctx : nullptr;
}
}  // namespace

bool LegVifIrq::DispatchHere(int cause) {
    CpuContext* ctx = CurrentGuestContext();
    if (!ctx) return false;
    return core_.intc.Dispatch(*ctx, cause, Intc::kSiteOther);
}

void LegVifIrq::OnIrq(void* user, int vif) {
    auto* self = static_cast<LegVifIrq*>(user);
    Intc& intc = self->core_.intc;
    const int cause = vif == 1 ? Intc::kVif1 : Intc::kVif0;
    MtvuStack* stack = self->mtvu_.mtvu.get();
    if (vif == 1 && stack && stack->worker().OnWorkerThread()) {
        // The worker publishes the stall, latches the cause, parks until STC (or Stop).
        ++self->stalls;
        stack->worker().EnterIrqStall([](void* u) { static_cast<Intc*>(u)->RaiseDeferred(Intc::kVif1); }, &intc);
        return;
    }
    // Inline walk on the guest thread: the handler runs now (the stall is this nested call).
    if (!CurrentGuestContext()) {
        ++self->inline_refused;
        return;
    }
    intc.Raise(cause);
    if (self->DispatchHere(cause)) ++self->inline_isrs;
    else ++self->inline_refused;
}

void LegVifIrq::OnStc(void* user, int vif) {
    auto* self = static_cast<LegVifIrq*>(user);
    if (vif == 1 && self->mtvu_.mtvu) self->mtvu_.mtvu->worker().ReleaseIrqStall();
}

int LegVifIrq::Service(void* user) {
    auto* self = static_cast<LegVifIrq*>(user);
    Intc& intc = self->core_.intc;
    if (intc.InDispatch()) {
        ++self->sync_refused;
        return -1;
    }
    intc.CollectDeferred();
    if (!intc.Pending(Intc::kVif1)) {
        ++self->sync_not_pending;
        return 0;
    }
    if (!self->DispatchHere(Intc::kVif1)) {
        ++self->sync_refused;
        return -1;
    }
    ++self->sync_isrs;
    return 1;
}

}  // namespace ps2r::title::sly2
