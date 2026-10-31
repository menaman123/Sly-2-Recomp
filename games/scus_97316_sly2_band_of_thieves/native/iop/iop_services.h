// iop_services.h -- what a NATIVE body of a hosted IOP module calls instead of the recompiled body's
// `import_call` / JAL sites: the stock libraries' services by their export ordinals, typed, over the SAME
// IopContext the recompiled body would have used.
//
// EXACT BY CONSTRUCTION: every service here sets a0-a3 (gpr[4..7]) and runs the engine's HLE body through
// IopAbiSurface::ImportCall -- the very function the recompiled TU's `iop_stubs::import_call("thsemap", 8,
// iop_ctx)` reaches -- so the kernel-side effects (a sema wait that parks the coroutine, the intrman
// enable flag, the cdvdman completion due, the libsd op tap the census reads) and the register-file
// effects (v0 in gpr[2]) are the recompiled body's. A native body that calls WaitSema here parks exactly
// where the recompiled one did; the hosted kernel still owns every thread, semaphore, event flag and
// timer. The ordinals are the stock modules' (engine/iop/hle/stock_library_binder.cpp): verified there.
//
// CallGuest(ctx, addr) runs another guest function (recompiled or native: through its seam wrapper).
// IopRam reads/writes the module's state words in IOP RAM (the module's own layout: the port's state).
// Frame pushes/pops a stack frame on the guest $sp for the rare callee that takes stack arguments.
#pragma once

#include <cstdint>

#include "engine/iop/iop_context.h"

