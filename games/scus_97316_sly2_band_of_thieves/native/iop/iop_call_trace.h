// iop_call_trace.h -- IopCallTrace: the per-call trace of the hosted IOP modules' guest functions and their
// library imports (NATIVEEE_IOP_TRACE=<file>) -- the module tracks' per-function oracle.
//
// Bound on the seam's two hooks (iop_seam::trace / iop_seam::import_trace, engine/iop/abi/host_app/iop_seam.h):
// every guest function entry/exit of every `--seam` module and every `import_call` entry/exit, with the IOP
// clock. Run the lane with a function's recompiled body (NATIVEEE_IOP_NATIVE=none) and with its native body,
// diff the two traces with iop_trace_compare.py: the first divergent row names the call whose port is wrong
// (the native body itself when its exit row differs; an inner service when an I/J row differs).
//
// Rows (text, one event per line; hex without 0x; `n`/`r` = the body that ran, native / recompiled):
//   # iop_call_trace v1 <modules...>
//   E <seq> <mod> <addr> <n|r> <a0> <a1> <a2> <a3> <sp> <ra> <t_us> <tid> <depth>     guest function entry
//   X <seq> <mod> <addr> <n|r> <v0> <v1> <t_us>                                      guest function exit
//   I <seq> <lib>:<idx> <a0> <a1> <a2> <a3> <t_us> <depth>                             import entry
//   J <seq> <lib>:<idx> <v0> <t_us>                                                    import exit
// Filters: NATIVEEE_IOP_TRACE_MODULES=snd989,spiop (default: every module), NATIVEEE_IOP_TRACE_IMPORTS=0
// (default 1: imports traced), NATIVEEE_IOP_TRACE_MAX_DEPTH=N (default: unlimited; the depth is the guest
// call nesting since the kernel entered the module). The exit row of a body that never returns (a thread
// loop that parks forever) is never written.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "engine/iop/abi/host_app/iop_seam.h"
#include "engine/iop/iop_context.h"

namespace ps2r::title::sly2 {

class IopCallTrace {
public:
    struct Module {
        const char* id;
        uint32_t base, extent;
    };
    struct Deps {
        std::vector<Module> modules;                 // address -> module id
        std::function<uint64_t()> now_us;            // the IOP kernel clock
        std::function<int()> current_tid;            // the running guest thread (0 = a host-run callback)
    };
    // Opens `path` and binds the hooks; false (nothing bound) when the file cannot be opened.
    static bool Open(const char* path, Deps deps);
    static void Close();
    static bool IsOpen() { return file_ != nullptr; }
    static uint64_t Rows() { return rows_; }

private:
    static void OnCall(const iop_seam::IopFnSlot& slot, bool native, bool entry, iop::IopContext& ctx);
    static void OnImport(const char* lib, uint32_t index, bool entry, iop::IopContext& ctx);
    static const char* ModuleOf(uint32_t addr);
    static bool ModuleTraced(const char* id);

    static std::FILE* file_;
    static Deps deps_;
    static std::vector<std::string> only_modules_;
    static bool imports_;
    static int max_depth_;
    static int depth_;
    static uint64_t seq_;
    static uint64_t rows_;
};

}  // namespace ps2r::title::sly2
