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
    if (const char* env = std::getenv("NATIVEEE_SHADOW_SPLIT")) k.shadow_split = std::string(env) != "0";
    if (const char* env = std::getenv("NATIVEEE_GS_VERTS_CAP")) k.gs_verts_cap = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    if (const char* env = std::getenv("NATIVEEE_GS_SKIP_FROM")) k.gs_skip_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    if (const char* env = std::getenv("NATIVEEE_GS_SKIP_TO")) k.gs_skip_to = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    k.gs_skip_class = Text(std::getenv("NATIVEEE_GS_SKIP_CLASS"));
    if (const char* env = std::getenv("NATIVEEE_DUMP_EVERY")) k.dump_every = std::max(1u, static_cast<uint32_t>(std::strtoul(env, nullptr, 10)));
    if (const char* env = std::getenv("NATIVEEE_DUMP_FROM")) k.dump_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    k.gif_trace = std::getenv("NATIVEEE_GIF_TRACE") != nullptr;
    k.gif_trace_value = Text(std::getenv("NATIVEEE_GIF_TRACE"));
    k.vu1_mode = Text(std::getenv("NATIVEEE_VU1_MODE"));
    if (const char* env = std::getenv("NATIVEEE_MTVU")) {
        k.mtvu_set = true;
        k.mtvu_on = std::string(env) == "1";
    }
    if (const char* env = std::getenv("NATIVEEE_GIF_DRAIN")) {
        k.gif_drain_set = true;
        k.gif_drain_on = std::string(env) == "1";
    }
    if (const char* env = std::getenv("NATIVEEE_MTVU_EARLY_FINISH")) {
        k.early_finish_set = true;
        k.early_finish_on = std::string(env) == "1";
    }
    if (const char* env = std::getenv("NATIVEEE_MTVU_WAIT_PROBE")) {
        k.wait_probe_set = true;
        k.wait_probe_on = std::string(env) == "1";
    }
    if (const char* env = std::getenv("NATIVEEE_EE_YIELD_SPIN_GATE")) {
        k.yield_spin_gate_set = true;
        k.yield_spin_gate_on = std::string(env) == "1";
    }
    if (const char* env = std::getenv("NATIVEEE_MEM_LANE")) {
        k.mem_lane_set = true;
        k.mem_lane_on = std::string(env) == "1";
    }
    k.ipu_dump = Text(std::getenv("NATIVEEE_IPU_DUMP"));
    k.ipu_out_dump = Text(std::getenv("NATIVEEE_IPU_OUT_DUMP"));
    k.isr_trace = std::getenv("NATIVEEE_ISR_TRACE") != nullptr;
    k.idle_trace = std::getenv("NATIVEEE_IDLE_TRACE") != nullptr;
    k.audio_dump = Text(std::getenv("NATIVEEE_AUDIO_DUMP"));
    if (const char* env = std::getenv("NATIVEEE_AUDIO_DUMP_SOURCE")) k.audio_dump_mixer = std::string(env) == "mixer";
    k.spu_trace = Text(std::getenv("NATIVEEE_SPU_TRACE"));
    k.rpc_trace = Text(std::getenv("NATIVEEE_RPC_TRACE"));
    k.pad_script = Text(std::getenv("NATIVEEE_PAD_SCRIPT"));
    k.pad_record = Text(std::getenv("NATIVEEE_PAD_RECORD"));
    k.key_script = Text(std::getenv("NATIVEEE_KEY_SCRIPT"));
    k.lat_pulse = Text(std::getenv("NATIVEEE_LAT_PULSE"));
    k.fn_trace = Text(std::getenv("NATIVEEE_FN_TRACE"));
    k.fn_census = Text(std::getenv("NATIVEEE_FN_CENSUS"));
    k.vu0_trace = Text(std::getenv("NATIVEEE_VU0_TRACE"));
    k.vu1_cap = Text(std::getenv("NATIVEEE_VU1_CAP"));
    k.gs_routes = Text(std::getenv("NATIVEEE_GS_ROUTES"));
    k.gs_rt_dump = Text(std::getenv("NATIVEEE_GS_RT_DUMP"));
    k.gs_texres = Text(std::getenv("NATIVEEE_GS_TEXRES"));
    k.gs_capture = Text(std::getenv("NATIVEEE_GS_CAPTURE"));
    if (const char* env = std::getenv("NATIVEEE_GS_CONSUMER_LEVERS")) k.gs_consumer_levers = std::string(env) != "0";
    if (const char* env = std::getenv("NATIVEEE_GL_SHADER_BLEND")) k.gl_shader_blend = std::string(env) == "1";
    if (const char* env = std::getenv("NATIVEEE_GS_SKIP_DEPTH")) k.gs_skip_depth = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    if (const char* env = std::getenv("NATIVEEE_GS_QUEUE_STAGE")) k.gs_queue_stage = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    if (const char* env = std::getenv("NATIVEEE_TEX_DUMP")) {
        char* end = nullptr;
        k.tex_dump_from = static_cast<uint32_t>(std::strtoul(env, &end, 10));
        k.tex_dump_to = (end && *end == ',') ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 10)) : k.tex_dump_from;
    }
    if (const char* env = std::getenv("NATIVEEE_WATCHDOG_S")) k.watchdog_s = std::strtoull(env, nullptr, 10);
    k.ram_dump = Text(std::getenv("NATIVEEE_RAM_DUMP"));
    k.nav = Text(std::getenv("NATIVEEE_NAV"));
    k.warp = Text(std::getenv("NATIVEEE_WARP"));
    k.unpaced = std::getenv("NATIVEEE_UNPACED") != nullptr;
    return k;
}

}  // namespace ps2r::title::sly2
