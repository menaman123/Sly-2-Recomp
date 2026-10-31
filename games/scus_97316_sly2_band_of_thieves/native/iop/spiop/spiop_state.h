// spiop_state.h -- the IOP RAM layout of Sly 2's `spiop` module (Sucker Punch's streaming layer under
// 989snd; load base 0x5a000, memsz 0x5700), named from the recompiled module (the generated TU is the
// ground truth; every offset below is read off its loads/stores). THE STATE STAYS IN THE MODULE'S RAM:
// a native body and the recompiled body are interchangeable at any call because both read and write
// exactly these words -- nothing is mirrored on the host.
//
// Globals (.data/.bss, 0x5f4b8..0x5f700):
//   0x5f4b8  the stream thread's state jump table (5 words: states 0..4 -> code addresses)
//   0x5f4e8  the 'SUKR' external-command handler table (14 words: op -> handler address)
//   0x5f520  the sceCdRead mode block ({0x7f, ...}: the 4th argument of every disc read)
//   0x5f524  Context* (allocated 0x5d0 bytes on first use by fn_5a3bc)
//   0x5f600  the allocation-failure handler (0 = the default fn_5cc08)
//   0x5f690  the registered external-command record (0x38 bytes, filled by the module entry)
//
// CtxL -- the Context (0x5d0 bytes, at g[0x5f524]):
//   +0x00 shutting_down   +0x04 headset device (-1)        +0x08 sema: headset feeder lock
//   +0x0c thread A (headset feeder, fn_5b2a0)              +0x10 sema: thread B exit handshake
//   +0x14 thread B (headset monitor, fn_5b0cc)             +0x18 Slot* fed to the headset (0)
//   +0x1c 0x800-byte sector buffer (headset capture)       +0x20 mic level (op 0x0b's reply)
//   +0x24 sema: thread C exit handshake                    +0x28 thread C (tray poll, fn_5b438)
//   +0x2c sceCdTrayReq count (op 0x0c's reply)             +0x30 sema: the disc lock
//   +0x34 0x22800-byte disc read buffer                    +0x38 handle[3] (0 = slot free)
//   +0x44 Slot[3] (0x1d8 each)                             +0x5cc next handle
//
// Slot (0x1d8 bytes): a stream = up to 5 interleaved voices read from one disc file.
//   +0x00 lba            +0x04 state (-1 idle, 0 header, 1 voices, 2 arming, 3 playing, 4 stopping)
//   +0x08 sema (the slot lock)   +0x0c volume   +0x10 pan   +0x14 slot index   +0x18 loop flag
//   +0x1c headset flag   +0x20 open flags   +0x24 thread (fn_5be48)   +0x28 voice count
//   +0x2c Voice[5] (0x50 each)   +0x1bc headset stream (-1)
//   +0x1c0 stream position (bytes consumed of the file's voice data)   +0x1c4 data start (file offset)
//   +0x1c8 data end   +0x1cc loop start   +0x1d0 ring buffers (voice count x 0xc000)   +0x1d4 write cursor
//
// Voice (0x50 bytes): +0x00 989snd stream handle (0 = none)  +0x04 id  +0x08 volume  +0x0c pan
//   +0x10.. the 989snd stream descriptor handed to snd989:49 {ring bytes 0xc000, ring base, read cursor
//   (989snd's tick advances it), 0, 0, 0, -1}.
#pragma once

#include <cstdint>

#include "engine/iop/abi/host_app/iop_memory.h"

