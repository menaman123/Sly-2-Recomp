// native_snd_server.h -- Snd989Server: the title's native answer to Sly 2's 989snd SIF RPC calls.
//
// The seam: RpcRouter::RegisterServer(sid, server) re-points the EE-facing row of a server id, so this
// Server takes 0x123456 (and 0x123457) over from the hosted module's IopRpcServer (iop_host.cpp
// OnServerRegistered) and keeps THAT server as its fallback: a request it serves natively never reaches
// the IOP kernel (no coroutine dispatch, no recompiled handler); a request it does not serve is
// forwarded unchanged (the engine's model = the oracle). The reply lands at the same instant either
// way (FRAME dispatch completes a round trip inside the EE's sceSifCallRpc).
//
// EXACTNESS RULES (what makes a natively served round trip byte-identical in EE RAM, SPU state and the
// libsd op stream to the module's own answer):
//   1. THE REPLY BUFFER IS STATE. 989snd answers every 0x123456 call from ONE IOP buffer {-1, result,
//      -1}; its dispatcher never writes the result word -- only a shim that has a value stores it, a void
//      shim leaves the previous caller's word in place, and the EE copies 12 bytes of that buffer. The
//      native server therefore answers THROUGH THE SAME BUFFER (g[Image::kReplyBufPtr] in IOP RAM): it
//      stores the result word only when the module's shim would, writes the terminator, and copies the
//      buffer's first 12 bytes to the EE -- so the hosted and native answers interleave exactly.
//   2. THE MODULE'S RAM IS THE STATE. A body reads the words the module's body reads (snd989_image.h
//      names them) and performs the same libsd ops in the same order through LibsdModule::Call (the
//      same face and tap the hosted module uses) or, with title.native.spu, directly on SpuCore.
//   3. THE KERNEL PASS. The hosted round trip drains the IOP kernel at the current clock (Deps::drain_kernel);
//      a native answer runs that pass too, after its body -- the module's 240 Hz tick thread, woken by the
//      hard timer inside the pass, lands in the same order relative to the EE's requests either way.
//   4. NOTHING THE MODULE LATER RELIES ON IS MUTATED NATIVELY. The fnos served here (10, 26, 90, 91) are
//      functions of the module's state + libsd ops; the ones that mutate shared state (voice / SPU RAM
//      allocation, the tick's pending key-on masks, banks, streams) are forwarded until the full port
//      owns that state. A batch (fno 77) is served natively only when EVERY record is.
//
// Portable (no OS headers); EE RAM / IOP RAM / SPU accesses go through the engine's own types.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/sif/hle/rpc_router.h"
#include "games/scus_97316_sly2_band_of_thieves/native/snd/snd989_image.h"
#include "games/scus_97316_sly2_band_of_thieves/native/snd/snd_rpc_protocol.h"

namespace ps2r::sound {
class SpuCore;
}

namespace ps2r::title::sly2 {

// The libsd face a native body performs its ops through. Production: LibsdFace (LibsdModule::Call -- the
// hosted module's own entry, tap included) or SpuCoreFace (title.native.spu: SpuCore directly, the tap fed
// here); the replay test substitutes a face that answers from the recorded trace.
class SdFace {
public:
    virtual ~SdFace() = default;
    virtual uint32_t GetAddr(uint32_t entry) = 0;
    virtual uint32_t VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr, uint32_t size) = 0;
    virtual uint32_t VoiceTransStatus(uint32_t chan, uint32_t flag) = 0;
};

class Snd989Server final : public RpcRouter::Server {
public:
    using OpEvent = iop::hle::LibsdModule::OpEvent;
    using OpTracer = iop::hle::LibsdModule::OpTracer;
    // One line per round trip this server answered or forwarded (the RPC trace's native rows).
    using CallTracer = std::function<void(const RpcRouter::CallRequest& req, bool native, const uint8_t* reply, uint32_t reply_bytes)>;
    // RAM access: a pointer to `size` bytes at `addr` (null = unmapped). EE addresses are the guest's
    // (the engine maps them); IOP addresses are the 2 MiB IOP RAM's.
    using RamAccess = std::function<uint8_t*(uint32_t addr, uint32_t size)>;

    struct Deps {
        SdFace* face = nullptr;                        // the libsd face the bodies' ops go through (required to serve)
        RamAccess ee_ram;                              // the EE's RAM (request / reply buffers)
        RamAccess iop_ram;                             // the IOP's RAM (the module's state, its reply buffer, its DMA buffers)
        RpcRouter::Server* fallback = nullptr;         // the hosted module's server for this sid (null: nothing to forward to)
        // THE SCHEDULING SEAM: one IOP kernel dispatch pass at the current IOP clock (IopKernel::Dispatch()).
        // The hosted round trip is SifRpcBridge::RunOnRpcThread -> DriveUntil: a Dispatch pass that fires the
        // due timer events (989snd's 240 Hz tick thread) and runs every ready thread, the RPC thread among
        // them, until idle. A native answer must leave the kernel in the same place: after the body it runs
        // the same pass (minus the RPC thread), so the tick keeps its order against the EE's requests.
        std::function<void()> drain_kernel;
    };

