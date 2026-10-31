// snd989_cdthread.cpp -- fn_47fec, the CD stream thread (the coroutine form): the body that takes a finished CD read
// (fn_46ed0's request, completed by fn_3a9dc -> kCdReadSema) or a current refill (kRefillCurrent) and advances the
// stream: the header pass (a VAG header -> rates, sizes, the paired record; a VPK header -> the voice chain allocated
// (fn_4ba00) and programmed; anything else -> error 0x45), the data pass (the ADPCM flags fixed (fn_4a7c4), the
// loop-point tail copied (bcopy), the half record queued for upload (fn_4a9d4) or the stream keyed on (fn_4a050)),
// and the CD completion (the sectors DMA'd to the EE in memory-source mode, the request's position advanced, a failed
// read finished (fn_493b0)). Every pass ends by signalling the stream semaphore and, when everything is idle, waking
// the reader waiter. Ported from the recompiled 989snd_Library over the module's RAM.
#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kCd = G::kData + 0x22f0;              // snd989_play.cpp's CD block
constexpr uint32_t kRefillCurrent = G::kData + 0x234c;
constexpr uint32_t kStartQueueHead = G::kData + 0x233c;
constexpr uint32_t kSearchName = G::kData + 0x2368;
constexpr uint32_t kReaderWaiter = G::kData + 0x235c;
constexpr uint32_t kPositionDmaSema = G::kData + 0x62c;
constexpr uint32_t kPositionDmaCallback = 0x351c8;
constexpr uint32_t kVoiceTable = 0x593a8;                // 56-byte voice records (snd989_voice.cpp)
constexpr uint32_t kVagLE = 0x70474156u, kVagBE = 0x56414770u, kVpk = 0x56504b20u;
inline uint32_t Bswap(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }
}  // namespace

// fn_47fec(): the CD stream thread. Forever: wait on the CD-read semaphore (a failed wait reported, 0x3f); then
// handle the finished CD request (kCd + 0x60) and / or the current refill (kRefillCurrent); signal the stream
// semaphore; when idle, wake the reader waiter.
void fn_47fec(IopContext& c) {
    Frame f(c, 0x68);
    const uint32_t intr = f.At(0x28);
    uint32_t fp = 0;            // $fp: a start-queue refusal -> the handle is stopped at the end of the pass
    St32(f.At(0x2c), 0);        // wake the refill waiter at the end of the pass
    uint32_t suspended, status, cur, st, hdl, hdr, first = 0;
top:
    status = imp::WaitSema(c, Ld32(G::kCdReadSema));
    if (status != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x3f, status, 0, 0);
    }
    if (Ld32(kCd + 0x60) == 0 && Ld32(kRefillCurrent) == 0) goto signal_stream;
    if (Ld32(kCd + 0x60) != 0) {
        if (Ld32(G::kCdError) == 1) {
            St32(kCd + 0x30, 0xffffffffu);
            St32(G::kCdError, 0);
        } else {
            Import(c, "cdvdman", 11, 0);
            St32(kCd + 0x30, Import(c, "cdvdman", 8, 0));
        }
    } else {
        if (Ld32(G::kCdError) == 1) {
            St32(kCd + 0x2c, 0xffffffffu);
            St32(G::kCdError, 0);
        } else if ((Ld32(Ld32(Ld32(kRefillCurrent) + 0x10)) & 0x1f800u) == 0) {
            Import(c, "cdvdman", 11, 0);
            St32(kCd + 0x2c, Import(c, "cdvdman", 8, 0));
        }
        const uint32_t err = Ld32(kCd + 0x2c);
        if (err != 0) {
            St32(f.At(0x10), Ld32(kCd + 0x44));
            Call(c, 0x35140, 0x40, err, Ld32(kCd + 0x3c), Ld32(kCd + 0x40));
        }
    }
    if (Ld32(kCd + 0x60) == 0) goto refill;
    // ---- the CD request finished (0x48164)
    {
        const uint32_t err = Ld32(kCd + 0x30);
        if (err != 0) {
            St32(f.At(0x10), Ld32(kCd + 0x44));
            Call(c, 0x35140, 0x41, err, Ld32(kCd + 0x3c), Ld32(kCd + 0x40));
            St32(kCd + 0x1c, 0);
            goto cd_done;
        }
        if (Ld32(kCd + 0x28) != 0) {
            St32(kCd + 0x1c, 0);
            goto cd_done;
        }
        if (Ld32(kCd + 0x60) == 0xffffffffu) goto cd_done;
        if (Ld32(kCd + 0x38) != 0 && Ld32(kCd + 0x1c) != 0) {
            // memory source: the read sectors DMA'd to the EE
            St32(f.At(0x18), Ld32(kCd + 0x14));
            St32(f.At(0x1c), Ld32(kCd + 0x18));
            St32(f.At(0x24), 0);
            St32(f.At(0x20), Ld32(kCd + 0x20) << 11);
            suspended = imp::CpuSuspendIntr(c, intr);
            const uint32_t id = Import(c, "sifman", 32, f.At(0x18), 1, kPositionDmaCallback, kPositionDmaSema);
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
            if (id == 0) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0x42, 0, 0, 0);
                St32(kCd + 0x1c, 0);
                St32(kCd + 0x30, 0xffffffffu);
            } else {
                for (;;) {
                    if (S32(Import(c, "sifman", 8, id)) < 0) break;
                    if (imp::WaitSema(c, Ld32(kPositionDmaSema)) != 0) {
                        St32(f.At(0x10), 0);
                        Call(c, 0x35140, 0xfa3, 0, 0, 0);
                    }
                }
            }
        }
        const uint32_t n = Ld32(kCd + 0x20);
        St32(kCd + 0x1c, Ld32(kCd + 0x1c) - n);
        if (Ld32(kCd + 0x1c) != 0) {
            St32(kCd + 0x24, Ld32(kCd + 0x24) + n);
            St32(kCd + 0x18, Ld32(kCd + 0x18) + (n << 11));
        }
    }
