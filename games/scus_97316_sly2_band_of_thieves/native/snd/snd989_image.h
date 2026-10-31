// snd989_image.h -- the layout facts of THIS disc's 989snd_Library image (SCEA 989snd 3.01, exports
// "snd989" v1.08, recompiled at base 0x33000: iop/iop_modules.h) that the native sound driver relies on.
//
// Every number is an OFFSET or a RULE read off the module's disassembly (the build-time recompiled C++ is
// the same bytes; the evidence trail is ~/ra-orch/notes/nat2-snd.md). No game bytes live here.
//
// WHY the native driver needs the image's layout at all: 989snd keeps its state in its own .data/.bss
// (IOP RAM 0x50000..0x59e88) and in heap blocks it allocates; while the hosted module still owns part
// of that state (the voice / SPU-RAM allocators, banks, streams, the 240 Hz tick), a native body that
// is exact must read the SAME words the module's body reads -- the module's RAM is the state, and
// reading it is how a natively served request stays byte-for-byte what the module would have answered.
// The native driver's own state (Snd989State) mirrors these fields; the `hosted-ram` backend reads them
// from IOP RAM, the `native` backend (the full port) owns them. Either way the field list below IS the
// contract: what the full port has to carry.
#pragma once

#include <cstdint>

namespace ps2r::title::sly2::snd989 {

struct Image {
    static constexpr uint32_t kBase = 0x33000;      // the load base the module was recompiled for
    static constexpr uint32_t kEnd = 0x59e88;       // base + memsz
    static constexpr uint32_t kData = 0x50000;      // the `lui $x, 5` data page every body addresses from

    // --- the RPC server (fn_3497c, the 0x123456 dispatcher) -------------------------------------
    // reply_buf = g[kReplyBufPtr]: the IOP reply buffer {-1, result[0..], -1}; result_ptr = g[kResultPtr].
    // Dispatcher: result_ptr = reply_buf + 4; shim(buff); *(result_ptr + 4) = -1; return reply_buf.
    // Only a shim that has a value stores it: `*result_ptr = v0` (fno 91's 0x34184, fno 26's 0x338bc, ...);
    // a void shim (fno 90's 0x34158) stores nothing and the reply carries the last stored word.
    // The batch shim (fn_34880, fno 77): per record result_ptr += 4 after the record's shim; -4 at the end.
    static constexpr uint32_t kReplyBufPtr = kData + 0x63c;
    static constexpr uint32_t kResultPtr = kData + 0x640;
    static constexpr uint32_t kHandlerTable = kData + 0x648;   // 107 words: fno -> shim address
    static constexpr uint32_t kHandlerCount = 107;

    // --- fno 10: snd_GetSystemParam(n) = g[kParamTable + n*4] (fn_4e220) ---------------------------
    static constexpr uint32_t kParamTable = kData + 0x2418;

    // --- the movie sound (fnos 59 open / 62 start / 60 stop / 61 close / 90 push / 91 read position) --
    // fn_3e284 (open) allocates: the IOP buffer g[kMovieIopBuf] (reply of fno 59), the SPU region
    // g[kMovieSpuBase] (fn_4272c: the SPU RAM allocator), two voices g[kMovieVoice0] / g[kMovieVoice1]
    // (fn_4c750: the voice allocator, priority 0x7f), one SPU DMA channel g[kMovieChan] (s16).
    // fn_3e5c0 (push, fno 90): SdVoiceTrans(g[kMovieChan], 0, g[kMovieIopBuf], g[kMovieSpuBase] + a1, a0);
    //                          SdVoiceTransStatus(g[kMovieChan], 1). Void: no result word.
    // fn_3e610 (read position, fno 91): SdGetAddr(SD_VA_NAX | voice entry of g[kMovieVoice0]) - g[kMovieSpuBase].
    static constexpr uint32_t kMovieChan = kData + 0x19ac;      // s16
    static constexpr uint32_t kMovieIopBuf = kData + 0x19b0;
    static constexpr uint32_t kMovieSpuBase = kData + 0x19b4;
    static constexpr uint32_t kMovieSpuSize = kData + 0x19b8;
    static constexpr uint32_t kMovieVoice0 = kData + 0x19cc;    // voice index 0..47 (core = v / 24); -1 = none
    static constexpr uint32_t kMovieVoice1 = kData + 0x19d0;

    // --- fno 26: snd_BankSoundIsValid(bank, index) (fn_3fac8) over the bank header at IOP `bank` ----
    // *(u32*)bank == 'SBlk' -> the sound-block path (fn_39b24): e = *(u32*)(bank+0x1c) + index * 12;
    //   result = *(u16*)(e + 6) & 1 (no bounds check);
    // else count = *(s16*)(bank+0x14); index in [0, count): e = *(u32*)(bank+0x1c) + index * 28;
    //   e[4] == 0 -> 0; type = e[0]: 4 -> (*(s8*)(e+0x12) == 0); 3 or 5 -> 1; else 0.
    static constexpr uint32_t kBankMagicSBlk = 0x6b6c4253u;
    static constexpr uint32_t kBankSoundCount = 0x14;    // s16
    static constexpr uint32_t kBankSoundTable = 0x1c;    // u32 pointer (IOP address)
    static constexpr uint32_t kBankSoundEntryBytes = 28;
    static constexpr uint32_t kSoundTypeOff = 0x0, kSoundPtrOff = 0x4, kSoundFlagOff = 0x12;
    static constexpr uint32_t kBlockSoundEntryBytes = 12, kBlockSoundFlagsOff = 6;

    // The voice entry (libsd low byte) of a 989snd voice index: core = v / 24, voice = v % 24.
    static constexpr uint32_t VoiceEntryLow(int voice_index) {
        const uint32_t core = static_cast<uint32_t>(voice_index / 24);
        const uint32_t v = static_cast<uint32_t>(voice_index - static_cast<int>(core) * 24);
        return core | (v << 1);
    }
};

}  // namespace ps2r::title::sly2::snd989
