// LegDisc: the disc image (--iso, else <game_files>/<disc_image_file>), the sector reader behind
// libcdvd (Sly 2 reads raw sectors with sceCdRead), and the stream reader.
//
// It also hooks the kernel's SifDmaTransfer id. The engine returns 0 for a list without the command
// attribute (attr & 4), which Sly 2's plain data pushes never set; its IOP boot code at 0x1a7630 polls
// `while (!sceSifSetDma(&t, 1))` and would spin forever, so the hook supplies an id for such lists.
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

    // Null when no disc image opened.
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