    // The production faces.
    class LibsdFace final : public SdFace {
    public:
        // `iop_ram`: SdVoiceTrans's fifth argument travels on the IOP stack (a scratch frame under the module's).
        LibsdFace(iop::hle::LibsdModule& libsd, RamAccess iop_ram) : libsd_(libsd), iop_ram_(std::move(iop_ram)) {}
        uint32_t GetAddr(uint32_t entry) override;
        uint32_t VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr, uint32_t size) override;
        uint32_t VoiceTransStatus(uint32_t chan, uint32_t flag) override;
    private:
        iop::hle::LibsdModule& libsd_;
        RamAccess iop_ram_;
    };
    class SpuCoreFace final : public SdFace {
    public:
        // `tap`: the libsd op tap (census parity: the ops report like the module's would).
        SpuCoreFace(sound::SpuCore& spu, RamAccess iop_ram, OpTracer tap) : spu_(spu), iop_ram_(std::move(iop_ram)), tap_(std::move(tap)) {}
        uint32_t GetAddr(uint32_t entry) override;
        uint32_t VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop_addr, uint32_t spu_addr, uint32_t size) override;
        uint32_t VoiceTransStatus(uint32_t chan, uint32_t flag) override;
    private:
        sound::SpuCore& spu_;
        RamAccess iop_ram_;
        OpTracer tap_;
    };

    Snd989Server(uint32_t sid, Deps deps);
    Snd989Server(const Snd989Server&) = delete;
    Snd989Server& operator=(const Snd989Server&) = delete;

    void Call(const RpcRouter::CallRequest& req) override;
    // The face and the fallback for a (re)registration of this sid.
    void Rebind(Deps deps) { d_ = std::move(deps); }
    void SetAfterCall(std::function<void()> f) { after_call_ = std::move(f); }
    void SetCallTracer(CallTracer t) { call_tracer_ = std::move(t); }

    // The fnos this server answers natively (the decision is static per fno; a body may still decline
    // on state it cannot read -- e.g. a bank header of the 'SBlk' kind -- and the call is forwarded).
    static bool IsNativeFno(uint32_t fno);

    // One native body on raw request bytes. Returns true when served: `result` / `has_result` carry
    // what the module's shim would have stored in the reply buffer (nothing for a void shim).
    // `dry` = check the preconditions only (the state the body reads), perform no libsd op.
    // Exposed for the replay test.
    bool Serve(uint32_t fno, const uint8_t* body, uint32_t body_size, uint32_t& result, bool& has_result, bool dry = false);

    uint32_t Sid() const { return sid_; }
    uint64_t Served() const { return served_; }
    uint64_t Forwarded() const { return forwarded_; }
    uint64_t ServedFno(uint32_t fno) const { return fno < snd989::kFnoCount ? served_fno_[fno] : 0; }
    uint64_t BatchesServed() const { return batches_served_; }
    uint64_t BatchesForwarded() const { return batches_forwarded_; }
    // "fno10=n:fno26=n:fno90=n:fno91=n:batch=s/f"
    std::string Detail() const;

private:
    uint32_t Iop32(uint32_t addr, bool& ok) const;
    int32_t Iop16s(uint32_t addr, bool& ok) const;
    int8_t Iop8s(uint32_t addr, bool& ok) const;
    bool IopWrite32(uint32_t addr, uint32_t v);

    // The bodies (snd989_image.h documents each). `ok` false = cannot answer (forward).
    bool BodySystemParam(const uint8_t* body, uint32_t size, uint32_t& result);
    bool BodyBankSoundIsValid(const uint8_t* body, uint32_t size, uint32_t& result);
    bool BodyExternalCommand(const uint8_t* body, uint32_t size, uint32_t& result);
    bool BodyMoviePush(const uint8_t* body, uint32_t size, bool dry);
    bool BodyMovieReadPos(uint32_t& result, bool dry);

    // The whole round trip for a native fno (single or batch) through the module's reply buffer.
    bool ServeRoundTrip(const RpcRouter::CallRequest& req, const uint8_t* send, uint8_t reply_out[snd989::kReplyBytes]);
    void Forward(const RpcRouter::CallRequest& req);

    uint32_t sid_;
    Deps d_;
    CallTracer call_tracer_;
    std::function<void()> after_call_;
    uint64_t served_ = 0, forwarded_ = 0, batches_served_ = 0, batches_forwarded_ = 0;
    uint64_t served_fno_[snd989::kFnoCount] = {};
};

}  // namespace ps2r::title::sly2
