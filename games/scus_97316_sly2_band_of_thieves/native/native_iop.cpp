#include "games/scus_97316_sly2_band_of_thieves/native/native_iop.h"

#include <string>

#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/sif/hle/iop_rpc_server.h"
#include "engine/sif/hle/rpc_router.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"

namespace ps2r::title::sly2 {

std::unique_ptr<NativeIop> NativeIop::Install(const NativeKnobs& knobs, Deps deps) {
    if (!knobs.iop) return nullptr;
    if (!deps.router || !deps.libsd || !deps.set_server_observer || !deps.spu || !deps.ee_ram) return nullptr;
    return std::unique_ptr<NativeIop>(new NativeIop(knobs, std::move(deps)));
}

NativeIop::NativeIop(const NativeKnobs& knobs, Deps deps) : d_(std::move(deps)), direct_spu_(knobs.spu) {
    iop::hle::LibsdModule& libsd = *d_.libsd;
    // The native servers' own ops report to the tap already installed (the census trace), like the module's.
    NativeSndRpcServer::Deps sd;
    sd.spu = d_.spu;
    sd.libsd = &libsd;
    sd.ee_ram = d_.ee_ram;
    sd.direct_spu = direct_spu_;
    sd.op_tracer = libsd.GetOpTracer();
    command_ = std::make_unique<NativeSndRpcServer>(snd989::kSidCommand, sd);
    bank_ = std::make_unique<NativeSndRpcServer>(snd989::kSidBank, sd);
    command_->SetAfterCall([this] { UpdateCensusDetail(); });
    bank_->SetAfterCall([this] { UpdateCensusDetail(); });
    if (d_.rpc_trace) {
        std::FILE* f = d_.rpc_trace;
        auto tracer = [f](const RpcRouter::CallRequest& req, bool native, const uint8_t* reply, uint32_t n) {
            std::fprintf(f, "NAT %s %08x %u %u ->", native ? "served" : "forwarded", req.server_id, req.func, req.send_size);
            for (uint32_t i = 0; i < n; ++i) std::fprintf(f, "%s%02x", i ? "" : " ", reply[i]);
            std::fputc('\n', f);
        };
        command_->SetCallTracer(tracer);
        bank_->SetCallTracer(tracer);
    }
    // The forwarded path's libsd ops: observed by the native servers, then the previous tap.
    {
        iop::hle::LibsdModule::OpTracer prev = libsd.GetOpTracer();
        NativeSndRpcServer* cmd = command_.get();
        NativeSndRpcServer* bank = bank_.get();
        libsd.SetOpTracer([prev, cmd, bank](const iop::hle::LibsdModule::OpEvent& ev) {
            cmd->ObserveForwardedOp(ev);
            bank->ObserveForwardedOp(ev);
            if (prev) prev(ev);
        });
    }
    // Take the router rows when (and every time) the hosted 989snd registers them.
    d_.set_server_observer([this](uint32_t sid, IopRpcServer& hosted) {
        NativeSndRpcServer* s = sid == snd989::kSidCommand ? command_.get() : sid == snd989::kSidBank ? bank_.get() : nullptr;
        if (!s) return;
        // Re-point the fallback (a restore replaces the hosted server object) and the router row.
        NativeSndRpcServer::Deps nd;
        nd.spu = d_.spu;
        nd.libsd = d_.libsd;
        nd.ee_ram = d_.ee_ram;
        nd.direct_spu = direct_spu_;
        nd.fallback = &hosted;
        s->Rebind(nd);
        d_.router->RegisterServer(sid, *s, sid == snd989::kSidCommand ? "native:989snd" : "native:989snd-bank");
        UpdateCensusDetail();
    });
    UpdateCensusDetail();
}

NativeIop::~NativeIop() {
    UpdateCensusDetail();
    if (d_.set_server_observer) d_.set_server_observer(nullptr);
}

void NativeIop::UpdateCensusDetail() {
    NativeCensus& c = NativeCensus::Instance();
    c.SetDetail(NativeUnit::Iop, "fno91=" + std::to_string(command_->ServedReadPos()) + ":" + command_->Detail() +
                                     ":bank_fwd=" + std::to_string(bank_->Forwarded()));
    c.SetDetail(NativeUnit::Spu, direct_spu_ ? "face=spu_core" : "face=libsd");
}

}  // namespace ps2r::title::sly2
