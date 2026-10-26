// LegServices: the RPC servers the stock IOP modules answer (padman, mcserv, sdrdrv), with the memory
// card store, the pad protocol, the SPU2 core, the mixer and the sceSdRemote block-transfer model behind
// them. A sceSifBindRpc to one of their ids routes here instead of parking sif_server_unbound.
#pragma once

#include "engine/core/config/config.h"
#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/hle/mc_host_store.h"
#include "engine/mc/hle/mcserv_module.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/hle/padman_module.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/hle/sdr_remote.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"

namespace ps2r::title::sly2 {

class LegServices {
public:
    LegServices(const ps2r::Config& cfg, LegCore& core);
    LegServices(const LegServices&) = delete;
    LegServices& operator=(const LegServices&) = delete;

    RpcRouter rpc_router;
    McHostStore mc_store;
    McCommandModel mc_model;
    McservModule mcserv;
    PadProtocol pad_protocol;
    PadmanModule padman;
    ps2r::sound::SpuCore spu_core;
    ps2r::sound::Mixer mixer;
    ps2r::sound::hle::SdBlockTrans sd_block_trans;
    ps2r::sound::hle::SdrRemote sdr_remote;
};

}  // namespace ps2r::title::sly2