namespace ps2r::title::sly2 {

using iop::IopContext;

struct IopServices {
    // The generic import: the arguments given set a0.. (the registers a MIPS caller loads: a0-a3 the body does
    // not pass keep the caller's values, as the recompiled call site leaves them), the HLE body run, v0
    // returned (gpr[2]).
    static uint32_t Import(IopContext& ctx, const char* lib, uint32_t index);
    static uint32_t Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0);
    static uint32_t Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1);
    static uint32_t Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2);
    static uint32_t Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2,
                           uint32_t a3);
    // Another guest function at `addr` (its seam wrapper: the installed native body or the recompiled one).
    // `ra`, when non-zero, is what a JAL would have left in $ra (the recompiled body saves it to its frame;
    // control never depends on it). Returns v0. False-returning variant: no function at `addr`.
    static bool CallGuest(IopContext& ctx, uint32_t addr, uint32_t ra = 0);

    // -- thbase 1.02 -------------------------------------------------------------------------------
    static int32_t CreateThread(IopContext& ctx, uint32_t param) { return S(Import(ctx, "thbase", 4, param)); }
    static int32_t DeleteThread(IopContext& ctx, int32_t tid) { return S(Import(ctx, "thbase", 5, U(tid))); }
    static int32_t StartThread(IopContext& ctx, int32_t tid, uint32_t arg) { return S(Import(ctx, "thbase", 6, U(tid), arg)); }
    static void ExitThread(IopContext& ctx) { Import(ctx, "thbase", 8); }
    static int32_t TerminateThread(IopContext& ctx, int32_t tid) { return S(Import(ctx, "thbase", 10, U(tid))); }
    static int32_t ChangeThreadPriority(IopContext& ctx, int32_t tid, uint32_t prio) { return S(Import(ctx, "thbase", 14, U(tid), prio)); }
    static int32_t RotateThreadReadyQueue(IopContext& ctx, uint32_t prio) { return S(Import(ctx, "thbase", 16, prio)); }
    static int32_t GetThreadId(IopContext& ctx) { return S(Import(ctx, "thbase", 20)); }
    static int32_t ReferThreadStatus(IopContext& ctx, int32_t tid, uint32_t info) { return S(Import(ctx, "thbase", 22, U(tid), info)); }
    static int32_t SleepThread(IopContext& ctx) { return S(Import(ctx, "thbase", 24)); }
    static int32_t WakeupThread(IopContext& ctx, int32_t tid) { return S(Import(ctx, "thbase", 25, U(tid))); }
    static int32_t iWakeupThread(IopContext& ctx, int32_t tid) { return S(Import(ctx, "thbase", 26, U(tid))); }
    static int32_t DelayThread(IopContext& ctx, uint32_t usec) { return S(Import(ctx, "thbase", 33, usec)); }
    static int32_t USec2SysClock(IopContext& ctx, uint32_t usec, uint32_t clock_ptr) { return S(Import(ctx, "thbase", 39, usec, clock_ptr)); }

    // -- thsemap 1.01 (the 989snd global lock, spiop's context sema) ---------------------------------
    static int32_t CreateSema(IopContext& ctx, uint32_t param) { return S(Import(ctx, "thsemap", 4, param)); }
    static int32_t DeleteSema(IopContext& ctx, int32_t sid) { return S(Import(ctx, "thsemap", 5, U(sid))); }
    static int32_t SignalSema(IopContext& ctx, int32_t sid) { return S(Import(ctx, "thsemap", 6, U(sid))); }
    static int32_t iSignalSema(IopContext& ctx, int32_t sid) { return S(Import(ctx, "thsemap", 7, U(sid))); }
    static int32_t WaitSema(IopContext& ctx, int32_t sid) { return S(Import(ctx, "thsemap", 8, U(sid))); }   // parks the coroutine
    static int32_t PollSema(IopContext& ctx, int32_t sid) { return S(Import(ctx, "thsemap", 9, U(sid))); }   // never blocks: 0 taken / SemaZero

    // -- thevent 1.01 -------------------------------------------------------------------------------
    static int32_t CreateEventFlag(IopContext& ctx, uint32_t param) { return S(Import(ctx, "thevent", 4, param)); }
    static int32_t DeleteEventFlag(IopContext& ctx, int32_t ef) { return S(Import(ctx, "thevent", 5, U(ef))); }
    static int32_t SetEventFlag(IopContext& ctx, int32_t ef, uint32_t bits) { return S(Import(ctx, "thevent", 6, U(ef), bits)); }
    static int32_t iSetEventFlag(IopContext& ctx, int32_t ef, uint32_t bits) { return S(Import(ctx, "thevent", 7, U(ef), bits)); }
    static int32_t ClearEventFlag(IopContext& ctx, int32_t ef, uint32_t bits) { return S(Import(ctx, "thevent", 8, U(ef), bits)); }
    static int32_t iClearEventFlag(IopContext& ctx, int32_t ef, uint32_t bits) { return S(Import(ctx, "thevent", 9, U(ef), bits)); }
    static int32_t WaitEventFlag(IopContext& ctx, int32_t ef, uint32_t bits, uint32_t mode, uint32_t result_ptr) {
        return S(Import(ctx, "thevent", 10, U(ef), bits, mode, result_ptr));
    }
    static int32_t PollEventFlag(IopContext& ctx, int32_t ef, uint32_t bits, uint32_t mode, uint32_t result_ptr) {
        return S(Import(ctx, "thevent", 11, U(ef), bits, mode, result_ptr));
    }

    // -- intrman 1.02 -------------------------------------------------------------------------------
    static int32_t CpuDisableIntr(IopContext& ctx) { return S(Import(ctx, "intrman", 8)); }
    static int32_t CpuEnableIntr(IopContext& ctx) { return S(Import(ctx, "intrman", 9)); }
    static int32_t CpuSuspendIntr(IopContext& ctx, uint32_t state_ptr) { return S(Import(ctx, "intrman", 17, state_ptr)); }
    static int32_t CpuResumeIntr(IopContext& ctx, uint32_t state) { return S(Import(ctx, "intrman", 18, state)); }

    // -- timrman 1.03 (the 240 Hz hard-timer beat: AllocHardTimer / SetTimerHandler / SetupHardTimer / StartHardTimer) --
    static int32_t AllocHardTimer(IopContext& ctx, uint32_t source, uint32_t size, uint32_t prescale) {
        return S(Import(ctx, "timrman", 4, source, size, prescale));
    }
    static int32_t FreeHardTimer(IopContext& ctx, int32_t timid) { return S(Import(ctx, "timrman", 6, U(timid))); }
    static int32_t SetTimerHandler(IopContext& ctx, int32_t timid, uint32_t compare, uint32_t handler, uint32_t arg) {
        return S(Import(ctx, "timrman", 20, U(timid), compare, handler, arg));
    }
    static int32_t SetupHardTimer(IopContext& ctx, int32_t timid, uint32_t source, uint32_t mode, uint32_t prescale) {
        return S(Import(ctx, "timrman", 22, U(timid), source, mode, prescale));
    }
    static int32_t StartHardTimer(IopContext& ctx, int32_t timid) { return S(Import(ctx, "timrman", 23, U(timid))); }
    static int32_t StopHardTimer(IopContext& ctx, int32_t timid) { return S(Import(ctx, "timrman", 24, U(timid))); }

    // -- sifman 1.01 (IOP -> EE DMA: the bank-load handshake words, the movie push) ---------------------
    static uint32_t sceSifSetDma(IopContext& ctx, uint32_t desc_ptr, uint32_t count) { return Import(ctx, "sifman", 7, desc_ptr, count); }
    static int32_t sceSifDmaStat(IopContext& ctx, uint32_t id) { return S(Import(ctx, "sifman", 8, id)); }
    static uint32_t sceSifSetDmaIntr(IopContext& ctx, uint32_t desc_ptr, uint32_t count, uint32_t func, uint32_t data) {
        return Import(ctx, "sifman", 32, desc_ptr, count, func, data);
    }

    // -- sifcmd 1.01 (the RPC servers: the request/reply buffers are the module's own RAM) -------------
    static int32_t sceSifSendCmd(IopContext& ctx, uint32_t cid, uint32_t packet, uint32_t size) { return S(Import(ctx, "sifcmd", 12, cid, packet, size)); }
    static void sceSifInitRpc(IopContext& ctx, uint32_t mode) { Import(ctx, "sifcmd", 14, mode); }
    static void sceSifRegisterRpc(IopContext& ctx, uint32_t sd, uint32_t sid, uint32_t func, uint32_t buff) {
        Import(ctx, "sifcmd", 17, sd, sid, func, buff);
    }
    static void sceSifSetRpcQueue(IopContext& ctx, uint32_t qd, int32_t tid) { Import(ctx, "sifcmd", 19, qd, U(tid)); }
    static void sceSifRpcLoop(IopContext& ctx, uint32_t qd) { Import(ctx, "sifcmd", 22, qd); }   // parks in Wait::Rpc
    static int32_t sceSifGetOtherData(IopContext& ctx, uint32_t rd, uint32_t src, uint32_t dest, uint32_t size) {
        return S(Import(ctx, "sifcmd", 23, rd, src, dest, size));
    }

    // -- cdvdman 1.01 (spiop's stream refills: the engine's completion due, unchanged) ------------------
    static int32_t sceCdRead(IopContext& ctx, uint32_t lsn, uint32_t sectors, uint32_t buf, uint32_t mode_ptr) {
        return S(Import(ctx, "cdvdman", 6, lsn, sectors, buf, mode_ptr));
    }
    static int32_t sceCdGetError(IopContext& ctx) { return S(Import(ctx, "cdvdman", 8)); }
    static int32_t sceCdSearchFile(IopContext& ctx, uint32_t file_ptr, uint32_t name_ptr) { return S(Import(ctx, "cdvdman", 10, file_ptr, name_ptr)); }
    static int32_t sceCdSync(IopContext& ctx, uint32_t mode) { return S(Import(ctx, "cdvdman", 11, mode)); }
    static int32_t sceCdGetDiskType(IopContext& ctx) { return S(Import(ctx, "cdvdman", 12)); }
    static int32_t sceCdDiskReady(IopContext& ctx, uint32_t mode) { return S(Import(ctx, "cdvdman", 13, mode)); }
    static uint32_t sceCdCallback(IopContext& ctx, uint32_t func) { return Import(ctx, "cdvdman", 37, func); }
    static int32_t sceCdBreak(IopContext& ctx) { return S(Import(ctx, "cdvdman", 39)); }

    // -- sysmem 1.02 / stdio 1.03 / loadcore 1.03 --------------------------------------------------------
    static uint32_t AllocSysMemory(IopContext& ctx, uint32_t mode, uint32_t size, uint32_t addr) { return Import(ctx, "sysmem", 4, mode, size, addr); }
    static int32_t FreeSysMemory(IopContext& ctx, uint32_t addr) { return S(Import(ctx, "sysmem", 5, addr)); }
    // printf(fmt, ...): the format string and the register args the recompiled body loaded (the stdio HLE
    // formats from IOP RAM; stack args past a3 at $sp+0x10.. as the ABI lays them).
    static int32_t Printf(IopContext& ctx, uint32_t fmt) { return S(Import(ctx, "stdio", 4, fmt)); }
    static int32_t Printf(IopContext& ctx, uint32_t fmt, uint32_t a1) { return S(Import(ctx, "stdio", 4, fmt, a1)); }
    static int32_t Printf(IopContext& ctx, uint32_t fmt, uint32_t a1, uint32_t a2) { return S(Import(ctx, "stdio", 4, fmt, a1, a2)); }
    static int32_t Printf(IopContext& ctx, uint32_t fmt, uint32_t a1, uint32_t a2, uint32_t a3) {
        return S(Import(ctx, "stdio", 4, fmt, a1, a2, a3));
    }
    static int32_t RegisterLibraryEntries(IopContext& ctx, uint32_t table) { return S(Import(ctx, "loadcore", 6, table)); }

    // -- libsd 1.05 (the SPU2 driver: the hosted LibsdModule, whose op tap is the census / oracle) ---------
    // `index` is the libsd export index as the module's import stub carries it (4 = Init, 5 = SetParam,
    // 6 = GetParam, 7 = SetSwitch, 8 = GetSwitch, 9 = SetAddr, 10 = GetAddr, 11 = SetCoreAttr, 13 = Note2Pitch,
    // 15 = ProcBatch, 17 = VoiceTrans, 18 = BlockTrans, 19 = VoiceTransStatus, 20 = BlockTransStatus,
    // 23 = SetTransCallback, 25 = SetEffectAttr, 26 = GetEffectAttr -- stock_library_binder.cpp's kLibsd).
    static uint32_t Libsd(IopContext& ctx, uint32_t index) { return Import(ctx, "libsd", index); }
    static uint32_t Libsd(IopContext& ctx, uint32_t index, uint32_t a0) { return Import(ctx, "libsd", index, a0); }
    static uint32_t Libsd(IopContext& ctx, uint32_t index, uint32_t a0, uint32_t a1) { return Import(ctx, "libsd", index, a0, a1); }
    static uint32_t Libsd(IopContext& ctx, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2) {
        return Import(ctx, "libsd", index, a0, a1, a2);
    }
    static uint32_t Libsd(IopContext& ctx, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
        return Import(ctx, "libsd", index, a0, a1, a2, a3);
    }

