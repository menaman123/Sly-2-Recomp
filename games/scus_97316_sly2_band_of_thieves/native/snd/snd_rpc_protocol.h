// snd_rpc_protocol.h -- the 989snd EE <-> IOP RPC protocol as Sly 2 (SCUS_973.16) uses it.
//
// Facts, not game bytes (derived from the title's 989snd_Library image and the lane's RPC census,
// ~/ra-orch/notes/nat-iop-spu.md "STEP 1 CENSUS"; evidence/nat-iop-spu/snd989_rpc_handlers.txt):
//
//   * Two SIF RPC servers, both registered by 989snd itself: 0x123456 (the command server; every reply
//     is 12 bytes) and 0x123457 (the bank-load server; reply 4 bytes).
//   * 0x123456's handler is a 107-entry dispatcher: `fno` indexes a table of thin shims, each calling one
//     989snd API function with the request buffer as its argument. The reply is a fixed IOP buffer
//     {kReplyMark, result, kReplyMark}; the shim writes `result` (one word; a shim with nothing to say
//     leaves 0), the dispatcher terminates with kReplyMark.
//   * fno kBatch (77) is a BATCH: the request is {count, then `count` records {u16 fno, u16 len, body[len
//     rounded up to 4]}}; each record is dispatched through the same table and writes its own result word
//     (reply = {kReplyMark, result0, result1, ..., kReplyMark}). The EE's 989snd client queues the
//     "asynchronous" API calls (sound play / stop / volume, the SUKR stream commands, ...) as records and
//     flushes one batch per frame; the "synchronous" calls (polls, opens, the movie-sound chunk push) go
//     as their own fno.
//   * The SUKR commands (Sucker Punch's spiop streaming module, hooked into 989snd's external-command
//     slot) are records of fno kExternalCommand (76): {'SUKR', op, args...}.
//
// Header-only, portable, no engine dependency: the decoders are pure functions over the request bytes
// (little-endian words), used by the native server and its tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::title::sly2::snd989 {

constexpr uint32_t kSidCommand = 0x123456;   // the 107-entry command server
constexpr uint32_t kSidBank = 0x123457;      // the bank-load server
constexpr uint32_t kReplyMark = 0xFFFFFFFFu;
constexpr uint32_t kReplyBytes = 12;         // {mark, result, mark}: every kSidCommand reply the EE copies
constexpr uint32_t kBankReplyBytes = 4;
constexpr uint32_t kFnoCount = 107;

// The fnos the lane's census saw, named by their observed behaviour (the 989snd API has no public
// ordinal table; a name here is a claim about what the shim DOES, evidenced in the census file).
enum Fno : uint32_t {
    kStartSoundSystem = 0,     // (ee_sync_word_addr, mode) -> SdInit + master/effect/external volumes
    kBatchVolume = 9,          // batched: (group|flags, level) volume / fade records
    kQuery10 = 10,             // (n) -> 0x400
    kSilenceAllVoices = 24,    // () -> VOLL/VOLR = 0 on all 48 voices
    kBankSoundQuery = 26,      // (bank_handle, index) -> 0/1 per sound (no libsd op)
    kQuery54 = 54,             // (1) -> 1
    kMovieSoundOpen = 59,      // (0x400, 0x1000, 0x400, 0, 2, 3) -> IOP buffer; VOLL/VOLR of two voices, NON, trans handlers
    kMovieSoundStop = 60,      // () -> position
    kMovieSoundClose = 61,     // () -> position
    kMovieSoundStart = 62,     // (iop_buf, 0x1000, 0, sample_rate, 2) -> iop_buf; pitch/ADSR/SSA/VMIX of the two voices
    kExternalCommand = 76,     // {'SUKR', op, args...} -> spiop's hook (batched as a record, 3 x direct at boot)
    kBatch = 77,               // {count, records...}
    kMovieSoundPush = 90,      // (0x400, spu_offset) -> VoiceTrans(IOP buf -> movie SPU base + offset) + status; reply = iop_buf
    kMovieSoundReadPos = 91,   // () -> NAX(movie voice) - movie SPU base   (ONE libsd GetAddr; the FMV's A/V clock poll)
    kBatchPlaySound = 92,      // batched (len 0x30): play a bank sound -> handle 0x85xx0002
    kBatch104 = 104,           // batched (len 0x24)
};
// kSidBank: fno 3 = the blocking bank load from EE RAM (reply = bank handle).
constexpr uint32_t kBankLoadFromEe = 3;

