// native_iop.h -- the installer of Sly 2's native IOP / SPU2 path (title.native.iop, title.native.spu).
//
// NativeIop owns one NativeSndRpcServer per 989snd server id and plugs them into the seams:
//   * IopHost::SetServerObserver -- when the hosted 989snd registers 0x123456 / 0x123457 (boot, and
//     again on a savestate restore), the native server takes the RpcRouter row and keeps the hosted
//     IopRpcServer as its fallback (the oracle);
//   * LibsdModule::SetOpTracer -- the native servers watch the libsd ops the FORWARDED calls perform
//     (what they learn the movie sound's voice / base from); the tap already installed (the census
//     trace) keeps firing;
//   * the RPC trace file (NATIVEEE_RPC_TRACE), when open, gets one `NAT` row per round trip this path
//     answered or forwarded, beside the hosted rows.
// title.native.spu (requires iop) selects the direct SpuCore face for the native servers' own ops.
#pragma once

#include <cstdio>
#include <functional>
#include <memory>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_snd_server.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_knobs.h"

class IopRpcServer;
class RpcRouter;
namespace ps2r::sound {
class SpuCore;
}
namespace ps2r::iop::hle {
class LibsdModule;
}

namespace ps2r::title::sly2 {

class NativeIop {
public:
    // IopHost::SetServerObserver's shape: the composition hands the setter in so this TU needs no IopHost.
    using ServerObserver = std::function<void(uint32_t sid, IopRpcServer& hosted)>;
    struct Deps {
        RpcRouter* router = nullptr;
        iop::hle::LibsdModule* libsd = nullptr;                        // IopHost::Libsd()
        std::function<void(ServerObserver)> set_server_observer;       // IopHost::SetServerObserver
        sound::SpuCore* spu = nullptr;
        NativeSndRpcServer::EeRam ee_ram;                              // MainMemory::Pointer(Phys(addr), size)
        std::FILE* rpc_trace = nullptr;   // the open NATIVEEE_RPC_TRACE file, or null
    };
    // Installs when knobs.iop is on; returns null when it is off (nothing touched).
    static std::unique_ptr<NativeIop> Install(const NativeKnobs& knobs, Deps deps);
    ~NativeIop();
    NativeIop(const NativeIop&) = delete;
    NativeIop& operator=(const NativeIop&) = delete;

    NativeSndRpcServer& Command() { return *command_; }
    NativeSndRpcServer& Bank() { return *bank_; }
    // The census detail: "fno91:<served>/<forwarded> movie=..."
    void UpdateCensusDetail();

private:
    NativeIop(const NativeKnobs& knobs, Deps deps);
    Deps d_;
    bool direct_spu_ = false;
    std::unique_ptr<NativeSndRpcServer> command_, bank_;
};

}  // namespace ps2r::title::sly2
