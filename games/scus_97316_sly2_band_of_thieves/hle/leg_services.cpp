// games/scus_97316_sly2_band_of_thieves/hle/leg_services.cpp -- LegServices (leg_services.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"

#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/sound/voice.h"

namespace ps2r::title::sly2 {

LegServices::LegServices(const ps2r::Config& cfg, LegCore& core)
    : mc_store(cfg.paths.mc_dir),
      mc_model(cfg.cdvd.mc_latency_polls),
      mcserv(mc_model, [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); }),
      pad_protocol([](uint32_t a, uint32_t v, int size) {
          if (size == 1) memory::write<uint8_t>(a, static_cast<uint8_t>(v));
          else if (size == 2) memory::write<uint16_t>(a, static_cast<uint16_t>(v));
          else memory::write<uint32_t>(a, v);
      }),
      padman(
          pad_protocol, [](uint32_t a) { return memory::read<uint32_t>(a); },
          [](uint32_t a, uint8_t v) { memory::write<uint8_t>(a, v); },
          [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); },
          [&domain = core.domain] { return domain.DomainTick(); }),
      mixer(spu_core),
      sd_block_trans(cfg.audio.sdr_block_ms, [this] { return mixer.FramesMixed(); }),
      sdr_remote(spu_core, sd_block_trans, [](uint32_t a) { return memory::read<uint32_t>(a); },
                 [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); }) {
    // The SPU2 voice resampler (audio.interpolation): linear by default, the hardware's 4-tap Gaussian opt-in.
    ps2r::sound::Voice::SetGaussian(cfg.audio.interpolation == ps2r::AudioInterpolation::Gaussian);
    rpc_router.RegisterServer(McservModule::kServerId, mcserv, "MCSERV");
    rpc_router.RegisterServer(PadmanModule::kCommandServerId, padman, "PADMAN");
    rpc_router.RegisterServer(PadmanModule::kDataServerId, padman, "PADMAN_DATA");
    rpc_router.RegisterServer(ps2r::sound::hle::SdrRemote::kServerId, sdr_remote, "SDRDRV");
}

}  // namespace ps2r::title::sly2
