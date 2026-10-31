// native_fileio.cpp -- the FILEIO_service bodies (native_fileio.h). Each body is the recompiled function at its
// address (the generated TU `iop/fileio/iop_recompiled.cpp`, the ground truth) over the module's RAM words: the
// same library calls in the same order through IopServices, the same state words and reply-block bytes written,
// v0 as the recompiled body leaves it at every exit. Frames are pushed with the recompiled prologue's size so the
// callees see the same $sp; the blocks a kernel service or the reply DMA READS from the stack (thread/sema/event
// parameters, the SIF command header, the reply blocks) are written at the recompiled offsets.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_fileio.h"

#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"

namespace ps2r::title::sly2 {

namespace {

NativeFileio g_fileio;

inline uint32_t A0(const IopContext& c) { return c.gpr[4]; }
inline uint32_t A1(const IopContext& c) { return c.gpr[5]; }
inline uint32_t A2(const IopContext& c) { return c.gpr[6]; }
inline void Ret(IopContext& c, uint32_t v) { c.gpr[2] = v; }
inline void Ret(IopContext& c, int32_t v) { c.gpr[2] = static_cast<uint32_t>(v); }
inline int32_t S(uint32_t v) { return static_cast<int32_t>(v); }
inline uint32_t U(int32_t v) { return static_cast<uint32_t>(v); }
inline uint32_t R32(uint32_t a) { return IopRam::R32(a); }
inline void W32(uint32_t a, uint32_t v) { IopRam::W32(a, v); }

uint32_t Guest(IopContext& ctx, uint32_t addr, uint32_t a0) {
    ctx.gpr[4] = a0;
    IopServices::CallGuest(ctx, addr);
    return ctx.gpr[2];
}
uint32_t Guest(IopContext& ctx, uint32_t addr, uint32_t a0, uint32_t a1) {
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    IopServices::CallGuest(ctx, addr);
    return ctx.gpr[2];
}
// ioman by export ordinal (stock_library_binder.cpp): open 4 close 5 read 6 write 7 lseek 8 ioctl 9 ...
uint32_t Ioman(IopContext& ctx, uint32_t idx, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0) {
    return IopServices::Import(ctx, "ioman", idx, a0, a1, a2);
}
// A thread-parameter block {attr, option, entry, stack size, priority} at `at`.
void ThreadParam(uint32_t at, uint32_t entry, uint32_t stack, uint32_t prio) {
    W32(at + 0x0, 0x2000000);
    W32(at + 0x4, 0);
    W32(at + 0x8, entry);
    W32(at + 0xc, stack);
    W32(at + 0x10, prio);
}
// The kernel's DMA-id spin the module uses after every SIF DMA / command.
void WaitDma(IopContext& ctx, uint32_t id) {
    while (IopServices::sceSifDmaStat(ctx, id) >= 0) {
    }
}

}  // namespace

void NativeFileio::Register(NativeIopModule& m) {
    using M = NativeIopModule;
    using F = NativeFileio;
    m.Add(kEntry, &M::Thunk<F, &F::Entry>, "entry", &g_fileio);
    m.Add(kBufferAlloc, &M::Thunk<F, &F::BufferAlloc>, "buffer_alloc", &g_fileio);
    m.Add(kBufferFree, &M::Thunk<F, &F::BufferFree>, "buffer_free", &g_fileio);
    m.Add(kBufferReset, &M::Thunk<F, &F::BufferReset>, "buffer_reset", &g_fileio);
    m.Add(kRecordAlloc, &M::Thunk<F, &F::RecordAlloc>, "record_alloc", &g_fileio);
    m.Add(kRecordFree, &M::Thunk<F, &F::RecordFree>, "record_free", &g_fileio);
    m.Add(kReply, &M::Thunk<F, &F::Reply>, "reply", &g_fileio);
    m.Add(kOpen, &M::Thunk<F, &F::Open>, "open", &g_fileio);
    m.Add(kClose, &M::Thunk<F, &F::Close>, "close", &g_fileio);
    m.Add(kLseek, &M::Thunk<F, &F::Lseek>, "lseek", &g_fileio);
    m.Add(kRead, &M::Thunk<F, &F::Read>, "read", &g_fileio);
    m.Add(kWrite, &M::Thunk<F, &F::Write>, "write", &g_fileio);
    m.Add(kIoctl, &M::Thunk<F, &F::Ioctl>, "ioctl", &g_fileio);
    m.Add(kFdThread, &M::Thunk<F, &F::FdThread>, "fd_thread", &g_fileio);
    m.Add(kThreadEntryFor, &M::Thunk<F, &F::ThreadEntryFor>, "thread_entry_for", &g_fileio);
    m.Add(kHandler, &M::Thunk<F, &F::Handler>, "rpc_handler", &g_fileio);
    m.Add(kPowerOffThread, &M::Thunk<F, &F::PowerOffThread>, "poweroff_thread", &g_fileio);
    m.Add(kServerThread, &M::Thunk<F, &F::ServerThread>, "server_thread", &g_fileio);
    m.Add(kHeapServerThread, &M::Thunk<F, &F::HeapServerThread>, "heap_server_thread", &g_fileio);
}

void RegisterFileioNatives(NativeIopModule& fileio) {
    NativeFileio::Register(fileio);
    RegisterFileioThreads(fileio);
}

// _start at 0x20000 (frame 0x38): QueryBootMode(3) may forbid SIF / fileio; else the two server threads.
void NativeFileio::Entry(IopContext& ctx) {
    IopStackFrame f(ctx, 0x38);
    const uint32_t sp = f.Sp();
    const uint32_t mode = IopServices::Import(ctx, "loadcore", 12, 3);   // QueryBootMode
    if (mode != 0) {
        const uint32_t flags = R32(mode + 4);
        if (flags & 1) {
            IopServices::Import(ctx, "stdio", 4, kStrNoSif);
            Ret(ctx, 1);
            return;
        }
        if (flags & 2) {
            IopServices::Import(ctx, "stdio", 4, kStrNoFileio);
            Ret(ctx, 1);
            return;
        }
    }
    IopServices::CpuEnableIntr(ctx);
    ThreadParam(sp + 0x10, kServerThread, 0x1000, 0x60);
    int32_t tid = IopServices::CreateThread(ctx, sp + 0x10);
    if (tid <= 0) {
        Ret(ctx, 1);
        return;
    }
    IopServices::StartThread(ctx, tid, 0);
    ThreadParam(sp + 0x10, kHeapServerThread, 0x800, 0x60);
    tid = IopServices::CreateThread(ctx, sp + 0x10);
    if (tid <= 0) {
        Ret(ctx, 1);
        return;
    }
    IopServices::StartThread(ctx, tid, 0);
    Ret(ctx, 0);
}

// fn_20100(int* size) (frame 0x30): a r/w transfer buffer -- the cached one when free, else a fresh AllocSysMemory
// (halving the size up to 8 times), registered in the buffer table; the buffer event flag gates the callers.
void NativeFileio::BufferAlloc(IopContext& ctx) {
    const uint32_t out = A0(ctx);
    IopStackFrame f(ctx, 0x30);
    const uint32_t sp = f.Sp();
    IopServices::WaitEventFlag(ctx, S(R32(kBufferEvent)), 1, 0x10, sp + 0x10);
    IopServices::CpuSuspendIntr(ctx, sp + 0x14);
    int32_t tries = 0;
    uint32_t buf = 0, size = 0;
    uint32_t in_use_mark = 1;
    bool cached = false;
    if (R32(kCachedBuf) != 0) {
        if (R32(kCachedSize) != 0 && R32(kCachedInUse) == 0) {
            buf = R32(kCachedBuf);
            W32(kCachedInUse, 1);
            W32(kLiveBuffers, R32(kLiveBuffers) + 1);
            size = R32(kCachedSize);
            cached = true;
        }
    }
    if (!cached) {
        W32(out, 0);
        size = R32(kBufferSize);
        for (;;) {
            buf = IopServices::AllocSysMemory(ctx, 1, size, 0);
            if (buf == 0) {
                ++tries;
                size = U(S(size + (size >> 31)) >> 1);
                if (tries < 8) continue;
                break;
            }
            W32(kLiveBuffers, R32(kLiveBuffers) + 1);
            if (R32(kCachedBuf) == 0 || R32(kCachedSize) == 0) {
                W32(kCachedBuf, buf);
                W32(kCachedSize, size);
                W32(kCachedInUse, in_use_mark);
            }
            const uint32_t n = R32(kTableSize);
            for (uint32_t i = 0; i < n; ++i) {
                if (R32(kBufferTable + 4 * i) == 0) {
                    W32(kBufferTable + 4 * i, buf);
                    break;
                }
            }
            break;
        }
        if (tries == 8) {
            IopServices::SetEventFlag(ctx, S(R32(kBufferEvent)), 1);
            IopServices::CpuResumeIntr(ctx, f.Word(0x14));
            Ret(ctx, 0);
            return;
        }
    }
    W32(out, size);
    if (R32(kLiveBuffers) < R32(kTableSize)) IopServices::SetEventFlag(ctx, S(R32(kBufferEvent)), 1);
    IopServices::CpuResumeIntr(ctx, f.Word(0x14));
    Ret(ctx, buf);
}

// fn_20328(buffer) (frame 0x20): release the cached buffer (mark free) or FreeSysMemory + table slot; event flag set.
void NativeFileio::BufferFree(IopContext& ctx) {
    const uint32_t buf = A0(ctx);
    IopStackFrame f(ctx, 0x20);
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    if (buf == R32(kCachedBuf) && buf != 0) {
        W32(kCachedInUse, 0);
        W32(kLiveBuffers, R32(kLiveBuffers) - 1);
        IopServices::SetEventFlag(ctx, S(R32(kBufferEvent)), 1);
    } else {
        const int32_t rc = IopServices::FreeSysMemory(ctx, buf);
        if (rc == 0) {
            W32(kLiveBuffers, R32(kLiveBuffers) - 1);
            const uint32_t n = R32(kTableSize);
            for (uint32_t i = 0; i < n; ++i) {
                if (R32(kBufferTable + 4 * i) == buf) {
                    W32(kBufferTable + 4 * i, 0);
                    break;
                }
            }
            IopServices::SetEventFlag(ctx, S(R32(kBufferEvent)), 1);
        }
    }
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
}

// fn_20434() (frame 0x20): drop the cached buffer (fno 254 reconfigures the sizes).
void NativeFileio::BufferReset(IopContext& ctx) {
    IopStackFrame f(ctx, 0x20);
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    if (R32(kCachedBuf) != 0 && R32(kCachedSize) != 0) IopServices::FreeSysMemory(ctx, R32(kCachedBuf));
    W32(kCachedInUse, 0);
    W32(kCachedSize, 0);
    W32(kCachedBuf, 0);
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
}

// fn_204a8() (frame 0x20): a 0xc40-byte request record. fn_204ec(record) (frame 0x20): its release.
void NativeFileio::RecordAlloc(IopContext& ctx) {
    IopStackFrame f(ctx, 0x20);
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    const uint32_t p = IopServices::AllocSysMemory(ctx, 1, kRecordBytes, 0);
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
    Ret(ctx, p);
}

void NativeFileio::RecordFree(IopContext& ctx) {
    const uint32_t p = A0(ctx);
    IopStackFrame f(ctx, 0x20);
    IopServices::CpuSuspendIntr(ctx, f.Sp() + 0x10);
    IopServices::FreeSysMemory(ctx, p);
    IopServices::CpuResumeIntr(ctx, f.Word(0x10));
}

// fn_20528(block, size) (frame 0x40): the reply -- under the reply sema, isceSifSendCmd(0x80000011, header at
// sp+0x18 {.., opt = the reply-buffer index}, 0x10, block -> the EE's reply buffer [index], size); a refused send
// retries after 2 ms; the index flips on every accepted send; the DMA is spun to completion.
void NativeFileio::Reply(IopContext& ctx) {
    const uint32_t block = A0(ctx), size = A1(ctx);
    IopStackFrame f(ctx, 0x40);
    const uint32_t sp = f.Sp();
    IopServices::WaitSema(ctx, S(R32(kReplySema)));
    uint32_t id;
    for (;;) {
        IopServices::CpuSuspendIntr(ctx, sp + 0x28);
        const uint32_t idx = R32(kReplyIndex);
        W32(sp + 0x24, idx);                           // the command header's option word
        W32(sp + 0x14, size);                          // dsize
        W32(sp + 0x10, R32(kEeReplyBufs + 4 * idx));   // dest
        id = IopServices::Import(ctx, "sifcmd", 13, kCmdReply, sp + 0x18, 0x10, block);   // isceSifSendCmd
        if (id != 0) W32(kReplyIndex, R32(kReplyIndex) ^ 1);
        IopServices::CpuResumeIntr(ctx, f.Word(0x28));
        if (id != 0) break;
        IopServices::DelayThread(ctx, 0x7d0);
    }
    WaitDma(ctx, id);
    IopServices::SignalSema(ctx, S(R32(kReplySema)));
}

void NativeFileio::ReplySimple(IopContext& ctx, uint32_t block, uint32_t req, uint32_t fno, uint32_t result) {
    W32(block + 0x4, fno);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    W32(block + 0x10, result);
    W32(block + 0x1c, 0);
    W32(block + 0x18, 0);
    W32(block + 0x14, 0);
    Guest(ctx, kReply, block, 0x20);
}

// fn_2061c(req) (frame 0x38): ioman open(req+0x14, req[0xc], req[0x10]); the 0x20-byte reply; returns the fd.
void NativeFileio::Open(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x38);
    if (S(R32(kDebug)) > 0)
        IopServices::Import(ctx, "stdio", 4, kFmtOpen, req + 0x14, R32(req + 0xc), R32(req + 0x10));
    const uint32_t fd = Ioman(ctx, 4, req + 0x14, R32(req + 0xc), R32(req + 0x10));
    ReplySimple(ctx, f.Sp() + 0x10, req, 0, fd);
    Ret(ctx, fd);
}

