// snd989_state.h -- named views of the 989snd_Library module's globals the native bodies touch (STAGE NATIVE-3).
//
// 989snd keeps its state in its own .data/.bss (IOP RAM 0x50000..0x59e88: every body addresses it from
// `lui $x, 5` / `lui $x, 6`) and in heap blocks it allocates. A native body reads and writes THE SAME WORDS
// through these names, so the module's RAM stays the one state and a native body and the recompiled body
// are interchangeable at any call (native/snd/snd989_image.h holds the RPC-face subset; this file is the
// port's). Every number is an offset read off the recompiled module (the generated C++ / the disassembly);
// no game bytes live here. Names follow OpenGOAL's 989snd where the intent is clear, else the role.
#pragma once

#include <cstdint>

namespace ps2r::title::sly2::snd989 {

struct G {
    static constexpr uint32_t kData = 0x50000;

    // --- the global lock (fn_42f58 lock / fn_430dc unlock / fn_431bc trylock) -------------------------------
    static constexpr uint32_t kLockDepth = kData + 0x2214;      // nesting depth (0 = free)
    static constexpr uint32_t kLockTags = kData + 0x2218;       // 32 words: the tag each level took the lock with
    static constexpr uint32_t kLockTag = kData + 0x2298;        // the current level's tag (0xffff = the tick's trylock)
    static constexpr uint32_t kLockOwner = kData + 0x229c;      // owner thread id, -1 = free
    static constexpr uint32_t kLockSema = kData + 0x2528;       // the semaphore id
    static constexpr uint32_t kLockDiag = kData + 0x23b0;       // a word the lock diagnostics print
    // The diagnostic format strings (module .data) the lock / the tick print.
    static constexpr uint32_t kFmtLockDeadlock = kData + 0x36c, kFmtLockOwner = kData + 0x3b4, kFmtLockMe = kData + 0x3d4,
                              kFmtLockHdr = kData + 0x3f4, kFmtLockTag = kData + 0x40c, kFmtLockStack = kData + 0x424,
                              kFmtLockDiag = kData + 0x440, kFmtTickStuck = kData + 0x460, kFmtTickDepth = kData + 0x490,
                              kFmtTickCd = kData + 0x4ac, kFmtTickCdA = kData + 0x4e4, kFmtTickCdB = kData + 0x4ec;

    // --- the error reporter fn_35140(code, a, b, c, d) -----------------------------------------------------
    static constexpr uint32_t kErrorHook = kData + 0x7f4;       // a function pointer, or 0 -> printf
    static constexpr uint32_t kFmtError = kData - 0x388;        // 0x4fc78 (in .text's tail: the module's rodata)
    static constexpr uint32_t kQuiet = kData + 0x1914;          // non-zero: no diagnostics

    // --- the 240 Hz tick fn_4325c ----------------------------------------------------------------------------
    static constexpr uint32_t kTickCount = kData + 0x2210;      // steps taken (fn_43638 reads it)
    static constexpr uint32_t kTickBusy = kData + 0x22a0;       // re-entrancy guard
    static constexpr uint32_t kTickPending = kData + 0x23a4;    // beats to process (also the stuck-lock counter)
    static constexpr uint32_t kTickNoLock = kData + 0x23ac;     // non-zero: the tick does not take the lock
    static constexpr uint32_t kActiveVoices = kData + 0x23f8;   // list head: node {next, voice index}
    static constexpr uint32_t kCdWatch = kData + 0x1938;        // non-zero: the stream reader's CD watch is armed
    static constexpr uint32_t kCdWatchTicks = kData + 0x193c;   // ticks since; 0x961 -> sceCdReadIOPm + reset

    // --- the sound-handle list the tick steps (fn_4218c) and the sequencer (fn_3926c) -------------------------
    static constexpr uint32_t kSoundList = kData + 0x21e8;      // head of the playing handles (h[0x24] = next)
    static constexpr uint32_t kSeqEventHandlers = kData + 0x1838;   // sequence event type -> handler(h, def)
    static constexpr uint32_t kEndHandlerList = kData + 0x17f8;     // the sound-end handler records (fn_35a04)

