// native_iop.h -- the installer of Sly 2's native IOP / SPU2 path (title.native.iop, title.native.spu).
//
// NativeIop owns one Snd989Server (native/snd/) per 989snd server id and plugs them into the seams:
//   * IopHost::SetServerObserver -- when the hosted 989snd registers 0x123456 / 0x123457 (boot, and
//     again on a savestate restore), the native server takes the RpcRouter row and keeps the hosted
//     IopRpcServer as its fallback (the oracle);
//   * IopHost::Memory() -- the module's RAM: its reply buffer and the state words the native bodies read
//     (native/snd/snd989_image.h), and the IOP-side DMA buffers the movie push transfers from;
//   * the RPC trace file (NATIVEEE_RPC_TRACE), when open, gets one `NAT` row per round trip this path
//     answered or forwarded, beside the hosted rows.
// title.native.spu (requires iop) selects the direct SpuCore face for the native servers' own ops.
#pragma once

#include <cstdio>
#include <functional>
#include <memory>

#include "games/scus_97316_sly2_band_of_thieves/native/snd/native_snd_server.h"
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
        Snd989Server::RamAccess ee_ram;                                // MainMemory::Pointer(Phys(addr), size)
        Snd989Server::RamAccess iop_ram;                               // IopHost::Memory().Pointer(addr, size)
        std::function<void()> drain_kernel;                            // one IopKernel::Dispatch() pass (Snd989Server::Deps)
        std::FILE* rpc_trace = nullptr;   // the open NATIVEEE_RPC_TRACE file, or null
    };
    // Installs when knobs.iop is on; returns null when it is off (nothing touched).
    static std::unique_ptr<NativeIop> Install(const NativeKnobs& knobs, Deps deps);
    ~NativeIop();
    NativeIop(const NativeIop&) = delete;
    NativeIop& operator=(const NativeIop&) = delete;

    Snd989Server& Command() { return *command_; }
    Snd989Server& Bank() { return *bank_; }
    // The census detail: "fno10=..:fno26=..:fno90=..:fno91=..:batch=s/f:bank_fwd=n"
    void UpdateCensusDetail();

private:
    NativeIop(const NativeKnobs& knobs, Deps deps);
    Deps d_;
    bool direct_spu_ = false;
    std::unique_ptr<Snd989Server> command_, bank_;
};

}  // namespace ps2r::title::sly2
