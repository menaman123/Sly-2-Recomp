// snd989_volume.cpp -- the master-volume ramps, the group fades and the voice-volume refresh the tick steps
// (fn_40f64 / fn_369c8 / fn_40eb8 / fn_3e1cc / fn_4efb4 / fn_4edbc), and the voice-table lock pair (fn_4c1ac /
// fn_4c2f8) they take. Ported from the recompiled 989snd_Library over the module's RAM (snd989_state.h).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// fn_4c1ac(wait, tag): the voice-table lock. wait = 0 polls (0 = busy), 1 waits. Returns 1 when taken (also when
// the caller already owns it). While held, g[kTickNoLock] = 1 (the tick skips the global lock) and the tag shows.
void fn_4c1ac(IopContext& c) {
    const uint32_t wait = A0(c), tag = A1(c);
    Frame f(c, 0x30);
    const uint32_t tid = imp::GetThreadId(c);
    if (tid == Ld32(G::kVLockOwner)) {
        Ret(c, 1);
        return;
    }
    if (wait == 0) {
        const uint32_t r = imp::PollSema(c, Ld32(G::kVLockSema));
        if (r == static_cast<uint32_t>(-0x1a3)) {
            Ret(c, 0);
            return;
        }
    } else {
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x18));
        if (suspended != 0) {
            St32(f.At(0x10), 0);
            Call(c, iop_snd989::iop_func_00035140, 0x7c, tag, 0, 0);
        } else {
            imp::CpuResumeIntr(c, Ld32(f.At(0x18)));
        }
        const uint32_t r = imp::WaitSema(c, Ld32(G::kVLockSema));
        if (r != 0) {
            St32(f.At(0x10), 0);
            Call(c, iop_snd989::iop_func_00035140, 0x50, r, 0, 0);
            Ret(c, 0);
            return;
        }
    }
    St32(G::kVLockOwner, imp::GetThreadId(c));
    St32(G::kLockDiag, tag);
    St32(G::kTickNoLock, 1);
    Ret(c, 1);
}

// fn_4c2f8: release the voice-table lock (owner -1, the tick takes its lock again, tag 0, SignalSema).
void fn_4c2f8(IopContext& c) {
    Frame f(c, 0x18);
    St32(G::kVLockOwner, 0xffffffffu);
    const uint32_t sema = Ld32(G::kVLockSema);
    St32(G::kTickNoLock, 0);
    St32(G::kLockDiag, 0);
    imp::SignalSema(c, sema);
}

// fn_3e1cc(slot): the master-volume ramp of `slot` is done -- clear its busy word, wake the waiter when one is armed.
// (slot = -1, from a core whose ramp was never armed, clears the word below the table: g[kCdWatchTicks].)
void fn_3e1cc(IopContext& c) {
    const uint32_t slot = A0(c);
    Frame f(c, 0x18);
    St32(G::kRampDone + slot * 4, 0);
    if (Ld32(G::kRampWaiterArmed) != 0) imp::SignalSema(c, Ld32(G::kRampWaiterSema));
}

// fn_40eb8(core, left, right): set a core's master volume (MVOLL / MVOLR) unless a ramp owns it (g[kMasterRampId]).
void fn_40eb8(IopContext& c) {
    const uint32_t core = A0(c), left = A1(c), right = A2(c);
    Frame f(c, 0x28);
    const uint32_t l = G::kMasterVolL + core * 2, r = G::kMasterVolR + core * 2;
    St16(l, left);
    const uint32_t ramp = Ld32(G::kMasterRampId + core * 4);
    St16(r, right);
    if (ramp != 0) return;
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    imp::SdSetParam(c, (core | 0xb80) & 0xffff, Ld16(l));
    imp::SdSetParam(c, (core | 0xc80) & 0xffff, Ld16(r));
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
}

