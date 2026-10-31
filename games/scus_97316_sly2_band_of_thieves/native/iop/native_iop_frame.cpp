// native_iop_frame.cpp -- NativeIopFrame.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_frame.h"

#include <cstdio>
#include <cstring>

#include "games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"

namespace ps2r::title::sly2 {

NativeIopFrame::NativeIopFrame(Deps deps) : deps_(std::move(deps)), filter_(NativeIopFilter::FromEnvironment()) {
    const IopModules::Module order[] = {IopModules::kFileio, IopModules::kLgaud, IopModules::kSnd989, IopModules::kSpiop};
    for (size_t i = 0; i < 4; ++i) {
        modules_.push_back(std::make_unique<NativeIopModule>(order[i].id, order[i].base, order[i].extent));
        if (i < deps_.tables.size()) modules_.back()->Bind(deps_.tables[i]);
    }
    StepPark::BindRpc(deps_.rpc_loop_bind, deps_.rpc_drain);
    RegisterNativeIopBodies(*this);
    if (deps_.trace_path && *deps_.trace_path) {
        IopCallTrace::Deps td = deps_.trace;
        if (td.modules.empty())
            for (const auto& m : modules_) td.modules.push_back(IopCallTrace::Module{m->Id(), m->Base(), m->Extent()});
        IopCallTrace::Open(deps_.trace_path, std::move(td));
    }
    if (deps_.enabled) Install();
    // The step-thread provider: a thread entry registered by AddThread on the module that owns the address
    // (admitted by the filter when the knob is on; empty = the coroutine form).
    if (deps_.set_step_provider) {
        deps_.set_step_provider([this](uint32_t entry, const IopContext& initial) -> iop::IopKernel::StepBody {
            for (auto& m : modules_)
                if (m->Owns(entry)) return m->StepBodyFor(entry, initial);
            return {};
        });
    }
}

NativeIopFrame::~NativeIopFrame() {
    Uninstall();
    IopCallTrace::Close();
}

NativeIopModule* NativeIopFrame::ModuleById(const char* id) {
    for (auto& m : modules_)
        if (std::strcmp(m->Id(), id) == 0) return m.get();
    return nullptr;
}

int NativeIopFrame::Install() {
    installed_ = 0;
    for (auto& m : modules_) installed_ += m->Install(filter_);
    return installed_;
}

void NativeIopFrame::Uninstall() {
    for (auto& m : modules_) m->Uninstall();
    installed_ = 0;
}

std::string NativeIopFrame::Census() const {
    std::string s;
    for (const auto& m : modules_) {
        if (!s.empty()) s += ',';
        s += m->Census();
    }
    return s;
}

std::string NativeIopFrame::Remaining(size_t per_module) const {
    std::string s;
    for (const auto& m : modules_) {
        const auto rem = m->RemainingCalls();
        if (rem.empty()) continue;
        if (!s.empty()) s += ',';
        s += std::string(m->Id()) + ":" + std::to_string(rem.size()) + "fns";
        size_t n = 0;
        for (const auto& [addr, calls] : rem) {
            if (n++ >= per_module) break;
            char buf[48];
            std::snprintf(buf, sizeof(buf), ":%x=%llu", addr, static_cast<unsigned long long>(calls));
            s += buf;
        }
    }
    return s.empty() ? "none" : s;
}

}  // namespace ps2r::title::sly2
