// snd989_bank.cpp -- the boot-time path, part 3: the stream system init (fn_43654: the four stream threads, the
// semaphores, the stream records and buffers), the bank load (fn_3be20: the header, the block allocation, the body
// read in chunks uploaded to the SPU (fn_3cd18: the EE -> IOP -> SPU SdVoiceTrans path), the relocation (fn_3d168)),
// the loader's sector read (fn_3c308: partial / whole sectors through the small sector buffer), and the movie start
// (fn_3e284: the buffer, the voices, the DMA channel; fn_3e920: the voice programming). Ported from the recompiled
// 989snd_Library over the module's RAM.
#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

namespace {
constexpr uint32_t kStreamRecList = G::kData + 0x2338;
constexpr uint32_t kStreamRecords = G::kData + 0x22e4;    // the n x 120-byte stream records
constexpr uint32_t kStreamCount = G::kData + 0x22e8;
constexpr uint32_t kCd = G::kData + 0x22f0;
constexpr uint32_t kStreamThreads = G::kData + 0x2378;    // refill, reader, transfer, CD stream
constexpr uint32_t kHeapAlloc = G::kData + 0x1988, kHeapFree = G::kData + 0x198c;
constexpr uint32_t kBannerWord = G::kData + 0x624;
constexpr uint32_t kReplySema = G::kData + 0x630;
constexpr uint32_t kLoadError = G::kData + 0x1974, kLoadChan = G::kData + 0x1920, kLoadWord = G::kData + 0x1924;
constexpr uint32_t kCachedSector = G::kData + 0x19a0, kLoadFromCd = G::kData + 0x195c;
constexpr uint32_t kBankList = G::kData + 0x1994, kBankList2 = G::kData + 0x1998;
constexpr uint32_t kCdSmallBuffer = G::kData + 0x27b8;
constexpr uint32_t kFmtLoadReadFailed = G::kData - 0xf4;
constexpr uint32_t kMovieBuf = G::kData + 0x19b0, kMovieArg = G::kData + 0x19b4, kMovieCount = G::kData + 0x19b8;
constexpr uint32_t kMovieBufSize = G::kData + 0x19bc, kMovieVolL = G::kData + 0x19c0, kMovieVolR = G::kData + 0x19c4;
constexpr uint32_t kMovieGroup = G::kData + 0x19c8, kMovieVoice0 = G::kData + 0x19cc, kMovieVoice1 = G::kData + 0x19d0;
constexpr uint32_t kMovieBusy = G::kData + 0x19d4, kMovieMode = G::kData + 0x19d8, kMovieW1 = G::kData + 0x19dc;
constexpr uint32_t kMovieW2 = G::kData + 0x19e0, kMovieOffset = G::kData + 0x19e4, kMovieVol = G::kData + 0x2538;
constexpr uint32_t kSbv2 = 0x32764253u, kSblk = 0x6b6c4253u;
constexpr uint32_t kBankTransHandler = 0x3a964;   // fn_3a964: the libsd transfer-done callback

inline uint32_t Alloc(IopContext& c, uint32_t size, uint32_t kind, uint32_t a2, uint32_t a3) {
    return CallAddr(c, Ld32(kHeapAlloc), size, kind, a2, a3);
}
inline void Free(IopContext& c, uint32_t p) { CallAddr(c, Ld32(kHeapFree), p, A1(c), A2(c), A3(c)); }
inline int32_t Sra11(int32_t v) { return (v < 0 ? v + 0x7ff : v) >> 11; }
}  // namespace

