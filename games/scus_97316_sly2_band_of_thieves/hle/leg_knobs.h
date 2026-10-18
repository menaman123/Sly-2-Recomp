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
    // NATIVEEE_GS_SKIP_FROM=<n>: with GS_VERTS, drop the traced frame's draws from index n on (0 = off).
    uint32_t gs_skip_from = 0;
    // NATIVEEE_GS_SKIP_CLASS=<c>[,<c>...]: m16 / fbmsk / t8h / rtblend / date (every frame).
    std::optional<std::string> gs_skip_class;
    // NATIVEEE_DUMP_EVERY=N (>= 1) / NATIVEEE_DUMP_FROM=M: which present keys --dump-frames writes.
    uint32_t dump_every = 1;
    uint32_t dump_from = 0;
    // NATIVEEE_GIF_TRACE=1: every GIF tag / register write to stderr.
    bool gif_trace = false;
    // NATIVEEE_VU1_MODE=interp|cached|block|aot on top of Config engine.vu1_mode.
    std::optional<std::string> vu1_mode;
    // NATIVEEE_MTVU=1|0: force the MTVU stack on / off in every mode (set = the env decides; "1" = on).
    bool mtvu_set = false;
    bool mtvu_on = false;
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
