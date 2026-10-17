// games/scus_97316_sly2_band_of_thieves/hle/leg_text.h -- the text helpers the composition's modules share
// (a free-function group, no class): Hex renders a value as 0x<hex> for the run.meta rows and the triage
// prints exactly as the single-file composition did.
#pragma once

#include <cstdint>
#include <string>

namespace ps2r::title::sly2 {

std::string Hex(uint64_t v);

}  // namespace ps2r::title::sly2
