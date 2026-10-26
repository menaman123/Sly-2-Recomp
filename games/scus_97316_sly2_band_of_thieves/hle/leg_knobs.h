// LegKnobs: the NATIVEEE_* environment knobs, read once at the top of RunLeg. FromEnvironment is the
// title's only getenv call. Every knob is a debugging or replay aid, never a substitute for a modelled
// mechanism.
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
    // NATIVEEE_GS_VERTS_CAP=<n>: vertices printed per traced draw (default 64; shadow-volume draws have up
    // to 420).
    uint32_t gs_verts_cap = 64;
    // NATIVEEE_SHADOW_SPLIT=0: forward the stencil-shadow counter draws unsplit (hle/shadow_counter_split.h).
    bool shadow_split = true;
    // NATIVEEE_GS_SKIP_FROM=<n> [NATIVEEE_GS_SKIP_TO=<m>]: with GS_VERTS, drop the traced frame's draws with
    // index in [n, m) (SKIP_TO unset or 0 = to the end of the frame; SKIP_FROM 0 = off).
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
    // NATIVEEE_MTVU_EARLY_FINISH=1|0: override engine.mtvu_early_finish (raise GS FINISH at the EE's staging
    // point, defer the boundary, compose GIF_STAT). MTVU only.
    bool early_finish_set = false;
    bool early_finish_on = false;
    // NATIVEEE_MTVU_WAIT_PROBE=1|0: override engine.mtvu_wait_probe (probe the kernel object before idling
    // on the worker at a blocking wait). MTVU only.
    bool wait_probe_set = false;
    bool wait_probe_on = false;
    // NATIVEEE_EE_YIELD_SPIN_GATE=1|0: override engine.ee_yield_spin_gate.
    bool yield_spin_gate_set = false;
    bool yield_spin_gate_on = false;
    // NATIVEEE_MEM_LANE=1|0: force the harness mem lane (a 34 MB guest-RAM hash every 60 boundaries, ~40 ms
    // on the boundary thread). Default: on headless, off in play.
    bool mem_lane_set = false;
    bool mem_lane_on = false;
    // NATIVEEE_IPU_DUMP=<file> / NATIVEEE_IPU_OUT_DUMP=<file>: the toIPU feed / the fromIPU drain, appended.
    std::optional<std::string> ipu_dump;
    std::optional<std::string> ipu_out_dump;
    // NATIVEEE_ISR_TRACE=1: one stderr line per GS SIGNAL / FINISH event and per syscall entry with a
    // pending INTC cause.
    bool isr_trace = false;
    // NATIVEEE_IDLE_TRACE=1: one stderr line, with the guest thread table, per idle field (a vblank passed
    // with every guest thread blocked and the GS pipeline owing nothing).
    bool idle_trace = false;
    // NATIVEEE_AUDIO_DUMP=<file.wav>: 48 kHz stereo int16 WAV of the device stream, or with
    // NATIVEEE_AUDIO_DUMP_SOURCE=mixer (and always headless) the mixer's output per boundary.
    std::optional<std::string> audio_dump;
    bool audio_dump_mixer = false;
    // NATIVEEE_SPU_TRACE=<file.txt>: the SPU voice model's event record (engine VoiceTrace: key-on/off with the
    // programmed registers, loop ends, transfers, unmodelled services, a per-second level census per voice).
    std::optional<std::string> spu_trace;
    // NATIVEEE_RPC_TRACE=<file>: every EE -> IOP RPC round trip into a hosted module (IOP frame, sid, fno,
    // request bytes, reply pointer and bytes).
    std::optional<std::string> rpc_trace;
    // NATIVEEE_PAD_SCRIPT=<script text | file> / NATIVEEE_PAD_RECORD=<file> / NATIVEEE_KEY_SCRIPT=<steps>.
    std::optional<std::string> pad_script;
    std::optional<std::string> pad_record;
    std::optional<std::string> key_script;
    std::optional<std::string> lat_pulse;   // NATIVEEE_LAT_PULSE: input edges for the latency census
    // NATIVEEE_FN_TRACE=addr[,addr...]: entry / return prints around generated functions.
    std::optional<std::string> fn_trace;
    // NATIVEEE_FN_CENSUS=<file>: count calls of every generated function; one `addr calls` line each at exit.
    std::optional<std::string> fn_census;
    // NATIVEEE_VU0_TRACE=<file>:<pc>[,<pc>...]: the COP2 block before and after every VCALLMS of those micro
    // pcs, written at exit.
    std::optional<std::string> vu0_trace;
    // NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>]: vu1_probe.h.
    std::optional<std::string> vu1_cap;
    // NATIVEEE_GS_ROUTES=<key>[,<key>...]: the pipeline's per-draw route trace of those frames.
    std::optional<std::string> gs_routes;
    // NATIVEEE_GS_RT_DUMP=<key>,<fbp hex>[,cap=<n>][,at=<i>[+<i>...]][,noclass][,nofbmsk][,nopost]: dump one
    // frame's render target.
    std::optional<std::string> gs_rt_dump;
    // NATIVEEE_TEX_DUMP=<from>,<to>: every distinct texture decoded in the frames ending at present keys
    // [from, to], once each, to textures/<hash>.{ppm,alpha.pgm,json,win,clut}. tex_dump_to 0 = off.
    uint32_t tex_dump_from = 0, tex_dump_to = 0;
    // NATIVEEE_GS_TEXRES=<tbp hex>[,<cbp hex>] | '*': trace how the texture resolver composed textured reads
    // at that TEX0.TBP (and CLUT base), written with NATIVEEE_GS_ROUTES frames. '*' counts every read only.
    std::optional<std::string> gs_texres;
    // NATIVEEE_GS_CAPTURE=<file.gsstream>: record the GS consumer's input stream for tools/gs_replay and
    // verify/judge_render_throughput.py.
    std::optional<std::string> gs_capture;
    // NATIVEEE_GS_CONSUMER_LEVERS=0: turn off the renderer optimisations (deferred present capture, grab and
    // scanout pixel sharing, the hidden window's swap as a flush), for A/B comparison.
    bool gs_consumer_levers = true;
    // NATIVEEE_GS_SKIP_DEPTH=<1|2>: complete frames the pipelined queue may hold before dropping the oldest.
    // 0 = default (1).
    uint32_t gs_skip_depth = 0;
    // NATIVEEE_GS_QUEUE_STAGE=<n>: records the producer batches before entering the ring under one lock.
    // 1 = push each record at once; 0 = default (64).
    uint32_t gs_queue_stage = 0;
    // NATIVEEE_GL_SHADER_BLEND=1: blend every blended draw in the shader (no fixed-function path): the
    // per-pixel reference the Metal identity judge compares against.
    bool gl_shader_blend = false;
    // NATIVEEE_WATCHDOG_S=<s> (default 60; 0 = off) / NATIVEEE_RAM_DUMP=<file> at the watchdog park.
    uint64_t watchdog_s = 60;
    std::optional<std::string> ram_dump;
    // NATIVEEE_NAV=<item>[;...]: nav_probe.h (the position / camera probe, its trace, the RAM-dump hunt).
    std::optional<std::string> nav;
    // NATIVEEE_UNPACED=1: play mode keeps the boundary ticking inline (no wall-locked pacer).
    bool unpaced = false;
    // NATIVEEE_WARP=<episode>[,<job>]: rewrite a private card's save1 so Resume Game loads that episode.
    std::optional<std::string> warp;

    static LegKnobs FromEnvironment();
};

}  // namespace ps2r::title::sly2