// fn_43654(n, bufsize, cd_block, has_small): the stream system init, once (records present -> 0x25, 0): the queues
// cleared, sceCdSearchFile(0x10), the CD block / count stored, the buffer size rounded to 4 KiB (at least 8 KiB), the
// four stream threads created (a failure -> 0x26..0x29, fn_4ae4c, 0), the stream / refill-waiter / transfer / CD-read /
// file-search semaphores, the n stream records (120 bytes: {i << 16 | 0x4000001, ...}), the stream buffer (n x bufsize; the position record from fn_4272c), the n half-record pairs (0xd4
// bytes each, linked from kStreamRecList), the small sector buffer when `has_small`, the 64 refill records cleared, the
// threads started, the 16 group entries {0, n - 1}, the ready word. Returns 1.
void fn_43654(IopContext& c) {
    const uint32_t n = A0(c), bufsize_in = A1(c), cd_block = A2(c), has_small = A3(c);
    Frame f(c, 0x78);
    St32(f.At(0x7c), bufsize_in);
    St32(f.At(0x78), n);
    St32(f.At(0x84), has_small);
    if (Ld32(kStreamRecList) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x25, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    for (uint32_t a : {0x233cu, 0x2340u, 0x2344u, 0x2354u, 0x2348u, 0x234cu, 0x2358u}) St32(G::kData + a, 0);
    Import(c, "cdvdman", 11, 0x10);
    St32(kCd, cd_block);
    St32(kStreamCount, n);
    int32_t s1 = S32(bufsize_in) >> 12;
    if (S32(bufsize_in) < 0) s1 = S32(bufsize_in + 0xfff) >> 12;
    if (s1 < 2) s1 = 2;
    s1 <<= 12;
    const uint32_t bufsize = U32(s1);
    St32(kCd + 4, U32(s1 >> 1));
    St32(f.At(0x7c), bufsize);
    auto fail = [&](uint32_t code, uint32_t a1, uint32_t a2) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, code, a1, a2, 0);
        Call(c, 0x4ae4c);
        Ret(c, 0);
    };
    St32(f.At(0x18), 0x2000000);
    St32(f.At(0x1c), 0);
    St32(f.At(0x24), 0x800);
    const uint32_t entries[4] = {0x49428, 0x46520, 0x49854, 0x47fec};
    const uint32_t prios[4] = {0, 2, 1, 3};
    for (int i = 0; i < 4; ++i) {
        St32(f.At(0x20), entries[i]);
        St32(f.At(0x28), Ld32(kBannerWord) + prios[i]);
        const uint32_t tid = Import(c, "thbase", 4, f.At(0x18));
        St32(kStreamThreads + 4 * i, tid);
        if (S32(tid) <= 0) {
            fail(0x26 + i, 0, 0);
            return;
        }
    }
    St32(f.At(0x2c), 1);
    St32(f.At(0x34), 0);
    St32(f.At(0x38), 1);
    // Five CreateSema calls; each store sits in the NEXT call's delay slot, so it records the previous result.
    St32(G::kStreamSema, Import(c, "thsemap", 4, f.At(0x2c)));
    St32(G::kRampWaiterSema, Import(c, "thsemap", 4, f.At(0x2c)));
    St32(G::kData + 0x1950, Import(c, "thsemap", 4, f.At(0x2c)));
    St32(G::kCdReadSema, Import(c, "thsemap", 4, f.At(0x2c)));
    St32(G::kData + 0x2370, Import(c, "thsemap", 4, f.At(0x2c)));
    const uint32_t recs = Alloc(c, n * 120, 4, 0, Ld32(kHeapAlloc));
    St32(kStreamRecords, recs);
    if (recs == 0) {
        fail(0x2a, n, 0x78);
        return;
    }
    Import(c, "sysclib", 14, recs, 0, n * 120);
    for (uint32_t i = 0; i < n && S32(n) > 0; ++i) {
        const uint32_t rec = Ld32(kStreamRecords) + i * 120;
        St32(rec, (i << 16) | 0x4000001u);
        St32(rec + 0x1c, 0);
        St32(rec + 0x18, 0);
        St32(rec + 0x38, 0xffffffffu);
        St32(rec + 0x74, 1);
        for (uint32_t j = 0; j < 16; ++j) St8(Ld32(kStreamRecords) + i * 120 + 0x3c + j, 0x7f);
    }
    const uint32_t total = bufsize * n;
    const uint32_t buf = Alloc(c, total, 5, 0, n);
    if (buf == 0) {
        fail(0x2b, bufsize, n);
        return;
    }
    uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x3c));
    St32(f.At(0x44), Call(c, 0x4272c, total));
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x3c)));
    if (Ld32(f.At(0x44)) == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x2c, 0, 0, 0);
        Free(c, buf);
        Call(c, 0x4ae4c);
        Ret(c, 0);
        return;
    }
    St32(f.At(0x40), kStreamRecList);
    if (S32(n) > 0) {
        const int32_t half = S32((bufsize + (bufsize >> 31)) ) >> 1;
        const uint32_t s7 = U32(half) * n;
        uint32_t s3 = buf, s5 = Ld32(f.At(0x44)), fp = 0;
        St32(f.At(0x48), total);
        uint32_t s4 = s5 + s7;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t rec = Alloc(c, 0xd4, 4, 0, i == 0 ? Ld32(f.At(0x44)) : Ld32(f.At(0x40)));
            if (rec == 0) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0x2d, 0, 0, 0);
                suspended = imp::CpuSuspendIntr(c, f.At(0x3c));
                Call(c, 0x42924, Ld32(f.At(0x44)), Ld32(f.At(0x48)));
                if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x3c)));
                Free(c, buf);
                Call(c, 0x4ae4c);
                Ret(c, 0);
                return;
            }
            Import(c, "sysclib", 14, rec, 0, 0xd4);
            const uint32_t v0 = buf + s7 + fp;
            St32(rec, 1);
            St32(rec + 0x44, s4);
            s4 += U32(half);
            St32(rec + 0x18, s5);
            s5 += U32(half);
            St32(rec + 0x14, s3);
            St32(rec + 0x38, s3);
            s3 += U32(half);
            fp += U32(half);
            St32(rec + 0x40, v0);
            St32(rec + 0x64, v0);
            St32(rec + 0x20, rec);
            St32(rec + 0x4c, rec);
            St32(rec + 0xc, bufsize);
            St32(Ld32(f.At(0x40)), rec);
            St32(f.At(0x40), rec + 0xcc);
        }
    }
    if (Ld32(f.At(0x84)) == 0) {
        St32(kCd + 8, 0);
        St32(kCd + 0x14, 0);
    } else {
        const uint32_t half = Ld32(kCd + 4);
        St32(kCd + 8, half);
        const uint32_t p = Alloc(c, half, 5, 0, G::kData);
        St32(kCd + 0x14, p);
        if (p == 0) {
            fail(0x2e, 0, 0);
            return;
        }
        St32(kCd + 8, U32(Sra11(Lds32(kCd + 8))));
    }
    for (uint32_t k = 0; k < 64; ++k) St32(0x58ea8 + k * 0x14, 0);
    for (int i = 0; i < 4; ++i) Import(c, "thbase", 6, Ld32(kStreamThreads + 4 * i), 0);
    for (uint32_t i = 0; i < 16; ++i) {
        St16(G::kData + 0x22a4 + i * 4, 0);
        St16(G::kData + 0x22a6 + i * 4, n - 1);
    }
    St32(G::kData + 0x22ec, 1);
    Ret(c, 1);
}