constexpr uint32_t kSukrTag = 0x524B5553u;   // 'SUKR' as the request's little-endian word
enum SukrOp : uint32_t { kSukrInit = 0x00, kSukrOpen = 0x01, kSukrClose = 0x02, kSukrSet = 0x06, kSukrStart = 0x07,
                         kSukrStatus = 0x08, kSukrInit2 = 0x0a, kSukrPoll = 0x0c };

// libsd entry codes the movie-sound path programs (SD_VA_* | SD_VOICE(core, v): the 0x40 is the
// address-register selector, the low bits core | voice<<1).
constexpr uint32_t kVaSsa = 0x2040, kVaLsax = 0x2140, kVaNax = 0x2240;
constexpr uint32_t kVaSelectorMask = 0x0040;   // stripped before the voice bits are read

inline uint32_t ReadWord(const uint8_t* p, size_t off) {
    return static_cast<uint32_t>(p[off]) | (static_cast<uint32_t>(p[off + 1]) << 8) |
           (static_cast<uint32_t>(p[off + 2]) << 16) | (static_cast<uint32_t>(p[off + 3]) << 24);
}
inline void WriteWord(uint8_t* p, size_t off, uint32_t v) {
    p[off] = static_cast<uint8_t>(v);
    p[off + 1] = static_cast<uint8_t>(v >> 8);
    p[off + 2] = static_cast<uint8_t>(v >> 16);
    p[off + 3] = static_cast<uint8_t>(v >> 24);
}

// A kSidCommand reply with one result word.
inline void EncodeReply(uint8_t out[kReplyBytes], uint32_t result) {
    WriteWord(out, 0, kReplyMark);
    WriteWord(out, 4, result);
    WriteWord(out, 8, kReplyMark);
}

// One record of a kBatch request.
struct BatchRecord {
    uint32_t fno = 0;
    uint32_t len = 0;          // body bytes as declared (the next record starts at len rounded up to 4)
    size_t body = 0;           // offset of the body in the request
};

// Walks a kBatch request: `visit(record)` per record, in order. Returns the record count, or -1 when
// the request is malformed (a record past the end). A `visit` returning false stops the walk.
template <typename Visit>
inline int WalkBatch(const uint8_t* req, size_t size, Visit visit) {
    if (!req || size < 4) return -1;
    const uint32_t count = ReadWord(req, 0);
    size_t off = 4;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > size) return -1;
        BatchRecord r;
        r.fno = static_cast<uint16_t>(req[off]) | (static_cast<uint16_t>(req[off + 1]) << 8);
        r.len = static_cast<uint16_t>(req[off + 2]) | (static_cast<uint16_t>(req[off + 3]) << 8);
        r.body = off + 4;
        const size_t padded = (static_cast<size_t>(r.len) + 3u) & ~static_cast<size_t>(3u);
        if (r.body + padded > size) return -1;
        if (!visit(r)) return static_cast<int>(i + 1);
        off = r.body + padded;
    }
    return static_cast<int>(count);
}

// The voice index (0..47 = core * 24 + voice) a libsd voice entry's low byte names; -1 when out of range.
inline int EntryVoice(uint32_t entry) {
    const uint32_t low = entry & 0xFFu & ~kVaSelectorMask;
    const int v = static_cast<int>((low >> 1) & 0x1F);
    if (v >= 24) return -1;
    return static_cast<int>(low & 1) * 24 + v;
}
// The libsd entry (func << 8 | core | voice << 1) for a voice index and a function code.
inline uint32_t VoiceEntry(uint32_t func, int voice_index) {
    const uint32_t core = static_cast<uint32_t>(voice_index / 24) & 1u;
    const uint32_t v = static_cast<uint32_t>(voice_index % 24);
    return (func << 8) | core | (v << 1);
}

}  // namespace ps2r::title::sly2::snd989
