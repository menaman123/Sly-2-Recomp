// native_iop_frame.h -- NativeIopFrame: the four hosted modules as NativeIopModule objects, the knob, the
// filter, the trace and the census -- what hle/boot_leg.cpp installs and the module tracks plug into.
//
//   title.native.iopsys (Config, default off, fingerprinted): when on, every admitted native body is
//     installed into its module's seam slot (NativeIopModule::Install); off = the recompiled bodies run
//     everywhere (the oracle arm; the seam's counters still count).
//   NATIVEEE_IOP_NATIVE=<spec>: the per-function admission (NativeIopFilter) for bisecting on the lane.
//   NATIVEEE_IOP_TRACE=<file>: the call trace (IopCallTrace), on whether or not the knob is.
//
// The module tracks register their bodies in RegisterNativeIopBodies() (native_iop_bodies.cpp: one line per
// module -- `RegisterSnd989(frame.Snd989())` ...), each body `Add()`ed by the address of the recompiled
// function it replaces. The census row `native_iop=` lists every module as
// `<id>:<native fns>/<total>:<native calls>/<recompiled calls>`; the goal state reads N/0 on every lane.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

class NativeIopFrame {
public:
    struct Deps {
        bool enabled = false;                           // title.native.iopsys
        std::vector<NativeIopModule::Table> tables;     // one per module below, in the order of Modules() (unbound = empty)
        const char* trace_path = nullptr;               // NATIVEEE_IOP_TRACE
        IopCallTrace::Deps trace;                       // the clock / tid for the trace rows
        // IopHost::SetStepProvider: the frame binds the step-thread provider (a module's AddThread entries).
        std::function<void(std::function<iop::IopKernel::StepBody(uint32_t entry, const IopContext& initial)>)> set_step_provider;
        // The step-form RPC server loop (iop_step.h StepPark::RpcLoopEnter / RpcDrain): bind the running thread's
        // register file as its sceSifRpcLoop context (SifcmdModule::SetLoopContext(kernel.CurrentTid(), ctx)) and
        // drain the running thread's RPC queue once (SifRpcBridge::RpcServiceDrain).
        std::function<void(const IopContext* loop_ctx)> rpc_loop_bind;
        std::function<void()> rpc_drain;
    };
    explicit NativeIopFrame(Deps deps);
    ~NativeIopFrame();
    NativeIopFrame(const NativeIopFrame&) = delete;
    NativeIopFrame& operator=(const NativeIopFrame&) = delete;

    // The four modules, in iop_modules.h order: fileio, lgaud, snd989, spiop.
    NativeIopModule& Fileio() { return *modules_[0]; }
    NativeIopModule& Lgaud() { return *modules_[1]; }
    NativeIopModule& Snd989() { return *modules_[2]; }
    NativeIopModule& Spiop() { return *modules_[3]; }
    const std::vector<std::unique_ptr<NativeIopModule>>& Modules() const { return modules_; }
    NativeIopModule* ModuleById(const char* id);

    // Installs the registered bodies (knob on, filter applied); returns the installed count over all modules.
    int Install();
    void Uninstall();
    bool Enabled() const { return deps_.enabled; }
    const NativeIopFilter& Filter() const { return filter_; }

    // run.meta rows: native_iop=<module census>,... ; native_iop_filter=<spec>; native_iop_trace_rows=<n>
    std::string Census() const;
    // The functions still reached as recompiled bodies, per module, most-called first (addr:calls ...).
    std::string Remaining(size_t per_module = 12) const;
    // Every slot of every module: "<id> <addr> <end> <native?> <native_calls> <recompiled_calls> <name>" one per line
    // (the reachability census: a slot with 0/0 over every leg is a function no lane reaches). Written to `path`
    // (NATIVEEE_IOP_SLOT_REPORT at the census; the file is run-derived -> keep it under the agent data dir).
    bool WriteSlotReport(const char* path) const;

private:
    Deps deps_;
    NativeIopFilter filter_;
    std::vector<std::unique_ptr<NativeIopModule>> modules_;
    int installed_ = 0;
};

// native_iop_bodies.cpp: the module tracks' registrations (each adds its module's bodies to the frame).
void RegisterNativeIopBodies(NativeIopFrame& frame);

}  // namespace ps2r::title::sly2
