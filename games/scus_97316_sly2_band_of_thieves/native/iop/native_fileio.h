// native_fileio.h -- FILEIO_service (the IOPRP300 reboot image's stock fileio, "Multi Threaded Fileio module.
// (99/11/15)", hosted at 0x20000) as native bodies over the module's own IOP RAM on the per-function seam.
//
// WHAT THE MODULE IS. The EE's libfileio (sceFsInit 0x237bd8, sceOpen 0x237ea8, sceClose 0x238138, sceRead
// 0x2382b8, sceWrite 0x238528, sceIoctl 0x2387e8 -- the only six linked into SCUS_973.16, fnos 255 0 1 2 3 5)
// calls the RPC server 0x80000001 whose handler (0x21e68, thread 0x22384) answers the init/config requests in
// place and hands every file request to a thread: an open creates a per-fd thread (0x21b78) that opens through
// ioman and then serves close/read/write/lseek from a message box (fnos 1-4, 22); the other requests get a
// one-shot thread each (0x21ca4's table). Every request thread replies through the SIF command 0x80000011
// (0x20528: the reply block DMA'd into one of the EE's two reply buffers, alternating). The module also hosts
// the IOP-heap RPC server 0x80000003 (thread 0x2290c, handler 0x227d4) -- the engine HLEs the EE's
// sceSifAllocIopHeap, so it never sees a call -- and a power-off event thread (0x22298) whose devctl("cdrom0:")
// the engine's ioman refuses, so it retries every 128 ms forever.
//
// THE MODULE'S STATE (.data/.bss 0x23220..0x24280, read off the generated TU + the IRX):
//   g[0x2323c] version word (the init reply)      g[0x23240] debug level     g[0x23244] r/w buffer size (0x4000)
//   g[0x23248] request-thread priority (0x60)      g[0x2324c] reply-buffer index (0/1)   g[0x23250] buffer table size (4)
//   g[0x23254] live buffers   g[0x23258]/g[0x2325c]/g[0x23260] the cached buffer: address / size / in-use
//   g[0x23264..0x232e4) 32 request-thread slots (-1 free)     g[0x232f0], g[0x232f4] the EE's two reply buffers
//   g[0x23300] the reply sema   g[0x23304] the buffer event flag   g[0x23308..0x23318) the buffer table
//   g[0x23330] the RPC return block {result, 2}   g[0x23340] RPC queue   g[0x23358] RPC server   g[0x233a0] RPC recv buffer
//   g[0x24200 + 4*fd] the per-fd message box      g[0x23ff0]/g[0x24008]/g[0x24050] the heap server's queue/server/buffer
// Every body reproduces the recompiled one's writes to these words and its reply block bytes (they are DMA'd into
// EE RAM: mem.mhash), its library calls in the same order (IopServices), and v0 at every exit.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

namespace ps2r::title::sly2 {

class NativeFileio {
public:
    // Functions (the recompiled TU's slots).
    static constexpr uint32_t kEntry = 0x20000;
    static constexpr uint32_t kBufferAlloc = 0x20100;    // fn_20100(int* size) -> buffer
    static constexpr uint32_t kBufferFree = 0x20328;     // fn_20328(buffer)
    static constexpr uint32_t kBufferReset = 0x20434;    // fn_20434()
    static constexpr uint32_t kRecordAlloc = 0x204a8;    // fn_204a8() -> 0xc40-byte request record
    static constexpr uint32_t kRecordFree = 0x204ec;     // fn_204ec(record)
    static constexpr uint32_t kReply = 0x20528;          // fn_20528(block, size): SIF command 0x80000011
    static constexpr uint32_t kOpen = 0x2061c;           // fn_2061c(req) -> fd
    static constexpr uint32_t kClose = 0x206b4;
    static constexpr uint32_t kLseek = 0x20744;
    static constexpr uint32_t kRead = 0x207b8;
    static constexpr uint32_t kWrite = 0x20b14;
    static constexpr uint32_t kIoctl = 0x20fe4;          // one-shot thread body (fno 5)
    static constexpr uint32_t kFdThread = 0x21b78;       // the per-fd thread: open, then the message box loop
    static constexpr uint32_t kThreadEntryFor = 0x21ca4; // fn_21ca4(fno) -> one-shot thread entry
    static constexpr uint32_t kHandler = 0x21e68;        // the RPC 0x80000001 handler(fno, buf, size)
    static constexpr uint32_t kPowerOffThread = 0x22298;
    static constexpr uint32_t kServerThread = 0x22384;   // RPC 0x80000001
    static constexpr uint32_t kHeapServerThread = 0x2290c;   // RPC 0x80000003
    // Generic one-shot shims (fno -> ioman op), each `shim(record)`: reply + free + ExitThread.
    static constexpr uint32_t kRemove = 0x2116c, kMkdir = 0x211e4, kRmdir = 0x21264, kFormat = 0x212dc, kAddDrv = 0x2135c,
                              kDelDrv = 0x213dc, kDopen = 0x21454, kDclose = 0x214cc, kDread = 0x2154c, kGetstat = 0x215c8,
                              kChstat = 0x2163c, kRename = 0x216bc, kChdir = 0x21734, kSync = 0x217ac, kMount = 0x2182c,
                              kUmount = 0x218b8, kLseek64 = 0x21930, kDevctl = 0x219a8, kSymlink = 0x21a7c, kReadlink = 0x21af4,
                              kIoctl2 = 0x21094, kDevctlBlk = 0x20ccc;

