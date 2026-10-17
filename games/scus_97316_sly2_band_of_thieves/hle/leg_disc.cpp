// games/scus_97316_sly2_band_of_thieves/hle/leg_disc.cpp -- LegDisc (leg_disc.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"

#include <cstdio>

#include "engine/core/verify/run_meta.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/syscall_table.h"

namespace ps2r::title::sly2 {

LegDisc::LegDisc(const ps2r::Config& cfg, const TitleProfile& title, ps2r::verify::VerifyHost& host)
    : iso_path(!cfg.paths.iso.empty() ? cfg.paths.iso : cfg.paths.game_files + "/" + title.disc_image_file),
      iso_open(!title.disc_image_file.empty() && iso_fs.Open(iso_path, &iso_error)),
      read_engine(iso_fs),
      stream_reader(iso_open ? &read_engine : nullptr) {
    if (!iso_open) std::fprintf(stderr, "nativeee: disc image: %s (sceCdRead answers 0)\n", iso_error.c_str());
    if (auto* meta = host.Meta()) {
        meta->Set("iso_path", iso_open ? iso_path : std::string());
        meta->Set("iso_open", iso_open ? "1" : "0");
    }
    // The kernel answers a SifDmaTransfer list with the transfer id (nonzero when queued; 0 only
    // while the SIF1 channel is busy); this hook supplies the id for lists without the command attribute.
    SyscallTable::Instance().SetSifDmaHook([this](CpuContext& ctx) {
        const int32_t count = ctx.cpuRegs.GPR.r[5].SL[0];
        if (ctx.cpuRegs.GPR.r[2].UL[0] == 0 && count > 0) ctx.cpuRegs.GPR.r[2].UD[0] = ++title_sif_dma_id;
    });
}

}  // namespace ps2r::title::sly2
