// snd989_misc.cpp -- handle validation (fn_417f8 / fn_3fa7c), the hard-timer callback (fn_3a90c), the CD-read
// callback (fn_3a9dc), the group volume scaling (fn_4ed08), the next-owned-voice scan (fn_42064). Ported from the
// recompiled 989snd_Library over the module's RAM (snd989_state.h).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// fn_417f8(handle): the record a sound handle names, or 0 (with the error 0x64 / 0x65 reported for -1 / a bad index).
// handle = type << 24 | index << 16 | ...: type 5 -> the 0x124-byte records at g[kSeqHandles] (index < 0x40);
// type 4 -> the 0x78-byte records at g[kStreamHandles] (index < g[kStreamHandleCount]); the record's first word must
// equal the handle.
void fn_417f8(IopContext& c) {
    const uint32_t h = A0(c);
    const int32_t index = static_cast<int32_t>((h >> 16) & 0xff);
    const uint32_t type = (h >> 24) & 0x1f;
    Frame f(c, 0x20);
    if (h == 0) {
        Ret(c, 0);
        return;
    }
    auto error = [&](uint32_t code) {
        St32(f.At(0x10), 0);
        Call(c, iop_snd989::iop_func_00035140, code, 0, 0, 0);
        Ret(c, 0);
    };
    if (h == 0xffffffffu) {
        error(0x64);
        return;
    }
    if (index < 0) {
        error(0x65);
        return;
    }
    if (type == 5) {
        if (index >= 0x40) {
            error(0x65);
            return;
        }
        const uint32_t rec = G::kSeqHandles + static_cast<uint32_t>(index) * 0x124;
        Ret(c, Ld32(rec) == h ? rec : 0);
        return;
    }
    if (type != 4) {
        Ret(c, 0);
        return;
    }
    if (Lds32(G::kStreamHandleCount) - 1 < index) {
        error(0x65);
        return;
    }
    const uint32_t rec = Ld32(G::kStreamHandles) + static_cast<uint32_t>(index) * 0x78;
    Ret(c, Ld32(rec) == h ? rec : 0);
}

// fn_3fa7c(handle): the handle itself when fn_417f8 validates it, else 0 (0 and -1 pass through as 0).
void fn_3fa7c(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x18);
    if (h == 0 || h == 0xffffffffu) {
        Ret(c, 0);
        return;
    }
    Ret(c, Call(c, iop_snd989::iop_func_000417f8, h) != 0 ? h : 0);
}

// fn_3a90c(timer_arg): the hard-timer beat (240 Hz): unless the tick is being held off (g[kTickHold] > 0 -> set it to
// 2, return 0), iWakeupThread(*(*arg)) -- the tick thread -- and return arg[4].
void fn_3a90c(IopContext& c) {
    const uint32_t arg = A0(c);
    Frame f(c, 0x18);
    if (Lds32(G::kTickHold) > 0) {
        St32(G::kTickHold, 2);
        Ret(c, 0);
        return;
    }
    Import(c, "thbase", 26, Ld32(Ld32(arg)));   // iWakeupThread
    Ret(c, Ld32(arg + 4));
}

// fn_3a9dc(status): the stream reader's CD-read completion -- the watch disarmed; status 1 = ok, else the error latched
// (g[kCdError] = 1, g[kCdErrorStatus] = status); iSignalSema(g[kCdReadSema]).
void fn_3a9dc(IopContext& c) {
    const uint32_t status = A0(c);
    Frame f(c, 0x18);
    St32(G::kCdWatch, 0);
    if (status != 1) {
        St32(G::kCdError, 1);
        St32(G::kCdErrorStatus, status);
    }
    imp::iSignalSema(c, Ld32(G::kCdReadSema));
}

// fn_4ed08(vol, group): the voice volume scaled by the group's fade level (squared, 16.16) and the group volume
// (0..0x400, squared, 10-bit): vol (s16, capped 0x7ffe) * ((gv^2 >> 10) * (fade^2 >> 16) >> 16) with the module's
// rounding; group >= 15 = unscaled.
void fn_4ed08(IopContext& c) {
    int32_t vol = static_cast<int16_t>(A0(c) & 0xffff);
    const int32_t group = static_cast<int32_t>(A1(c));
    if (!(group < 15)) {
        Ret(c, static_cast<uint32_t>(vol));
        return;
    }
    if (!(vol < 0x7fff)) vol = 0x7ffe;
    uint32_t fade = Ld32(G::kGroupFade + static_cast<uint32_t>(group) * 4);
    if (!(0xfffeu < fade - 1u)) fade = (fade * fade) >> 16;
    const uint32_t gv = Ld32(G::kParamTable + static_cast<uint32_t>(group) * 4);
    int32_t sq = static_cast<int32_t>(gv * gv);
    if (sq < 0) sq += 0x3ff;
    sq >>= 10;
    const uint32_t scale = (static_cast<uint32_t>(sq) * fade) >> 16;
    int32_t r = static_cast<int32_t>(static_cast<uint32_t>(vol) * scale);
    if (r < 0) r += 0x3ff;
    Ret(c, static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(r) << 6) >> 16));
}

// fn_42064(h, start): the first voice >= start the handle owns (its two mask words at h + 0x18), or -1.
void fn_42064(IopContext& c) {
    const uint32_t h = A0(c);
    for (int32_t v = static_cast<int32_t>(A1(c)); v < 0x30; ++v) {
        if (Ld32(h + 0x18 + static_cast<uint32_t>(VoiceCore(v)) * 4) & (1u << (static_cast<uint32_t>(VoiceBit(v)) & 31u))) {
            Ret(c, static_cast<uint32_t>(v));
            return;
        }
    }
    Ret(c, 0xffffffffu);
}

}  // namespace ps2r::title::sly2::snd989
