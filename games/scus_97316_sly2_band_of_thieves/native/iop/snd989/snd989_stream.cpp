// snd989_stream.cpp -- the type-4 (streamed) sound handle's tick (fn_45904), its per-voice poll (fn_45ccc: NAX
// tracking over the two half-buffers, the refill request, the end detection), the played-bytes accounting
// (fn_461e8) and the next-chunk hand-off (fn_44768). Ported from the recompiled 989snd_Library over the module's RAM.
//
// A stream voice record `st` (0xd8 bytes, chained at st[0xc8]): st[0] flags (bit 3 = playing, bit 4 = ..., 0x40 =
// ending, 0x200 = loop-address pending, 6 = ...), st[0xc] the SPU buffer size (two halves), st[0x10] mode flags,
// st[0x18] / st[0x44] the two half-buffers' SPU addresses (0x2c-byte half records at st + 0x18 + half * 0x2c),
// st[0x68] the half NAX is in, st[0x6c] the last NAX, st[0x70] the tick the refill was requested, st[0x74] /
// st[0x78] stall counters, st[0x7c] the SPU voice, st[0xb4] bytes played, st[0xbc] the last position, st[0xd0] the
// owning stream handle. The handle `h` (0x78 bytes, fn_417f8): h[4] the first voice record, h[0x38] a helper voice,
// h[0x6c] the chunk being played, h[0x70] / h[0x74] queued chunks.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

static inline uint32_t VoiceEntry(int v) { return static_cast<uint32_t>(VoiceCore(v)) | (static_cast<uint32_t>(VoiceBit(v)) << 1); }

// fn_461e8(st, pos): add the bytes played since the last position to st[0xb4] (crossing a half boundary adds the
// rest of the old half and the start of the new one); st[0xbc] = pos. Returns the sum (same half) or the new half's
// record offset (crossed) -- the value $v0 carries.
void fn_461e8(IopContext& c) {
    const uint32_t st = A0(c);
    uint32_t pos = A1(c);
    const uint32_t half_bytes = Ld32(st + 0xc) >> 1;
    const uint32_t end1 = Ld32(st + 0x44) + half_bytes;
    if (end1 < pos) pos = end1;
    const uint32_t base0 = Ld32(st + 0x18);
    const uint32_t last = Ld32(st + 0xbc);
    uint32_t half = 1;
    if (base0 - 1 < last && last < base0 + half_bytes) half = 0;
    const uint32_t cur_half = Ld32(st + 0x68);
    uint32_t v0;
    if (half == cur_half) {
        v0 = Ld32(st + 0xb4) + (pos - last);
        St32(st + 0xb4, v0);
    } else {
        const uint32_t old_base = Ld32(st + 0x18 + half * 0x2c);
        const uint32_t new_rec = st + 0x18 + cur_half * 0x2c;
        uint32_t acc = Ld32(st + 0xb4) + (old_base + (Ld32(st + 0xc) >> 1) - last);
        St32(st + 0xb4, acc);
        acc += pos - Ld32(new_rec);
        St32(st + 0xb4, acc);
        v0 = cur_half * 0x2c;
    }
    St32(st + 0xbc, pos);
    Ret(c, v0);
}

