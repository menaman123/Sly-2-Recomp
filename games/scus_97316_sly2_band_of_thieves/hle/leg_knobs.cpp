// games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.cpp -- LegKnobs::FromEnvironment, the title's one
// getenv site. The parse of each knob is exactly the one the composition applied at its use before the
// split: a bare presence test, strtoul base 10, strtoull, or the raw string handed on.
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"

#include <algorithm>
#include <cstdlib>

namespace ps2r::title::sly2 {

namespace {

std::optional<std::string> Text(const char* value) {
    if (!value) return std::nullopt;
    return std::string(value);
}

}  // namespace

LegKnobs LegKnobs::FromEnvironment() {
    LegKnobs k;
    k.watch = Text(std::getenv("NATIVEEE_WATCH"));
    if (const char* env = std::getenv("NATIVEEE_GS_VERTS")) k.gs_verts_key = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    if (const char* env = std::getenv("NATIVEEE_GS_SKIP_FROM")) k.gs_skip_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    k.gs_skip_class = Text(std::getenv("NATIVEEE_GS_SKIP_CLASS"));
    if (const char* env = std::getenv("NATIVEEE_DUMP_EVERY")) k.dump_every = std::max(1u, static_cast<uint32_t>(std::strtoul(env, nullptr, 10)));
    if (const char* env = std::getenv("NATIVEEE_DUMP_FROM")) k.dump_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    k.gif_trace = std::getenv("NATIVEEE_GIF_TRACE") != nullptr;
    k.vu1_mode = Text(std::getenv("NATIVEEE_VU1_MODE"));
    if (const char* env = std::getenv("NATIVEEE_MTVU")) {
        k.mtvu_set = true;
        k.mtvu_on = std::string(env) == "1";
    }
    k.ipu_dump = Text(std::getenv("NATIVEEE_IPU_DUMP"));
    k.ipu_out_dump = Text(std::getenv("NATIVEEE_IPU_OUT_DUMP"));
    k.isr_trace = std::getenv("NATIVEEE_ISR_TRACE") != nullptr;
    k.audio_dump = Text(std::getenv("NATIVEEE_AUDIO_DUMP"));
    k.pad_script = Text(std::getenv("NATIVEEE_PAD_SCRIPT"));
    k.pad_record = Text(std::getenv("NATIVEEE_PAD_RECORD"));
    k.key_script = Text(std::getenv("NATIVEEE_KEY_SCRIPT"));
    k.fn_trace = Text(std::getenv("NATIVEEE_FN_TRACE"));
    k.vu1_cap = Text(std::getenv("NATIVEEE_VU1_CAP"));
    k.gs_routes = Text(std::getenv("NATIVEEE_GS_ROUTES"));
    k.gs_rt_dump = Text(std::getenv("NATIVEEE_GS_RT_DUMP"));
    if (const char* env = std::getenv("NATIVEEE_TEX_DUMP")) {
        char* end = nullptr;
        k.tex_dump_from = static_cast<uint32_t>(std::strtoul(env, &end, 10));
        k.tex_dump_to = (end && *end == ',') ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 10)) : k.tex_dump_from;
    }
    if (const char* env = std::getenv("NATIVEEE_WATCHDOG_S")) k.watchdog_s = std::strtoull(env, nullptr, 10);
    k.ram_dump = Text(std::getenv("NATIVEEE_RAM_DUMP"));
    k.unpaced = std::getenv("NATIVEEE_UNPACED") != nullptr;
    return k;
}

}  // namespace ps2r::title::sly2