// fn_3d168(bank): the loaded bank relocated and appended to its list: an 'SBv2' bank -> the sound entries' data
// offsets rebased on the data base, the 8-byte entries' second words made pointers, the bank appended to kBankList;
// else the legacy layout (the data base, the typed 8-byte entries' targets, the 12-byte entries' offsets and flag
// bit 14 cleared, the optional 0x38 / 0x3c sub-tables), appended to kBankList2. Returns 0.
void fn_3d168(IopContext& c) {
    const uint32_t bank = A0(c);
    auto append = [&](uint32_t head_addr) {
        const uint32_t head = Ld32(head_addr);
        if (head == 0) {
            St32(head_addr, bank);
            return;
        }
        uint32_t n = head;
        while (Ld32(n + 0x30) != 0) n = Ld32(n + 0x30);
        St32(n + 0x30, bank);
    };
    if (Ld32(bank) == kSbv2) {
        if (Lds16(bank + 0x18) > 0) {
            uint32_t off = 0;
            for (int32_t i = 0; i < Lds16(bank + 0x18); ++i, off += 0x18) {
                const uint32_t e = off + Ld32(bank + 0x24);
                St32(e + 0x10, Ld32(e + 0x10) + Ld32(bank + 0x28));
            }
        }
        if (Lds16(bank + 0x16) > 0) {
            for (int32_t i = 0; i < Lds16(bank + 0x16); ++i) {
                const uint32_t e = U32(i) * 8 + Ld32(bank + 0x20);
                St32(e + 4, bank + Ld32(e + 4));
            }
        }
        append(kBankList);
        Ret(c, 0);
        return;
    }
    St32(bank + 0x34, Ld32(bank + 0x34) + bank);
    if (Lds16(bank + 0x18) > 0) {
        for (int32_t i = 0; i < Lds16(bank + 0x18); ++i) {
            const uint32_t e = U32(i) * 8 + Ld32(bank + 0x20);
            if (Ld8(e + 3) == 1) {
                const uint32_t t = Ld32(bank + 0x34) + (Ld32(e) & 0xffffffu);
                St32(t + 0x10, Ld32(t + 0x10) + Ld32(bank + 0x24));
            }
        }
    }
    if (Lds16(bank + 0x16) > 0) {
        uint32_t off = 0;
        for (int32_t i = 0; i < Lds16(bank + 0x16); ++i, off += 0xc) {
            const uint32_t e = off + Ld32(bank + 0x1c);
            St32(e + 8, Ld32(e + 8) + Ld32(bank + 0x20));
            St16(e + 6, Ld16(e + 6) & 0xbfff);
        }
    }
    if (Ld32(bank + 8) & 0x100) {
        const uint32_t t = Ld32(bank + 0x38) + bank;
        St32(bank + 0x38, t);
        St32(t + 8, Ld32(t + 8) + t);
        St32(t + 0xc, Ld32(t + 0xc) + t);
        St32(t + 0x10, Ld32(t + 0x10) + t);
        St32(t + 0x14, Ld32(t + 0x14) + t);
    }
    if (Ld32(bank + 8) & 0x200) St32(bank + 0x3c, bank + Ld32(bank + 0x3c));
    append(kBankList2);
    Ret(c, 0);
}