// fn_44768(h, kind): hand the next queued chunk(s) at h[0x70] to fresh voice records (fn_43d2c allocates one or two:
// two when the stream is stereo and fn_4b374 says 2), configured by fn_44a58, then started (fn_4aa70) and h[0x6c] set;
// the stream thread is signalled. Nothing when a chunk is already playing or none is queued.
void fn_44768(IopContext& c) {
    const uint32_t h = A0(c), kind = A1(c);
    Frame f(c, 0x38);
    const uint32_t loc_a = f.At(0x18), loc_b = f.At(0x1c);
    St32(loc_a, 0);
    St32(loc_b, 0);
    if (Ld32(h + 0x6c) != 0) return;
    if (Ld32(h + 0x70) == 0) return;
    uint32_t two = 0;
    const uint32_t first = Ld32(h + 4);
    if (Call(c, 0x4b374, first) == 2) two = (Ld32(first + 0x10) & 0x100) == 0 ? 1 : 0;
    St32(f.At(0x10), static_cast<uint32_t>(Lds8(h + 0x17)));
    if (Call(c, 0x43d2c, loc_a, loc_b, two, Ld32(first + 0x10) & 0x100) == 0) return;
    auto fill = [&](uint32_t loc) {
        const uint32_t rec = Ld32(loc);
        St32(rec, Ld32(rec) | 0x10);
        if (Ld32(Ld32(h + 0x70)) & 2) St32(rec, Ld32(rec) | 0x100);
        if (Ld32(Ld32(h + 0x70)) & 4) St32(rec, Ld32(rec) | 0x400);
        uint32_t q = Ld32(h + 0x70);
        St32(f.At(0x10), 1);
        St32(f.At(0x14), kind);
        Call(c, 0x44a58, h, rec, Ld32(q + 4), Ld32(q + 8));
        q = Ld32(h + 0x70);
        St8(rec + 0xc5, Ld8(q + 0xd));
        if (Ld8(h + 0x16) & 0x10) {
            St8(rec + 0xc4, Ld8(Ld32(h + 0x70) + 0xc));
            St16(rec + 0xc6, Ld16(Ld32(h + 0x70) + 0xe));
        }
        q = Ld32(h + 0x70);
        St32(q, 0);
        St32(h + 0x70, Ld32(Ld32(h + 0x70) + 0x10));
    };
    fill(loc_a);
    if (two != 0) fill(loc_b);
    Call(c, 0x4aa70, Ld32(loc_a) + 0x10);
    St32(h + 0x6c, Ld32(loc_a));
    imp::SignalSema(c, Ld32(G::kStreamSema));
}