cd_done:
    St32(kCd + 0x60, 0);
    if (Ld32(kCd + 0x1c) == 0 || Ld32(kCd + 0x28) == 1) Call(c, 0x493b0);
    imp::SignalSema(c, Ld32(G::kStreamSema));
    goto signal_stream;

refill:  // ---- the current refill (0x48364)
    cur = Ld32(kRefillCurrent);
    if (cur == 0) goto clear_signal;
    st = Ld32(cur + 0x10);
    {
        const uint32_t flags = Ld32(st);
        hdl = Ld32(st + 0xd0);
        if (flags & 1) goto mark_stopping;
        if (Ld8(hdl + 0x16) & 4) goto mark_stopping;
        if (Ld32(kCd + 0x2c) != 0) {
            St32(kRefillCurrent, 0);
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto signal_stream;
        }
        const uint32_t curflags = Ld32(cur);
        if (curflags & 0x10) {
        // ---- the header pass (0x483e8)
        hdr = Ld32(cur + 4) + Ld32(st + 0x8c);
        const uint32_t magic = Ld32(hdr);
        if (magic == kVagLE || magic == kVagBE) {
            uint32_t rate = Ld32(hdr + 0x10);
            if (magic == kVagLE) rate = Bswap(rate);
            St32(st + 0xb8, rate);
            St32(st + 8, MulHiU(rate << 12, 0x057619f1u) >> 10);
            uint32_t size = Ld32(hdr + 0xc);
            if (Ld32(hdr) == kVagLE) size = Bswap(size);
            St32(st + 0xb0, (size >> 4) * 28);
            const uint32_t data = size + Ld32(st + 0x8c) + 0x30;
            const uint32_t bufsize = Ld32(st + 0xc);
            St32(st + 0x10, 9);
            St32(st + 0xb4, 0);
            St32(st + 0xac, data);
            St32(st + 0xa8, data);
            St32(st + 0x3c, 1);
            if (data < (bufsize >> 1) + 1) {
                St32(st + 0x10, (flags & 0x400) ? 0x80d : 0xd);
            } else if (data < bufsize + 1) {
                St32(st + 0x3c, 5);
            } else {
                St32(st + 0x10, 0x29);
            }
            const uint32_t pair = Ld32(st + 0xc8);
            if (pair == 0) {
                St8(st + 4, 0x7f);
                St8(st + 5, 0);
                St16(st + 6, 0);
            } else {
                St32(pair + 0xac, size + Ld32(pair + 0x8c) + 0x30);
                St32(pair + 0xa8, Ld32(pair + 0xac));
                St32(pair + 8, Ld32(st + 8));
                St32(pair + 0xb8, Ld32(st + 0xb8));
                St32(pair + 0x10, 9);
                St32(pair + 0x3c, 1);
                St32(pair + 0x10, Ld32(pair + 0x10) | (Ld32(st + 0x10) & 0x800));
                St32(pair + 0xb4, 0);
                St32(pair + 0xb0, Ld32(st + 0xb0));
                if (Call(c, 0x4aa70, pair + 0x10) == 0) fp = 1;
                St8(st + 4, 0x7f);
                St8(st + 5, 0);
                St16(st + 6, 0x10e);
                St8(pair + 4, 0x7f);
                St8(pair + 5, 0);
                St16(pair + 6, 0x5a);
                if (Ld32(st + 0x10) & 4) {
                    St32(st + 0x10, Ld32(st + 0x10) & ~4u);
                    St32(pair + 0x10, Ld32(pair + 0x10) | 0x1000);
                }
            }
            if (fp == 0) {
                if ((Ld32(st + 0xc) >> 1) < Ld32(st + 0xac)) {
                    const uint32_t p = Ld32(st + 0xc8);
                    if (p != 0 && Call(c, 0x4aa70, p + 0x3c) == 0) fp = 1;
                    if (fp == 0 && Call(c, 0x4aa70, st + 0x3c) == 0) fp = 1;
                }
                if (fp == 0) {
                    Call(c, 0x471a0, 0);
                    goto signal_stream;
                }
            }
            hdl = Ld32(st + 0xd0);
            St32(kRefillCurrent, 0);
            fp = 0;
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto signal_stream;
        }
        if (magic != kVpk) {
            // 0x48ce0: not a header we know
            St32(f.At(0x10), 0);
            St32(kRefillCurrent, 0);
            Call(c, 0x35140, 0x45, Ld32(st + 0x88), Ld32(st + 0x8c), 0);
            St8(Ld32(st + 0xd0) + 0x16, 4);
            goto signal_stream;
        }
        // ---- a VPK header: the voice chain (0x486e8)
        St32(f.At(0x30), Ld32(hdl + 0x6c) == st ? 1 : 0);
        if (Ld32(hdl + 0x38) != 0xffffffffu) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x6a, 0, 0, 0);
            Call(c, 0x4dec0, Ld32(Ld32(st + 0xd0) + 0x38));
            St32(Ld32(st + 0xd0) + 0x38, 0xffffffffu);
        }
        if (Ld32(hdr + 0xc) != Ld32(st + 0xc)) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x43, Ld32(st + 0xc), Ld32(hdr + 0xc), 0);
            goto mark_stopping;
        }
        Call(c, 0x42f58, 0x58);
        Call(c, 0x4c1ac, 1, 0x5c);
        suspended = imp::CpuSuspendIntr(c, intr);
        first = Call(c, 0x4ba00, st, Ld32(hdr + 0x14));
        if (first == 0) {
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x44, 0, 0, 0);
            St32(kRefillCurrent, 0);
            hdl = Ld32(st + 0xd0);
            St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
            goto unlock_signal;
        }
        St32(f.At(0x34), 1);
        St32(f.At(0x38), 0);
        if (Ld32(hdr + 0x14) != 0) {
            uint32_t rec = first, i = 0;
            do {
                St32(rec, flags | 2);
                if (rec == st) St32(f.At(0x34), 0);
                ++i;
                rec = Ld32(rec + 0xcc);
            } while (i < Ld32(hdr + 0x14));
        }
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (Ld32(hdr + 0x14) != 0) {
            uint32_t rec = first, prev = first, i = 0, hp = hdr;
            do {
                hdl = Ld32(st + 0xd0);
                St32(rec + 0xd0, hdl);
                if (Ld8(hdl + 0x16) & 2) St32(rec, Ld32(rec) | 0x10);
                St32(rec + 0xc8, 0);
                St32(rec + 0x88, Ld32(st + 0x88));
                if (i == 0) {
                    St32(rec + 0x7c, Ld32(st + 0x7c));
                    if (rec != st) St32(st + 0x7c, 0xffffffffu);
                } else {
                    St32(prev + 0xc8, rec);
                    prev = rec;
                    St32(rec + 0x7c, Call(c, 0x4c750, U32(Lds8(Ld32(st + 0xd0) + 0x17)), 0x7f, 1));
                }
                const uint32_t voice = Ld32(rec + 0x7c);
                if (voice == 0xffffffffu) {
                    St32(f.At(0x10), 0);
                    Call(c, 0x35140, 0x7a, Ld32(hdr + 0x14), 0, 0);
                    if (first != rec) {
                        uint32_t r = first;
                        do {
                            const uint32_t v = Ld32(r + 0x7c);
                            if (v != 0xffffffffu) {
                                Call(c, 0x4dec0, v);
                                St32(r + 0x7c, 0xffffffffu);
                            }
                            r = Ld32(r + 0xcc);
                        } while (r != rec);
                    }
                    hdl = Ld32(st + 0xd0);
                    St32(kRefillCurrent, 0);
                    St32(f.At(0x38), 1);
                    St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
                    break;
                }
                const uint32_t vt = kVoiceTable + voice * 56;
                St32(vt + 8, 4);
                St32(vt + 0xc, Ld32(rec + 0xd0));
                const uint32_t rate = Ld32(hdr + 0x10);
                St32(rec + 0xb8, rate);
                St32(rec + 8, U32(DivMagic(S32(rate << 12), 0x057619f1u, 10)));
                const uint32_t size = Ld32(hdr + 4);
                St32(rec + 0xb4, 0);
                St32(rec + 0x10, 0x109);
                St32(rec + 0x3c, 0x101);
                St32(rec + 0xb0, (size >> 4) * 28);
                St32(rec + 0xac, size);
                St32(rec + 0xa8, size);
                if (i == 0) {
                    St32(rec + 0x10, 0x309);
                    St32(rec + 0x3c, 0x301);
                }
                if (Ld32(rec + 0xac) < (Ld32(rec + 0xc) >> 1) + 1) {
                    St32(rec + 0x10, Ld32(rec + 0x10) | 4);
                } else if (Ld32(st + 0xac) < Ld32(st + 0xc) + 1) {
                    St32(rec + 0x3c, Ld32(rec + 0x3c) | 4);
                } else {
                    St32(rec + 0x10, Ld32(rec + 0x10) | 0x20);
                }
                St8(rec + 4, Ld8(hp + 0x18));
                St16(rec + 6, Ld16(hdr + 0x1a + i * 4));
                St8(rec + 5, Ld8(hp + 0x19));
                St32(rec + 0x68, 0);
                St32(rec + 0x24, 0);
                St32(rec + 0x50, 0);
                St32(rec + 0x6c, 0);
                St32(rec + 0x74, 0);
                St32(rec + 0x70, 0);
                St32(rec + 0xbc, 0);
                St32(rec + 0xc0, 0);
                St32(rec + 0x8c, 0);
                if (i == 0) {
                    St32(rec + 0x84, Ld32(st + 0x88) + 1);
                    St32(rec + 0x88, Ld32(st + 0x88));
                    if (Ld32(rec) & 0x1c000u) {
                        const uint32_t pos = Ld32(rec + 0xa0), base = Ld32(rec + 0x9c), half = Ld32(rec + 0xc) >> 1;
                        if (half == 0) {
                            std::printf("[IOP-RECOMP] BREAK at 0x48b24\n");
                            return;
                        }
                        const uint32_t rem = (pos - base) % half;
                        const uint32_t adv = (rem == 0 ? 0 : half - rem) + 0x800;
                        const uint32_t len = Ld32(rec + 0x98);
                        St32(rec + 0xa0, pos + adv);
                        if (!(pos + adv < base + len)) St32(rec + 0xa0, pos + adv - len);
                        Call(c, 0x47950, rec, adv, 0);
                    }
                }
                ++i;
                rec = Ld32(rec + 0xcc);
                hp += 4;
            } while (i < Ld32(hdr + 0x14));
        }
        if (Ld32(f.At(0x38)) != 0) goto unlock_signal;
        suspended = imp::CpuSuspendIntr(c, intr);
        if (Ld32(f.At(0x34)) != 0) {
            St32(st, 1);
            St32(st + 0x7c, 0xffffffffu);
        }
        if (Ld32(f.At(0x30)) == 0) {
            St32(Ld32(first + 0xd0) + 4, first);
            St32(Ld32(first + 0xd0) + 0x34, Ld32(hdr + 0x14));
        }
        for (uint32_t r = first; r != 0; r = Ld32(r + 0xc8)) {
            const int32_t v = Lds32(r + 0x7c);
            const int32_t q = Div24(v);
            const uint32_t bit = U32(v - q * 24);
            const uint32_t word = Ld32(first + 0xd0) + 0x18 + U32(q) * 4;
            St32(word, Ld32(word) | (1u << (bit & 31)));
        }
        St32(kRefillCurrent, first + 0x10);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(intr));
        if (fp == 0) {
            if ((Ld32(first + 0xc) >> 1) < Ld32(first + 0xac)) {
                if (Call(c, 0x4aa70, first + 0x3c) == 0) fp = 1;
            }
            if (fp == 0) {
                Call(c, 0x471a0, 0);
                goto unlock_signal;
            }
        }
        hdl = Ld32(first + 0xd0);
        St32(kRefillCurrent, 0);
        fp = 0;
        St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
    unlock_signal:
        Call(c, 0x4c2f8);
        Call(c, 0x430dc);
        goto signal_stream;
        }
        // ---- the data pass (0x48d08)
        if (curflags & 0x100) goto chunk_sizes;
        if (curflags & 0x400) {
            St32(cur, curflags & ~0x400u);
            Import(c, "sysclib", 12, Ld32(cur + 0x20), Ld32(cur + 0x1c), Ld32(cur + 0x24));
            cur = Ld32(kRefillCurrent);
            St32(cur + 0x18, Ld32(cur + 0x18) - (Ld32(cur + 0x1c) - Ld32(cur + 0x20)));
        }
        cur = Ld32(kRefillCurrent);
        if (Ld32(cur + 0x14) != 0 && (Ld32(Ld32(cur + 0x10)) & 0x400)) {
            if (Ld32(cur) & 0x800) goto tail_800;
            // the stream's end inside this half (0x48d9c)
            uint32_t got = Ld32(cur + 0x18);
            uint32_t st2 = Ld32(cur + 0x10);
            St8(Ld32(cur + 4) + got - 0xf, 0);
            const int32_t left = S32(((Ld32(st2 + 0xc) >> 1) - got) + 0x10);
            cur = Ld32(kRefillCurrent);
            got = Ld32(cur + 0x18);
            if (!(got < 0x11)) St8(Ld32(cur + 4) + got - 0x1f, 0);
            cur = Ld32(kRefillCurrent);
            st2 = Ld32(cur + 0x10);
            St32(cur + 0x14, 0);
            St32(st2, Ld32(st2) & ~0x20u);
            St32(st2 + 0xac, Ld32(st2 + 0xa8));
            St32(st2 + 0x84, Ld32(st2 + 0x88));
            if (Ld32(st2) & 0x1c000u) {
                uint32_t adv = 0;
                const uint32_t pos = Ld32(st2 + 0xa0), half = Ld32(st2 + 0xc) >> 1;
                if (half == 0) {
                    std::printf("[IOP-RECOMP] BREAK at 0x48e68\n");
                    return;
                }
                const uint32_t rem = (pos - Ld32(st2 + 0x9c)) % half;
                if (rem != 0) {
                    adv = half - rem;
                    St32(st2 + 0xa0, pos + adv);
                }
                st2 = Ld32(Ld32(kRefillCurrent) + 0x10);
                const uint32_t p = Ld32(st2 + 0xa0), len = Ld32(st2 + 0x98);
                if (!(p < Ld32(st2 + 0x9c) + len)) St32(st2 + 0xa0, p - len);
                if (adv != 0) Call(c, 0x47950, Ld32(Ld32(kRefillCurrent) + 0x10), adv, 0);
            }
            if (!(left < 0x801)) {
                cur = Ld32(kRefillCurrent);
                const uint32_t t = Ld32(cur + 0x18), base = Ld32(cur + 4), s3 = Ld32(cur + 0x10);
                St32(cur, Ld32(cur) | 0x400);
                St32(cur + 0x20, base + t - 0x10);
                St32(cur + 0x1c, base + Ld32(s3 + 0x8c) + t + 0x30);
                St32(cur + 0x24, U32(left) - 0x40 - Ld32(s3 + 0x8c));
                Call(c, 0x471a0, t - 0x10);
                goto signal_stream;
            }
            cur = Ld32(kRefillCurrent);
            {
                const uint32_t s3 = Ld32(cur + 0x10);
                uint32_t other = s3 + 0x10;
                St32(cur + 0x18, Ld32(cur + 0x18) - 0x10);
                if (cur == other) other = s3 + 0x3c;
                const uint32_t so = Ld32(other + 0x10);
                St32(other, Ld32(other) | 0x400);
                St32(other + 0x20, Ld32(other + 4));
                St32(other + 0x1c, Ld32(other + 4) + Ld32(so + 0x8c) + 0x40);
                St32(other + 0x24, (Ld32(Ld32(cur + 0x10) + 0xc) >> 1) - 0x40 - Ld32(so + 0x8c));
            }
        }
        cur = Ld32(kRefillCurrent);
        {
            uint32_t buf;
            if (Ld32(cur) & 0x800) {
            tail_800:
                cur = Ld32(kRefillCurrent);
                const uint32_t s3 = Ld32(cur + 0x10), hoff = Ld32(s3 + 0x8c);
                buf = Ld32(cur + 4) + hoff + 0x30;
                St32(cur + 0x18, Ld32(cur + 0x18) - 0x40 - hoff);
            } else {
                buf = Ld32(cur + 4);
            }
            cur = Ld32(kRefillCurrent);
            const uint32_t n = Call(c, 0x4a7c4, buf, Ld32(cur + 0x18), Ld32(cur + 0x14), Ld32(cur) & 0x800);
            St32(Ld32(kRefillCurrent) + 0x18, n);
            goto finish;
        }
    chunk_sizes:  // 0x4901c: the chain's per-record ADPCM fix
        {
            const uint32_t off = (cur != st + 0x10) ? 44 : 0;
            for (uint32_t r = st; r != 0; r = Ld32(r + 0xc8)) {
                cur = Ld32(kRefillCurrent);
                St32(r + off + 0x28, Call(c, 0x4a7c4, Ld32(r + off + 0x14), Ld32(cur + 0x18), Ld32(cur + 0x14), 0));
            }
        }
    finish:  // 0x49074
        cur = Ld32(kRefillCurrent);
        {
            const uint32_t v1 = Ld32(cur) | 0x80;
            St32(cur, v1);
            if (v1 & 1) {
                Call(c, 0x4a9d4, cur);
                St32(f.At(0x2c), 1);
            } else if (v1 & 0x40) {
                const uint32_t h = Ld32(Ld32(cur + 0x10) + 0xd0);
                St8(h + 0x16, Ld8(h + 0x16) | 8);
                Call(c, 0x4a050, Ld32(Ld32(kRefillCurrent) + 0x10), 1);
            }
        }
        {
            const uint32_t wake = Ld32(f.At(0x2c));
            St32(kRefillCurrent, 0);
            if (wake != 0) {
                St32(f.At(0x2c), 0);
                imp::SignalSema(c, Ld32(G::kRampWaiterSema));
            }
        }
        goto signal_stream;
    }
mark_stopping:  // 0x4876c
    hdl = Ld32(st + 0xd0);
    St32(kRefillCurrent, 0);
    St8(hdl + 0x16, Ld8(hdl + 0x16) | 4);
    goto signal_stream;
clear_signal:  // 0x49110
    St32(kRefillCurrent, 0);
signal_stream:  // 0x49118
    imp::SignalSema(c, Ld32(G::kStreamSema));
    if (Ld32(kRefillCurrent) != 0 || Ld32(kCd + 0x60) != 0 || Ld32(kStartQueueHead) != 0 || Ld32(kSearchName) != 0) goto top;
    {
        const uint32_t waiter = Ld32(kReaderWaiter);
        if (waiter == 0) goto top;
        Import(c, "thbase", 25, waiter);
        St32(kReaderWaiter, 0);
    }
    goto top;
}

}  // namespace ps2r::title::sly2::snd989