// fn_369c8: step the master-volume ramps (per core: a left and a right step towards a target) -> fn_40eb8.
void fn_369c8(IopContext& c) {
    Frame f(c, 0x38);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t s = i * 0x48;
        const uint32_t curL = G::kMasterVolL + i * 2, curR = G::kMasterVolR + i * 2;
        const int32_t stepL = Lds32(G::kRampStepL + s);
        if (stepL != 0) {
            int32_t v = Lds16(curL) + stepL;
            const int32_t target = Lds32(G::kRampTargetL + s);
            if (stepL > 0) {
                if (target < v) v = target;
            } else if (v < target) {
                v = target;
            }
            Call(c, iop_snd989::iop_func_00040eb8, i, static_cast<uint32_t>(v), static_cast<uint32_t>(Lds16(curR)));
            if (v == Lds32(G::kRampTargetL + s)) St32(G::kRampStepL + s, 0);
        }
        const int32_t stepR = Lds32(G::kRampStepR + s);
        if (stepR != 0) {
            int32_t v = Lds16(curR) + stepR;
            const int32_t target = Lds32(G::kRampTargetR + s);
            if (stepR > 0) {
                if (target < v) v = target;
            } else if (v < target) {
                v = target;
            }
            Call(c, iop_snd989::iop_func_00040eb8, i, static_cast<uint32_t>(Lds16(curL)), static_cast<uint32_t>(v));
            if (v == Lds32(G::kRampTargetR + s)) St32(G::kRampStepR + s, 0);
        }
    }
}

// fn_40f64: the per-core "master volume ramp done" countdowns (MVOLL/MVOLR applied, SdSetCoreAttr(core+2, 1), the
// ramp's slot released), then the ramp steps (fn_369c8). Called once per tick step.
void fn_40f64(IopContext& c) {
    Frame f(c, 0x20);
    for (uint32_t core = 0; core < 2; ++core) {
        const uint32_t cd = G::kMasterRampCountdown + core * 4;
        const int32_t n = Lds32(cd);
        if (n == 0) continue;
        St32(cd, static_cast<uint32_t>(n - 1));
        if (n - 1 != 0) continue;
        const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
        imp::SdSetParam(c, 0xb80 + core, Ld16(G::kMasterVolL + core * 2));
        imp::SdSetParam(c, 0xc80 + core, Ld16(G::kMasterVolR + core * 2));
        imp::SdSetCoreAttr(c, 2 + core, 1);
        if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
        const uint32_t id = Ld32(G::kMasterRampId + core * 4);
        Call(c, iop_snd989::iop_func_0003e1cc, id - 1);
        St32(G::kMasterRampId + core * 4, 0);
    }
    Call(c, iop_snd989::iop_func_000369c8);
}

// 1 << (n & 31): MIPS sllv takes the low five bits of the shift amount.
static inline uint32_t Bit(int32_t n) { return 1u << (static_cast<uint32_t>(n) & 31u); }

