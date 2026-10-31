// snd989_seq.cpp -- the sound-handle list the tick steps (fn_4218c) and its per-handle work: the type-5 (sequenced
// sound) step counter fn_377a4 / fn_3926c, the timer tree fn_4226c, the stop fn_41da0, and the sound-end handler
// walk fn_35a04. Ported from the recompiled 989snd_Library over the module's RAM (snd989_state.h).
//
// A sound handle `h` (a heap record the module allocates): h[0] the id word (bit 31 set = playing; bits 24..28 the
// type: 2, 4 = a bank sound, 5 = a sequence), h[4] the definition it plays, h[8] the timer tree root, h[0x16] u8
// flags (2 = paused, 4 = stopping, 0x40 = ...), h[0x18] / h[0x1c] / h[0x2c] the children / voices it still owns,
// h[0x24] the next handle in the list, h[0x30] the next sibling, h->s16[0x48] ticks to the next step, h->s16[0x4a]
// the step index (-1 = done), h->s8[0x50] / u16[0x52] / u16[0x54] a pending jump (countdown, offset).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// fn_3926c(h): run the current step of the sequence (the event handler table g[kSeqEventHandlers] by the entry's
// type byte: handler(h, def, entry)), advance; returns 1 when the sequence ended with nothing left to wait for (or
// the handler failed), 0 else.
void fn_3926c(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x20);
    uint32_t ended = 0;
    const uint32_t def = Ld32(h + 4);
    const int32_t idx = Lds16(h + 0x4a);
    const uint32_t entry = Ld32(def + 8) + static_cast<uint32_t>(idx) * 8;
    const uint32_t handler = Ld32(G::kSeqEventHandlers + Ld8(entry + 3) * 4);
    const int32_t r = static_cast<int32_t>(CallAddr(c, handler, h, def, entry, handler));
    if (r < 0) {
        Ret(c, 1);
        return;
    }
    if (Lds8(h + 0x50) != 0) {
        const uint32_t left = (Ld16(h + 0x52) - 1) & 0xffff;
        St16(h + 0x52, left);
        if (left == 0) {
            St8(h + 0x50, 0);
            St16(h + 0x4a, Ld16(h + 0x4a) + Ld16(h + 0x54));
        }
    }
    const int32_t next = static_cast<int16_t>(Ld16(h + 0x4a) + 1);
    St16(h + 0x4a, static_cast<uint32_t>(next));
    if (Lds8(def + 4) - 1 < next) {
        if (Ld32(h + 0x18) == 0 && Ld32(h + 0x1c) == 0 && Ld32(h + 0x2c) == 0) ended = 1;
        St16(h + 0x4a, 0xffff);
    } else {
        const uint32_t e2 = Ld32(def + 8) + static_cast<uint32_t>(next) * 8;
        St16(h + 0x48, Ld16(e2 + 4) + static_cast<uint32_t>(r));
    }
    Ret(c, ended);
}

// fn_377a4(h): one tick of a sequenced sound: count down to the next step, run steps (fn_3926c) until a wait
// remains; returns 1 when the handle is finished, 0 else.
void fn_377a4(IopContext& c) {
    const uint32_t h = A0(c);
    Frame f(c, 0x20);
    if (Lds16(h + 0x4a) == -1) {
        if (Ld32(h + 0x2c) != 0 || Ld32(h + 0x18) != 0) {
            Ret(c, 0);
            return;
        }
        Ret(c, Ld32(h + 0x1c) == 0 ? 1 : 0);
        return;
    }
    const uint32_t left = (Ld16(h + 0x48) - 1) & 0xffff;
    St16(h + 0x48, left);
    St8(h + 0x16, Ld8(h + 0x16) & 0xbf);
    if (static_cast<int16_t>(left) > 0) {
        Ret(c, 0);
        return;
    }
    for (;;) {
        const uint32_t r = Call(c, 0x3926c, h);
        if (r != 0) {
            Ret(c, r);
            return;
        }
        if (Lds16(h + 0x4a) == -1) {
            Ret(c, 0);
            return;
        }
        if (Lds16(h + 0x48) > 0) {
            Ret(c, 0);
            return;
        }
    }
}

// fn_4226c(node, h): the timer tree -- depth-first over node[0xc]; a live node (node[0] >= 0) of kind 1 counts
// node[4] down and fires node[0x10](node, h) at zero (the callback's result is returned; 1 stops the walk).
void fn_4226c(IopContext& c) {
    const uint32_t node = A0(c), h = A1(c);
    Frame f(c, 0x20);
    const uint32_t child = Ld32(node + 0xc);
    if (child != 0) {
        if (Call(c, 0x4226c, child, h) == 1) {
            Ret(c, 1);
            return;
        }
    }
    if (Lds32(node) < 0) {
        Ret(c, 0);
        return;
    }
    if (Ld32(node + 8) == 1) St32(node + 4, Ld32(node + 4) - 1);
    if (Ld32(node + 4) != 0) {
        Ret(c, 0);
        return;
    }
    CallAddr(c, Ld32(node + 0x10), node, h);
}

