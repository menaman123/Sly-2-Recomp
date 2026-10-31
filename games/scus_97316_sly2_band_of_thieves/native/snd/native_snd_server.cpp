#include "games/scus_97316_sly2_band_of_thieves/native/snd/native_snd_server.h"

#include <cstring>

#include "engine/iop/iop_context.h"
#include "engine/sound/spu_core.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"

namespace ps2r::title::sly2 {

namespace {
constexpr int kOrdGetAddr = 6, kOrdVoiceTrans = 9, kOrdVoiceTransStatus = 11;   // LibsdModule ordinals
// The libsd face's SdVoiceTrans reads its fifth argument from the caller's stack (sp + 0x10): a scratch
// frame just under the module's own completion-handler stack (LibsdModule::kScratchStack).
constexpr uint32_t kScratchSp = iop::hle::LibsdModule::kScratchStack - 0x40;
using snd989::Image;
using OpEvent = iop::hle::LibsdModule::OpEvent;
}  // namespace

Snd989Server::Snd989Server(uint32_t sid, Deps deps) : sid_(sid), d_(std::move(deps)) {}

bool Snd989Server::IsNativeFno(uint32_t fno) {
    switch (fno) {
    case snd989::kQuery10:
    case snd989::kBankSoundQuery:
    case snd989::kMovieSoundPush:
    case snd989::kMovieSoundReadPos:
        return true;
    default:
        return false;
    }
}

// --- IOP RAM ------------------------------------------------------------------------------------
uint32_t Snd989Server::Iop32(uint32_t addr, bool& ok) const {
    const uint8_t* p = d_.iop_ram ? d_.iop_ram(addr, 4) : nullptr;
    if (!p) { ok = false; return 0; }
    return snd989::ReadWord(p, 0);
}
int32_t Snd989Server::Iop16s(uint32_t addr, bool& ok) const {
    const uint8_t* p = d_.iop_ram ? d_.iop_ram(addr, 2) : nullptr;
    if (!p) { ok = false; return 0; }
    return static_cast<int16_t>(static_cast<uint16_t>(p[0] | (p[1] << 8)));
}
int8_t Snd989Server::Iop8s(uint32_t addr, bool& ok) const {
    const uint8_t* p = d_.iop_ram ? d_.iop_ram(addr, 1) : nullptr;
    if (!p) { ok = false; return 0; }
    return static_cast<int8_t>(p[0]);
}
bool Snd989Server::IopWrite32(uint32_t addr, uint32_t v) {
    uint8_t* p = d_.iop_ram ? d_.iop_ram(addr, 4) : nullptr;
    if (!p) return false;
    snd989::WriteWord(p, 0, v);
    return true;
}

// --- the production faces --------------------------------------------------------------------------
uint32_t Snd989Server::LibsdFace::GetAddr(uint32_t entry) {
    iop::IopContext ctx;
    ctx.gpr[4] = entry;
    ctx.gpr[29] = kScratchSp;
    libsd_.Call(kOrdGetAddr, ctx);
    return ctx.gpr[2];
}
uint32_t Snd989Server::LibsdFace::VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr, uint32_t size) {
    uint8_t* p = iop_ram_ ? iop_ram_(kScratchSp + 0x10, 4) : nullptr;
    if (!p) return 0;
    snd989::WriteWord(p, 0, size);
    iop::IopContext ctx;
    ctx.gpr[4] = chan;
    ctx.gpr[5] = mode;
    ctx.gpr[6] = iop_addr;
    ctx.gpr[7] = spu_addr;
    ctx.gpr[29] = kScratchSp;
    libsd_.Call(kOrdVoiceTrans, ctx);
    return ctx.gpr[2];
}
uint32_t Snd989Server::LibsdFace::VoiceTransStatus(uint32_t chan, uint32_t flag) {
    iop::IopContext ctx;
    ctx.gpr[4] = chan;
    ctx.gpr[5] = flag;
    ctx.gpr[29] = kScratchSp;
    libsd_.Call(kOrdVoiceTransStatus, ctx);
    return ctx.gpr[2];
}

