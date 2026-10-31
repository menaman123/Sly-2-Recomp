#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_snd_server.h"

#include <cstring>
#include <string>

#include "engine/iop/iop_context.h"
#include "engine/sound/spu_core.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"

namespace ps2r::title::sly2 {

namespace {
constexpr int kLibsdGetAddr = 6;   // LibsdModule ordinal of SdGetAddr
}

NativeSndRpcServer::NativeSndRpcServer(uint32_t sid, Deps deps) : sid_(sid), d_(std::move(deps)) {}

uint32_t NativeSndRpcServer::GetAddr(uint32_t entry) {
    uint32_t v0 = 0;
    if (!d_.direct_spu && d_.libsd) {
        // The libsd face: the same body, the same tap, as the hosted module's import.
        iop::IopContext ctx;
        ctx.gpr[4] = entry;
        d_.libsd->Call(kLibsdGetAddr, ctx);
        return ctx.gpr[2];
    }
    if (d_.spu) v0 = d_.spu->GetAddr(entry);
    if (d_.op_tracer) {
        OpEvent ev;
        ev.ordinal = kLibsdGetAddr;
        ev.a0 = entry;
        ev.v0 = v0;
        d_.op_tracer(ev);
    }
    return v0;
}

bool NativeSndRpcServer::Serve(uint32_t fno, const uint8_t* send, uint32_t send_size, uint8_t* reply) {
    (void)send;
    if (sid_ != snd989::kSidCommand) return false;
    switch (fno) {
    case snd989::kMovieSoundReadPos: {
        // 989snd: sceSdGetAddr(SD_VA_NAX | SD_VOICE(movie voice)) - movie SPU base. The voice and base are
        // the ones the forwarded start programmed; without a live movie the module's own state answers.
        if (send_size != 0 || !movie_.learned || !movie_.active) return false;
        const uint32_t nax = GetAddr(snd989::VoiceEntry(0x22, movie_.voice) | snd989::kVaSelectorMask);
        snd989::EncodeReply(reply, nax - movie_.spu_base);
        served_readpos_++;
        return true;
    }
    default:
        return false;
    }
}

void NativeSndRpcServer::ObserveForwardedOp(const OpEvent& ev) {
    if (forwarding_fno_ != snd989::kMovieSoundStart) return;
    // The first SSA the start writes is the movie's primary voice and its SPU base.
    if (ev.ordinal == 5 && ((ev.a0 >> 8) & 0xFF) == 0x20 && !movie_.learned) {
        const int voice = snd989::EntryVoice(ev.a0);
        if (voice < 0) return;
        movie_.learned = true;
        movie_.voice = voice;
        movie_.spu_base = ev.a1;
    }
}

void NativeSndRpcServer::Call(const RpcRouter::CallRequest& req) {
    const uint8_t* send = nullptr;
    if (req.send && req.send_size && d_.ee_ram) send = d_.ee_ram(req.send, req.send_size);
    uint8_t reply[snd989::kReplyBytes] = {};
    // A reply shape other than the protocol's is not ours to fake.
    const bool shape_ok = req.recv_size == snd989::kReplyBytes && req.recv != 0 && d_.ee_ram && (req.send_size == 0 || send);
    if (shape_ok && Serve(req.func, send, req.send_size, reply)) {
        if (uint8_t* dst = d_.ee_ram ? d_.ee_ram(req.recv, snd989::kReplyBytes) : nullptr) std::memcpy(dst, reply, snd989::kReplyBytes);
        served_++;
        NativeCensus::Instance().Served(NativeUnit::Iop);
        if (call_tracer_) call_tracer_(req, true, reply, snd989::kReplyBytes);
        if (after_call_) after_call_();
        return;
    }
    forwarded_++;
    NativeCensus::Instance().Fallback(NativeUnit::Iop);
    if (d_.fallback) {
        forwarding_fno_ = req.func;
        d_.fallback->Call(req);
        forwarding_fno_ = 0xFFFFFFFFu;
    }
    if (sid_ == snd989::kSidCommand) {
        // The movie sound's lifetime, from the EE's own requests.
        if (req.func == snd989::kMovieSoundStart && movie_.learned) movie_.active = true;
        if (req.func == snd989::kMovieSoundStop || req.func == snd989::kMovieSoundClose) {
            movie_.active = false;
            movie_.learned = false;
        }
    }
    if (call_tracer_) call_tracer_(req, false, nullptr, 0);
    if (after_call_) after_call_();
}

std::string NativeSndRpcServer::Detail() const {
    if (!movie_.learned) return "movie=none";
    return "movie=v" + std::to_string(movie_.voice) + ":base=0x" + [this] {
        char b[16];
        std::snprintf(b, sizeof(b), "%x", movie_.spu_base);
        return std::string(b);
    }() + (movie_.active ? ":active" : ":idle");
}

}  // namespace ps2r::title::sly2