namespace ps2r::title::sly2::spiop {

constexpr uint32_t kBase = 0x5a000;
constexpr uint32_t kExtent = 0x5f700;

struct G {
    static constexpr uint32_t kStateJumpTable = 0x5f4b8;
    static constexpr uint32_t kHandlers = 0x5f4e8;
    static constexpr uint32_t kHandlerCount = 14;
    static constexpr uint32_t kReadMode = 0x5f520;
    static constexpr uint32_t kContextPtr = 0x5f524;
    static constexpr uint32_t kAllocFailHandler = 0x5f600;
    static constexpr uint32_t kRecordPtr = 0x5f690;
    static constexpr uint32_t kLongjmpBuf = 0x5f6b0;
};

struct CtxL {
    static constexpr uint32_t kSize = 0x5d0;
    static constexpr uint32_t kShutdown = 0x00, kHeadsetDev = 0x04, kHeadsetSema = 0x08, kThreadA = 0x0c,
                              kSemaB = 0x10, kThreadB = 0x14, kHeadsetSlot = 0x18, kSectorBuf = 0x1c,
                              kMicLevel = 0x20, kSemaC = 0x24, kThreadC = 0x28, kTrayCount = 0x2c,
                              kDiscSema = 0x30, kReadBuf = 0x34, kHandles = 0x38, kSlots = 0x44,
                              kNextHandle = 0x5cc;
    static constexpr uint32_t kSlotCount = 3;
    static constexpr uint32_t kSlotStride = 0x1d8;
    static constexpr uint32_t kReadBufBytes = 0x22800;
    static constexpr uint32_t kSectorBufBytes = 0x800;
    static constexpr uint32_t Slot(uint32_t ctx, uint32_t i) { return ctx + kSlots + i * kSlotStride; }
    static constexpr uint32_t Handle(uint32_t ctx, uint32_t i) { return ctx + kHandles + i * 4; }
};

struct Slot {
    static constexpr uint32_t kLba = 0x00, kState = 0x04, kSema = 0x08, kVolume = 0x0c, kPan = 0x10, kIndex = 0x14,
                              kLoop = 0x18, kHeadset = 0x1c, kFlags = 0x20, kThread = 0x24, kVoiceCount = 0x28,
                              kVoices = 0x2c, kHeadsetStream = 0x1bc, kStreamPos = 0x1c0, kDataStart = 0x1c4,
                              kDataEnd = 0x1c8, kLoopStart = 0x1cc, kRing = 0x1d0, kWriteCursor = 0x1d4;
    static constexpr uint32_t kVoiceStride = 0x50;
    static constexpr uint32_t kMaxVoices = 5;
    static constexpr uint32_t kRingBytes = 0xc000;      // per voice
    static constexpr uint32_t kInterleave = 0x1000;     // the file interleaves the voices in 4 KiB chunks
    static constexpr uint32_t kReadCap = 0x6000;        // one refill reads at most this much per voice
    static constexpr uint32_t Voice(uint32_t slot, uint32_t i) { return slot + kVoices + i * kVoiceStride; }
    // The ring buffer bytes a slot owns, by slot index (fn_5b568): 4, 5 and 1 voices' worth.
    static constexpr uint32_t RingBytesForIndex(uint32_t i) { return i == 0 ? 0x30000 : i == 1 ? 0x3c000 : 0xc000; }
};

struct Voice {
    static constexpr uint32_t kHandle = 0x00, kId = 0x04, kVolume = 0x08, kPan = 0x0c, kDesc = 0x10,
                              kDescRingBytes = 0x10, kDescRing = 0x14, kDescReadCursor = 0x18, kDesc1c = 0x1c,
                              kDesc20 = 0x20, kDesc24 = 0x24, kDesc28 = 0x28;
};

// The 'SUKR' external-command record the entry registers with 989snd (snd989:93).
constexpr uint32_t kRecordTag = 0x524b5553;   // 'SUKR' as the little-endian word the module stores
constexpr uint32_t kRecordBytes = 0x38;

// The two stream file headers (first word of the first sector, as stored little-endian).
constexpr uint32_t kMagicVagp = 0x70474156;   // "VAGp": one voice, a 0x30-byte header
constexpr uint32_t kMagicVpk = 0x56504b20;    // " KPV": the multi-voice container

// Memory access in the module's own terms (the same accessors the recompiled TU uses).
inline uint32_t R32(uint32_t a) { return iop_memory::read<uint32_t>(a); }
inline int32_t R32s(uint32_t a) { return static_cast<int32_t>(iop_memory::read<uint32_t>(a)); }
inline uint16_t R16(uint32_t a) { return iop_memory::read<uint16_t>(a); }
inline int16_t R16s(uint32_t a) { return static_cast<int16_t>(iop_memory::read<uint16_t>(a)); }
inline int8_t R8s(uint32_t a) { return static_cast<int8_t>(iop_memory::read<uint8_t>(a)); }
inline void W32(uint32_t a, uint32_t v) { iop_memory::write<uint32_t>(a, v); }
inline void W16(uint32_t a, uint16_t v) { iop_memory::write<uint16_t>(a, v); }
inline void W8(uint32_t a, uint8_t v) { iop_memory::write<uint8_t>(a, v); }

inline uint32_t Bswap32(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xff00u) | ((v & 0xff00u) << 8) | (v << 24);
}

}  // namespace ps2r::title::sly2::spiop
