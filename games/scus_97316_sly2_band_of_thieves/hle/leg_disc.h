// games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h -- LegDisc: the disc of the title's play composition
// -- IsoFs over the image (--iso, else <game_files>/<disc_image_file>), the sector reader the libcdvd
// rows copy from (Sly 2 reads raw sectors with sceCdRead), the StreamReader, the run.meta iso_path= /
// iso_open= rows -- and the kernel's SifDmaTransfer id hook: the engine's syscall keeps its first title's
// rule (0 for a list without the command attribute, attr & 4) which Sly 2's plain data pushes never
// carry; the IOP-boot function 0x1a7630 polls `while (!sceSifSetDma(&t, 1))` and would spin forever, so
// this hook supplies the id for such lists (upstream candidate: the kernel rule in the engine).
#pragma once

#include <cstdint>
#include <string>

#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"
#include "engine/cdvd/stream_reader.h"
#include "engine/core/config/config.h"
#include "engine/core/verify/verify_host.h"
#include "engine/title/title_profile.h"

namespace ps2r::title::sly2 {

class LegDisc {
public:
    LegDisc(const ps2r::Config& cfg, const TitleProfile& title, ps2r::verify::VerifyHost& host);
    LegDisc(const LegDisc&) = delete;
    LegDisc& operator=(const LegDisc&) = delete;

    // The ReadEngine the libcdvd rows / the IOP host read from; null when no disc image opened.
    ReadEngine* Reader() { return iso_open ? &read_engine : nullptr; }

    IsoFs iso_fs;
    const std::string iso_path;
    std::string iso_error;
    const bool iso_open;
    ReadEngine read_engine;
    StreamReader stream_reader;
    uint32_t title_sif_dma_id = 0x1000;   // the ids the SifDma hook hands out
};

}  // namespace ps2r::title::sly2