// fn_45ccc(st, set_flag): poll a playing stream voice. Reads NAX (three times, settling), classifies it into a half
// (st[0x68]); a half change requests the refill (fn_4a9d4 unless the mode says the data is already there), a stuck
// NAX counts st[0x74] towards the error 0x34/0x35/0x36; the pending loop address (0x200) is written (SdSetAddr LSAX)
// for this voice and the helper voice; an ending voice (0x40) polls ENVX until silent (-1). Returns 0, -1 (ended) or
// -2 (error); *set_flag = 1 when the refill was requested; the played bytes are accounted (fn_461e8).
void fn_45ccc(IopContext& c) {
    const uint32_t st = A0(c), set_flag = A1(c);
    Frame f(c, 0x50);
    const uint32_t loc18 = f.At(0x18);
    uint32_t result = 0, flag = 0;
    if ((Ld32(st) & 0x18) == 8) {
        const int v = Lds32(st + 0x7c);
        const uint32_t ventry = VoiceEntry(v);
        uint32_t suspended = imp::CpuSuspendIntr(c, loc18);
        const uint32_t nax_entry = (ventry | 0x2240) & 0xffff;
        uint32_t a = imp::SdGetAddr(c, nax_entry);
        uint32_t b = imp::SdGetAddr(c, nax_entry);
        uint32_t d = imp::SdGetAddr(c, nax_entry);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(loc18));
        uint32_t pos;
        bool settle = false;
        if (a == b) {
            pos = a;
            settle = true;
        } else if (b == d) {
            pos = b;
        } else {
            suspended = imp::CpuSuspendIntr(c, loc18);
            a = imp::SdGetAddr(c, nax_entry);
            b = imp::SdGetAddr(c, nax_entry);
            d = imp::SdGetAddr(c, nax_entry);
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(loc18));
            if (a == b) {
                pos = a;
                settle = true;
            } else if (b == d) {
                pos = b;
            } else {
                pos = Ld32(st + 0x6c);
            }
        }
        if (settle && pos != d) pos = b;
        // Which half is `pos` in.
        const uint32_t base0 = Ld32(st + 0x18), base1 = Ld32(st + 0x44);
        uint32_t half = 0;
        bool located = false;
        if (base0 - 1 < pos) {
            half = 0;
            if (pos < base1) located = true;
        }
        if (!located) {
            if (base1 - 1 < pos) {
                half = 1;
                if (pos < base1 + (Ld32(st + 0xc) >> 1)) located = true;
            }
        }
        if (!located) {
            const int32_t stalls = Lds32(st + 0x74) + 1;
            St32(st + 0x74, static_cast<uint32_t>(stalls));
            uint32_t code, a2;
            bool error = false;
            if (stalls < 9) {
                if (Ld32(st + 0xb4) != 0 && Ld32(st + 0x6c) != 0) {
                    code = 0x35;
                    a2 = pos;
                    error = true;
                } else {
                    pos = Ld32(st + 0x18);
                    half = 0;
                }
            } else {
                code = 0x34;
                a2 = 0;
                error = true;
            }
            if (error) {
                St32(f.At(0x10), 0);
                Call(c, 0x35140, code, Ld32(Ld32(st + 0xd0)), a2, 0);
                Ret(c, 0xfffffffeu);
                return;
            }
        }
        if (Ld32(st) & 0x200) {
            suspended = imp::CpuSuspendIntr(c, loc18);
            imp::SdSetAddr(c, (ventry | 0x2140) & 0xffff, Ld32(st + 0x44));
            const int helper = Lds32(Ld32(st + 0xd0) + 0x38);
            if (helper != -1) imp::SdSetAddr(c, (VoiceEntry(helper) | 0x2140) & 0xffff, Ld32(st + 0x44));
            St32(st, Ld32(st) & ~0x200u);
            if (suspended == 0) imp::CpuResumeIntr(c, Ld32(loc18));
        }
        if (Ld32(st) & 0x40) {
            if (!(Ld32(st + 0xb4) < 0x21)) {
                const int vv = Lds32(st + 0x7c);
                const uint32_t e = VoiceEntry(vv);
                const uint32_t envx = imp::SdGetParam(c, (e | 0x500) & 0xffff);
                pos = imp::SdGetAddr(c, (e | 0x2240) & 0xffff);
                if (envx == 0) result = 0xffffffffu;
            } else {
                const int32_t n = Lds32(st + 0x78) + 1;
                St32(st + 0x78, static_cast<uint32_t>(n));
                if (!(n < 0x79)) {
                    imp::Printf(c, G::kFmtStreamStuck, st);
                    result = 0xfffffffeu;
                }
            }
            St32(st + 0x68, half);
        } else if (half != Ld32(st + 0x68)) {
            // A half change within 10 ticks of the last refill request is a stall (the data is not there yet);
            // only an older request (or none) lets this one through.
            bool request = true;
            if (Ld32(st + 0x70) != 0) {
                const uint32_t now = Call(c, 0x43638);
                if (!(Ld32(st + 0x70) + 10 < now)) {
                    request = false;
                    const int32_t stalls = Lds32(st + 0x74) + 1;
                    St32(st + 0x74, static_cast<uint32_t>(stalls));
                    if (!(stalls < 9)) {
                        St32(f.At(0x10), 0);
                        Call(c, 0x35140, 0x36, Ld32(Ld32(st + 0xd0)), 0, 0);
                        Ret(c, 0xfffffffeu);
                        return;
                    }
                }
            }
            if (request) {
                bool ok = true;
                if ((Ld32(st + 0x10) & 0x300) != 0x100)
                    ok = Call(c, 0x4a9d4, st + 0x10 + Ld32(st + 0x68) * 0x2c) != 0;
                if (ok) {
                    flag = 1;
                    St32(st + 0x68, half);
                    St32(st + 0x70, Call(c, 0x43638));
                } else {
                    result = 0xfffffffeu;
                }
            }
            St32(st + 0x6c, pos);
        } else {
            const uint32_t last = Ld32(st + 0x6c);
            if (last != 0 && pos < last && !(Ld32(st + 0x10) & 0x800)) {
                St32(f.At(0x10), 0);
                result = 0xfffffffeu;
                Call(c, 0x35140, 0x37, Ld32(Ld32(st + 0xd0)), 0, 0);
            }
            St32(st + 0x6c, pos);
        }
        if (result == 0) Call(c, 0x461e8, st, pos);
    }
    const uint32_t w = Ld32(st);
    if ((w & 6) == 6 && !(w & 0x10)) St32(st, (w & ~2u) | 8);
    if (flag != 0) St32(set_flag, 1);
    Ret(c, result);
}

