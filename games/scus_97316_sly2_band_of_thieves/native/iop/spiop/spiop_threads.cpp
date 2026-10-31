// spiop_threads.cpp -- spiop's two headset threads in the STEP form (NativeIopModule::AddThread; iop_step.h):
// thread B fn_5b0cc (the Logitech headset monitor, started by op 0x00) and thread A fn_5b2a0 (the headset feeder,
// started by the headset stream open fn_5ad80 -- never on this disc's lanes: the engine's USB bus carries no
// device). The tray-poll thread C fn_5b438 is spiop_tray_thread.cpp (iopsys); the three slot stream threads
// fn_5be48 stay in the COROUTINE form (spiop_native.cpp): they call the 989snd exports (snd989:49/99/25/29 ...),
// which take the 989snd global lock (fn_42f58 -> WaitSema on a contended semaphore) INSIDE the hosted callee,
// and a park inside a callee cannot be returned as a step body's Park.
//
// Each body here is the coroutine body of spiop_native.cpp (itself the recompiled function's transliteration)
// with every blocking call turned into a returned park and the loop into a phase machine; the frame, the RAM
// writes, the non-blocking imports and the sibling calls are the same code. The headset-connected arm of thread B
// and the whole of thread A are 'ported, unexercised': no lane reaches them (lgaud:18 reports state 0 forever);
// they call lgaud's exports 4/5/7/11/6/22 directly, as the coroutine body does -- lgaud's own waits (USB
// completion semaphores) would be parks inside a callee: the kernel reports such a call from a step thread
// (iop_kernel.cpp LeaveThread), fail-loud, never silent.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_threads.h"

#include <memory>

#include "engine/iop/abi/host_app/iop_stubs.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_step.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/spiop/spiop_state.h"