// fn_3cd18(bank, src, count, offset, [flag, spu_base, spu_size, word]): one chunk of a bank's sample data uploaded to
// the SPU. A busy load channel -> 0x19, -10. The SPU allocation recorded in the bank (an explicit `spu_size` below the
// bank's own is reported, 0x69; an explicit `spu_base` clears flag bit 2); unless `flag`: flag bit 2 -> the SPU block
// allocated (fn_4272c; a failure -> 0x1a, -11), else the given block claimed (fn_42a94; a failure -> 0x1b, -11); the
// bank's header pointers made absolute once (flag bit 0). Then a DMA channel taken (fn_3e110, spinning while none is
// free), the transfer handler set (fn_3a964), SdVoiceTrans(ch, 0, src, spu_base + offset, count); a short transfer
// -> the channel released (fn_3e1cc), 0x105, -12. Returns 0.
void fn_3cd18(IopContext& c) {
    const uint32_t bank = A0(c), src = A1(c), count = A2(c), offset = A3(c);
    const uint32_t flag = StackArg(c, 0), spu_base_in = StackArg(c, 1), spu_size_in = StackArg(c, 2), word = StackArg(c, 3);
    Frame f(c, 0x48);
    St32(f.At(0x4c), src);
    if (Ld32(kLoadChan) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x19, 0, 0, 0);
        St32(kLoadError, 0x104);
        Ret(c, U32(-10));
        return;
    }
    const bool v2 = Ld32(bank) == kSbv2;
    const uint32_t size_slot = 0x2c, base_slot = v2 ? 0x28 : 0x24, cmp_slot = v2 ? 0x2c : 0x28;
    if (spu_size_in != 0) {
        if (spu_size_in < Ld32(bank + cmp_slot)) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x69, spu_size_in, Ld32(bank + cmp_slot), 0);
        }
        St32(bank + size_slot, spu_size_in);
    }
    if (spu_base_in != 0) {
        St32(bank + base_slot, spu_base_in);
        St32(bank + 8, Ld32(bank + 8) & ~4u);
    }
    const uint32_t spu_size = Ld32(bank + size_slot);
    uint32_t spu_base = Ld32(bank + base_slot);
    if (flag == 0) {
        if (Ld32(bank + 8) & 4) {
            const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
            spu_base = Call(c, 0x4272c, spu_size);
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
            if (spu_base == 0) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0x1a, spu_size, 0, 0);
                St32(kLoadError, 0x103);
                Ret(c, U32(-11));
                return;
            }
            St32(bank + (Ld32(bank) == kSbv2 ? 0x28 : 0x24), spu_base);
        } else {
            const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
            const uint32_t ok = Call(c, 0x42a94, spu_base, spu_size);
            if (ok == 0) {
                if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0x1b, spu_size, spu_base, 0);
                St32(kLoadError, 0x103);
                Ret(c, U32(-11));
                return;
            }
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
        }
        const uint32_t fl = Ld32(bank + 8);
        if (!(fl & 1)) {
            St32(bank + 8, fl | 1);
            St32(bank + 0x1c, Ld32(bank + 0x1c) + bank);
            St32(bank + 0x20, Ld32(bank + 0x20) + bank);
            if (Ld32(bank) == kSbv2) St32(bank + 0x24, Ld32(bank + 0x24) + bank);
        }
    }
    St32(kLoadWord, word);
    const uint32_t dst = spu_base + offset;
    uint32_t ch;
    for (;;) {
        ch = Call(c, 0x3e110);
        if (ch != 0xffffffffu) break;
    }
    St32(kLoadChan, ch + 1);
    Call(c, 0x3d0d8);
    Import(c, "libsd", 26, ch, kBankTransHandler, 0);
    St32(f.At(0x10), count);
    const uint32_t done = imp::SdVoiceTrans(c, U32(S32(ch << 16) >> 16), 0, Ld32(f.At(0x4c)), dst);
    if (!(done < count)) {
        Ret(c, 0);
        return;
    }
    Call(c, 0x3e1cc, Ld32(kLoadChan) - 1);
    St32(kLoadChan, 0);
    St32(kLoadError, 0x105);
    Ret(c, U32(-12));
}