// fn_206b4(req) (frame 0x38): close(req[0xc]) + the reply (v0 = the reply's).
void NativeFileio::Close(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x38);
    if (S(R32(kDebug)) > 0) IopServices::Import(ctx, "stdio", 4, kFmtClose, R32(req + 0xc));
    const uint32_t block = f.Sp() + 0x10;
    W32(block + 0x4, 1);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    const uint32_t rc = Ioman(ctx, 5, R32(req + 0xc));
    W32(block + 0x10, rc);
    W32(block + 0x1c, 0);
    W32(block + 0x18, 0);
    W32(block + 0x14, 0);
    Guest(ctx, kReply, block, 0x20);
}

// fn_20744(req) (frame 0x38): lseek(req[0xc], req[0x10], req[0x14]) + the reply.
void NativeFileio::Lseek(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x38);
    const uint32_t block = f.Sp() + 0x10;
    W32(block + 0x4, 4);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    const uint32_t rc = Ioman(ctx, 8, R32(req + 0xc), R32(req + 0x10), R32(req + 0x14));
    W32(block + 0x10, rc);
    W32(block + 0x1c, 0);
    W32(block + 0x18, 0);
    W32(block + 0x14, 0);
    Guest(ctx, kReply, block, 0x20);
}

// fn_207b8(req) (frame 0x110): read(req[0xc], -> EE req[0x10], req[0x14] bytes): the unaligned head (up to 0x40 B)
// and tail are read into the reply block (sp+0x44 / sp+0x84) for the EE to copy; the 64-byte-aligned middle goes
// through the transfer buffer by sceSifSetDma chunks. The 0xa4-byte reply {req[0], 2, req[4], req[8], total,
// head bytes, tail bytes, head dst, tail dst, head[0x40], tail[0x40]} is DMA'd to the EE.
void NativeFileio::Read(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x110);
    const uint32_t sp = f.Sp();
    const uint32_t block = sp + 0x20;
    int32_t rc = 0;         // s2: the last ioman result
    int32_t total = 0;      // s6
    int32_t head = 0;       // s7
    int32_t tail = 0;       // s4
    int32_t mid = 0;        // s3
    uint32_t mid_dst = 0;   // s5
    uint32_t head_dst = 0, tail_dst = 0;
    uint32_t fd = 0;
    const uint32_t buf = Guest(ctx, kBufferAlloc, sp + 0xc8);
    if (buf == 0) {
        IopServices::Import(ctx, "sysmem", 14, kStrNoBuffer);   // Kprintf
        rc = -0xc;
    } else {
        const int32_t size = S(R32(req + 0x14));
        fd = R32(req + 0xc);
        const uint32_t dst = R32(req + 0x10);
        head_dst = dst;
        if (size < 0x40) {
            head = size;
        } else {
            head = (dst & 0x3f) == 0 ? 0 : S(((dst >> 6) << 6) - (dst - 0x40));
            mid_dst = dst + U(head);
            const uint32_t end = dst + U(size);
            mid = S(((end >> 6) << 6) - mid_dst);
            tail_dst = mid_dst + U(mid);
            tail = S(end - tail_dst);
        }
        uint32_t dma = 0;   // s0
        bool done = false;
        if (head > 0) {
            rc = S(Ioman(ctx, 6, fd, sp + 0x44, U(head)));
            if (rc != head) {
                head = rc > 0 ? rc : 0;
                total += head;
                done = true;
            } else {
                total += rc;
            }
        }
        if (!done && mid > 0) {
            for (;;) {
                const int32_t bufsize = S(R32(sp + 0xc8));
                const int32_t chunk = bufsize < mid ? bufsize : mid;
                WaitDma(ctx, dma);
                rc = S(Ioman(ctx, 6, fd, buf, U(chunk)));
                if (rc != chunk) {
                    if (rc <= 0) break;
                    const int32_t aligned = (rc >> 6) << 6;
                    tail = rc - aligned;
                    tail_dst = mid_dst + U(aligned);
                    for (int32_t i = 0; i < tail; ++i) IopRam::W8(block + 0x64 + U(i), IopRam::R8(buf + U(aligned) + U(i)));
                    if (aligned != 0) {
                        W32(sp + 0x10, buf);
                        W32(sp + 0x14, mid_dst);
                        W32(sp + 0x18, U(aligned));
                        W32(sp + 0x1c, 0);
                        for (;;) {
                            IopServices::CpuSuspendIntr(ctx, sp + 0xcc);
                            dma = IopServices::sceSifSetDma(ctx, sp + 0x10, 1);
                            IopServices::CpuResumeIntr(ctx, f.Word(0xcc));
                            if (dma != 0) break;
                            IopServices::DelayThread(ctx, 0x7d0);
                        }
                    }
                    total += rc;
                    break;
                }
                W32(sp + 0x10, buf);
                W32(sp + 0x14, mid_dst);
                W32(sp + 0x18, U(rc));
                W32(sp + 0x1c, 0);
                for (;;) {
                    IopServices::CpuSuspendIntr(ctx, sp + 0xcc);
                    dma = IopServices::sceSifSetDma(ctx, sp + 0x10, 1);
                    IopServices::CpuResumeIntr(ctx, f.Word(0xcc));
                    if (dma != 0) break;
                    IopServices::DelayThread(ctx, 0x7d0);
                }
                total += rc;
                mid -= rc;
                mid_dst += U(rc);
                if (mid <= 0) {
                    // the tail
                    if (tail > 0) {
                        rc = S(Ioman(ctx, 6, fd, sp + 0x84, U(tail)));
                        if (rc != tail) tail = rc > 0 ? rc : 0;
                        total += tail;
                    }
                    break;
                }
            }
        } else if (!done && tail > 0) {
            rc = S(Ioman(ctx, 6, fd, sp + 0x84, U(tail)));
            if (rc != tail) tail = rc > 0 ? rc : 0;
            total += tail;
        }
    }
    W32(block + 0x1c, head_dst);
    W32(block + 0x20, tail_dst);
    W32(block + 0x14, U(head));
    W32(block + 0x18, U(tail));
    W32(block + 0x4, 2);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    W32(block + 0x10, rc >= 0 ? U(total) : (total > 0 ? U(total) : U(rc)));
    Guest(ctx, kReply, block, 0xa4);
    Guest(ctx, kBufferFree, buf);
}

