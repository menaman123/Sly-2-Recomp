// snd989_refill.cpp -- the stream refill machinery: the request queues (fn_4a9d4 refill, fn_4aa70 start), the refill
// dispatcher (fn_471a0), the memory-source refill (fn_47a88: EE RAM -> IOP half buffer by sceSifGetOtherData / bcopy,
// the position record pushed to the EE by DMA), the SPU upload (fn_49788 SdVoiceTrans) and its completion (fn_3aa3c
// the libsd callback, fn_49c58 the slot's transfer-done step), the DMA completion callback (fn_351c8), and the ADPCM
// block header fix-up (fn_4a7c4). Ported from the recompiled 989snd_Library over the module's RAM.
//
// A half record `hr` (0x2c bytes inside a stream voice record `st` at st + 0x18 + half * 0x2c; st = hr - 0x10 for the
// first half): hr[0] flags (0x10 playing, 0x80 refillable, 0x100 stereo pair, 0x800 CD source, 0x2000 / 0x1c000 the
// other sources), hr[4] its IOP buffer, hr[8] its SPU address, hr[0xc] queue link, hr[0x10] the owning st,
// hr[0x14] / hr[0x18] the end-of-data flag and byte count the refill leaves. Stream voice record words used here:
// st[0xc] the SPU buffer size (two halves), st[0x84] bytes queued, st[0x88] the source handle (-1 = none), st[0x8c],
// st[0x94] the hook block, st[0x98] / st[0x9c] the EE source region (size, base), st[0xa0] / st[0xa4] the read
// position / bytes read, st[0xac] bytes left, st[0xd0] the owning handle (h[0x34] its chunk count).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kStartQueueHead = G::kData + 0x233c;    // fn_4aa70: half records waiting to start
constexpr uint32_t kStartQueueTail = G::kData + 0x2340;
constexpr uint32_t kRefillQueueHead = G::kData + 0x2344;   // fn_4a9d4: half records waiting for data
constexpr uint32_t kRefillQueueTail = G::kData + 0x2348;
constexpr uint32_t kRefillCurrent = G::kData + 0x234c;     // the half record being refilled (fn_471a0 / fn_47a88)
constexpr uint32_t kTransSlot = G::kData + 0x2354;         // per SPU DMA channel: the half record being uploaded
constexpr uint32_t kTransBusy = G::kData + 0x1940;         // per channel: an upload is in flight
constexpr uint32_t kTransDone0 = G::kData + 0x1948;        // channel 0 / 1 completed flags (fn_3aa3c)
constexpr uint32_t kTransDone1 = G::kData + 0x194c;
constexpr uint32_t kTransSema = G::kData + 0x1950;         // signalled from the libsd callback
constexpr uint32_t kRefillStatus = G::kData + 0x231c;      // -1 while a refill failed, 0 after one completed
constexpr uint32_t kPositionDmaSema = G::kData + 0x62c;    // the position record's EE DMA completion (fn_351c8)
constexpr uint32_t kPositionRecord = 0x58e68;              // {size, base, position, bytes}: 16 bytes DMA'd to the EE
constexpr uint32_t kPositionDmaCallback = 0x351c8;         // fn_351c8
}  // namespace

// fn_351c8(p): the EE DMA completion: iSignalSema(*p).
void fn_351c8(IopContext& c) {
    const uint32_t p = A0(c);
    Frame f(c, 0x18);
    imp::iSignalSema(c, Ld32(p));
}

// fn_3aa3c(ch): the libsd transfer callback for SPU DMA channel `ch`: the channel is free, its done flag set, the
// transfer semaphore signalled (from the interrupt). Returns 0.
void fn_3aa3c(IopContext& c) {
    const uint32_t ch = A0(c);
    Frame f(c, 0x18);
    St32(kTransBusy + ch * 4, 0);
    if (ch == 0) St32(kTransDone0, 1);
    else St32(kTransDone1, 1);
    imp::iSignalSema(c, Ld32(kTransSema));
    Ret(c, 0);
}

// fn_4a9d4(hr): queue a half record for refill (flag 0x80 required, else error 0x48 and 0). Returns 1.
void fn_4a9d4(IopContext& c) {
    const uint32_t hr = A0(c);
    Frame f(c, 0x28);
    if (!(Ld32(hr) & 0x80)) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x48, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
    const uint32_t tail = Ld32(kRefillQueueTail);
    if (tail == 0) St32(kRefillQueueHead, hr);
    else St32(tail + 0xc, hr);
    St32(kRefillQueueTail, hr);
    St32(hr + 0xc, 0);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
    Ret(c, 1);
}

