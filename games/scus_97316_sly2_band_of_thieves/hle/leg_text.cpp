#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

#include <cstdio>

namespace ps2r::title::sly2 {

std::string Hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

}  // namespace ps2r::title::sly2
