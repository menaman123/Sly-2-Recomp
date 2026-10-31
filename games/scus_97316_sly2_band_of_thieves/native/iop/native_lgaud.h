// native_lgaud.h -- lgAud (Logitech "LGTGliblgaud" USB headset audio driver, "lgAud Version 1.10.001, built on
// Mar 23 2004", hosted at 0x28000) as native bodies over the module's own IOP RAM, one function at a time on
// the per-function seam (native_iop_module.h).
//
// WHAT THE MODULE IS ON THIS DISC. lgAud exports the library "lgaud" v1.08 (23 entries, table at 0x30b00) and
// registers a USB logical device driver (sceUsbdRegisterLdd 0x311ec); it is loaded with `rpc=0`, so its own
// SIF RPC server ('BLIP', thread 0x29ed8) is never created and the EE never talks to it. Its only client is
// spiop's headset thread (fn_5b0cc), which polls export 18 (GetDeviceState, 0x2a3b8) every 500 ms and, with
// no headset on the engine's idle USB bus (UsbdModule: no device, no probe/connect callback ever), never
// enters the VAG-to-headset path (exports 4 5 6 7 11 13 14 15 16 19 22). So the functions the lanes REACH are
// the boot closure of the entry (argv parse, the self-image hash, the USB_driver version scan, the library
// registration, the sema) and export 18; everything else is ported for completeness as time allows, the
// recompiled body staying the oracle for each.
//
// THE MODULE'S STATE (its .data/.bss words at 0x31190..0x323c0, read off the generated TU + the IRX):
//   g[0x311a0] hash seed (0x6873756d)        g[0x311a4] expected text hash (0x7dff7a41, XOR'd at boot)
//   g[0x311b0] the device-state sema         g[0x311bc] device state (0 none, 1 just connected, 2 ...)
//   g[0x311c0] thread priority ("thpri=", default 0x30)      g[0x311d0] -> "lgAud"   g[0x311d8] -> version string
//   g[0x311e0] RPC buffer size   g[0x311e4] max stream ("maxstream=")   g[0x311e8] RPC buffer (0 with rpc=0)
//   g[0x311ec] the sceUsbdLddOps record (probe 0x28354, connect 0x29238, disconnect 0x28508)
//   g[0x3121c] RPC thread id     g[0x31260] the running text hash    g[0x31270] RPC queue   g[0x31288] RPC server
//   g[0x32390..0x323b0) 8 per-slot words cleared at boot
// Every body reproduces the recompiled one's writes to these words and its library calls (same ordinal, same
// arguments, same order: the IopServices rule) and leaves v0 as the recompiled body does at every exit.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

class NativeLgaud {
public:
    // The module's addresses (IOP RAM, the module relocated to 0x28000).
    static constexpr uint32_t kEntry = 0x299f4;          // _start(argc, argv)
    static constexpr uint32_t kUnload = 0x29938;         // fn_29938: the argc < 0 arm of the entry
    static constexpr uint32_t kParseArg = 0x297e4;       // fn_297e4(arg, key, default, min, [sp+0x10] max)
    static constexpr uint32_t kStrncasecmp = 0x296ac;    // fn_296ac(a, b, n)
    static constexpr uint32_t kModuleStatus = 0x29794;   // fn_29794(name, info) -> 1 found / 0
    static constexpr uint32_t kHashReset = 0x303a8;      // fn_303a8()
    static constexpr uint32_t kHashStep = 0x302b0;       // fn_302b0(word, mode) -> 0 at the export-table magic
    static constexpr uint32_t kHashGet = 0x30398;        // fn_30398()
    static constexpr uint32_t kAlloc = 0x2c1a0;          // fn_2c1a0(size): AllocSysMemory under CpuSuspendIntr
    static constexpr uint32_t kFree = 0x2c1e8;           // fn_2c1e8(ptr): FreeSysMemory under CpuSuspendIntr
    static constexpr uint32_t kMax = 0x2c288;            // fn_2c288(a, b) -> max
    static constexpr uint32_t kMin = 0x2c270;            // fn_2c270(a, b) -> min
    static constexpr uint32_t kMulDivRound = 0x2c22c;    // fn_2c22c(a, b, c) -> (a*b + c/2) / c (toward -inf)
    static constexpr uint32_t kAbs = 0x2c2a0;            // fn_2c2a0(a)
    static constexpr uint32_t kGetDeviceState = 0x2a3b8; // export 18: lgaud:18(int* state)
    static constexpr uint32_t kRpcThread = 0x29ed8;      // the 'BLIP' RPC server thread (rpc=1 only)