// fn_4aa70(hr): queue a half record to start; one already queued is an error (0x49, the voice number) and 0.
// Returns 1.
void fn_4aa70(IopContext& c) {
    const uint32_t hr = A0(c);
    Frame f(c, 0x28);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
    for (uint32_t p = Ld32(kStartQueueHead); p != 0; p = Ld32(p + 0xc)) {
        if (p != hr) continue;
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x49, Ld32(Ld32(hr + 0x10) + 0x7c), 0, 0);
        Ret(c, 0);
        return;
    }
    const uint32_t tail = Ld32(kStartQueueTail);
    if (tail == 0) St32(kStartQueueHead, hr);
    else St32(tail + 0xc, hr);
    St32(kStartQueueTail, hr);
    St32(hr + 0xc, 0);
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
    Ret(c, 1);
}

// fn_4a7c4(buf, size, more, mark_end): the ADPCM block flags of a half buffer: `mark_end` -> first block flag 6,
// block 1 flag 2, last block 3; a 16-byte buffer is doubled (the block copied, flags 7 / 1) and sized 0x20; a 32-byte
// one gets flag 1; else the last block gets 3 (no more: first 6, block 1 2) or 7 with the one before 1 (first 4).
// Returns the size.
void fn_4a7c4(IopContext& c) {
    const uint32_t buf = A0(c), size = A1(c), more = A2(c), mark_end = A3(c);
    Frame f(c, 0x18);
    if (mark_end != 0) {
        St8(buf + 1, 6);
        St8(buf + 0x11, 2);
        St8(buf + size - 0xf, 3);
        Ret(c, size);
        return;
    }
    if (size == 0x10) {
        Import(c, "sysclib", 13, buf + 0x10, buf, 0x10);
        Import(c, "sysclib", 14, buf, 0, 0x10);
        St8(buf + 0x11, 7);
        St8(buf + 1, 1);
        Ret(c, 0x20);
        return;
    }
    if (size == 0x20) {
        St8(buf + 1, 1);
        Ret(c, 0x20);
        return;
    }
    if (more == 0) {
        St8(buf + 1, 6);
        St8(buf + 0x11, 2);
        St8(buf + size - 0xf, 3);
    } else {
        St8(buf + 1, 4);
        St8(buf + size - 0xf, 7);
        St8(buf + size - 0x1f, 1);
    }
    Ret(c, size);
}

// fn_49788(ch): start the SPU upload of channel `ch`'s half record (SdVoiceTrans(ch, 0, iop, spu, size): size = half
// the stream's buffer, times the handle's chunk count for a stereo pair); no record -> the channel is free.
void fn_49788(IopContext& c) {
    const int32_t ch = S32(A0(c));
    Frame f(c, 0x20);
    const uint32_t hr = Ld32(kTransSlot + U32(ch) * 4);
    if (hr == 0) {
        St32(kTransBusy + U32(ch) * 4, 0);
        return;
    }
    const uint32_t st = Ld32(hr + 0x10);
    uint32_t size;
    if (Ld32(hr) & 0x100) size = (Ld32(st + 0xc) >> 1) * Ld32(Ld32(st + 0xd0) + 0x34);
    else size = Ld32(st + 0xc) >> 1;
    St32(f.At(0x10), size);
    const uint32_t rec = Ld32(kTransSlot + U32(ch) * 4);
    imp::SdVoiceTrans(c, U32(static_cast<int16_t>(ch)), 0, Ld32(rec + 4), Ld32(rec + 8));
}

