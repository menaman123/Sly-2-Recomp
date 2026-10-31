// iop_step.h -- what a STEP-form thread body (NativeIopModule::AddThread; engine/iop/iop_kernel.h Park/Resume)
// uses where the coroutine body called a blocking kernel service: the park TYPED after the service, with the
// register-file and trace effects the import would have had.
//
// THE ONE DIFFERENCE between the two forms is the park: the coroutine body calls `WaitSema(sid)` through the
// seam's import_call (an I row in NATIVEEE_IOP_TRACE, a0 = sid, the coroutine switched out, v0 = the wait's
// result and a J row when it comes back); the step body RETURNS Park::Sema(sid) and is called again with
// Resume{result}. Everything else (the module's RAM writes, the non-blocking services, the sibling calls) is
// the same code. So a step body's parks go through here:
//     return StepPark::WaitSema(st, r, sid);          // a0 = sid, the I row, Park::Sema(sid)
//     ...on the next call: StepPark::Resumed(st, r, resume);   // v0 = result, the J row
// and the body's FIRST call writes the entry row the seam wrapper would have (`StepPark::Entry(entry, initial)`:
// the step body never enters its iop_func_X wrapper -- the kernel calls it directly). With these, the two
// arms' traces compare row for row (iop_trace_compare.py) and the register file at every later call is the
// coroutine form's: the step form is checkable by the same oracle as every other native body.
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_kernel.h"

namespace ps2r::title::sly2 {

using iop::IopContext;

struct StepPark {
    using Park = iop::IopKernel::Park;
    using Resume = iop::IopKernel::Resume;

    // The park a body is waiting on (for the J row and v0 at the resume).
    struct Pending {
        const char* lib = nullptr;
        uint32_t index = 0;
    };

    // The body's first call: the E row of the thread entry (what the seam wrapper writes for the coroutine
    // form; `initial` = the thread's starting register file, before any prologue).
    static void Entry(uint32_t entry, const IopContext& initial);

    // thsemap:8 WaitSema(sid) -> Park::Sema. thbase:33 DelayThread(usec) -> Park::Delay. thbase:24 SleepThread ->
    // Park::Sleep. Each sets the import's a0 in `regs`, writes the I row and records `pending`.
    static Park WaitSema(IopContext& regs, Pending& pending, int32_t sid);
    static Park DelayThread(IopContext& regs, Pending& pending, uint32_t usec);
    static Park SleepThread(IopContext& regs, Pending& pending);
    // At the next call after a park: v0 = the wait's result (what the blocking import returned to the coroutine
    // body), the J row. A no-op for the first call / no pending park.
    static void Resumed(IopContext& regs, Pending& pending, const Resume& r);
};

}  // namespace ps2r::title::sly2