// fn_4efb4: the group fades (every 4th tick step). Under the voice-table lock, the groups with a playing voice
// (not paused) fade down towards their target, the others back up to 0x10000; the 15 group levels g[kGroupFade]
// take the minimum over the fade entries that cover them; the voices of the groups whose level changed are
// re-volumed (fn_4edbc).
void fn_4efb4(IopContext& c) {
    Frame f(c, 0x60);
    uint32_t playing = 0;   // bit per voice group id (rec[0x1b]) with a playing, unpaused voice
    Call(c, iop_snd989::iop_func_0004c1ac, 1, 0x8b);
    for (int v = 0; v < 48; ++v) {
        const uint32_t rec = G::Voice(v);
        const uint32_t st = Ld32(rec + G::kVoiceState);
        if (st != 1 && st != 4) continue;
        const uint32_t owner = Ld32(rec + G::kVoiceOwner);
        if (owner == 0) continue;
        if (Ld8(owner + 0x16) & 2) continue;
        playing |= Bit(Lds8(rec + G::kVoiceGroup));
    }
    Call(c, iop_snd989::iop_func_0004c2f8);
    const uint32_t local = f.At(0x10);   // 15 words: the new group levels
    for (uint32_t i = 0; i < 15; ++i) St32(local + i * 4, 0x10000);
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t e = G::kFadeTable + k * 0x18;
        const int32_t id = Lds32(e + G::kFadeId);
        if (id == -1) continue;
        uint32_t cover;
        if (playing & Bit(id)) {
            int32_t cur = Lds32(e + G::kFadeCur);
            const int32_t target = Lds32(e + G::kFadeTarget);
            if (cur != target) {
                cur -= Lds32(e + G::kFadeDown);
                St32(e + G::kFadeCur, static_cast<uint32_t>(cur));
                if (cur < Lds32(e + G::kFadeTarget)) St32(e + G::kFadeCur, Ld32(e + G::kFadeTarget));
            }
            cover = Ld32(e + G::kFadeGroups);
        } else {
            uint32_t cur = Ld32(e + G::kFadeCur);
            if (cur == 0x10000) continue;
            cur += Ld32(e + G::kFadeUp);
            St32(e + G::kFadeCur, cur);
            if (0x10000 < cur) St32(e + G::kFadeCur, 0x10000);
            cover = Ld32(e + G::kFadeGroups);
        }
        if (cover == 0) continue;
        for (uint32_t g = 0; g < 15; ++g) {
            if (!(cover & (1u << g))) continue;
            const int32_t cur = Lds32(e + G::kFadeCur);
            if (cur < Lds32(local + g * 4)) St32(local + g * 4, static_cast<uint32_t>(cur));
        }
    }
    uint32_t changed = 0;
    for (uint32_t g = 0; g < 15; ++g) {
        const uint32_t nv = Ld32(local + g * 4);
        if (Ld32(G::kGroupFade + g * 4) != nv) {
            St32(G::kGroupFade + g * 4, nv);
            changed |= 1u << g;
        }
    }
    if (changed != 0) Call(c, iop_snd989::iop_func_0004edbc, changed);
}

// fn_4edbc(groups): re-apply VOLL / VOLR of every playing, unpaused voice whose group is in `groups` (the group
// scaling fn_4ed08 over the voice's own left/right), under the voice-table lock and CpuSuspendIntr.
void fn_4edbc(IopContext& c) {
    const uint32_t groups = A0(c);
    Frame f(c, 0x40);
    Call(c, iop_snd989::iop_func_0004c1ac, 1, 0x8a);
    const uint32_t suspended = imp::CpuSuspendIntr(c, f.At(0x10));
    for (int v = 0; v < 48; ++v) {
        const uint32_t rec = G::Voice(v);
        const uint32_t owner = Ld32(rec + G::kVoiceOwner);
        if (owner != 0 && (Ld8(owner + 0x16) & 2)) continue;
        const uint32_t st = Ld32(rec + G::kVoiceState);
        if (st != 1 && st != 4) continue;
        const int32_t grp = Lds8(rec + G::kVoiceGroup);
        if (!(groups & Bit(grp))) continue;
        const uint32_t entry = static_cast<uint32_t>(VoiceCore(v)) | (static_cast<uint32_t>(VoiceBit(v)) << 1);
        uint32_t vol = Call(c, iop_snd989::iop_func_0004ed08, static_cast<uint32_t>(Lds16(rec + G::kVoiceVolL)), static_cast<uint32_t>(grp));
        imp::SdSetParam(c, entry & 0xffff, (vol & 0xffff) >> 1);
        vol = Call(c, iop_snd989::iop_func_0004ed08, static_cast<uint32_t>(Lds16(rec + G::kVoiceVolR)), static_cast<uint32_t>(grp));
        imp::SdSetParam(c, (entry | 0x100) & 0xffff, (vol & 0xffff) >> 1);
    }
    if (suspended == 0) imp::CpuResumeIntr(c, Ld32(f.At(0x10)));
    Call(c, iop_snd989::iop_func_0004c2f8);
}

}  // namespace ps2r::title::sly2::snd989