// fn_471a0(offset): refill the current half record (kRefillCurrent) from `offset` into its buffer: a stopping handle
// drops it; a playing record (0x10) asks for 0x800 (< 0x7d1 left) / 0x1000 bytes; else the request is the rest of the
// half (rounded down to 2 KiB) or what is left (st[0xac], then the record is marked ended: hr[0x14] = 1, st bit 0x20);
// the block count (a2) follows; the source kind (st flags 0x800 CD / 0x2000 / 0x1c000 memory / else) picks the reader.
void fn_471a0(IopContext& c) {
    const uint32_t offset = A0(c);
    Frame f(c, 0x18);
    const uint32_t hr = Ld32(kRefillCurrent);
    if (hr == 0) return;
    const uint32_t st = Ld32(hr + 0x10);
    if (Ld8(Ld32(st + 0xd0) + 0x16) & 4) {
        St32(kRefillCurrent, 0);
        return;
    }
    const uint32_t flags = Ld32(hr);
    uint32_t t0, a2;
    if (flags & 0x10) {
        if (Ld32(st + 0x8c) < 0x7d1) {
            t0 = 0x800;
            a2 = 1;
        } else {
            t0 = 0x1000;
            a2 = 2;
        }
    } else {
        const int32_t rest = S32((Ld32(st + 0xc) >> 1) - offset);
        t0 = U32(((rest < 0 ? rest + 0x7ff : rest) >> 11) << 11);
        const uint32_t left = Ld32(st + 0xac);
        if (t0 < left) {
            St32(hr + 0x14, 0);
            St32(hr + 0x18, t0 + offset);
            St32(st + 0xac, Ld32(st + 0xac) - t0);
        } else {
            const uint32_t end = left + offset;
            if (!(flags & 0x100)) t0 = left;
            St32(hr + 0x18, end);
            St32(st + 0xac, 0);
            const uint32_t st2 = Ld32(hr + 0x10);
            St32(st2, Ld32(st2) | 0x20);
            St32(hr + 0x14, 1);
        }
        const uint32_t cur = Ld32(kRefillCurrent);
        if (Ld32(cur) & 0x100) {
            const uint32_t st3 = Ld32(cur + 0x10);
            const uint32_t h = Ld32(st3 + 0xd0);
            a2 = ((Ld32(st3 + 0xc) >> 1) * Ld32(h + 0x34) + 0x7ff) >> 11;
        } else {
            const int32_t v = S32(t0 + 0x7ff);
            a2 = U32(v >= 0 ? v >> 11 : S32(t0 + 0xffe) >> 11);
        }
    }
    const uint32_t cur = Ld32(kRefillCurrent);
    const uint32_t a3 = Ld32(Ld32(cur + 0x10));
    if (a3 & 0x800) Call(c, 0x47388, offset, t0, a2, a3);
    else if (a3 & 0x2000) Call(c, 0x4781c, offset, t0, a2, a3);
    else if (a3 & 0x1c000) Call(c, 0x47a88, offset, t0, a2, a3);
    else Call(c, 0x47e7c, offset, t0, a2, a3);
}