    static constexpr uint32_t kHashSeed = 0x311a0, kHashExpect = 0x311a4, kSema = 0x311b0, kDeviceState = 0x311bc;
    static constexpr uint32_t kThreadPrio = 0x311c0, kModName = 0x311d0, kVersion = 0x311d8;
    static constexpr uint32_t kRpcBufSize = 0x311e0, kMaxStream = 0x311e4, kRpcBuf = 0x311e8, kLdd = 0x311ec;
    static constexpr uint32_t kRpcTid = 0x3121c, kTextHash = 0x31260, kRpcQueue = 0x31270, kRpcServer = 0x31288;
    static constexpr uint32_t kSlots = 0x32390, kSlotCount = 8;
    static constexpr uint32_t kExportTable = 0x30b00;
    // .rodata strings
    static constexpr uint32_t kFmtAdjust = 0x30f38;      // "%s: adjusting %s to %d\n" (24 bytes, copied to the stack)
    static constexpr uint32_t kKeyThpri = 0x30f50;       // "thpri=" (7 bytes)
    static constexpr uint32_t kKeyRpc = 0x30f58;         // "rpc=" (5 bytes)
    static constexpr uint32_t kKeyMaxstream = 0x30f60;   // "maxstream=" (11 bytes)
    static constexpr uint32_t kStrUsbDriver = 0x30f6c;   // "USB_driver"
    static constexpr uint32_t kFmtBanner = 0x30f78;      // "%s %s%c\n\t* thread priority %d\n"
    static constexpr uint32_t kFmtNoRpc = 0x30f98;       // "\t* no RPC server (no access from EE)\n"
    static constexpr uint32_t kFmtRpc = 0x30fc0;         // "\t* RPC server with max stream size: %d bytes\n"
    static constexpr uint32_t kFmtAllocFail = 0x30ff0;   // "%s: failed to allocate SIF RPC buffer, quitting!\n"
    static constexpr uint32_t kFmtUsbdVersion = 0x31024; // "lgaud: USBD Version insufficient (found: %c.%c.%c.%c, needed: >= 2.4.3.0)\n"
    static constexpr uint32_t kRpcServerId = 0x50494c42; // 'BLIP'
    static constexpr uint32_t kExportMagic = 0x41c00000;
    static constexpr uint32_t kPsIITag = 0x49497350;     // "PsII" (the SDK module version tag)
    static constexpr uint32_t kUsbdMinVersion = 0x3234332f;

    static void Register(NativeIopModule& lgaud);

    // -- bodies (one per recompiled function; names after the role, addresses above) --------------
    void Entry(IopContext& ctx);
    void Unload(IopContext& ctx);
    void ParseArg(IopContext& ctx);
    void Strncasecmp(IopContext& ctx);
    void ModuleStatus(IopContext& ctx);
    void HashReset(IopContext& ctx);
    void HashStep(IopContext& ctx);
    void HashGet(IopContext& ctx);
    void Alloc(IopContext& ctx);
    void Free(IopContext& ctx);
    void Max(IopContext& ctx);
    void Min(IopContext& ctx);
    void MulDivRound(IopContext& ctx);
    void Abs(IopContext& ctx);
    void GetDeviceState(IopContext& ctx);
};

void RegisterLgaudNatives(NativeIopModule& lgaud);

}  // namespace ps2r::title::sly2