// fn_20b14(req) (frame 0x88): write(req[0xc]) of req[0x18] inline bytes (req+0x1c) then the rest fetched from the
// EE (req[0x10] + inline) through the transfer buffer by sceSifGetOtherData chunks; the 0x20-byte reply.
void NativeFileio::Write(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x88);
    const uint32_t sp = f.Sp();
    int32_t rc = 0;      // s0
    int32_t total = 0;   // s4
    const uint32_t buf = Guest(ctx, kBufferAlloc, sp + 0x58);
    if (buf == 0) {
        IopServices::Import(ctx, "sysmem", 14, kStrNoBuffer);   // Kprintf
        rc = -0xc;
    } else {
        int32_t left = S(R32(req + 0x14));
        const int32_t inline_bytes = S(R32(req + 0x18));
        const uint32_t fd = R32(req + 0xc);
        bool done = false;
        if (inline_bytes > 0) {
            rc = S(Ioman(ctx, 7, fd, req + 0x1c, U(inline_bytes)));
            if (rc != inline_bytes) {
                if (rc > 0) total = rc;
                done = true;
            } else {
                total = rc;
            }
        }
        if (!done) {
            uint32_t src = R32(req + 0x10) + U(inline_bytes);
            left -= inline_bytes;
            while (left != 0) {
                const int32_t bufsize = S(R32(sp + 0x58));
                const int32_t chunk = bufsize < left ? bufsize : left;
                for (;;) {
                    W32(sp + 0x10, 0);
                    const int32_t got = S(IopServices::sceSifGetOtherData(ctx, sp + 0x38, src, buf, U(chunk)));
                    if (got >= 0) break;
                    IopServices::DelayThread(ctx, 0x7d0);
                }
                rc = S(Ioman(ctx, 7, fd, buf, U(chunk)));
                if (rc != chunk) {
                    if (rc > 0) total += rc;
                    break;
                }
                total += rc;
                left -= rc;
                src += U(rc);
            }
        }
    }
    const uint32_t block = sp + 0x18;
    W32(block + 0x4, 3);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    W32(block + 0x10, rc >= 0 ? U(total) : (total > 0 ? U(total) : U(rc)));
    W32(block + 0x1c, 0);
    W32(block + 0x18, 0);
    W32(block + 0x14, 0);
    Guest(ctx, kReply, block, 0x20);
    Guest(ctx, kBufferFree, buf);
}

