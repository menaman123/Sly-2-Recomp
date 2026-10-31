// native_fileio_threads.cpp -- FILEIO_service's threads in the STEP form (NativeIopModule::AddThread; iop_step.h):
// the RPC 0x80000001 server thread fn_22384, the IOP-heap RPC 0x80000003 server thread fn_2290c, the power-off
// thread fn_22298 and the per-fd request thread fn_21b78. Each is the coroutine body of native_fileio.cpp (the
// recompiled function's transliteration) with every blocking call returned as its park:
//   * the two RPC loops: sceSifRpcLoop = StepPark::RpcLoopEnter (the I row, the loop context the handlers dispatch
//     from) + Park::Sleep; every wake drains the thread's RPC queue (SifRpcBridge::RpcServiceDrain) and sleeps again;
//   * the power-off thread: devctl("cdrom0:") refused by the engine's ioman -> DelayThread(128 ms) forever = Park::Delay;
//     the arm after a successful devctl (never on this engine) keeps the blocking WaitEventFlag import (fail-loud);
//   * the per-fd thread: the open (fn_2061c -> the reply fn_20528: its WaitSema on the reply semaphore succeeds
//     inline -- one request thread at a time on this title's lanes; a block would be reported by the kernel) then
//     ReceiveMbx = Park::Mbx; close ends the thread (the ExitThread import row, Park::Exit).
// The one-shot request threads (fno 5+: fn_20fe4, fn_2116c ...) are not step bodies: no lane creates one (the EE
// issues only fno 255 and fno 0 on this title); their coroutine bodies are native_fileio.cpp's.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_fileio.h"

#include <memory>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"

namespace ps2r::title::sly2 {

namespace {

using Park = iop::IopKernel::Park;
using Resume = iop::IopKernel::Resume;
using F = NativeFileio;

inline uint32_t R32(uint32_t a) { return IopRam::R32(a); }
inline void W32(uint32_t a, uint32_t v) { IopRam::W32(a, v); }
inline int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
inline uint32_t U(int32_t v) { return static_cast<uint32_t>(v); }
uint32_t Guest(IopContext& ctx, uint32_t addr, uint32_t a0) {
    ctx.gpr[4] = a0;
    IopServices::CallGuest(ctx, addr);
    return ctx.gpr[2];
}
void ThreadParam(uint32_t at, uint32_t entry, uint32_t stack, uint32_t prio) {
    W32(at + 0x0, 0x2000000);
    W32(at + 0x4, 0);
    W32(at + 0x8, entry);
    W32(at + 0xc, stack);
    W32(at + 0x10, prio);
}
void WaitDma(IopContext& ctx, uint32_t id) {
    while (IopServices::sceSifDmaStat(ctx, id) >= 0) {
    }
}
void Prologue(IopContext& regs, uint32_t frame) { regs.gpr[29] = U(S(regs.gpr[29]) - S(frame)); }

struct ThreadState {
    IopContext regs;
    IopContext initial;
    uint32_t a0 = 0;      // the thread's argument
    int phase = 0;
    uint32_t w0 = 0;      // body-specific words across parks
    StepPark::Pending pending;
};

// fn_22384 (frame 0xa8): the RPC 0x80000001 server thread.
Park ServerBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(F::kServerThread, st.initial);
        Prologue(c, 0xa8);
        const uint32_t sp = c.gpr[29];
        if (IopServices::Import(c, "sifman", 29) == 0) IopServices::Import(c, "sifman", 5);
        IopServices::Import(c, "stdio", 4, F::kStrBanner);
        IopServices::sceSifInitRpc(c, 0);
        W32(sp + 0x20, 2);
        W32(sp + 0x28, 0);
        W32(sp + 0x24, 0);
        const int32_t ef = IopServices::CreateEventFlag(c, sp + 0x20);
        W32(F::kBufferEvent, U(ef));
        IopServices::SetEventFlag(c, ef, 1);
        W32(sp + 0x30, 1);
        W32(sp + 0x38, 1);
        W32(sp + 0x3c, 1);
        W32(sp + 0x34, 0);
        W32(F::kReplySema, U(IopServices::CreateSema(c, sp + 0x30)));
        IopServices::ReferThreadStatus(c, 0, sp + 0x58);
        const uint32_t prio = R32(sp + 0x74);
        ThreadParam(sp + 0x40, F::kPowerOffThread, 0x800, prio);
        const int32_t tid = IopServices::CreateThread(c, sp + 0x40);
        IopServices::StartThread(c, tid, 0);
        const int32_t self = IopServices::GetThreadId(c);
        IopServices::sceSifSetRpcQueue(c, F::kRpcQueue, self);
        W32(sp + 0x10, 0);
        W32(sp + 0x14, 0);
        W32(sp + 0x18, F::kRpcQueue);
        IopServices::sceSifRegisterRpc(c, F::kRpcServer, 0x80000001u, F::kHandler, F::kRpcBuffer);
        return StepPark::RpcLoopEnter(c, F::kRpcQueue);
    }
    StepPark::RpcDrain();
    return Park::Sleep();
}

