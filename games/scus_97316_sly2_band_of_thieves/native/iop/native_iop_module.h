// native_iop_module.h -- NativeIopModule: a hosted IOP module of this title implemented, function by
// function, in host C++ over the module's OWN IOP RAM image.
//
// THE FRAME (native/iop/README.md). The four title modules the hosted IOP still runs as
// recompiled MIPS (FILEIO_service, lgAud, 989snd_Library, spiop) are generated with the per-function seam
// (`ps2r_iop_recompiler --seam`: engine/iop/abi/host_app/iop_seam.h): every guest function is reached
// through its slot in the module's `iop_fn_slots[]` table -- a JAL site, a JR/JALR, a thread entry, an RPC
// handler, a hard-timer or DMA callback alike -- and a slot holding a native body runs it INSTEAD of the
// recompiled one, on the same IopContext, the same IOP RAM, the same guest stack. So:
//   * the module's export table, thread entries, RPC registrations, timer handlers need NO re-registration:
//     the hosted kernel (IopKernel + thbase/thsemap/thevent/sifcmd/timrman) keeps every record; only the
//     BODY at an address is swapped;
//   * the module's RAM region stays allocated and IS its state (the rule: exact by construction
//     over the module's RAM layout) -- a native body and the recompiled body are interchangeable at any
//     call, and the recompiled body is the fallback and the oracle;
//   * a module is ported one function at a time: each candidate is `Add()`ed by address; `Install()` puts
//     the admitted ones (NativeIopFilter: the NATIVEEE_IOP_NATIVE spec, for bisecting on the lane) into the
//     slots; the census reads `native_iop=<id>:<native fns>/<total>:<native calls>/<recompiled calls>`.
//
// A native module derives (or just fills) a NativeIopModule: `Add(0x42f58, &Thunk<Snd989, &Snd989::Lock>,
// "lock")`. The body gets the IopContext (a0-a3 in gpr[4..7], the result goes to gpr[2]) and `self`.
// Services (kernel calls, imports, other guest functions, the module's RAM words): iop_services.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <functional>

#include "engine/iop/abi/host_app/iop_seam.h"
#include "engine/iop/iop_context.h"
#include "engine/iop/iop_kernel.h"

namespace ps2r::title::sly2 {

// NATIVEEE_IOP_NATIVE=<spec>: which candidates Install() admits (the per-function gate on the lane).
//   (unset) / all            every candidate of every module
//   none                     no candidate (the recompiled bodies everywhere: the oracle arm)
//   <mod>[:a,b,...][;...]    only these modules; within a module only the listed addresses (hex) when given
//   -<mod>[:a,b,...][;...]   every candidate EXCEPT these modules / addresses
// Example: NATIVEEE_IOP_NATIVE='snd989:0x42f58,0x430dc;spiop'   or   NATIVEEE_IOP_NATIVE='-snd989:0x3497c'
class NativeIopFilter {
public:
    NativeIopFilter() = default;                      // = all
    explicit NativeIopFilter(const char* spec);       // nullptr / "" = all
    static NativeIopFilter FromEnvironment();         // NATIVEEE_IOP_NATIVE
    bool Admits(const char* module_id, uint32_t addr) const;
    const std::string& Spec() const { return spec_; }

private:
    struct Rule {
        std::string module;
        std::vector<uint32_t> addrs;   // empty = the whole module
    };
    std::string spec_;
    bool all_ = true, none_ = false, exclude_ = false;
    std::vector<Rule> rules_;
};

class NativeIopModule {
public:
    // The generated TU's slot table (iop_<id>::iop_fn_slots / kFunctionCount), bound by the composition
    // root (hle/iop_host.cpp includes the generated headers; this TU is portable and does not).
    struct Table {
        iop_seam::IopFnSlot* slots = nullptr;
        uint32_t count = 0;
    };
    struct Candidate {
        uint32_t addr;
        iop_seam::NativeFn fn;      // the self form, or nullptr when `plain` is set
        iop_seam::PlainFn plain;    // the plain `void(IopContext&)` form
        void* self;
        const char* name;
        bool installed;
    };

    NativeIopModule(const char* id, uint32_t base, uint32_t extent);
    virtual ~NativeIopModule();
    NativeIopModule(const NativeIopModule&) = delete;
    NativeIopModule& operator=(const NativeIopModule&) = delete;

    const char* Id() const { return id_; }
    uint32_t Base() const { return base_; }
    uint32_t Extent() const { return extent_; }
    bool Owns(uint32_t addr) const { return addr >= base_ && addr < extent_; }

    void Bind(Table t) { table_ = t; }
    const Table& Bound() const { return table_; }

    // A native body for the guest function at `addr` (`self` defaults to this module) -- or the plain
    // `void(IopContext&)` form (a free function over the module's RAM; no object).
    void Add(uint32_t addr, iop_seam::NativeFn fn, const char* name, void* self = nullptr);
    void Add(uint32_t addr, iop_seam::PlainFn fn, const char* name);
    // Member-function trampoline: Add(addr, &Thunk<T, &T::Body>, name, this).
    template <class T, void (T::*Body)(IopContext&)>
    static void Thunk(IopContext& ctx, void* self) { (static_cast<T*>(self)->*Body)(ctx); }

    // A thread ENTRY in the step form (engine/iop/iop_kernel.h: a body that returns its Park instead of
    // blocking; no coroutine). Asked at every thbase StartThread of a thread whose entry is `entry` with the
    // thread's initial IopContext ($sp/$gp/$a0); admitted by the same filter as the function bodies. A module
    // whose every thread entry has a step factory runs without a coroutine switch (iop_dispatch= census:
    // coro_switches=0 for it). The function body at `entry` may ALSO be Add()ed: it serves the coroutine form
    // (knob off / not admitted) and any direct call.
    using StepFactory = std::function<iop::IopKernel::StepBody(const IopContext& initial)>;
    void AddThread(uint32_t entry, StepFactory factory, const char* name);
    // The step body for `entry` when admitted and registered; empty otherwise (-> the coroutine body).
    iop::IopKernel::StepBody StepBodyFor(uint32_t entry, const IopContext& initial) const;
    size_t StepThreads() const { return threads_.size(); }

    // Installs the admitted candidates into the bound table; returns how many. A candidate whose address
    // is not a generated function start is reported (stderr) and skipped. Uninstall() restores every slot.
    int Install(const NativeIopFilter& filter);
    void Uninstall();
    bool Installed() const { return installed_ > 0; }

    const std::vector<Candidate>& Candidates() const { return candidates_; }
    // "<id>:<native fns>/<total>:<native calls>/<recompiled calls>" over the bound table.
    std::string Census() const;
    uint32_t NativeFunctions() const;
    uint64_t NativeCalls() const;
    uint64_t RecompiledCalls() const;
    // The functions the lane still reached as recompiled bodies (addr -> calls), most-called first:
    // what remains to port.
    std::vector<std::pair<uint32_t, uint64_t>> RemainingCalls() const;

private:
    const char* id_;
    uint32_t base_, extent_;
    Table table_;
    struct ThreadEntry {
        uint32_t entry;
        StepFactory factory;
        const char* name;
    };
    std::vector<Candidate> candidates_;
    std::vector<ThreadEntry> threads_;
    const NativeIopFilter* filter_ = nullptr;   // the filter of the last Install (StepBodyFor's admission)
    int installed_ = 0;
};

}  // namespace ps2r::title::sly2