// fn_20fe4(record) (frame 0x438; a one-shot thread): ioctl(req[0xc], req[0x10], req[0x41c] ? req+0x14 : 0); the
// 0x41c-byte reply {req[0], 5, req[4], req[8], result, req[0x414], req[0x418], ...}; record freed; ExitThread.
void NativeFileio::Ioctl(IopContext& ctx) {
    const uint32_t req = A0(ctx);
    IopStackFrame f(ctx, 0x438);
    const uint32_t block = f.Sp() + 0x10;
    W32(block + 0x4, 5);
    W32(block + 0x0, R32(req + 0));
    W32(block + 0x8, R32(req + 4));
    W32(block + 0xc, R32(req + 8));
    W32(block + 0x14, R32(req + 0x414));
    W32(block + 0x18, R32(req + 0x418));
    const uint32_t arg = R32(req + 0x41c) != 0 ? req + 0x14 : 0;
    const uint32_t rc = Ioman(ctx, 9, R32(req + 0xc), R32(req + 0x10), arg);
    W32(block + 0x10, rc);
    Guest(ctx, kReply, block, 0x41c);
    Guest(ctx, kRecordFree, req);
    IopServices::ExitThread(ctx);
}

// fn_21b78(record) (frame 0x28): the per-fd thread. Opens (the record's request at +0x10), frees the record; a
// failed open ends the thread; else serves the message box (record[0xc]) -- close (fno 1) ends it, read/write/
// lseek/lseek64 (2/3/4/22) reply and loop, anything else is just freed.
void NativeFileio::FdThread(IopContext& ctx) {
    uint32_t record = A0(ctx);
    IopStackFrame f(ctx, 0x28);
    const uint32_t sp = f.Sp();
    const uint32_t mbx = R32(record + 0xc);
    const int32_t fd = S(Guest(ctx, kOpen, record + 0x10));
    Guest(ctx, kRecordFree, record);
    if (fd >= 0) {
        for (;;) {
            IopServices::Import(ctx, "thmsgbx", 8, sp + 0x10, mbx);   // ReceiveMbx
            record = R32(sp + 0x10);
            if (S(R32(kDebug)) > 0) IopServices::Import(ctx, "stdio", 4, kFmtReceiveMbx, mbx, R32(record + 8), record);
            const uint32_t fno = R32(record + 8);
            bool exit = false;
            if (fno - 1 < 0x16) {
                switch (fno) {
                    case 1:
                        Guest(ctx, kClose, record + 0x10);
                        Guest(ctx, kRecordFree, record);
                        exit = true;
                        break;
                    case 2: Guest(ctx, kRead, record + 0x10); break;
                    case 3: Guest(ctx, kWrite, record + 0x10); break;
                    case 4: Guest(ctx, kLseek, record + 0x10); break;
                    case 0x16: Guest(ctx, kLseek64, record + 0x10); break;
                    default: break;
                }
            }
            if (exit) break;
            Guest(ctx, kRecordFree, record);
        }
    }
    IopServices::Import(ctx, "thmsgbx", 5, mbx);   // DeleteMbx
    IopServices::ExitThread(ctx);
}