uint32_t Snd989Server::SpuCoreFace::GetAddr(uint32_t entry) {
    const uint32_t v0 = spu_.GetAddr(entry);
    if (tap_) {
        OpEvent ev;
        ev.ordinal = kOrdGetAddr;
        ev.a0 = entry;
        ev.v0 = v0;
        tap_(ev);
    }
    return v0;
}
uint32_t Snd989Server::SpuCoreFace::VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr, uint32_t size) {
    // The direct face: the bytes move now; the module's completion handler for this channel is what the
    // open programmed (the movie open clears it), so no deferred completion is modelled here.
    uint32_t moved = 0;
    uint8_t* host = iop_ram_ ? iop_ram_(iop_addr, size) : nullptr;
    if (host) moved = spu_.VoiceTrans(static_cast<int>(chan & 1), mode, host, spu_addr, size);
    if (tap_) {
        OpEvent ev;
        ev.ordinal = kOrdVoiceTrans;
        ev.a0 = chan; ev.a1 = mode; ev.a2 = iop_addr; ev.a3 = spu_addr; ev.extra = size; ev.v0 = moved;
        tap_(ev);
    }
    return moved;
}
uint32_t Snd989Server::SpuCoreFace::VoiceTransStatus(uint32_t chan, uint32_t flag) {
    const uint32_t v0 = static_cast<uint32_t>(spu_.VoiceTransStatus(static_cast<int>(chan & 1), static_cast<int>(flag)));
    if (tap_) {
        OpEvent ev;
        ev.ordinal = kOrdVoiceTransStatus;
        ev.a0 = chan; ev.a1 = flag; ev.v0 = v0;
        tap_(ev);
    }
    return v0;
}

// --- the bodies ---------------------------------------------------------------------------------
// fno 10 (fn_4e220): result = g[kParamTable + n * 4]. The shim stores the result.
bool Snd989Server::BodySystemParam(const uint8_t* body, uint32_t size, uint32_t& result) {
    if (size < 4 || !body) return false;
    const uint32_t n = snd989::ReadWord(body, 0);
    if (n >= 64) return false;   // the table is 64 words wide in this image; anything else is not ours to guess
    bool ok = true;
    result = Iop32(Image::kParamTable + n * 4, ok);
    return ok;
}

// fno 26 (fn_3fac8): snd_BankSoundIsValid(bank, index) over the bank header in IOP RAM. The shim stores the result.
bool Snd989Server::BodyBankSoundIsValid(const uint8_t* body, uint32_t size, uint32_t& result) {
    if (size < 8 || !body) return false;
    const uint32_t bank = snd989::ReadWord(body, 0);
    const int32_t index = static_cast<int32_t>(snd989::ReadWord(body, 4));
    if (bank == 0) { result = 0; return true; }
    bool ok = true;
    const uint32_t magic = Iop32(bank, ok);
    if (!ok) return false;
    if (magic == Image::kBankMagicSBlk) {
        // The sound-block path (fn_39b24): e = table + index * 12; result = *(u16*)(e + 6) & 1 (no bounds check in the module).
        const uint32_t table = Iop32(bank + Image::kBankSoundTable, ok);
        const uint32_t e = table + static_cast<uint32_t>(index) * Image::kBlockSoundEntryBytes;
        const uint8_t* p = d_.iop_ram ? d_.iop_ram(e + Image::kBlockSoundFlagsOff, 2) : nullptr;
        if (!ok || !p) return false;
        result = static_cast<uint32_t>(p[0] & 1);
        return true;
    }
    const int32_t count = Iop16s(bank + Image::kBankSoundCount, ok);
    if (!ok) return false;
    if (index >= count) { result = 0; return true; }
    if (index < 0) { result = 0; return true; }
    const uint32_t table = Iop32(bank + Image::kBankSoundTable, ok);
    const uint32_t e = table + static_cast<uint32_t>(index) * Image::kBankSoundEntryBytes;
    const uint32_t ptr = Iop32(e + Image::kSoundPtrOff, ok);
    const uint32_t type = Iop32(e + Image::kSoundTypeOff, ok);
    if (!ok) return false;
    if (ptr == 0) { result = 0; return true; }
    if (type == 4) {
        const int8_t flag = Iop8s(e + Image::kSoundFlagOff, ok);
        if (!ok) return false;
        result = flag == 0 ? 1u : 0u;
        return true;
    }
    result = (type == 3 || type == 5) ? 1u : 0u;
    return true;
}

