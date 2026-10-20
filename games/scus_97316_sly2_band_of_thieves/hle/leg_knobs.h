// games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h -- LegKnobs: the NATIVEEE_* environment knobs of
// the title composition, read ONCE at the top of RunLeg (LegKnobs::FromEnvironment is the title's only
// getenv site -- the engine's one-getenv rule applied to the title tree). Every knob is boot triage /
// replay plumbing, never a replacement for a modelled mechanism; the module that acts on a knob
// documents its grammar next to the use (leg_gs.cpp, leg_pad.cpp, leg_verify_slot.cpp ...).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ps2r::title::sly2 {

struct LegKnobs {
    // NATIVEEE_WATCH=<hexaddr>,<bytes>[;...][;max=<n>][;val=<hex>] -- store_watch.h (nullopt = unset);
    // NATIVEEE_WATCH=text[;<hexaddr>,<bytes>...][;every=<n>][;max=<n>][;ramdump=<file>] -- text_watch.h.
    std::optional<std::string> watch;
    // NATIVEEE_GS_VERTS=<key>: trace the draws of the frame ending at present key <key> (0 = off).
    uint32_t gs_verts_key = 0;
    // NATIVEEE_GS_VERTS_CAP=<n>: vertices printed per traced draw (default 64; the shadow-volume draws of
    // the stencil block carry up to 420).
    uint32_t gs_verts_cap = 64;
    // NATIVEEE_SHADOW_SPLIT=0: forward the stencil-shadow counter draws unsplit (hle/shadow_counter_split.h).
    bool shadow_split = true;
    // NATIVEEE_GS_SKIP_FROM=<n> [NATIVEEE_GS_SKIP_TO=<m>]: with GS_VERTS, drop the traced frame's draws with
    // index in [n, m) (SKIP_TO unset / 0 = to the end of the frame; SKIP_FROM 0 = off) -- the draw bisect.
    uint32_t gs_skip_from = 0;
    uint32_t gs_skip_to = 0;
    // NATIVEEE_GS_SKIP_CLASS=<c>[,<c>...]: m16 / fbmsk / t8h / rtblend / date (every frame).
    std::optional<std::string> gs_skip_class;
    // NATIVEEE_DUMP_EVERY=N (>= 1) / NATIVEEE_DUMP_FROM=M: which present keys --dump-frames writes.
    uint32_t dump_every = 1;
    uint32_t dump_from = 0;
    // NATIVEEE_GIF_TRACE=1 | tbp:<hex>: every GIF tag / register write to stderr (gif_trace_sink.h).
    bool gif_trace = false;
    std::optional<std::string> gif_trace_value;
    // NATIVEEE_VU1_MODE=interp|cached|block|aot on top of Config engine.vu1_mode.
    std::optional<std::string> vu1_mode;
    // NATIVEEE_MTVU=1|0: force the MTVU stack on / off in every mode (set = the env decides; "1" = on).
    bool mtvu_set = false;
    bool mtvu_on = false;
    // NATIVEEE_GIF_DRAIN=1|0: force the GIF drain thread (the GIF -> DrawIR decode off the EE) on / off
    // under the MTVU stack (set = the env decides; the default follows the DrawIR chain: on when the
    // consumer runs on its own thread or no video stack exists).
    bool gif_drain_set = false;
    bool gif_drain_on = false;
    // NATIVEEE_IPU_DUMP=<file> / NATIVEEE_IPU_OUT_DUMP=<file>: the toIPU feed / the fromIPU drain, appended.
    std::optional<std::string> ipu_dump;
    std::optional<std::string> ipu_out_dump;
    // NATIVEEE_ISR_TRACE=1: one stderr line per GS SIGNAL / FINISH event and per syscall entry with a
    // pending INTC cause.
    bool isr_trace = false;
    // NATIVEEE_AUDIO_DUMP=<file.wav>: a 48 kHz stereo int16 WAV of the audio -- the DEVICE stream (play
    // mode, the default) or, with NATIVEEE_AUDIO_DUMP_SOURCE=mixer (and always without a device: headless),
    // the MIXER's 800-frame output of every boundary (emulated-time audio, host speed independent).
    std::optional<std::string> audio_dump;
    bool audio_dump_mixer = false;
    // NATIVEEE_SPU_TRACE=<file.txt>: the SPU voice model's event record (engine VoiceTrace: key-on/off with the
    // programmed registers, loop ends, transfers, unmodelled services, a per-second level census per voice).
    std::optional<std::string> spu_trace;
    // NATIVEEE_PAD_SCRIPT=<script text | file> / NATIVEEE_PAD_RECORD=<file> / NATIVEEE_KEY_SCRIPT=<steps>.
    std::optional<std::string> pad_script;
    std::optional<std::string> pad_record;
    std::optional<std::string> key_script;
    // NATIVEEE_FN_TRACE=addr[,addr...]: entry / return prints around generated functions.
    std::optional<std::string> fn_trace;
    // NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>]: vu1_probe.h.
    std::optional<std::string> vu1_cap;
    // NATIVEEE_GS_ROUTES=<key>[,<key>...]: the pipeline's per-draw route trace of those frames.
    std::optional<std::string> gs_routes;
    // NATIVEEE_GS_RT_DUMP=<key>,<fbp hex>[,cap=<n>][,at=<i>[+<i>...]][,noclass][,nofbmsk][,nopost]: the target
    // dump of one frame (engine target_dump.h; parsed in leg_gs.cpp, armed by the Verify slot).
    std::optional<std::string> gs_rt_dump;
    // NATIVEEE_TEX_DUMP=<from>,<to>: the texture oracle dump -- every DISTINCT texture the resolver decodes
    // during the frames that END at present keys [from, to] (engine TextureDumpSink; written once each to
    // <dump dir or harness out>/textures/<hash>.{ppm,alpha.pgm,json,win,clut}; parsed in leg_gs.cpp, armed /
    // disarmed by the Verify slot). tex_dump_to 0 = off.
    uint32_t tex_dump_from = 0, tex_dump_to = 0;
    // NATIVEEE_GS_TEXRES=<tbp hex>[,<cbp hex>] | '*' (every read, no bytes): the resolver's source-composition trace (engine TexresTrace,
    // bytes kept) of the textured reads at that TEX0.TBP (and CLUT base), written with NATIVEEE_GS_ROUTES
    // as texres_<key>.txt + texres_<key>_<seq>_{win.bin,clut.bin,tex.ppm} (parsed in leg_gs.cpp).
    std::optional<std::string> gs_texres;
    // NATIVEEE_GS_CAPTURE=<file.gsstream>: the consumer-stream capture (engine GsStreamRecorder, in front of the
    // GS queue: draws + vertices, jobs with their upload pixels, frames with the CRTC snapshot, the EE image's
    // local-memory writes between records, the image at open) -- the renderer throughput harness's input
    // (tools/gs_replay stream; verify/judge_render_throughput.py). Wired in leg_gs.cpp.
    std::optional<std::string> gs_capture;
    // NATIVEEE_GS_CONSUMER_LEVERS=0: the consumer_b renderer levers OFF (the deferred present capture in play, the
    // grab -> scanout-deposit pixel sharing, the hidden window's swap as a flush) -- the A/B of one binary against
    // itself (leg_gs.cpp).
    bool gs_consumer_levers = true;
    // NATIVEEE_GL_SHADER_BLEND=1: the GL device blends EVERY blended draw through its exact shader arm (the
    // fixed-function route off) -- the per-pixel oracle the Metal identity judge compares against (leg_gs.cpp).
    bool gl_shader_blend = false;
    // NATIVEEE_WATCHDOG_S=<s> (default 60; 0 = off) / NATIVEEE_RAM_DUMP=<file> at the watchdog park.
    uint64_t watchdog_s = 60;
    std::optional<std::string> ram_dump;
    // NATIVEEE_NAV=<item>[;...]: nav_probe.h (the position / camera probe, its trace, the RAM-dump hunt).
    std::optional<std::string> nav;
    // NATIVEEE_UNPACED=1: play mode keeps the boundary ticking inline (no wall-locked pacer).
    bool unpaced = false;
    // NATIVEEE_WARP=<episode>[,<job>]: warp_tool.h (rewrite the private card's save1 before the boot so Resume
    // Game loads that episode's hub).
    std::optional<std::string> warp;

    // The ONE getenv site of the title: reads every knob above from the process environment.
    static LegKnobs FromEnvironment();
};

}  // namespace ps2r::title::sly2