// fn_21ca4(fno) (frame 0x18): the one-shot thread entry for a request kind (the module's table at 0x23000); the
// fd kinds (1-4, 22) and 27+ are "unrecognized" (printf, 0).
void NativeFileio::ThreadEntryFor(IopContext& ctx) {
    const uint32_t fno = A0(ctx);
    IopStackFrame f(ctx, 0x18);
    static const uint32_t kEntries[29] = {
        kFdThread, 0,       0,       0,        0,         kIoctl,  kRemove, kMkdir,   kRmdir,  kDopen,
        kDclose,   kDread,  kGetstat, kChstat, kFormat,   kAddDrv, kDelDrv, kRename,  kChdir,  kSync,
        kMount,    kUmount, 0,        kDevctl, kSymlink,  kReadlink, kIoctl2, 0,      kDevctlBlk};
    const uint32_t entry = fno < 29 ? kEntries[fno] : 0;
    if (entry == 0) {
        IopServices::Import(ctx, "stdio", 4, kFmtUnrecognized, fno);
        Ret(ctx, 0);
        return;
    }
    Ret(ctx, entry);
}

// fn_21e68(fno, buf, size) (frame 0xb0): the RPC 0x80000001 handler. Returns the module's return block (0x23330).
void NativeFileio::Handler(IopContext& ctx) {
    const uint32_t fno = A0(ctx), buf = A1(ctx), size = A2(ctx);
    IopStackFrame f(ctx, 0xb0);
    const uint32_t sp = f.Sp();
    W32(kReturnBlock, 1);
    if (fno == 0xff) {   // init: the EE's two reply buffers
        if (size == 8) {
            W32(kEeReplyBufs, R32(buf));
            W32(kEeReplyBufs + 4, R32(buf + 4));
        } else {
            W32(kEeReplyBufs + 4, R32(buf));
            W32(kEeReplyBufs, R32(kEeReplyBufs + 4));
        }
        W32(kReturnBlock + 4, 2);
        W32(kReturnBlock, R32(kVersionWord));
        Ret(ctx, kReturnBlock);
        return;
    }
    if (fno == 0xfe) {   // buffer configuration
        IopServices::CpuSuspendIntr(ctx, sp + 0x80);
        int32_t rc;
        if (R32(kLiveBuffers) != 0) {
            rc = -0x10;
        } else {
            Guest(ctx, kBufferReset, A0(ctx));
            W32(kBufferSize, R32(buf));
            W32(kTableSize, R32(buf + 4));
            rc = 0;
        }
        W32(kReturnBlock, U(rc));
        IopServices::CpuResumeIntr(ctx, f.Word(0x80));
        Ret(ctx, kReturnBlock);
        return;
    }
    if (fno == 0xfd) {   // request-thread priority
        const uint32_t prio = R32(buf);
        int32_t rc = 0;
        if (prio - 9 < 0x73) {
            W32(kThreadPrio, prio);
            IopServices::ChangeThreadPriority(ctx, 0, prio);
        } else {
            rc = -0x16;
        }
        W32(kReturnBlock, U(rc));
        Ret(ctx, kReturnBlock);
        return;
    }
    // A free request-thread slot: the highest free one, dormant threads deleted on the way.
    int32_t slot = -1;
    for (int32_t i = 0; i < S(kThreadSlotCount); ++i) {
        const uint32_t at = kThreadSlots + 4 * U(i);
        const int32_t tid = S(R32(at));
        if (tid == -1) {
            slot = i;
            continue;
        }
        IopServices::ReferThreadStatus(ctx, tid, sp + 0x38);
        if (R32(sp + 0x40) == 0x10) {   // THS_DORMANT
            IopServices::DeleteThread(ctx, tid);
            slot = i;
            W32(at, 0xffffffffu);
        }
    }
    if (slot == -1) {
        IopServices::Import(ctx, "stdio", 4, kStrThreadAllocFail);
        W32(kReturnBlock, 0);
        Ret(ctx, kReturnBlock);
        return;
    }
    const uint32_t record = Guest(ctx, kRecordAlloc, A0(ctx));
    if (record == 0) {
        W32(kReturnBlock, 0);
        Ret(ctx, kReturnBlock);
        return;
    }
    bool fail = false;
    if ((fno < 5 && fno != 0) || fno == 0x16) {
        // close/read/write/lseek/lseek64: the request goes to the fd's message box (its per-fd thread).
        IopServices::Import(ctx, "sysclib", 12, record + 0x10, buf, size);   // memcpy
        uint32_t fd = 0;
        switch (fno) {
            case 1: fd = R32(buf + 0x10); break;
            case 2: fd = R32(buf + 0x1c); break;
            case 3: fd = R32(buf + 0x2c); break;
            case 4: fd = R32(buf + 0x18); break;
            case 0x16: fd = R32(buf + 0x1c); break;
            default: break;
        }
        const uint32_t mbx = R32(kFdMbx + 4 * fd);
        IopRam::W8(record + 4, 0);
        W32(record + 8, fno);
        W32(record + 0xc, mbx);
        W32(kThreadSlots + 4 * U(slot), 0xffffffffu);
        if (S(R32(kDebug)) > 0) {
            f.Word(0x10, record);
            IopServices::Import(ctx, "stdio", 4, kFmtSendMbx, fd, mbx, fno);
        }
        const uint32_t rc = IopServices::Import(ctx, "thmsgbx", 6, R32(record + 0xc), record);   // SendMbx
        if (rc == 0) {
            Ret(ctx, kReturnBlock);
            return;
        }
        fail = true;
    } else {
        if (fno == 0) {
            IopServices::Import(ctx, "sysclib", 12, record + 0x10, buf, size);   // memcpy
            W32(sp + 0x30, 0);
            W32(sp + 0x34, 0xff);
            const int32_t mbx = S(IopServices::Import(ctx, "thmsgbx", 4, sp + 0x30));   // CreateMbx
            W32(record + 0xc, U(mbx));
            if (mbx < 0) {
                fail = true;
            } else {
                const uint32_t ee_fd = R32(buf + 0x414);
                if (S(R32(kDebug)) > 0) IopServices::Import(ctx, "stdio", 4, kFmtSceOpen, ee_fd, U(mbx));
                W32(kFdMbx + 4 * ee_fd, R32(record + 0xc));
                W32(record + 8, fno);
            }
        } else {
            IopServices::Import(ctx, "sysclib", 12, record, buf, size);   // memcpy: the record IS the request
        }
        if (!fail) {
            const uint32_t entry = Guest(ctx, kThreadEntryFor, fno);
            W32(sp + 0x18, 0x2000000);
            W32(sp + 0x20, entry);
            W32(sp + 0x24, 0x1800);
            W32(sp + 0x1c, 0);
            W32(sp + 0x28, R32(kThreadPrio));
            if (entry == 0) {
                fail = true;
            } else {
                const int32_t tid = IopServices::CreateThread(ctx, sp + 0x18);
                if (tid < 0) {
                    fail = true;
                } else {
                    W32(kThreadSlots + 4 * U(slot), U(tid));
                    if (IopServices::StartThread(ctx, tid, record) >= 0) {
                        Ret(ctx, kReturnBlock);
                        return;
                    }
                    fail = true;
                }
            }
        }
    }
    if (fail) Guest(ctx, kRecordFree, record);
    W32(kReturnBlock, 0);
    Ret(ctx, kReturnBlock);
}

