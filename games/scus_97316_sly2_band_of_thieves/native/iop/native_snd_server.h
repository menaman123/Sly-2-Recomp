// native_snd_server.h -- NativeSndRpcServer: the title's native answer to Sly 2's 989snd SIF RPC calls.
//
// The seam: RpcRouter::RegisterServer(sid, server) re-points the EE-facing row of a server id, so this
// Server takes 0x123456 / 0x123457 over from the hosted module's IopRpcServer (iop_host.cpp
// OnServerRegistered) and keeps THAT server as its fallback: a request it serves natively never reaches
// the IOP kernel (no coroutine dispatch, no recompiled handler, no libsd register emulation), a request
// it does not serve is forwarded unchanged (the engine's model = the oracle). The reply lands at the
// same instant either way (FRAME dispatch completes a round trip inside the EE's sceSifCallRpc).
//
// What is native TODAY (the lane's census, notes/nat-iop-spu.md): the movie-sound read-position poll
// (fno 91: the FMV's audio clock, 263 of the lane's 1184 round trips) -- one libsd GetAddr(NAX of the
// movie voice) minus the movie's SPU base. The voice and base are the parameters of the movie-sound
// START the EE requested (fno 62); this server learns them from the libsd ops the forwarded start
// performs (SetAddr SSA) and serves the poll natively until the EE stops / closes the movie sound
// (fno 60 / 61). Everything else is forwarded. The protocol decoder (snd_rpc_protocol.h) covers the
// whole census so the next fnos plug in here.
//
// title.native.spu chooses the SPU face: off = the engine's libsd face (LibsdModule::Call, the same
// op stream and tap the hosted module produces: the oracle-comparable trunk); on = SpuCore directly.
// Both program the same core; the lane's audio.ahash must not move under either.
//
// Portable (no OS headers); the EE RAM and SPU accesses go through the engine's own types.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>

#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/sif/hle/rpc_router.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd_rpc_protocol.h"

namespace ps2r::sound {
class SpuCore;
}

namespace ps2r::title::sly2 {

class NativeSndRpcServer final : public RpcRouter::Server {
public:
    using OpEvent = iop::hle::LibsdModule::OpEvent;
    using OpTracer = iop::hle::LibsdModule::OpTracer;
    // One line per round trip this server answered or forwarded (the RPC trace's native rows).
    using CallTracer = std::function<void(const RpcRouter::CallRequest& req, bool native, const uint8_t* reply, uint32_t reply_bytes)>;

    // EE RAM access: a writable pointer to `size` bytes at the EE address `addr` (null = unmapped).
    using EeRam = std::function<uint8_t*(uint32_t addr, uint32_t size)>;
    struct Deps {
        sound::SpuCore* spu = nullptr;                 // the SPU2 core (title.native.spu: the direct face)
        iop::hle::LibsdModule* libsd = nullptr;        // the libsd face (title.native.spu off)
        EeRam ee_ram;                                  // the EE's RAM (request / reply buffers)
        RpcRouter::Server* fallback = nullptr;         // the hosted module's server for this sid (may be null: nothing to forward to)
        bool direct_spu = false;                       // title.native.spu
        OpTracer op_tracer;                            // the libsd op tap this server's own ops report to (census parity)
    };

    NativeSndRpcServer(uint32_t sid, Deps deps);
    NativeSndRpcServer(const NativeSndRpcServer&) = delete;
    NativeSndRpcServer& operator=(const NativeSndRpcServer&) = delete;

    void Call(const RpcRouter::CallRequest& req) override;
    // The faces and the fallback for a (re)registration of this sid; the learned state is kept.
    void Rebind(Deps deps) {
        deps.op_tracer = d_.op_tracer;
        d_ = std::move(deps);
    }
    // Runs after every round trip (served or forwarded): the owner refreshes its census detail.
    void SetAfterCall(std::function<void()> f) { after_call_ = std::move(f); }

    // The decision + the native body, on raw request bytes: true = served (reply[kReplyBytes] filled),
    // false = not served (the caller forwards). Exposed for the replay test.
    bool Serve(uint32_t fno, const uint8_t* send, uint32_t send_size, uint8_t* reply);
    // A libsd op the FORWARDED path performed (the hosted module's): what this server learns from.
    void ObserveForwardedOp(const OpEvent& ev);

    void SetCallTracer(CallTracer t) { call_tracer_ = std::move(t); }

    uint32_t Sid() const { return sid_; }
    uint64_t Served() const { return served_; }
    uint64_t Forwarded() const { return forwarded_; }
    uint64_t ServedReadPos() const { return served_readpos_; }
    // "movie=voice:base" when learned, "movie=none" otherwise.
    std::string Detail() const;

    struct MovieSound {
        bool learned = false;    // SSA seen during a forwarded start
        bool active = false;     // between the forwarded start and the forwarded stop / close
        int voice = -1;          // 0..47
        uint32_t spu_base = 0;   // the SSA the start programmed
    };
    const MovieSound& Movie() const { return movie_; }

private:
    uint32_t GetAddr(uint32_t entry);

    uint32_t sid_;
    Deps d_;
    CallTracer call_tracer_;
    std::function<void()> after_call_;
    MovieSound movie_;
    uint32_t forwarding_fno_ = 0xFFFFFFFFu;   // the fno whose forwarded round trip is in flight
    uint64_t served_ = 0, forwarded_ = 0, served_readpos_ = 0;
};

}  // namespace ps2r::title::sly2
