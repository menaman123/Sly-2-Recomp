// games/scus_97316_sly2_band_of_thieves/hle/leg_services.h -- LegServices: the RPC servers the stock IOP
// modules would answer (padman, mcserv, sdrdrv) in the title's play composition. Title-neutral and
// cheap; they matter once hle/sdk_bindings.tsv binds the sceSif rows (a sceSifBindRpc to one of these
// ids then routes here instead of parking sif_server_unbound). The memory card host store + command
// model behind the MCSERV face (and the libmc rows), the pad protocol + the padman face (the SDK-3
// command table; its module facts follow the padman the title loads, leg_iop.cpp), the SPU2 core, the
// mixer, the sceSdRemote block-transfer model and the SDRDRV face.
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