// fn_22298 (frame 0x40; a thread): devctl("cdrom0:", 0x4391 = the power-off event flag) until it answers (the
// engine's ioman never does: 128 ms retries forever), then forwards each power-off event as SIF command 0x80000013.
void NativeFileio::PowerOffThread(IopContext& ctx) {
    IopStackFrame f(ctx, 0x40);
    const uint32_t sp = f.Sp();
    for (;;) {
        W32(sp + 0x10, sp + 0x28);
        W32(sp + 0x14, 4);
        const int32_t rc = S(IopServices::Import(ctx, "ioman", 31, kStrCdrom, 0x4391, 0, 0));   // devctl
        if (rc >= 0) break;
        if (S(R32(kDebug)) > 0) IopServices::Import(ctx, "stdio", 4, kStrPowerOffFail);
        IopServices::DelayThread(ctx, 0x1f400);
    }
    for (;;) {
        IopServices::ClearEventFlag(ctx, S(R32(sp + 0x28)), 0xffffffefu);
        IopServices::WaitEventFlag(ctx, S(R32(sp + 0x28)), 0x10, 0, sp + 0x2c);
        uint32_t id;
        for (;;) {
            W32(sp + 0x10, 0);
            W32(sp + 0x14, 0);
            id = IopServices::Import(ctx, "sifcmd", 12, kCmdPowerOff, sp + 0x18, 0x10, 0);   // sceSifSendCmd
            if (id != 0) break;
            IopServices::DelayThread(ctx, 0x7d0);
        }
        WaitDma(ctx, id);
    }
}