// fn_3be20(spu_base, spu_size): the bank load from the opened loader: the 0x20-byte header (a short read -> 0xe, 0;
// a type other than 1 / 3 -> 0x54, -> kLoadError 0x107, 0), the bank block allocated (kind 1; +4 for kind >= 3; a
// failure -> 0xf, 0x102, 0), the staging buffer for the sample data (kind 8, sized to the data or less; a failure ->
// 0x10, 0x102, 0), the header body read (a short read -> 0x11, 0), an 'SBlk' version < 2 refused (0x71), the sample
// data read in staging-buffer chunks each uploaded (fn_3cd18, then fn_3d118(1) waits for the DMA; a failure -> 0x12),
// the bank relocated (fn_3d168) and the staging buffer freed; a type-3 bank also loads its sequence block (fn_3caec,
// stored at the bank base; a failure undoes the bank: fn_3d410, 0x13) and sets flag bit 3. Returns the bank (header)
// address, or 0.
void fn_3be20(IopContext& c) {
    const uint32_t spu_base = A0(c), spu_size = A1(c);
    Frame f(c, 0x70);
    const uint32_t hdr = f.At(0x20);
    uint32_t s1 = 0, s5 = 0;
    if (Call(c, 0x3b080, hdr, 0x20) != 0x20) {
        St32(kCachedSector, 0);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0xe, Ld32(kLoadError), 0, 0);
        Ret(c, 0);
        return;
    }
    const uint32_t type = Ld32(hdr);
    if (type != 1 && type != 3) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x54, hdr, Ld32(kLoadFromCd), Ld32(kCachedSector));
        St32(kCachedSector, 0xffffffffu);
        St32(kLoadError, 0x107);
        Ret(c, 0);
        return;
    }
    const uint32_t kind = Ld32(hdr + 4);
    uint32_t s0 = Ld32(hdr + 0xc);
    if (!(kind < 3)) s0 += 4;
    const uint32_t s3 = Alloc(c, s0, 1, 0, 0);
    if (s3 == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0xf, s0, 0, 0);
        St32(kLoadError, 0x102);
        Ret(c, 0);
        return;
    }
    s1 = s3;
    if (kind == 3) s1 += 4;
    if (Ld32(hdr + 0x14) != 0) {
        s5 = Alloc(c, Ld32(hdr + 0x14), 8, f.At(0x40), 0);
        if (Ld32(hdr + 0x14) < Ld32(f.At(0x40))) St32(f.At(0x40), Ld32(hdr + 0x14));
        if (s5 == 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x10, 0, 0, 0);
            Free(c, s3);
            St32(kLoadError, 0x102);
            Ret(c, 0);
            return;
        }
    }
    Call(c, 0x3ad30, Ld32(hdr + 8), 0);
    if (Call(c, 0x3b080, s1, Ld32(hdr + 0xc)) != Ld32(hdr + 0xc)) {
        St32(kCachedSector, 0);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x11, Ld32(kLoadError), 0, 0);
        Free(c, s3);
        Free(c, s5);
        Ret(c, 0);
        return;
    }
    const uint32_t magic = Ld32(s1);
    if (magic == kSblk && Ld32(s1 + 4) < 2) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x71, 0, 0, 0);
        Free(c, s3);
        Free(c, s5);
        Ret(c, 0);
        return;
    }
    if (Ld32(hdr + 0x14) != 0) {
        uint32_t chunks = 0, done = 0;
        Call(c, 0x3ad30, Ld32(hdr + 0x10), 0);
        while (Ld32(hdr + 0x14) != 0) {
            const uint32_t got = Call(c, 0x3b080, s5, Ld32(f.At(0x40)));
            if (got != Ld32(f.At(0x40))) {
                St32(kCachedSector, 0);
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0xe, Ld32(kLoadError), 0, 0);
                Free(c, s3);
                Free(c, s5);
                Ret(c, 0);
                return;
            }
            St32(f.At(0x10), chunks == 0 ? 0 : 1);
            St32(f.At(0x14), spu_base);
            St32(f.At(0x18), spu_size);
            St32(f.At(0x1c), 0);
            const uint32_t r = Call(c, 0x3cd18, s1, s5, got, chunks == 0 ? 0 : done);
            if (S32(r) < 0) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, 0x12, 0, 0, 0);
                Free(c, s3);
                Free(c, s5);
                Ret(c, 0);
                return;
            }
            Call(c, 0x3d118, 1);
            ++chunks;
            const uint32_t step = Ld32(f.At(0x40));
            done += step;
            const uint32_t left = Ld32(hdr + 0x14) - step;
            St32(hdr + 0x14, left);
            if (left < step) St32(f.At(0x40), left);
        }
        Call(c, 0x3d168, s1);
        Free(c, s5);
    } else {
        St32(s1 + 0x1c, Ld32(s1 + 0x1c) + s1);
        St32(s1 + 0x20, Ld32(s1 + 0x20) + s1);
        if (magic == kSbv2) St32(s1 + 0x24, Ld32(s1 + 0x24) + s1);
        St32(s1 + 8, Ld32(s1 + 8) | 1);
        Call(c, 0x3d168, s1);
    }
    if (Ld32(hdr + 4) != 3) {
        Ret(c, s1);
        return;
    }
    Call(c, 0x3ad30, Ld32(hdr + 0x18), 0);
    const uint32_t seq = Call(c, 0x3caec);
    St32(s3, seq);
    if (seq == 0) {
        Call(c, 0x3d410, s1);
        Free(c, s3);
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x13, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    St32(s1 + 8, Ld32(s1 + 8) | 8);
    Ret(c, s1);
}