// fno 90 (fn_3e5c0): SdVoiceTrans(chan, 0, iop_buf, spu_base + offset, size); SdVoiceTransStatus(chan, 1). Void.
bool Snd989Server::BodyMoviePush(const uint8_t* body, uint32_t size, bool dry) {
    if (size < 8 || !body) return false;
    const uint32_t bytes = snd989::ReadWord(body, 0);
    const uint32_t offset = snd989::ReadWord(body, 4);
    bool ok = true;
    const int32_t chan = Iop16s(Image::kMovieChan, ok);
    const uint32_t iop_buf = Iop32(Image::kMovieIopBuf, ok);
    const uint32_t spu_base = Iop32(Image::kMovieSpuBase, ok);
    if (!ok || iop_buf == 0 || chan < 0 || chan > 1) return false;   // no movie open: the module answers
    if (dry) return true;
    d_.face->VoiceTrans(static_cast<uint32_t>(chan), 0, iop_buf, spu_base + offset, bytes);
    d_.face->VoiceTransStatus(static_cast<uint32_t>(chan), 1);
    return true;
}

// fno 91 (fn_3e610): SdGetAddr(SD_VA_NAX | voice entry(g[kMovieVoice0])) - g[kMovieSpuBase]. The shim stores the result.
bool Snd989Server::BodyMovieReadPos(uint32_t& result, bool dry) {
    bool ok = true;
    const int32_t voice = static_cast<int32_t>(Iop32(Image::kMovieVoice0, ok));
    const uint32_t spu_base = Iop32(Image::kMovieSpuBase, ok);
    if (!ok || voice < 0 || voice >= 48) return false;   // no movie voice: the module's own arithmetic answers
    if (dry) return true;
    const uint32_t entry = (Image::VoiceEntryLow(voice) | snd989::kVaNax) & 0xFFFFu;
    result = d_.face->GetAddr(entry) - spu_base;
    return true;
}

bool Snd989Server::Serve(uint32_t fno, const uint8_t* body, uint32_t body_size, uint32_t& result, bool& has_result, bool dry) {
    has_result = false;
    result = 0;
    switch (fno) {
    case snd989::kQuery10:
        if (!BodySystemParam(body, body_size, result)) return false;
        has_result = true;
        return true;
    case snd989::kBankSoundQuery:
        if (!BodyBankSoundIsValid(body, body_size, result)) return false;
        has_result = true;
        return true;
    case snd989::kMovieSoundPush:
        return BodyMoviePush(body, body_size, dry);
    case snd989::kMovieSoundReadPos:
        if (body_size != 0) return false;
        if (!BodyMovieReadPos(result, dry)) return false;
        has_result = true;
        return true;
    default:
        return false;
    }
}