// fn_22384 (frame 0xa8; the RPC 0x80000001 server thread).
void NativeFileio::ServerThread(IopContext& ctx) {
    IopStackFrame f(ctx, 0xa8);
    const uint32_t sp = f.Sp();
    if (IopServices::Import(ctx, "sifman", 29) == 0) IopServices::Import(ctx, "sifman", 5);   // sceSifCheckInit / sceSifInit
    IopServices::Import(ctx, "stdio", 4, kStrBanner);
    IopServices::sceSifInitRpc(ctx, 0);
    W32(sp + 0x20, 2);
    W32(sp + 0x28, 0);
    W32(sp + 0x24, 0);
    const int32_t ef = IopServices::CreateEventFlag(ctx, sp + 0x20);
    W32(kBufferEvent, U(ef));
    IopServices::SetEventFlag(ctx, ef, 1);
    W32(sp + 0x30, 1);
    W32(sp + 0x38, 1);
    W32(sp + 0x3c, 1);
    W32(sp + 0x34, 0);
    W32(kReplySema, U(IopServices::CreateSema(ctx, sp + 0x30)));
    IopServices::ReferThreadStatus(ctx, 0, sp + 0x58);
    const uint32_t prio = R32(sp + 0x74);
    ThreadParam(sp + 0x40, kPowerOffThread, 0x800, prio);
    const int32_t tid = IopServices::CreateThread(ctx, sp + 0x40);
    IopServices::StartThread(ctx, tid, 0);
    const int32_t self = IopServices::GetThreadId(ctx);
    IopServices::sceSifSetRpcQueue(ctx, kRpcQueue, self);
    f.Word(0x10, 0);
    f.Word(0x14, 0);
    f.Word(0x18, kRpcQueue);
    IopServices::sceSifRegisterRpc(ctx, kRpcServer, 0x80000001u, kHandler, kRpcBuffer);
    IopServices::sceSifRpcLoop(ctx, kRpcQueue);
    Ret(ctx, 0);
}

// fn_2290c (frame 0x28; the RPC 0x80000003 IOP-heap server thread; the engine HLEs its EE client).
void NativeFileio::HeapServerThread(IopContext& ctx) {
    IopStackFrame f(ctx, 0x28);
    if (IopServices::Import(ctx, "sifman", 29) == 0) IopServices::Import(ctx, "sifman", 5);
    IopServices::Import(ctx, "stdio", 4, kStrHeapBanner);
    IopServices::sceSifInitRpc(ctx, 0);
    const int32_t self = IopServices::GetThreadId(ctx);
    IopServices::sceSifSetRpcQueue(ctx, kHeapQueue, self);
    f.Word(0x10, 0);
    f.Word(0x14, 0);
    f.Word(0x18, kHeapQueue);
    IopServices::sceSifRegisterRpc(ctx, kHeapServer, 0x80000003u, 0x227d4, kHeapBuffer);
    IopServices::sceSifRpcLoop(ctx, kHeapQueue);
    Ret(ctx, 0);
}

}  // namespace ps2r::title::sly2