// fn_3c308(sector, offset, count, dst): the loader's read of `count` bytes at `offset` into sector `sector` from the
// CD, under the reply semaphore (a failed wait reported, 0x7d0): the DMA channels drained (fn_46d8c 0 / 0x10), the
// loader status pushed (fn_3dcb8(1, 0)); a partial first sector through the small sector buffer (cached by sector
// number in kCachedSector), whole sectors straight into `dst` (fn_46840 + fn_46e94), a partial last sector through
// the small buffer again. A failed read: the error pushed (fn_3dcb8(0, err), 0x30 when the request itself failed),
// kLoadError = err, 0. Success: fn_3dcb8(0, 0), 1.
void fn_3c308(IopContext& c) {
    uint32_t sector = A0(c);
    int32_t off = S32(A1(c));
    const uint32_t count = A2(c), dst = A3(c);
    Frame f(c, 0x48);
    if (imp::WaitSema(c, Ld32(kReplySema)) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x7d0, 0, 0, 0);
    }
    Call(c, 0x46d8c, 0);
    Call(c, 0x46d8c, 0x10);
    Call(c, 0x3dcb8, 1, 0);
    St8(f.At(0x18), 0x64);
    St8(f.At(0x19), 0);
    St8(f.At(0x1a), 0);
    uint32_t s0 = 0;
    const int32_t skip = Sra11(off);
    sector += U32(skip);
    off -= skip << 11;
    int32_t remaining = S32(count);
    uint32_t copied = 0;
    auto finish_ok = [&]() {
        Call(c, 0x3dcb8, 0, 0);
        imp::SignalSema(c, Ld32(kReplySema));
        Ret(c, 1);
    };
    auto finish_fail = [&](uint32_t err) {
        Call(c, 0x3dcb8, 0, err);
        St32(kLoadError, err);
        imp::SignalSema(c, Ld32(kReplySema));
        Ret(c, 0);
    };
    // the small sector buffer refreshed for `sector`; false = failed (s0 = the error, 0 when the request failed)
    auto fetch_small = [&](bool loud) -> bool {
        s0 = Call(c, 0x46840, sector, 1, kCdSmallBuffer);
        if (loud && s0 == 0 && Ld32(G::kQuiet) == 0) imp::Printf(c, kFmtLoadReadFailed);
        Call(c, 0x46d8c, 0);
        if (s0 == 0) {
            St32(kCachedSector, 0);
            return false;
        }
        s0 = Call(c, 0x46e94);
        if (s0 != 0) {
            St32(kCachedSector, 0);
            return false;
        }
        St32(kCachedSector, sector);
        return true;
    };
    if (remaining < 0x800 || off != 0) {
        if (sector != Ld32(kCachedSector) && !fetch_small(true)) {
            finish_fail(s0 != 0 ? s0 : 0x30);
            return;
        }
        const int32_t in = off - (Sra11(off) << 11);
        int32_t s4 = 0x800 - in;
        if (S32(count) < s4) s4 = S32(count);
        Import(c, "sysclib", 12, dst, kCdSmallBuffer + U32(in), U32(s4));
        copied = U32(s4);
        remaining = S32(count) - s4;
        sector += 1;
        if (remaining == 0) {
            finish_ok();
            return;
        }
    }
    const int32_t whole = Sra11(remaining);
    if (whole != 0) {
        s0 = Call(c, 0x46840, sector, U32(whole), dst + copied);
        Call(c, 0x46d8c, 0);
        if (s0 == 0) {
            finish_fail(0x30);
            return;
        }
        s0 = Call(c, 0x46e94);
        if (s0 != 0) {
            finish_fail(s0);
            return;
        }
    }
    remaining -= whole << 11;
    sector += U32(whole);
    if (remaining == 0) {
        finish_ok();
        return;
    }
    if (sector != Ld32(kCachedSector) && !fetch_small(false)) {
        finish_fail(s0 != 0 ? s0 : 0x30);
        return;
    }
    Import(c, "sysclib", 12, dst + (count - U32(remaining)), kCdSmallBuffer, U32(remaining));
    finish_ok();
}