// The module's dispatcher, natively: result_ptr = reply + 4; per record the shim stores its result (if
// any) at result_ptr, the batch advances result_ptr by 4 per record and steps back 4 at the end; the
// terminator -1 lands at result_ptr + 4; the EE receives reply[0..recv_size).
bool Snd989Server::ServeRoundTrip(const RpcRouter::CallRequest& req, const uint8_t* send, uint8_t reply_out[snd989::kReplyBytes]) {
    bool ok = true;
    const uint32_t reply = Iop32(Image::kReplyBufPtr, ok);
    if (!ok || reply == 0) return false;   // the module has not initialised its server yet
    const uint8_t* reply_bytes = d_.iop_ram(reply, snd989::kReplyBytes);
    if (!reply_bytes) return false;
    uint32_t result_ptr = reply + 4;
    if (req.func == snd989::kBatch) {
        // Decide first: every record native, else the whole batch goes to the module (the reply buffer
        // semantics do not split).
        bool all_native = true;
        const int n = snd989::WalkBatch(send, req.send_size, [&](const snd989::BatchRecord& r) {
            if (!IsNativeFno(r.fno)) all_native = false;
            return all_native;
        });
        if (n < 0 || !all_native) return false;
        // The bodies may still decline on state: a DRY pass checks every record's preconditions (no libsd
        // op runs) so a declined batch is forwarded whole with nothing applied twice; then the real pass.
        struct Slot { uint32_t result; bool has; };
        Slot slots[64];
        int count = 0;
        bool served = true;
        snd989::WalkBatch(send, req.send_size, [&](const snd989::BatchRecord& r) {
            if (count >= 64) { served = false; return false; }
            Slot& s = slots[count++];
            if (!Serve(r.fno, send + r.body, r.len, s.result, s.has, /*dry=*/true)) { served = false; return false; }
            return true;
        });
        if (!served) return false;
        count = 0;
        snd989::WalkBatch(send, req.send_size, [&](const snd989::BatchRecord& r) {
            Slot& s = slots[count++];
            if (!Serve(r.fno, send + r.body, r.len, s.result, s.has)) { served = false; return false; }
            return true;
        });
        if (!served) return false;   // unreachable after a passing dry pass: the state is read, not raced
        for (int i = 0; i < count; i++) {
            if (slots[i].has && !IopWrite32(result_ptr, slots[i].result)) return false;
            result_ptr += 4;
        }
        result_ptr -= 4;
        batches_served_++;
        snd989::WalkBatch(send, req.send_size, [&](const snd989::BatchRecord& r) {
            if (r.fno < snd989::kFnoCount) served_fno_[r.fno]++;
            return true;
        });
    } else {
        uint32_t result = 0;
        bool has = false;
        if (!Serve(req.func, send, req.send_size, result, has, /*dry=*/true)) return false;
        if (!Serve(req.func, send, req.send_size, result, has)) return false;
        if (has && !IopWrite32(result_ptr, result)) return false;
        if (req.func < snd989::kFnoCount) served_fno_[req.func]++;
    }
    IopWrite32(Image::kResultPtr, result_ptr);
    IopWrite32(result_ptr + 4, snd989::kReplyMark);
    // The reply DMA: recv_size bytes of the buffer (every 0x123456 client asks for 12).
    const uint32_t n = req.recv_size < snd989::kReplyBytes ? req.recv_size : snd989::kReplyBytes;
    if (uint8_t* dst = d_.ee_ram ? d_.ee_ram(req.recv, n) : nullptr) {
        const uint8_t* src = d_.iop_ram(reply, n);
        if (src) std::memcpy(dst, src, n);
    }
    std::memcpy(reply_out, reply_bytes, snd989::kReplyBytes);
    return true;
}

void Snd989Server::Forward(const RpcRouter::CallRequest& req) {
    forwarded_++;
    if (req.func == snd989::kBatch) batches_forwarded_++;
    NativeCensus::Instance().Fallback(NativeUnit::Iop);
    if (d_.fallback) d_.fallback->Call(req);
    if (call_tracer_) call_tracer_(req, false, nullptr, 0);
    if (after_call_) after_call_();
}

void Snd989Server::Call(const RpcRouter::CallRequest& req) {
    if (sid_ != snd989::kSidCommand || !d_.iop_ram || !d_.ee_ram || !d_.face) { Forward(req); return; }
    const bool candidate = req.func == snd989::kBatch || IsNativeFno(req.func);
    // A reply shape other than the protocol's is not ours to fake.
    if (!candidate || req.recv_size != snd989::kReplyBytes || req.recv == 0) { Forward(req); return; }
    const uint8_t* send = nullptr;
    if (req.send && req.send_size) {
        send = d_.ee_ram(req.send, req.send_size);
        if (!send) { Forward(req); return; }
    }
    uint8_t reply[snd989::kReplyBytes] = {};
    if (!ServeRoundTrip(req, send, reply)) { Forward(req); return; }
    if (d_.drain_kernel) d_.drain_kernel();   // the hosted round trip's kernel pass (rule 3)
    served_++;
    NativeCensus::Instance().Served(NativeUnit::Iop);
    if (call_tracer_) call_tracer_(req, true, reply, snd989::kReplyBytes);
    if (after_call_) after_call_();
}

std::string Snd989Server::Detail() const {
    return "fno10=" + std::to_string(ServedFno(snd989::kQuery10)) + ":fno26=" + std::to_string(ServedFno(snd989::kBankSoundQuery)) +
           ":fno90=" + std::to_string(ServedFno(snd989::kMovieSoundPush)) + ":fno91=" + std::to_string(ServedFno(snd989::kMovieSoundReadPos)) +
           ":batch=" + std::to_string(batches_served_) + "/" + std::to_string(batches_forwarded_);
}

}  // namespace ps2r::title::sly2