    // State words.
    static constexpr uint32_t kVersionWord = 0x2323c, kDebug = 0x23240, kBufferSize = 0x23244, kThreadPrio = 0x23248;
    static constexpr uint32_t kReplyIndex = 0x2324c, kTableSize = 0x23250, kLiveBuffers = 0x23254;
    static constexpr uint32_t kCachedBuf = 0x23258, kCachedSize = 0x2325c, kCachedInUse = 0x23260;
    static constexpr uint32_t kThreadSlots = 0x23264, kThreadSlotCount = 32;
    static constexpr uint32_t kEeReplyBufs = 0x232f0, kReplySema = 0x23300, kBufferEvent = 0x23304, kBufferTable = 0x23308;
    static constexpr uint32_t kReturnBlock = 0x23330, kRpcQueue = 0x23340, kRpcServer = 0x23358, kRpcBuffer = 0x233a0;
    static constexpr uint32_t kFdMbx = 0x24200;
    static constexpr uint32_t kHeapQueue = 0x23ff0, kHeapServer = 0x24008, kHeapBuffer = 0x24050;
    static constexpr uint32_t kFdDispatchTable = 0x22f80, kThreadEntryTable = 0x23000;
    // .rodata
    static constexpr uint32_t kStrNoSif = 0x22e90, kStrNoFileio = 0x22eac, kFmtOpen = 0x22ec0, kFmtClose = 0x22ee4;
    static constexpr uint32_t kStrNoBuffer = 0x22ef4, kFmtReceiveMbx = 0x22f50, kFmtUnrecognized = 0x22fd8;
    static constexpr uint32_t kStrThreadAllocFail = 0x23074, kFmtSceOpen = 0x23090, kFmtSendMbx = 0x230b4;
    static constexpr uint32_t kStrCdrom = 0x230e8, kStrPowerOffFail = 0x230f0, kStrBanner = 0x23118, kStrHeapBanner = 0x231ac;
    static constexpr uint32_t kCmdReply = 0x80000011, kCmdPowerOff = 0x80000013;
    static constexpr uint32_t kRecordBytes = 0xc40;

    static void Register(NativeIopModule& fileio);

    void Entry(IopContext& ctx);
    void BufferAlloc(IopContext& ctx);
    void BufferFree(IopContext& ctx);
    void BufferReset(IopContext& ctx);
    void RecordAlloc(IopContext& ctx);
    void RecordFree(IopContext& ctx);
    void Reply(IopContext& ctx);
    void Open(IopContext& ctx);
    void Close(IopContext& ctx);
    void Lseek(IopContext& ctx);
    void Read(IopContext& ctx);
    void Write(IopContext& ctx);
    void Ioctl(IopContext& ctx);
    void FdThread(IopContext& ctx);
    void ThreadEntryFor(IopContext& ctx);
    void Handler(IopContext& ctx);
    void PowerOffThread(IopContext& ctx);
    void ServerThread(IopContext& ctx);
    void HeapServerThread(IopContext& ctx);

private:
    // The 0x20-byte reply block {req[0], fno, req[4], req[8], result, 0, 0, 0} at `block`, then fn_20528(block, 0x20).
    void ReplySimple(IopContext& ctx, uint32_t block, uint32_t req, uint32_t fno, uint32_t result);
};

void RegisterFileioNatives(NativeIopModule& fileio);

}  // namespace ps2r::title::sly2
