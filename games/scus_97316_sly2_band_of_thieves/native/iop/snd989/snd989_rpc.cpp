// snd989_rpc.cpp -- the 0x123456 RPC face: the dispatcher (fn_3497c), the batch (fn_34880), the external-command
// dispatch (fn_344c4 / fn_3318c / fn_33148) and the hot shims + bodies the lanes exercise (fno 26 bank-sound-is-valid,
// 90 movie push, 91 movie read position, 9 group volume, the lookups). Ported from the recompiled 989snd_Library.
// THE REPLY BUFFER IS STATE (nat2-snd.md): the dispatcher never writes the result word; a shim with a value stores it
// at *g[kResultPtr]; the batch advances kResultPtr by 4 per record and steps back 4 at its end.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// fn_3497c(fno, buff, size): the RPC handler of 0x123456. result_ptr = reply + 4; handlers[fno](buff, buff, size, a3);
// *(result_ptr + 4) = -1; returns the reply buffer. ($a0 = $a1 = buff at the shim's entry: `move a0, a1`.)
void fn_3497c(IopContext& c) {
    const uint32_t fno = A0(c), buff = A1(c);
    Frame f(c, 0x20);
    const uint32_t shim = Ld32(G::kHandlerTable + fno * 4);
    St32(G::kResultPtr, Ld32(G::kReplyBufPtr) + 4);
    CallAddr(c, shim, buff, buff, A2(c), A3(c));
    St32(Ld32(G::kResultPtr) + 4, 0xffffffffu);
    Ret(c, Ld32(G::kReplyBufPtr));
}

// fn_34880(buff): fno 77, the batch {count, {u16 fno, u16 len, body}...}: each record's shim on its body (len rounded
// up to 4), kResultPtr += 4 per record, -= 4 at the end (returned).
void fn_34880(IopContext& c) {
    const uint32_t buff = A0(c);
    Frame f(c, 0x30);
    uint32_t p = buff + 4;
    const uint32_t a2 = A2(c), a3 = A3(c);
    for (int32_t i = 0; i < Lds32(buff); ++i) {   // the count is re-read per record, as the original does
        const int32_t fno = Lds16(p);
        p += 2;
        int32_t len = Lds16(p);
        if (len & 3) {
            const int32_t rounded_down = ((len >= 0 ? len : len + 3) >> 2) << 2;
            len = static_cast<int16_t>(len + 4 - (len - rounded_down));
        }
        p += 2;
        const uint32_t body = p;
        p += static_cast<uint32_t>(len);
        CallAddr(c, Ld32(G::kHandlerTable + static_cast<uint32_t>(fno) * 4), body, static_cast<uint32_t>(fno), a2, a3);
        St32(G::kResultPtr, Ld32(G::kResultPtr) + 4);
    }
    const uint32_t rp = Ld32(G::kResultPtr) - 4;
    St32(G::kResultPtr, rp);
    Ret(c, rp);
}

// fn_33148(tag): the registered external-module record with this tag (list g[kExtModuleList], next at +8), or 0.
void fn_33148(IopContext& c) {
    const uint32_t tag = A0(c);
    uint32_t n = Ld32(G::kExtModuleList);
    while (n != 0) {
        if (Ld32(n) == tag) break;
        n = Ld32(n + 8);
    }
    Ret(c, n);
}

// fn_3318c(tag, op, a, b, c, d, e): the external command -- record(tag)->handlers[op](a, b, c, d, e) (op < count),
// else the error (3: no such module, 4: no such op) and 0.
void fn_3318c(IopContext& c) {
    const uint32_t tag = A0(c), op = A1(c), p0 = A2(c), p1 = A3(c);
    const uint32_t s0 = StackArg(c, 0), s1 = StackArg(c, 1), s2 = StackArg(c, 2);
    Frame f(c, 0x30);
    const uint32_t rec = Call(c, 0x33148, tag);
    if (rec == 0) {
        St32(f.At(0x10), 0);
        Call(c, 0x35140, 3, tag, tag, 0);
        Ret(c, 0);
        return;
    }
    if (static_cast<int32_t>(op) < Lds32(rec + 4)) {
        St32(f.At(0x10), s2);
        CallAddr(c, Ld32(rec + 0x10 + op * 4), p0, p1, s0, s1);
        return;
    }
    St32(f.At(0x10), 0);
    Call(c, 0x35140, 4, tag, 0, 0);
    Ret(c, 0);
}