// fn_3e920(a, b, n, rate, [mode]): the movie's voice programming after its buffer is primed: the volume pair from the
// pan law (kMovieVolL/R); the SPU-DMA mode -> SdBlockTrans(ch, 0x13, a, b, n) and the channel's voice volumes (through
// fn_4ed08 group scaling) and attribute bits 6..7 under suspended interrupts; the voice mode (3) -> the two voices
// programmed (ADSR, pitch from `rate`, the start address, the key-on switches, the volumes; `mode` < 2 = one voice
// carrying both volumes, else the second voice carries the right side) and keyed on (fn_4dfc4). The busy word set. Returns 1.
void fn_3e920(IopContext& c) {
    const uint32_t a = A0(c), b = A1(c), n = A2(c);
    uint32_t rate = A3(c);
    const uint32_t mode = StackArg(c, 0);
    Frame f(c, 0x48);
    St16(kMovieVol, 0);
    St16(kMovieVol + 2, 0);
    Call(c, 0x4ea54, Ld32(kMovieVolL), Ld32(kMovieVolR), kMovieVol);
    if (Ld32(kMovieMode) != 3) {
        St32(f.At(0x10), n);
        Import(c, "libsd", 18, U32(Lds16(G::kMovieChan)), 0x13, a, b);
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
        St32(f.At(0x1c), suspended);
        uint32_t v = Call(c, 0x4ed08, U32(Lds16(kMovieVol)), Ld32(kMovieGroup));
        imp::SdSetParam(c, Ld16(G::kMovieChan) | 0xf80, v & 0xffff);
        v = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
        imp::SdSetParam(c, Ld16(G::kMovieChan) | 0x1080, v & 0xffff);
        const uint32_t p = imp::SdGetParam(c, Ld16(G::kMovieChan) | 0x800);
        imp::SdSetParam(c, Ld16(G::kMovieChan) | 0x800, (p | 0xc0) & 0xffff);
        if (Ld32(f.At(0x1c)) == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
    } else {
        St32(kMovieW1, mode);
        St32(kMovieW2, rate);
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
        St32(f.At(0x1c), suspended);
        auto program = [&](uint32_t voice_word, uint32_t addr) {
            const int32_t v = S32(voice_word);
            const int32_t core = Div24(v), voice = v - core * 24;
            const uint32_t e = U32(core) | (U32(voice) << 1);
            imp::SdSetParam(c, (e | 0x300) & 0xffff, 0xf);
            imp::SdSetParam(c, (e | 0x400) & 0xffff, 0x1fc0);
            imp::SdSetParam(c, (e | 0x200) & 0xffff, (MulHiU(rate, 0x057619f1u) >> 10) & 0xffff);
            imp::SdSetAddr(c, (e | 0x2040) & 0xffff, addr);
            uint32_t sw = (U32(core) | 0x1800) & 0xffff;
            const uint32_t bit = 1u << (U32(voice) & 31);
            imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | bit);
            sw = (U32(core) | 0x1a00) & 0xffff;
            imp::SdSetSwitch(c, sw, imp::SdGetSwitch(c, sw) | bit);
            return e;
        };
        rate <<= 12;
        const uint32_t e0 = program(Ld32(kMovieVoice0), Ld32(kMovieArg) + Ld32(kMovieOffset));
        uint32_t v = Call(c, 0x4ed08, U32(Lds16(kMovieVol)), Ld32(kMovieGroup));
        imp::SdSetParam(c, e0 & 0xffff, (v & 0xffff) >> 1);
        const bool two_voices = !(S32(mode) < 2);
        if (!two_voices) {
            v = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
            imp::SdSetParam(c, (e0 | 0x100) & 0xffff, (v & 0xffff) >> 1);
        } else {
            imp::SdSetParam(c, (e0 | 0x100) & 0xffff, 0);
            const uint32_t e1 = program(Ld32(kMovieVoice1), Ld32(kMovieArg) + Ld32(kMovieCount) + Ld32(kMovieOffset));
            imp::SdSetParam(c, e1 & 0xffff, 0);
            v = Call(c, 0x4ed08, U32(Lds16(kMovieVol + 2)), Ld32(kMovieGroup));
            imp::SdSetParam(c, (e1 | 0x100) & 0xffff, (v & 0xffff) >> 1);
            const int32_t v1 = Lds32(kMovieVoice1), c1 = Div24(v1);
            Call(c, 0x4dfc4, U32(c1), U32(v1 - c1 * 24), 0);
        }
        const int32_t v0 = Lds32(kMovieVoice0), c0 = Div24(v0);
        Call(c, 0x4dfc4, U32(c0), U32(v0 - c0 * 24), 0);
        if (Ld32(f.At(0x1c)) == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
    }
    St32(kMovieBusy, 1);
    Ret(c, 1);
}

// fn_3e284(size, count, a2, a3, [group, mode]): the movie started: a movie already open -> 0x70, 0; a size not a
// multiple of 4 -> 0x1e, 0; the buffer allocated (kind 6; a failure -> 0x1f, 0), its size and the mode recorded. The
// voice mode (3): two voices allocated (fn_4c750(group, 0x7f, 1) under the voice-table lock; a miss -> 0x68, the
// buffer freed, the voices released, 0), the SPU chunk count (count, or size * 4) and the SPU block (fn_4272c; a
// failure -> 0x67, undone, 0). Then a DMA channel taken (fn_3e110, spinning while none is free), its transfer handler
// cleared, the volume law words (a2 * 127 / 1024, a3), the group, fn_3e678(); the busy word and volume pair cleared.
// Returns the buffer.
void fn_3e284(IopContext& c) {
    const uint32_t size = A0(c), count = A1(c), a2 = A2(c), a3 = A3(c);
    const uint32_t group = StackArg(c, 0), mode = StackArg(c, 1);
    Frame f(c, 0x40);
    St32(f.At(0x48), a2);
    St32(f.At(0x4c), a3);
    if (Ld32(kMovieBuf) != 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x70, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    if (size & 3) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x1e, 0, 0, 0);
        Ret(c, 0);
        return;
    }
    const uint32_t buf = Alloc(c, size, 6, 0, A3(c));
    St32(kMovieBuf, buf);
    St32(kMovieBufSize, size);
    St32(kMovieMode, mode);
    if (buf == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 0x1f, size, 0, 0);
        Ret(c, 0);
        return;
    }
    auto release_voices = [&]() {
        if (Ld32(kMovieVoice0) != 0xffffffffu) {
            Call(c, 0x4dfa4, Ld32(kMovieVoice0));
            St32(kMovieVoice0, 0xffffffffu);
        }
        if (Ld32(kMovieVoice1) != 0xffffffffu) {
            Call(c, 0x4dfa4, Ld32(kMovieVoice1));
            St32(kMovieVoice1, 0xffffffffu);
        }
    };
    if (mode == 3) {
        Call(c, 0x4c1ac, 1, 0x3a);
        const uint32_t v0 = Call(c, 0x4c750, group, 0x7f, 1);
        St32(kMovieVoice0, v0);
        const uint32_t v1 = Call(c, 0x4c750, group, 0x7f, 1);
        St32(kMovieVoice1, v1);
        Call(c, 0x4c2f8);
        if (Ld32(kMovieVoice0) == 0xffffffffu || Ld32(kMovieVoice1) == 0xffffffffu) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x68, 0, 0, 0);
            Free(c, Ld32(kMovieBuf));
            release_voices();
            Ret(c, 0);
            return;
        }
        St32(kMovieCount, count == 0 ? size << 2 : count);
        const uint32_t spu = Call(c, 0x4272c, Ld32(kMovieCount) << 1);
        St32(kMovieArg, spu);
        if (spu == 0) {
            St32(f.At(0x10), 0);
            Call(c, 0x35140, 0x67, Ld32(kMovieCount) << 1, 0, 0);
            Free(c, Ld32(kMovieBuf));
            release_voices();
            Ret(c, 0);
            return;
        }
    }
    const uint32_t s2 = Ld32(f.At(0x48)) << 7;
    uint32_t ch;
    for (;;) {
        ch = Call(c, 0x3e110);
        St32(G::kMovieChan, ch);
        if (ch != 0xffffffffu) break;
        St32(G::kMovieChan, 999);
    }
    Import(c, "libsd", 26, ch, 0, 0);
    St32(kMovieVolL, U32(S32(s2 - Ld32(f.At(0x48))) >> 10));
    St32(kMovieOffset, 0);
    St32(kMovieVolR, Ld32(f.At(0x4c)));
    St32(kMovieGroup, group);
    Call(c, 0x3e678);
    const uint32_t ret = Ld32(kMovieBuf);
    St32(kMovieBusy, 0);
    St16(kMovieVol, 0);
    St16(kMovieVol + 2, 0);
    Ret(c, ret);
}

}  // namespace ps2r::title::sly2::snd989