// fn_45904(h): the stream handle's tick step. A stopping handle (flags & 4) finishes its stop (fn_454cc / fn_45470 /
// fn_4555c). Else every voice record is polled (fn_45ccc) until one reports: -1 = the chunk ended -> the next chunk
// (fn_44768) is swapped in (its voices re-keyed, or the whole handle stopped when nothing follows: fn_4144c), -2 =
// error -> the stop path; the refill thread is woken (g[kRampWaiterSema]) when a poll asked.
void fn_45904(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x110);
    const uint32_t loc_flag = f.At(0xd8), loc_intr = f.At(0xdc), loc_kind = f.At(0xe0), voices = f.At(0x18);
    St32(loc_flag, 0);
    if (Ld8(h + 0x16) & 4) {
        if (Call(c, 0x454cc, h) != 0) return;
        if (Call(c, 0x45470, h) != 0) return;
        Call(c, 0x4555c, Ld32(h), 1);
        return;
    }
    const uint32_t first = Ld32(h + 4);
    St32(loc_kind, Ld32(first) & 0x1f800);
    uint32_t r = 0;
    for (uint32_t st = first; st != 0; st = Ld32(st + 0xc8)) {
        r = Call(c, 0x45ccc, st, loc_flag);
        if (r != 0) break;
    }
    St32(first + 0x74, 0);
    if (r == 0xffffffffu) {
        if (Ld32(first) & 0x400) {
            Call(c, 0x49198, h);
        } else {
            const uint32_t stereo_pair = Ld32(first + 0x10) & 0x100;
            uint32_t n = 0;
            for (uint32_t st = first; st != 0; st = Ld32(st + 0xc8), ++n) {
                St32(voices + n * 4, Ld32(st + 0x7c));
                St32(st, 1);
                if (stereo_pair != 0) {
                    Call(c, 0x4d318, Ld32(st + 0x7c));
                    Call(c, 0x4de04, Ld32(st + 0x7c));
                    St8(G::Voice(Lds32(st + 0x7c)) + G::kVoicePrio, 0);
                    St32(st + 0x7c, 0xffffffffu);
                }
            }
            if (Ld32(h + 0x70) != 0) Call(c, 0x44768, h, Ld32(loc_kind));
            const uint32_t next = Ld32(h + 0x6c);
            if (next != 0) {
                St32(h + 4, next);
                uint32_t rekey = 0, i = 0;
                for (uint32_t st = next; st != 0; st = Ld32(st + 0xc8), ++i) {
                    if (stereo_pair == 0) St32(st + 0x7c, Ld32(voices + i * 4));
                    const uint32_t w = Ld32(st);
                    St32(st, w & ~0x10u);
                    if (w & 0x80) {
                        St32(st, w & ~0x10u & ~0x80u);
                        rekey = 1;
                    }
                }
                if (rekey != 0) {
                    Call(c, 0x4a050, Ld32(h + 4), 1);
                    St32(h + 0x6c, 0);
                } else {
                    const uint32_t id = Ld32(h);
                    St8(h + 0x16, Ld8(h + 0x16) | 2);
                    Call(c, 0x450c4, id);
                    St32(h + 0x6c, 0);
                }
            } else {
                const uint32_t suspended = imp::CpuSuspendIntr(c, loc_intr);
                const int helper = Lds32(h + 0x38);
                if (helper != -1) {
                    Call(c, 0x4d318, static_cast<uint32_t>(helper));
                    Call(c, 0x4de04, Ld32(h + 0x38));
                    St8(G::Voice(Lds32(h + 0x38)) + G::kVoicePrio, 0);
                    St32(h + 0x38, 0xffffffffu);
                }
                for (uint32_t st = first; st != 0;) {
                    const int v = Lds32(st + 0x7c);
                    St32(st, 1);
                    if (v != -1) {
                        Call(c, 0x4d318, static_cast<uint32_t>(v));
                        Call(c, 0x4de04, Ld32(st + 0x7c));
                        St8(G::Voice(Lds32(st + 0x7c)) + G::kVoicePrio, 0);
                        St32(st + 0x7c, 0xffffffffu);
                    }
                    const uint32_t count = Ld32(G::kStreamVoiceCount);
                    st = Ld32(st + 0xc8);
                    St32(G::kStreamVoiceCount, count - 1);
                }
                if (suspended == 0) imp::CpuResumeIntr(c, Ld32(loc_intr));
                Call(c, 0x4144c, h, 0);
            }
        }
    } else if (r == 0xfffffffeu) {
        if (Call(c, 0x454cc, h) == 0 && Call(c, 0x45470, h) == 0) {
            Call(c, 0x4555c, Ld32(h), 1);
        } else {
            St8(h + 0x16, Ld8(h + 0x16) | 4);
        }
        return;
    } else {
        if (Ld32(loc_flag) != 0) imp::SignalSema(c, Ld32(G::kRampWaiterSema));
    }
    if (Ld32(h + 0x74) != 0) Call(c, 0x44768, h, Ld32(loc_kind));
}

}  // namespace ps2r::title::sly2::snd989