// fn_47a88(offset, size, blocks, flags): the memory-source refill of the current half record: no source (st[0x88] ==
// -1) -> error 0x6e, the handle stopped, -1. The chunk callback (fn_47950 which=1) may veto. Then `size` bytes (a
// stereo pair: blocks << 11) are copied from the EE region (st[0x9c] base, st[0x98] size, position st[0xa0], wrapping
// at the end) into the half buffer + offset by sceSifGetOtherData (flags & 0x4000) or bcopy; a negative copy status
// is reported (0x7d) and stops the handle (kRefillStatus = -1). Otherwise the position advances; flags & 0xc000 push
// the position record to the EE by DMA (sceSifSetDmaIntr + the completion semaphore, waited while sceSifDmaStat says
// pending), flags & 0x10000 run the chunk callback (which=0), else the hook block takes the position. kRefillStatus
// = 0, st[0x84] += blocks (unless playing), the CD-read semaphore signalled. Returns 1.
void fn_47a88(IopContext& c) {
    const uint32_t offset = A0(c), size_in = A1(c), blocks = A2(c), flags = A3(c);
    Frame f(c, 0x70);
    uint32_t hr = Ld32(kRefillCurrent);
    uint32_t st = Ld32(hr + 0x10);
    uint32_t copied = 0;
    if (Ld32(st + 0x88) == 0xffffffffu) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x6e, 0, 0, 0);
        hr = Ld32(kRefillCurrent);
        st = Ld32(hr + 0x10);
        const uint32_t h = Ld32(st + 0xd0);
        St8(h + 0x16, Ld8(h + 0x16) | 4);
        St32(kRefillCurrent, 0);
        Ret(c, 0xffffffffu);
        return;
    }
    uint32_t size = size_in;
    uint32_t src = Ld32(st + 0xa0);
    uint32_t dst = Ld32(hr + 4) + offset;
    if (Ld32(hr) & 0x100) size = blocks << 11;
    int32_t status = S32(Call(c, 0x47950, st, size, 1));
    if (status == 0 && size != 0) {
        for (;;) {
            st = Ld32(Ld32(kRefillCurrent) + 0x10);
            const uint32_t to_end = Ld32(st + 0x9c) + Ld32(st + 0x98) - src;
            const uint32_t n = to_end < size ? to_end : size;
            if (flags & 0x4000) {
                St32(f.At(0x10), 0);
                status = S32(Import(c, "sifcmd", 23, f.At(0x18), src, dst, n));
            } else {
                Import(c, "sysclib", 12, dst, src, n);
                status = 0;
            }
            st = Ld32(Ld32(kRefillCurrent) + 0x10);
            src += n;
            if (src == Ld32(st + 0x9c) + Ld32(st + 0x98)) src = Ld32(st + 0x9c);
            size -= n;
            dst += n;
            copied += n;
            if (status != 0) break;
            if (size == 0) break;
        }
    }
    if (status < 0) {
        St32(kRefillStatus, 0xffffffffu);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x7d, U32(status), 0, 0);
        st = Ld32(Ld32(kRefillCurrent) + 0x10);
        const uint32_t h = Ld32(st + 0xd0);
        St8(h + 0x16, Ld8(h + 0x16) | 4);
        St32(kRefillCurrent, 0);
        Ret(c, U32(status));
        return;
    }
    hr = Ld32(kRefillCurrent);
    if (!(Ld32(hr) & 0x10)) {
        st = Ld32(hr + 0x10);
        St32(st + 0xa0, src);
        st = Ld32(hr + 0x10);
        St32(st + 0xa4, Ld32(st + 0xa4) + copied);
        if (flags & 0xc000) {
            st = Ld32(hr + 0x10);
            St32(kPositionRecord, Ld32(st + 0x98));
            st = Ld32(hr + 0x10);
            St32(kPositionRecord + 4, Ld32(st + 0x9c));
            st = Ld32(hr + 0x10);
            St32(kPositionRecord + 8, Ld32(st + 0xa0));
            st = Ld32(hr + 0x10);
            St32(f.At(0x34), kPositionRecord);
            St32(kPositionRecord + 0xc, Ld32(st + 0xa4));
            st = Ld32(hr + 0x10);
            St32(f.At(0x3c), 0x40);
            St32(f.At(0x40), 0);
            St32(f.At(0x38), Ld32(st + 0x94));
            imp::CpuSuspendIntr(c, f.At(0x44));
            const uint32_t id = Import(c, "sifman", 32, f.At(0x34), 1, kPositionDmaCallback, kPositionDmaSema);
            if (id == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x44)));
            for (;;) {
                const int32_t stat = S32(Import(c, "sifman", 8, id));
                if (stat < 0) break;
                if (imp::WaitSema(c, Ld32(kPositionDmaSema)) != 0) {
                    St32(f.At(0x10), 0);
                    Call(c, 0x35140, 0xfa2, 0, 0, 0);
                }
            }
        } else if (flags & 0x10000) {
            Call(c, 0x47950, Ld32(hr + 0x10), copied, 0);
        } else {
            st = Ld32(hr + 0x10);
            const uint32_t hook = Ld32(st + 0x94);
            St32(hook + 8, Ld32(st + 0xa0));
            st = Ld32(hr + 0x10);
            St32(hook + 0xc, Ld32(st + 0xa4));
        }
    }
    hr = Ld32(kRefillCurrent);
    St32(kRefillStatus, 0);
    if (!(Ld32(hr) & 0x10)) {
        st = Ld32(hr + 0x10);
        St32(st + 0x84, Ld32(st + 0x84) + blocks);
    }
    imp::SignalSema(c, Ld32(G::kCdReadSema));
    Ret(c, 1);
}