// fn_2290c (frame 0x28): the RPC 0x80000003 IOP-heap server thread (the engine HLEs its EE client: no calls).
Park HeapServerBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(F::kHeapServerThread, st.initial);
        Prologue(c, 0x28);
        const uint32_t sp = c.gpr[29];
        if (IopServices::Import(c, "sifman", 29) == 0) IopServices::Import(c, "sifman", 5);
        IopServices::Import(c, "stdio", 4, F::kStrHeapBanner);
        IopServices::sceSifInitRpc(c, 0);
        const int32_t self = IopServices::GetThreadId(c);
        IopServices::sceSifSetRpcQueue(c, F::kHeapQueue, self);
        W32(sp + 0x10, 0);
        W32(sp + 0x14, 0);
        W32(sp + 0x18, F::kHeapQueue);
        IopServices::sceSifRegisterRpc(c, F::kHeapServer, 0x80000003u, 0x227d4, F::kHeapBuffer);
        return StepPark::RpcLoopEnter(c, F::kHeapQueue);
    }
    StepPark::RpcDrain();
    return Park::Sleep();
}

// fn_22298 (frame 0x40): the power-off thread.
Park PowerOffBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(F::kPowerOffThread, st.initial);
        Prologue(c, 0x40);
    }
    StepPark::Resumed(c, st.pending, r);
    const uint32_t sp = c.gpr[29];
    for (;;) {
        switch (st.phase) {
            case 0: {   // devctl("cdrom0:", 0x4391): refused by the engine's ioman -> 128 ms retries forever
                W32(sp + 0x10, sp + 0x28);
                W32(sp + 0x14, 4);
                const int32_t rc = S(IopServices::Import(c, "ioman", 31, F::kStrCdrom, 0x4391, 0, 0));
                if (rc >= 0) {
                    st.phase = 2;
                    continue;
                }
                if (S(R32(F::kDebug)) > 0) IopServices::Import(c, "stdio", 4, F::kStrPowerOffFail);
                st.phase = 1;
                return StepPark::DelayThread(c, st.pending, 0x1f400);
            }
            case 1:
                st.phase = 0;
                continue;
            case 2:
            default: {
                // Never on this engine (the devctl is refused): the event-flag loop with its blocking WaitEventFlag
                // import kept as the coroutine body has it (the kernel reports a park from a step thread).
                IopServices::ClearEventFlag(c, S(R32(sp + 0x28)), 0xffffffefu);
                IopServices::WaitEventFlag(c, S(R32(sp + 0x28)), 0x10, 0, sp + 0x2c);
                uint32_t id;
                for (;;) {
                    W32(sp + 0x10, 0);
                    W32(sp + 0x14, 0);
                    id = IopServices::Import(c, "sifcmd", 12, F::kCmdPowerOff, sp + 0x18, 0x10, 0);
                    if (id != 0) break;
                    IopServices::DelayThread(c, 0x7d0);
                }
                WaitDma(c, id);
                continue;
            }
        }
    }
}

// fn_21b78(record) (frame 0x28): the per-fd thread.
Park FdBody(ThreadState& st, const Resume& r) {
    IopContext& c = st.regs;
    if (r.first) {
        StepPark::Entry(F::kFdThread, st.initial);
        Prologue(c, 0x28);
        uint32_t record = st.a0;
        st.w0 = R32(record + 0xc);   // the message box
        const int32_t fd = S(Guest(c, F::kOpen, record + 0x10));
        Guest(c, F::kRecordFree, record);
        st.phase = fd >= 0 ? 1 : 3;
    } else {
        StepPark::Resumed(c, st.pending, r);
    }
    const uint32_t sp = c.gpr[29];
    const uint32_t mbx = st.w0;
    for (;;) {
        switch (st.phase) {
            case 1:
                st.phase = 2;
                return StepPark::ReceiveMbx(c, st.pending, sp + 0x10, S(mbx));
            case 2: {
                const uint32_t record = R32(sp + 0x10);
                if (S(R32(F::kDebug)) > 0) IopServices::Import(c, "stdio", 4, F::kFmtReceiveMbx, mbx, R32(record + 8), record);
                const uint32_t fno = R32(record + 8);
                bool exit = false;
                if (fno - 1 < 0x16) {
                    switch (fno) {
                        case 1:
                            Guest(c, F::kClose, record + 0x10);
                            Guest(c, F::kRecordFree, record);
                            exit = true;
                            break;
                        case 2: Guest(c, F::kRead, record + 0x10); break;
                        case 3: Guest(c, F::kWrite, record + 0x10); break;
                        case 4: Guest(c, F::kLseek, record + 0x10); break;
                        case 0x16: Guest(c, F::kLseek64, record + 0x10); break;
                        default: break;
                    }
                }
                if (exit) {
                    st.phase = 3;
                    continue;
                }
                Guest(c, F::kRecordFree, record);
                st.phase = 1;
                continue;
            }
            case 3:
            default:
                IopServices::Import(c, "thmsgbx", 5, mbx);   // DeleteMbx
                c.gpr[4] = mbx;
                IopCallTrace::StepImport("thbase", 8, c);    // ExitThread's row (the import never returns)
                return Park::Exit();
        }
    }
}

template <Park (*Body)(ThreadState&, const Resume&)>
iop::IopKernel::StepBody Make(const IopContext& initial) {
    auto st = std::make_shared<ThreadState>();
    st->regs = initial;
    st->initial = initial;
    st->a0 = initial.gpr[4];
    return [st](const Resume& r) -> Park { return Body(*st, r); };
}

}  // namespace

void RegisterFileioThreads(NativeIopModule& fileio) {
    fileio.AddThread(F::kServerThread, &Make<&ServerBody>, "fileio_rpc_server");
    fileio.AddThread(F::kHeapServerThread, &Make<&HeapServerBody>, "fileio_heap_server");
    fileio.AddThread(F::kPowerOffThread, &Make<&PowerOffBody>, "fileio_poweroff");
    fileio.AddThread(F::kFdThread, &Make<&FdBody>, "fileio_fd");
}

}  // namespace ps2r::title::sly2