    // --- the voice-table lock (fn_4c1ac take / fn_4c2f8 release) ---------------------------------------------
    static constexpr uint32_t kVLockOwner = kData + 0x239c;     // owner thread id, -1 = free
    static constexpr uint32_t kVLockSema = kData + 0x23a0;      // the semaphore id (while held: kTickNoLock = 1, kLockDiag = tag)

    // --- master volume (per core) and its ramps (fn_40eb8 / fn_369c8 / fn_40f64 / fn_3e1cc) -----------------
    static constexpr uint32_t kMasterVolL = kData + 0x21a4;     // s16 x2 (core)
    static constexpr uint32_t kMasterVolR = kData + 0x21a8;     // s16 x2
    static constexpr uint32_t kMasterRampId = kData + 0x21ac;   // u32 x2: the owning ramp slot + 1, 0 = none
    static constexpr uint32_t kMasterRampCountdown = kData + 0x21b4;   // u32 x2: steps until the ramp's end applies
    static constexpr uint32_t kRampDone = kData + 0x1940;       // per ramp slot: busy word (fn_3e1cc clears)
    static constexpr uint32_t kRampWaiterArmed = kData + 0x2344;
    static constexpr uint32_t kRampWaiterSema = kData + 0x2364;
    static constexpr uint32_t kRampBase = kData + 0x2688;       // per core (stride 0x48): the ramp record
    static constexpr uint32_t kRampStepL = kRampBase + 0x18, kRampTargetL = kRampBase + 0x20;
    static constexpr uint32_t kRampStepR = kRampBase + 0x3c, kRampTargetR = kRampBase + 0x44;

    // --- the group fades (fn_4efb4 / fn_4edbc) ------------------------------------------------------------------
    static constexpr uint32_t kGroupFade = kData + 0x2498;      // 15 words: the group level 0..0x10000 (fn_4ed08 scales by it)
    static constexpr uint32_t kFadeTable = 0x59e28;             // 4 entries of 0x18 (lui 6, -0x61d8)
    static constexpr uint32_t kFadeId = 0, kFadeGroups = 4, kFadeTarget = 8, kFadeDown = 0xc, kFadeUp = 0x10, kFadeCur = 0x14;

    // --- the per-core voice masks (bit = voice % 24; word = core) -------------------------------------------
    static constexpr uint32_t kVmix = kData + 0x2548;           // VMIXEL/VMIXER masks written at key-on
    static constexpr uint32_t kKonPending = kData + 0x2550;     // key-ons the next step applies
    static constexpr uint32_t kKoffPending = kData + 0x2558;    // key-offs the next step applies
    static constexpr uint32_t kVoicesOn = kData + 0x2560;       // keyed on
    static constexpr uint32_t kVoicesEnding = kData + 0x2568;   // keyed off, releasing

    // --- the voice table: 48 records of 56 bytes at 0x593a8 (lui 6, -0x6c58) --------------------------------
    static constexpr uint32_t kVoiceTable = 0x593a8;
    static constexpr uint32_t kVoiceBytes = 56;
    static constexpr uint32_t kVoiceState = 0x8;                // 1 = owned (a sound plays on it)
    static constexpr uint32_t kVoiceOwner = 0xc;                // the owning sound handle (u8[0x16] & 2 = paused), or 0
    static constexpr uint32_t kVoiceCbArg = kVoiceOwner;        // ... passed to the owner's callback
    static constexpr uint32_t kVoiceCb = 0x10;                  // the owner's callback (voice, owner, why), or 0
    static constexpr uint32_t kVoicePrio = 0x1a;                // u8 priority (0x7f = the movie / stream voices)
    static constexpr uint32_t kVoiceGroup = 0x1b;               // s8 volume group (kGroupFade index)
    static constexpr uint32_t kVoiceLsaxBase = 0x1c;
    static constexpr uint32_t kVoiceVolL = 0x20, kVoiceVolR = 0x22;   // s16: the voice's own volumes before group scaling
    static uint32_t Voice(int v) { return kVoiceTable + static_cast<uint32_t>(v) * kVoiceBytes; }
    // The per-voice silence countdown bytes at 0x58e38 (lui 6, -0x71c8): ENVX == 0 for 4 steps -> key off.
    static constexpr uint32_t kVoiceSilentTicks = 0x58e38;
};

}  // namespace ps2r::title::sly2::snd989