namespace ps2r::title::sly2::spiop {

namespace {

using Ctx = ps2r::iop::IopContext;
using Park = iop::IopKernel::Park;
using Resume = iop::IopKernel::Resume;
enum Reg : int { v0 = 2, a0 = 4, a1 = 5, a2 = 6, a3 = 7, s0 = 16, s1, s2, s3, s4, s5, s6, s7, sp = 29, ra = 31 };

inline uint32_t Call(Ctx& c, uint32_t addr, uint32_t ret_pc) {
    c.gpr[ra] = ret_pc;
    const auto* fn = iop_stubs::iop_resolve(addr);
    if (fn) (*fn)(c);
    return c.gpr[v0];
}
inline uint32_t Import(Ctx& c, const char* lib, uint32_t idx) {
    iop_stubs::import_call(lib, idx, c);
    return c.gpr[v0];
}
inline void Args(Ctx& c, uint32_t x0) { c.gpr[a0] = x0; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1) { c.gpr[a0] = x0; c.gpr[a1] = x1; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1, uint32_t x2) { c.gpr[a0] = x0; c.gpr[a1] = x1; c.gpr[a2] = x2; }
inline void Args(Ctx& c, uint32_t x0, uint32_t x1, uint32_t x2, uint32_t x3) {
    c.gpr[a0] = x0; c.gpr[a1] = x1; c.gpr[a2] = x2; c.gpr[a3] = x3;
}
inline void SignalSema(Ctx& c, uint32_t id) { Args(c, id); Import(c, "thsemap", 6); }
inline void SignalIfValid(Ctx& c, uint32_t id) {
    if (static_cast<int32_t>(id) >= 0) SignalSema(c, id);
}
// The recompiled prologue's frame: $sp moved for the thread's whole life (the body never returns on the lanes).
struct Frame {
    Ctx& c;
    explicit Frame(Ctx& ctx, uint32_t bytes) : c(ctx) { c.gpr[sp] -= bytes; }
    uint32_t at(uint32_t off) const { return c.gpr[sp] + off; }
    void save(uint32_t off, int reg) const { W32(at(off), c.gpr[reg]); }
    void put(uint32_t off, uint32_t v) const { W32(at(off), v); }
    uint32_t get(uint32_t off) const { return R32(at(off)); }
};

// -- thread B: fn_5b0cc(ctx), frame 0x1a0 -----------------------------------------------------------------
struct MonitorState {
    Ctx regs;
    Ctx initial;
    uint32_t ctx = 0;
    int phase = 0;   // 0 loop top, 1 after a 500 ms delay, 2 the exit
    StepPark::Pending pending;
};

Park MonitorBody(MonitorState& st, const Resume& resume) {
    Ctx& c = st.regs;
    if (resume.first) {
        StepPark::Entry(SpiopThreads::kMonitor, st.initial);
        Frame f(c, 0x1a0);
        f.save(0x18c, s3);
        st.ctx = c.gpr[a0];
        f.save(0x19c, ra);
        f.save(0x198, s6);
        f.save(0x194, s5);
        f.save(0x190, s4);
        f.save(0x188, s2);
        f.save(0x184, s1);
        f.save(0x180, s0);
    }
    StepPark::Resumed(c, st.pending, resume);
    const uint32_t ctx = st.ctx;
    const uint32_t minus1 = static_cast<uint32_t>(-1);
    const auto at = [&](uint32_t off) { return c.gpr[sp] + off; };
    for (;;) {
        switch (st.phase) {
            case 1:
                st.phase = 0;
                continue;
            case 0: {
                if (R32(ctx + CtxL::kShutdown) != 0) {
                    st.phase = 2;
                    continue;
                }
                Args(c, at(0x174));
                Import(c, "lgaud", 18);   // GetDeviceState: lgaud's sema has this one accessor (no USB device) -- never parks
                const uint32_t state = R32(at(0x174));
                if (state != 1) {
                    st.phase = 1;
                    return StepPark::DelayThread(c, st.pending, 0x7a120);
                }
                // -- the headset-connected arm (unexercised: the engine's usbd never reports a device) --
                Args(c, 0, at(0x10));
                if (Import(c, "lgaud", 4) != 0) {
                    st.phase = 1;
                    return StepPark::DelayThread(c, st.pending, 0x7a120);
                }
                Args(c, 0, at(0x164), at(0x178));
                W8(at(0x164), 3);
                W16(at(0x16e), 0x5622);
                W8(at(0x16c), static_cast<uint8_t>(state));
                W8(at(0x16d), 0x10);
                W16(at(0x170), 0x64);
                W8(at(0x166), static_cast<uint8_t>(state));
                W8(at(0x167), 0x10);
                W16(at(0x168), 0x2b11);
                W16(at(0x16a), 0x64);
                Import(c, "lgaud", 5);
                const uint32_t dev = R32(at(0x178));
                if (dev == minus1) continue;
                W32(ctx + CtxL::kHeadsetDev, dev);
                Args(c, dev);
                Import(c, "lgaud", 11);
                W32(at(0x17c), 0x800);
                for (;;) {
                    if (R32(ctx + CtxL::kShutdown) != 0) break;
                    Args(c, R32(at(0x178)), 0, R32(ctx + CtxL::kSectorBuf), at(0x17c));
                    if (Import(c, "lgaud", 7) != 0) break;
                    uint32_t acc = 0;
                    uint32_t p = R32(ctx + CtxL::kSectorBuf);
                    for (int32_t n = 0x3ff; n >= 0; --n) {
                        const int32_t s = R16s(p);
                        p += 2;
                        Args(c, static_cast<uint32_t>(s * s));
                        const uint32_t fs = Call(c, 0x5d8e0, 0x5b1dc);
                        Args(c, acc, fs);
                        acc = Call(c, 0x5d078, 0x5b1e8);
                    }
                    Args(c, acc, 0x3a800000);
                    uint32_t r = Call(c, 0x5d134, 0x5b1fc);
                    Args(c, r);
                    r = Call(c, 0x5f0f0, 0x5b204);
                    Args(c, r, 0x38000000);
                    r = Call(c, 0x5d134, 0x5b210);
                    W32(ctx + CtxL::kMicLevel, r);
                }
                Args(c, R32(at(0x178)));
                Import(c, "lgaud", 6);
                W32(ctx + CtxL::kHeadsetDev, minus1);
                W32(ctx + CtxL::kMicLevel, 0);
                continue;
            }
            case 2:
            default:
                // The shutdown exit (never on the lanes). ChangeThreadPriority may leave the thread Ready behind a
                // higher one in the coroutine form; the step form signals and exits in one run.
                Args(c, 0, 9);
                Import(c, "thbase", 14);
                SignalSema(c, R32(ctx + CtxL::kSemaB));
                return Park::Exit();
        }
    }
}

// -- thread A: fn_5b2a0(ctx), frame 0x38 ------------------------------------------------------------------
struct FeederState {
    Ctx regs;
    Ctx initial;
    uint32_t ctx = 0;
    uint32_t slot = 0;
    int phase = 0;   // 0 loop top, 1 after the slot-lock park, 2 after the 5 ms delay, 3 the exit
    StepPark::Pending pending;
};

Park FeederBody(FeederState& st, const Resume& resume) {
    Ctx& c = st.regs;
    if (resume.first) {
        StepPark::Entry(SpiopThreads::kFeeder, st.initial);
        Frame f(c, 0x38);
        f.save(0x30, s4);
        st.ctx = c.gpr[a0];
        f.save(0x34, ra);
        f.save(0x2c, s3);
        f.save(0x28, s2);
        f.save(0x24, s1);
        f.save(0x20, s0);
    }
    StepPark::Resumed(c, st.pending, resume);
    const uint32_t ctx = st.ctx;
    const uint32_t lock = c.gpr[sp] + 0x10;
    for (;;) {
        switch (st.phase) {
            case 2:
                st.phase = 0;
                continue;
            case 0: {
                // fn_5b6e4(lock, slot) inlined around its park: out = {sema, slot}; WaitSema(sema) when valid. (The
                // sibling's own E/X trace rows are not written for this inlined form.)
                st.slot = R32(ctx + CtxL::kHeadsetSlot);
                Args(c, lock, st.slot);
                const uint32_t sema = R32(st.slot + Slot::kSema);
                W32(lock, sema);
                st.phase = 1;
                if (static_cast<int32_t>(sema) >= 0) return StepPark::WaitSema(c, st.pending, static_cast<int32_t>(sema));
                continue;
            }
            case 1: {
                W32(lock + 4, st.slot);
                c.gpr[v0] = lock;
                const uint32_t slot = R32(lock + 4);
                bool exit_now = true;
                if (slot) {
                    Args(c, slot);
                    if (Call(c, 0x5bc94, 0x5b3dc) != 0) exit_now = false;
                }
                if (exit_now) {
                    SignalIfValid(c, R32(lock));
                    st.phase = 3;
                    continue;
                }
                Args(c, slot, 0);
                const uint32_t desc = Call(c, 0x5bcf4, 0x5b2d0);
                if (desc == 0) {
                    SignalIfValid(c, R32(lock));
                    st.phase = 3;
                    continue;
                }
                const uint32_t end = R32(desc + 4) + R32(desc);
                Args(c, slot);
                const uint32_t avail = Call(c, 0x5bd4c, 0x5b2ec);
                const uint32_t to_end = end - R32(desc + 8);
                if (avail < to_end) {
                    Args(c, slot);
                    W32(lock + 8, Call(c, 0x5bd4c, 0x5b310));
                } else {
                    W32(lock + 8, to_end);
                }
                if (static_cast<int32_t>(R32(lock + 8)) > 0) {
                    Args(c, R32(ctx + CtxL::kHeadsetDev), 1, R32(desc + 8), lock + 8);
                    if (Import(c, "lgaud", 22) != 0) {
                        SignalIfValid(c, R32(lock));
                        st.phase = 3;
                        continue;
                    }
                    const uint32_t n = R32(lock + 8);
                    const uint32_t cur = R32(desc + 8) + n;
                    const uint32_t total = R32(desc + 0xc) + n;
                    W32(desc + 8, cur);
                    W32(desc + 0xc, total);
                    if (!(cur < end)) W32(desc + 8, R32(desc + 4));
                }
                SignalIfValid(c, R32(lock));
                st.phase = 2;
                return StepPark::DelayThread(c, st.pending, 0x1388);
            }
            case 3:
            default:
                Args(c, 0, 9);
                W32(ctx + CtxL::kHeadsetSlot, 0);
                Import(c, "thbase", 14);
                SignalSema(c, R32(ctx + CtxL::kHeadsetSema));
                return Park::Exit();
        }
    }
}

}  // namespace

iop::IopKernel::StepBody SpiopThreads::MakeMonitor(const IopContext& initial) {
    auto st = std::make_shared<MonitorState>();
    st->regs = initial;
    st->initial = initial;
    return [st](const Resume& r) -> Park { return MonitorBody(*st, r); };
}

iop::IopKernel::StepBody SpiopThreads::MakeFeeder(const IopContext& initial) {
    auto st = std::make_shared<FeederState>();
    st->regs = initial;
    st->initial = initial;
    return [st](const Resume& r) -> Park { return FeederBody(*st, r); };
}

void SpiopThreads::Register(NativeIopModule& spiop) {
    spiop.AddThread(kMonitor, &SpiopThreads::MakeMonitor, "spiop_headset_monitor");
    spiop.AddThread(kFeeder, &SpiopThreads::MakeFeeder, "spiop_headset_feeder");
}

}  // namespace ps2r::title::sly2::spiop