// fn_49c58(ch): the SPU upload of channel `ch` completed (its half record kTransSlot[ch]): the record's transient
// flags are cleared (keeps 0xf00); a stopped record / handle only releases the slot. An initial upload (flag 4, or
// 0x1000 for the handle's first record) keys the voices on (fn_4a050) and marks the handle started (h[0x16] |= 8);
// a continuing one (flag 1) does nothing more; otherwise the loop address (LSAX) is written: for a stereo pair every
// record's half address (st + 0x18 + 0x2c * (the record is not the first half)), else the helper voice's and the
// voice's from hr[8]. Then a record still playing (flags & 0x1a, not 0x420 == 0x20) that was flagged 0x20 gets 0x40
// and, unless a CD source, is queued to start (fn_4aa70; a failure stops the handle); hr[0x14] (ended) sets 0x40.
// The slot is cleared and both the ramp-waiter and the stream semaphores signalled.
void fn_49c58(IopContext& c) {
    const uint32_t ch = A0(c), a1_in = A1(c), a2_in = A2(c), a3_in = A3(c);
    Frame f(c, 0x40);
    const uint32_t slot = kTransSlot + ch * 4;
    uint32_t hr = Ld32(slot);
    if (hr == 0) {
        St32(kTransBusy + ch * 4, 0);
        return;
    }
    uint32_t st = Ld32(hr + 0x10);
    if (Ld32(st) & 0x800) Call(c, 0x4992c, hr, a1_in, a2_in, a3_in);
    hr = Ld32(slot);
    const uint32_t fl = Ld32(hr);
    St32(hr, fl & 0xf00);
    const uint32_t fl_first = fl & 1, fl_init = fl & 4, fl_init2 = fl & 0x1000, fl_more = fl & 0x20;
    hr = Ld32(slot);
    st = Ld32(hr + 0x10);
    const uint32_t ended = Ld32(hr + 0x14);
    St32(f.At(0x14), ended);
    auto finish = [&]() {
        const uint32_t sema = Ld32(G::kRampWaiterSema);
        St32(slot, 0);
        imp::SignalSema(c, sema);
        imp::SignalSema(c, Ld32(G::kStreamSema));
    };
    if (Ld32(st) & 1) {
        finish();
        return;
    }
    const uint32_t h = Ld32(st + 0xd0);
    const uint32_t hflags = Ld8(h + 0x16);
    if (hflags & 4) {
        finish();
        return;
    }
    if (fl_init != 0) {
        St8(h + 0x16, hflags | 8);
        Call(c, 0x4a050, Ld32(Ld32(slot) + 0x10), 1);
    } else if (fl_init2 != 0) {
        St8(h + 0x16, hflags | 8);
        const uint32_t st2 = Ld32(Ld32(slot) + 0x10);
        Call(c, 0x4a050, Ld32(Ld32(st2 + 0xd0) + 4), 1);
    } else if (fl_first == 0) {
        const int32_t helper = Lds32(h + 0x38);
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
        hr = Ld32(slot);
        if (Ld32(hr) & 0x100) {
            uint32_t rec = Ld32(hr + 0x10);
            if (rec != 0) {
                const uint32_t half_off = (hr != rec + 0x10) ? 0x2c : 0;
                for (; rec != 0; rec = Ld32(rec + 0xc8)) {
                    const int32_t v = Lds32(rec + 0x7c);
                    const uint32_t half = rec + 0x18 + half_off;
                    const uint32_t entry = (VoiceEntry(v) | 0x2140) & 0xffff;
                    imp::SdSetAddr(c, entry, Ld32(half));
                    imp::SdSetAddr(c, entry, Ld32(half));
                }
            }
        } else {
            if (helper != -1) imp::SdSetAddr(c, (VoiceEntry(helper) | 0x2140) & 0xffff, Ld32(hr + 8));
            hr = Ld32(slot);
            const int32_t v = Lds32(Ld32(hr + 0x10) + 0x7c);
            imp::SdSetAddr(c, (VoiceEntry(v) | 0x2140) & 0xffff, Ld32(hr + 8));
        }
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    }
    hr = Ld32(slot);
    const uint32_t stf = Ld32(Ld32(hr + 0x10));
    if ((stf & 0x1a) != 0 && (stf & 0x420) != 0x20) {
        if (fl_more != 0) {
            St32(hr, Ld32(hr) | 0x40);
            hr = Ld32(slot);
        }
        if (!(Ld32(hr) & 0x800)) {
            if (Call(c, 0x4aa70, hr) == 0) {
                const uint32_t h2 = Ld32(Ld32(Ld32(slot) + 0x10) + 0xd0);
                St8(h2 + 0x16, Ld8(h2 + 0x16) | 4);
            }
        }
    }
    if (Ld32(f.At(0x14)) != 0) {
        const uint32_t hr2 = Ld32(slot);
        St32(hr2, Ld32(hr2) | 0x40);
    }
    finish();
}

}  // namespace ps2r::title::sly2::snd989