// fn_344c4(buff): fno 76's shim -- fn_3318c(buff[0..6]) and the result stored.
void fn_344c4(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x28);
    St32(f.At(0x10), Ld32(b + 0x10));
    St32(f.At(0x14), Ld32(b + 0x14));
    St32(f.At(0x18), Ld32(b + 0x18));
    const uint32_t r = Call(c, 0x3318c, Ld32(b), Ld32(b + 4), Ld32(b + 8), Ld32(b + 0xc));
    St32(Ld32(G::kResultPtr), r);
}

// fn_3e610: the movie read position = SdGetAddr(NAX of the first movie voice) - the movie's SPU base (fno 91's body).
void fn_3e610(IopContext& c) {
    Frame f(c, 0x18);
    const int v = Lds32(G::kMovieVoice0);
    const uint32_t entry = (static_cast<uint32_t>(VoiceCore(v)) | (static_cast<uint32_t>(VoiceBit(v)) << 1) | 0x2240) & 0xffff;
    const uint32_t nax = imp::SdGetAddr(c, entry);
    Ret(c, nax - Ld32(G::kMovieSpuBase));
}

// fn_34184(buff): fno 91's shim -- the result stored.
void fn_34184(IopContext& c) {
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x3e610);
    St32(Ld32(G::kResultPtr), r);
}

// fn_3e5c0(size, offset): the movie push -- SdVoiceTrans(chan, 0, iop_buf, spu_base + offset, size) then the status
// poll SdVoiceTransStatus(chan, 1) (fno 90's body; void).
void fn_3e5c0(IopContext& c) {
    const uint32_t size = A0(c), offset = A1(c);
    Frame f(c, 0x20);
    const uint32_t spu = Ld32(G::kMovieSpuBase) + offset;
    const uint32_t iop_buf = Ld32(G::kMovieIopBuf);
    St32(f.At(0x10), size);
    imp::SdVoiceTrans(c, static_cast<uint32_t>(Lds16(G::kMovieChan)), 0, iop_buf, spu);
    imp::SdVoiceTransStatus(c, static_cast<uint32_t>(Lds16(G::kMovieChan)), 1);
}

// fn_34158(buff): fno 90's shim -- fn_3e5c0(buff[0], buff[1]); no result word.
void fn_34158(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    Call(c, 0x3e5c0, Ld32(b), Ld32(b + 4));
}

// fn_39b24(bank, index): the 'SBlk' sound block's entry flag bit 0 (12-byte entries at bank[0x1c]).
void fn_39b24(IopContext& c) {
    const uint32_t bank = A0(c), index = A1(c);
    Ret(c, Ld16(Ld32(bank + 0x1c) + index * 12 + 6) & 1);
}

// fn_3fac8(bank, index): snd_BankSoundIsValid -- 'SBlk' -> fn_39b24; else index in [0, count) and the 28-byte entry's
// type: 4 -> (s8[0x12] == 0), 3 or 5 -> 1, else 0; an entry with a null pointer -> 0.
void fn_3fac8(IopContext& c) {
    const uint32_t bank = A0(c);
    const int32_t index = static_cast<int32_t>(A1(c));
    Frame f(c, 0x18);
    if (bank == 0) {
        Ret(c, 0);
        return;
    }
    if (Ld32(bank) == 0x6b6c4253u) {
        Call(c, 0x39b24, bank, static_cast<uint32_t>(index));
        return;
    }
    if (!(index < Lds16(bank + 0x14)) || index < 0) {
        Ret(c, 0);
        return;
    }
    const uint32_t e = Ld32(bank + 0x1c) + static_cast<uint32_t>(index) * 28;
    if (Ld32(e + 4) == 0) {
        Ret(c, 0);
        return;
    }
    const int32_t type = Lds32(e);
    if (type == 4) Ret(c, Lds8(e + 0x12) == 0 ? 1 : 0);
    else if (type == 3 || type == 5) Ret(c, 1);
    else Ret(c, 0);
}

// fn_338bc(buff): fno 26's shim -- the result stored.
void fn_338bc(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    const uint32_t r = Call(c, 0x3fac8, Ld32(b), Ld32(b + 4));
    St32(Ld32(G::kResultPtr), r);
}

// fn_35200(_, i): the s16 table g[kVolumeCurve][i].
void fn_35200(IopContext& c) { Ret(c, static_cast<uint32_t>(Lds16(G::kVolumeCurve + A1(c) * 2))); }

// fn_33534(buff): fno 9's shim -- fn_4e100(buff[0], buff[1]) (group volume); no result word.
void fn_33534(IopContext& c) {
    const uint32_t b = A0(c);
    Frame f(c, 0x18);
    Call(c, 0x4e100, Ld32(b), Ld32(b + 4));
}

}  // namespace ps2r::title::sly2::snd989