// fn_41da0(h, force, release, why): stop a sound handle under the global lock -- children first, the type's own
// stop (4: fn_453fc; 2: clear the definition's flag; 5: fn_37870), then its voices (fn_4d5f0 / fn_4d438 under the
// voice-table lock) and the handle itself (fn_4144c) when it is still marked playing.
void fn_41da0(IopContext& c) {
    const uint32_t h = A0(c);
    uint32_t force = A1(c);
    const uint32_t release = A2(c), why = A3(c);
    Frame f(c, 0x30);
    uint32_t stop_voices = 1, stopping = 0;
    Call(c, 0x42f58, 0x4f);
    if (Lds32(h) >= 0) {
        Call(c, 0x430dc);
        return;
    }
    const uint32_t flags = Ld8(h + 0x16);
    if (flags & 4) {
        stopping = 1;
        force = 1;
    }
    St8(h + 0x16, flags | 4);
    if (force != 0) {
        for (uint32_t child = Ld32(h + 0x2c); child != 0; child = Ld32(child + 0x30))
            Call(c, 0x41da0, child, force, release, why);
    }
    const uint32_t id = Ld32(h);
    const uint32_t type = (id >> 24) & 0x1f;
    if (type == 4) {
        Call(c, 0x453fc, id);
        stop_voices = 0;
    } else if (type < 5) {
        if (type == 2) {
            const uint32_t d = Ld32(Ld32(h + 4) + 0x18);
            St8(d + 6, Ld8(d + 6) & 0xfd);
        }
    } else if (type == 5) {
        if (stopping != 0) {
            Call(c, 0x37870, h, 1, why);
        } else if (Call(c, 0x37870, h, release, why) != 0) {
            stop_voices = 0;
        }
    }
    if (stop_voices != 0) {
        Call(c, 0x4c1ac, 1, 0x4a);
        if (release != 0 || (Ld8(h + 0x16) & 2)) Call(c, 0x4d5f0, h + 0x18, 1);
        else Call(c, 0x4d438, h + 0x18, 1);
        Call(c, 0x4c2f8);
        if (Lds32(h) < 0) Call(c, 0x4144c, h, 0);
    }
    Call(c, 0x430dc);
}

// fn_4218c: one tick step over the sound-handle list g[kSoundList]: type 4 -> fn_45904; type 5 (unpaused) ->
// fn_377a4; then the handle's timer tree (fn_4226c); a finished handle is stopped (fn_41da0(h, 1, 0, 0)).
void fn_4218c(IopContext& c) {
    Frame f(c, 0x20);
    for (uint32_t h = Ld32(G::kSoundList); h != 0; h = Ld32(h + 0x24)) {
        uint32_t done = 0;
        const uint32_t type = Ld8(h + 3) & 0x1f;
        if (type == 4) {
            Call(c, 0x45904, h);
        } else if (type == 5) {
            if (!(Ld8(h + 0x16) & 2)) done = Call(c, 0x377a4, h);
        }
        if (done == 0) {
            const uint32_t tree = Ld32(h + 8);
            if (tree != 0 && !(Ld8(h + 0x16) & 2)) done = Call(c, 0x4226c, tree, h);
        }
        if (done != 0) Call(c, 0x41da0, h, 1, 0, 0);
    }
}

// fn_35a04: every other tick step, walk the registered sound-end handlers g[kEndHandlerList] (node: [0x28] the
// record, [0x2c] next): fn_355a4 polls it; bit 0 -> fn_393b0(*record, 0x7fffffff, -2, 1), bit 1 -> fn_398cc(record).
void fn_35a04(IopContext& c) {
    Frame f(c, 0x20);
    if (Call(c, 0x43638) & 1) return;
    for (uint32_t n = Ld32(G::kEndHandlerList); n != 0; n = Ld32(n + 0x2c)) {
        const uint32_t r = Call(c, 0x355a4, n);
        if (r & 1) Call(c, 0x393b0, Ld32(Ld32(n + 0x28)), 0x7fffffff, 0xfffffffeu, 1);
        if (r & 2) Call(c, 0x398cc, Ld32(n + 0x28));
    }
}

}  // namespace ps2r::title::sly2::snd989