private:
    static int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
    static uint32_t U(int32_t v) { return static_cast<uint32_t>(v); }
};

// The module's state in IOP RAM (the bound IopMemory): 32/16/8-bit little-endian words at IOP addresses,
// the same reads/writes the recompiled body's `iop_memory::read/write<T>` make.
struct IopRam {
    static uint32_t R32(uint32_t addr);
    static uint16_t R16(uint32_t addr);
    static uint8_t R8(uint32_t addr);
    static void W32(uint32_t addr, uint32_t v);
    static void W16(uint32_t addr, uint16_t v);
    static void W8(uint32_t addr, uint8_t v);
    // Host pointer to `size` bytes (masked), or nullptr past the end of RAM.
    static uint8_t* Ptr(uint32_t addr, uint32_t size);
    static const uint8_t* CPtr(uint32_t addr, uint32_t size);
};

// A guest stack frame around a native body that passes STACK arguments (the 5th+ arg at $sp+0x10..):
// `Frame f(ctx, 0x38); f.Arg(0x10, value); ... ` -- $sp is moved down by `bytes` for the object's life, as
// the recompiled body's prologue/epilogue did. Register spills to the frame are not reproduced (no oracle
// reads a module's own stack).
class IopStackFrame {
public:
    IopStackFrame(IopContext& ctx, uint32_t bytes);
    ~IopStackFrame();
    IopStackFrame(const IopStackFrame&) = delete;
    IopStackFrame& operator=(const IopStackFrame&) = delete;
    uint32_t Sp() const { return ctx_.gpr[29]; }
    void Word(uint32_t offset, uint32_t value);
    uint32_t Word(uint32_t offset) const;

private:
    IopContext& ctx_;
    uint32_t bytes_;
};

}  // namespace ps2r::title::sly2
