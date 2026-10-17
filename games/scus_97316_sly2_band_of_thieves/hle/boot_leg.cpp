// games/scus_97316_sly2_band_of_thieves/hle/boot_leg.cpp -- the title's play composition
// (TitlePlayMain). It arms the engine's title-neutral mechanisms (memory + MMIO sink, INTC + timers,
// the SIF register block, the emulated time domain and boundary owner, the kernel syscalls and the
// SDK binding manifest), hosts the title's three embedded IOP modules on the engine's cooperative IOP
// kernel (iop_host.cpp: lgAud, 989snd, spiop recompiled at build time; the stock Sony modules are the
// engine's HLE), the hardware fallbacks (GS / GIF / VU / VIF / DMAC / IPU), the memory card and pad
// faces, loads the ELF image, installs the generated dispatch table and runs the recompiled code from
// the ELF entry. Three shapes of one leg:
//   * the headless harness leg (--headless): a counting GS sink, no GL, the neutral pad, muted; the
//     park / frames-cap census in run.meta (LegCloser) + a `census:` block on stdout;
//   * the headless dump (--headless --dump-frames <dir>): the generic GS renderer over GL into a
//     hidden window, the DrawIR queue in LOCKSTEP over the shared VRAM image (the frames lane is the
//     inline path's by construction), every present as f<key>.ppm / p<key>.ppm;
//   * play (a window): the engine's first-run launcher on a plain start (the disc image verified
//     against the TitleProfile's ELF SHA-256, extracted under the user data root), the SessionRuntime
//     window on main with the live keyboard / gamepad pad, the SDL audio device fed one SPU2 quantum
//     per 60 Hz field, the DrawIR queue PIPELINED over a SPLIT VRAM image with frame skip (the
//     renderer never blocks the game), and the clean shutdown: the window close / Escape / the
//     --frames cap cancel the guest workers (ThreadExitUnwind), the join ladder runs, the census is
//     written, RunLeg returns through its destructors.
// No per-title HLE: hle/sdk_bindings.tsv is the only title data consulted at run time (iop/iop_modules.h
// is layout data); the disc image (--iso, else <game_files>/<disc_image_file>) feeds the engine's
// libcdvd rows (Sly 2 reads raw sectors with sceCdRead). The env knobs (NATIVEEE_*) are boot triage.
//
// Shape: the first title's EeBootstrap cold-boot composition (private app/host/ee_bootstrap*.cpp),
// reduced to its title-neutral phases, in the same construction order so object lifetimes nest
// the same way. Entered from app/host/main.cpp through engine/title/title_play.h.
#include "engine/title/title_play.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/host/build_id.h"
#include "app/host/leg_closer.h"
#include "app/host/first_run.h"
#include "app/host/launcher.h"
#include "app/host/platform_input.h"
#include "app/host/session_runtime.h"
#include "app/host/session_workers.h"
#include "app/host/tick_thread.h"
#include "app/host/user_data.h"
#include "engine/cdvd/hle/libcdvd_bindings.h"
#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"
#include "engine/cdvd/stream_reader.h"
#include "engine/core/config/config.h"
#include "engine/core/config/config_loader.h"
#include "engine/core/log/log.h"
#include "engine/core/log/logger.h"
#include "engine/core/log/ring_log.h"
#include "engine/core/triage/triage_bundle.h"
#include "engine/core/verify/frame_cap.h"
#include "engine/core/verify/memory_checkpointer.h"
#include "engine/core/verify/run_meta.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/cpu/cpu_context_init.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/cpu/ps2_float.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/alarms.h"
#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/hle/thread_manager.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/elf_loader.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/ee/sched/ee_event_queue.h"
#include "engine/ee/sched/ee_run_token.h"
#include "engine/ee/sched/ee_yield_point.h"
#include "engine/ee/timing/boundary_protocol.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/frame_rendezvous.h"
#include "engine/ee/timing/game_boundary.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/dmac/dmac.h"
#include "engine/dmac/dmac_channel.h"
#include "engine/dmac/dmac_handler_registry.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/hle/gs_sink_fanout.h"
#include "engine/gif/hle/gs_state_sink.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/hle/ir/job_rec.h"
#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/queue/draw_ir_queue_consumer.h"
#include "engine/gs/hle/queue/draw_ir_thread_queue.h"
#include "engine/gs/hle/sce/gs_sync.h"
#include "engine/gs/state/gs_mmio_handler.h"
#include "engine/gs/state/gs_priv_regs.h"
#include "engine/gs/state/gs_state.h"
#include "engine/gs/state/vertex_kick.h"
#include "engine/ipu/ipu_device.h"
#include "engine/ipu/ipu_fifo_port.h"
#include "engine/mc/hle/libmc_bindings.h"
#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/hle/mc_host_store.h"
#include "engine/mc/hle/mcserv_module.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/hle/padman_module.h"
#include "engine/platform/host/platform.h"
#include "engine/platform/posix/crash_handler.h"
#include "engine/platform/sdl/audio_pull_ring.h"
#include "engine/platform/sdl/sdl_audio_sink.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "engine/sif/sif_state.h"
#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/hle/sdr_remote.h"
#include "engine/sound/mixer.h"
#include "engine/sound/spu_core.h"
#include "engine/title/title_profile.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_mmio_gate.h"
#include "engine/vu/macro/vu_macro_bridge.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu0/vu0_registry.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_dump.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/iop_host.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/video_stack.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/vu1_probe.h"
#include "games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h"

#ifndef PS2R_SLY2_SHA_FILEIO
#define PS2R_SLY2_SHA_FILEIO ""
#endif
#ifndef PS2R_SLY2_SHA_LGAUD
#define PS2R_SLY2_SHA_LGAUD ""
#endif
#ifndef PS2R_SLY2_SHA_SND989
#define PS2R_SLY2_SHA_SND989 ""
#endif
#ifndef PS2R_SLY2_SHA_SPIOP
#define PS2R_SLY2_SHA_SPIOP ""
#endif

namespace {

using ps2r::Config;
using ps2r::ConfigLoader;
using ps2r::LogLevelSetting;
using ps2r::log::Logger;
using ps2r::ringlog::RingLog;

constexpr int kMainThreadPriority = 0;   // KernlBoot adopts tid 1 at priority 0

std::string Hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

ps2r::log::Level ToLoggerLevel(LogLevelSetting s) {
    switch (s) {
        case LogLevelSetting::Quiet: return ps2r::log::Level::Quiet;
        case LogLevelSetting::Error: return ps2r::log::Level::Error;
        case LogLevelSetting::Warn: return ps2r::log::Level::Warn;
        case LogLevelSetting::Info: return ps2r::log::Level::Info;
        case LogLevelSetting::Narrate: return ps2r::log::Level::Narrate;
    }
    return ps2r::log::Level::Quiet;
}

bool FileExists(const std::string& path) {
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

// Where the ELF comes from, in order: --game-files <dir>/<elf_dir>/<elf_path> (the shipped
// layout), else the build's SHA-gated copy under <build>/generated/.../inputs (PS2R_TITLE_ELF_PATH,
// a compile definition from hle/CMakeLists.txt: this is a development build running from its tree).
std::string ResolveElf(const Config& cfg, const TitleProfile& title) {
    const std::string shipped = cfg.paths.game_files + "/" + title.elf_dir + "/" + title.elf_path;
    if (FileExists(shipped)) return shipped;
#ifdef PS2R_TITLE_ELF_PATH
    if (FileExists(PS2R_TITLE_ELF_PATH)) return PS2R_TITLE_ELF_PATH;
#endif
    return shipped;
}

// The leg proper. Every object lives on this frame in construction order (the first title's
// phase order); a park / the frames cap leave through std::_Exit after LegCloser::Close.
int RunLeg(const Config& cfg, ps2r::verify::VerifyHost& host, ps2r::verify::FrameCap& cap, const TitleProfile& title,
           const std::string& elf_path, const std::string& title_dir) {
    // --- memory + MMIO (the sink answers every unmodelled page idle; INTC/timers/SIF displace it) --
    MainMemory ram;
    Scratchpad spad;
    AddressMap map(ram, spad);
    MmioSink sink;
    MmioRouter mmio(sink);
    FastMem fast_mem(ram, map, mmio);
    RecompAbi::BindMemory(&fast_mem);
    // NATIVEEE_WATCH=<hexaddr>,<bytes>[;<hexaddr>,<bytes>...][;max=<n>][;val=<hex>]: print every EE store that
    // overlaps one of the guest ranges (thread, innermost shadow-stack frame + its ra, the caller
    // frame, address, width, old -> new bits). Boot triage only; needs a non-NDEBUG engine build
    // (FastMem's observer is compiled out of Release). DMA writes (fromSPR / IPU / VIF) do not pass
    // through FastMem: a watched quadword that changes without a line here was written by a device
    // or never written at all. The observer sees the hull of the ranges; the callback filters.
    struct WatchState {
        std::vector<std::pair<uint32_t, uint32_t>> ranges;   // [lo, hi) physical
        uint64_t printed = 0, max = 2000;
        std::vector<uint64_t> vals;                          // val=<hex> (repeatable): only stores of these values
    } watch_state;
    if (const char* env = std::getenv("NATIVEEE_WATCH")) {
        std::string spec(env);
        uint32_t hull_lo = 0xFFFFFFFFu, hull_hi = 0;
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t semi = spec.find(';', pos);
            if (semi == std::string::npos) semi = spec.size();
            const std::string item = spec.substr(pos, semi - pos);
            pos = semi + 1;
            if (item.rfind("max=", 0) == 0) { watch_state.max = std::strtoull(item.c_str() + 4, nullptr, 0); continue; }
            if (item.rfind("val=", 0) == 0) { watch_state.vals.push_back(std::strtoull(item.c_str() + 4, nullptr, 16)); continue; }
            char* end = nullptr;
            const uint32_t lo = static_cast<uint32_t>(std::strtoul(item.c_str(), &end, 16)) & 0x1FFFFFFFu;
            const uint32_t bytes = (end && *end == ',') ? static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 0)) : 16u;
            watch_state.ranges.emplace_back(lo, lo + bytes);
            hull_lo = std::min(hull_lo, lo);
            hull_hi = std::max(hull_hi, lo + bytes);
        }
        if (!watch_state.ranges.empty() && !FastMem::HasWriteObserver()) {
            FastMem::SetWriteObserver([](void* user, uint32_t addr, unsigned width, uint64_t old_bits, uint64_t new_bits) {
                auto* st = static_cast<WatchState*>(user);
                const uint32_t phys = addr & 0x1FFFFFFFu;
                bool hit = false;
                for (const auto& r : st->ranges) hit = hit || (phys < r.second && phys + width > r.first);
                if (!hit) return;
                if (!st->vals.empty()) {
                    const uint64_t mask = width >= 8 ? ~0ull : ((1ull << (8 * width)) - 1);
                    bool match = false;
                    for (uint64_t v : st->vals) match = match || (new_bits & mask) == (v & mask);
                    if (!match) return;
                }
                if (st->printed++ >= st->max) return;
                ShadowCallStack::Frame f0{}, f1{};
                ShadowCallStack::Top(0, f0);
                ShadowCallStack::Top(1, f1);
                std::fprintf(stderr, "watch: tid=%d fn=0x%08x ra=0x%08x caller=0x%08x addr=0x%08x w=%u old=0x%016llx new=0x%016llx\n",
                             ThreadManager::Instance().CurrentTid(), f0.func_addr, f0.ra, f1.func_addr, addr, width,
                             (unsigned long long)old_bits, (unsigned long long)new_bits);
            }, &watch_state, hull_lo, hull_hi);
            std::fprintf(stderr, "watch: %zu range(s), hull [0x%08x, 0x%08x) max=%llu\n", watch_state.ranges.size(), hull_lo, hull_hi,
                         (unsigned long long)watch_state.max);
        }
#ifdef NDEBUG
        std::fprintf(stderr, "watch: NDEBUG build -- FastMem compiles the observer out; use a RelWithDebInfo/-UNDEBUG build\n");
#endif
    }

    // --- time domain, boundary owner, INTC + timers, SIF registers -------------------------------
    Intc& intc = Intc::Instance();
    intc.Register(mmio);
    EmuDomain domain;
    if (cfg.time.tick_deterministic) domain.SetRegimePin(1);
    DerivedClocks clocks(domain);
    clocks.SetRtcFixed(cfg.game.rtc_fixed);
    SetLibscfClocks(&clocks);
    EeTimers timers(clocks, intc);
    timers.Register(mmio);
    SifState sif_state;
    sif_state.Register(mmio);
    timers.SetCtxProvider([] {
        return ThreadManager::Instance().Current() ? ThreadManager::Instance().Current()->ctx : nullptr;
    });
    TickSource tick_source;
    tick_source.SetPolicy(cfg.time.tick_deterministic ? TickSource::Policy::Deterministic : TickSource::Policy::Play);
    tick_source.SetMode(cfg.harness.headless ? TickSource::Mode::Freerun : TickSource::Mode::Realtime);
    FrameRendezvous rendezvous;
    std::atomic<bool> running{true};
    // Play mode = a window exists and the boundary is wall-paced (neither --headless nor
    // time.tick_sync): the SessionRuntime's window on main, the live pad, the GS pipeline over GL.
    // Headless with --dump-frames <dir>: the same GS pipeline into a hidden GL window, every
    // present written as <dir>/f<key>.ppm (the grab) + p<key>.ppm (the presented image). Headless
    // without a dump dir is the boot leg exactly as before: a counting sink, no GL.
    const bool play_mode = !cfg.harness.headless && !cfg.time.tick_sync;
    const std::string dump_frames_dir = ConfigLoader::Request().dump_frames_dir;
    const bool want_video = play_mode || !dump_frames_dir.empty();

    // --- the boundary: sequencer + the game-thread boundary owner + the Phase-3 steps ------------
    BoundarySequencer sequencer;
    GameBoundary game_boundary(ThreadManager::Instance(), sequencer);
    if (!game_boundary.Attached()) {
        std::fprintf(stderr, "nativeee: the game boundary did not attach\n");
        return 2;
    }
    BoundarySequencer::Phase3Deps deps;
    deps.source = &tick_source;
    deps.domain = &domain;
    deps.rendezvous = &rendezvous;
    deps.intc = &intc;
    deps.alarms = &Alarms::Instance();
    deps.timers = &timers;
    deps.queue = &EeEventQueue::Instance();
    deps.running = &running;
    sequencer.InstallPhase3Steps(deps);   // slots 0-3: rendezvous ticket, tick advance, event drain
    BoundaryProtocol protocol(tick_source, sequencer.Ticks());
    protocol.SetRunahead(static_cast<uint32_t>(cfg.engine.ee_runahead));

    // --- kernel clocks + seams --------------------------------------------------------------------
    clocks.InstallKernelClocks(SyscallTable::Instance(), protocol.WaitHook());
    GsSync::Bind(&sequencer, &clocks);
    auto guest_call = [](uint32_t addr, CpuContext& ctx) {
        DispatchTable::Invoke(addr, ctx);
        return true;
    };
    SyscallTable::Instance().SetGuestCall(guest_call);
    std::string first_syscalls;
    uint64_t syscall_count = 0;
    SyscallTable::Instance().SetTrace([&](int num, const char* name) {
        constexpr uint64_t kKeep = 40;
        if (syscall_count++ < kKeep)
            first_syscalls += (first_syscalls.empty() ? "" : ",") + std::string(name ? name : "?") + "(" +
                              std::to_string(num) + ")";
    });
    SyscallTable::Instance().BindIntcMask(intc.MaskWord());
    intc.SetGuestCall(guest_call);
    ThreadManager::Instance().InstallEventApplier();
    ThreadManager::Instance().SetGuestBody([](int, CpuContext& ctx) { DispatchTable::Invoke(ctx.cpuRegs.pc, ctx); });

    // --- the hardware fallbacks: GS / GIF / VU / VIF / DMAC / IPU (the first title's construction
    // order, app/host/ee_bootstrap_{devices,services}.cpp), each displacing its MmioSink row ----
    // GsState's DrawIR goes to a counting sink (run.meta gs_draws=) which, when a video stack is
    // wanted (play mode / --dump-frames), chains to the DrawIR thread queue -> the queue consumer
    // -> the generic DrawPipeline over GL (hle/video_stack.h: no class renderers, an empty
    // VramLayout). The GS privileged page (CSR / IMR / DISPFB ...) is the real register file, so
    // the game's FINISH handshake (CSR |= 2 W1C, GsPutIMR, GIF chain, WaitSema signalled by the GS
    // INTC handler) is driven by the modelled transfer: Dmac::StartChannel(GIF) -> ChainWalker ->
    // GifParser -> GsState::WriteRegister(FINISH) -> GsPrivRegs::Finish() -> the observer below ->
    // Intc kGs.
    ps2r::gs::GsState gs_state;
    gs_state.SetQuirks(ps2r::gs::VertexKickQuirks{title.quirks.round_sprite_mode});
    ps2r::title::sly2::VideoStack video;
    // Play mode with gs.queue=pipelined (the Config default): the SPLIT image -- the pipeline
    // runs on ITS OWN VramImage on the consumer thread; the EE keeps GsState's image for its
    // writes, marks and upload epoch, and every EE-side call on it is logged (VramMirror) and
    // replayed on the pipeline's image in stream order by the queue (draw_ir_thread_queue.h).
    // The headless dump / harness legs keep the LOCKSTEP shared image: their frames lane is
    // byte-identical to the inline path by construction, and the Verify slot reads a
    // complete grab. The first title's composition made the same split (private
    // ee_bootstrap_devices.cpp, gs.queue=pipelined).
    const bool gs_queue_split_image = play_mode && cfg.video.backend == ps2r::VideoBackend::Gl &&
                                      cfg.gs.queue == ps2r::GsQueue::Pipelined;
    std::unique_ptr<ps2r::gs::VramImage> pipeline_vram;
    if (gs_queue_split_image) pipeline_vram = std::make_unique<ps2r::gs::VramImage>();
    const bool video_live = want_video && cfg.video.backend == ps2r::VideoBackend::Gl &&
                            video.Create(cfg.video, "Sly 2 -- NativeEE (GS)", pipeline_vram ? *pipeline_vram : gs_state.Vram());
    if (want_video && !video_live) std::fprintf(stderr, "nativeee: no GL video stack (video=stub; see the log)\n");
    std::unique_ptr<ps2r::gs::DrawIrQueueConsumer> consumer;
    std::unique_ptr<ps2r::gs::DrawIrThreadQueue> gs_queue;
    // gs.queue=inline: the consumer runs on the pushing EE thread (the context re-bound per
    // thread); otherwise the DrawIR ring carries the stream to the consumer on its own host
    // thread, which owns the GL context. Headless (dump): LOCKSTEP -- every push returns once
    // consumed, the shared VramImage interleaving is the inline one. Play: PIPELINED over the
    // split image with FRAME SKIP (the clock contract, rule 2: the renderer never blocks the
    // game -- one pending frame, the older dropped; pushes never wait for ring room; the
    // boundary does not WaitFrame).
    const bool gs_queue_thread = video_live && cfg.gs.queue != ps2r::GsQueue::Inline;
    const bool gs_queue_lockstep = !gs_queue_split_image;
    if (video_live) {
        // The display grab when no target sits at the scanout base: the CRTC's own read --
        // decode the DISPFB tuple out of local memory (an uploaded movie frame the title points
        // DISPFB at without drawing shows); the most-recently-drawn target only when the tuple
        // is unknown.
        video.Pipe()->Scanout().SetGrabPolicy(ps2r::gs::ScanoutComposer::GrabPolicy::LocalMemory);
        auto crtc_now = [&gs_state] {
            using P = ps2r::gs::GsPrivRegs;
            const P& p = gs_state.Priv();
            return ps2r::gs::CrtcClassifier::Classify(p.Read(P::kPmode), p.Read(P::kDispfb1), p.Read(P::kDispfb2),
                                                      p.Read(P::kDisplay1), p.Read(P::kDisplay2));
        };
        ps2r::gs::DrawIrQueueConsumer::Hooks hooks;
        hooks.enter = [&video] { video.EnsureCurrent(); };
        if (gs_queue_thread) {
            hooks.upload_pixels = [&gs_queue] { return gs_queue->LastUploadPayload(); };
            hooks.crtc = [&gs_queue] { return gs_queue->CurrentFrameCrtc(); };
        } else {
            hooks.upload_pixels = [&gs_state] { return &gs_state.Xfer().LastUploadPixels(); };
            hooks.crtc = crtc_now;
        }
        hooks.window_resized = [&video](int w, int h) { video.OnWindowResized(w, h); };
        consumer = std::make_unique<ps2r::gs::DrawIrQueueConsumer>(*video.Pipe(), std::move(hooks));
        // Play: the frames-lane hash is taken where the grab is made (the consumer thread, at
        // the present scale); the wall-locked boundary reads the header + hash, never the pixels.
        if (!gs_queue_lockstep) consumer->SetFrameHashScale(video.Scale());
        if (gs_queue_thread) {
            ps2r::gs::DrawIrThreadQueue::Hooks qh;
            qh.upload_pixels = [&gs_state] { return &gs_state.Xfer().LastUploadPixels(); };
            qh.crtc = crtc_now;
            gs_queue = std::make_unique<ps2r::gs::DrawIrThreadQueue>(
                *consumer, std::move(qh),
                gs_queue_lockstep ? ps2r::gs::DrawIrThreadQueue::Policy::Lockstep : ps2r::gs::DrawIrThreadQueue::Policy::Pipelined,
                4096, pipeline_vram.get());
            if (pipeline_vram) {
                gs_state.Vram().SetMirror(gs_queue->ProducerMirror());
                // A local->local copy's source under frame skip: never wait -- the EE image is
                // authoritative for what the EE wrote; a GPU target's pixels come from the
                // consumer's last completed frame, staleness counted (gs_queue l2l_*=).
                gs_state.Xfer().SetLocalToLocalPrepare(
                    [&gs_queue](ps2r::gs::VramImage::Span source, ps2r::gs::VramImage& image) {
                        return gs_queue->ServeLocalToLocalSource(source, image);
                    });
                gs_queue->SetFrameSkip(true);
            }
            video.ReleaseCurrent();
            gs_queue->Start([&video] {
                ps2r::platform::host::PinCurrentThreadInteractive("ps2r-gs");
                video.EnsureCurrent();
            });
            PS2R_LOG(HOST, Info, "GS queue: consumer thread started (%s%s)", gs_queue_lockstep ? "lockstep, shared VRAM image" : "pipelined",
                     pipeline_vram ? ", split VRAM image, frame skip" : "");
        }
    }
    ps2r::gs::DrawIrSink* gs_sink_head =
        gs_queue ? static_cast<ps2r::gs::DrawIrSink*>(gs_queue.get()) : static_cast<ps2r::gs::DrawIrSink*>(consumer.get());
    // NATIVEEE_GS_VERTS=<key>: every draw of the frame that ENDS at present key <key> (the draws
    // between boundaries key-1 and key) to stderr with its raster state and vertices (boot triage).
    // NATIVEEE_GS_SKIP_FROM=<n> (boot triage, with NATIVEEE_GS_VERTS): the traced frame's draws
    // from index n on are NOT forwarded to the renderer (a draw-order bisect of that frame's picture).
    struct VertTraceSink : ps2r::gs::DrawIrSink {
        ps2r::gs::DrawIrSink* next = nullptr;
        std::atomic<bool> on{false};
        uint32_t skip_from = 0, index = 0;
        // NATIVEEE_GS_SKIP_CLASS=<c>[,<c>...] (boot triage, every frame): drop draws of a class --
        // m16 (a 16-bit FRAME over the buffers), fbmsk (a partial FBMSK other than alpha-only),
        // t8h (a PSMT8H / PSMT4HH / PSMT4HL read), rtblend (a textured sprite blending a buffer
        // with FIX), date (DATE-tested draws).
        bool skip_m16 = false, skip_fbmsk = false, skip_t8h = false, skip_rtblend = false, skip_date = false;
        bool Skipped(const ps2r::gs::DrawIr& d) const {
            if (skip_m16 && (d.raster.frame.psm == 2 || d.raster.frame.psm == 10)) return true;
            if (skip_fbmsk && d.raster.frame.fbmsk != 0 && d.raster.frame.fbmsk != 0xff000000u) return true;
            if (skip_t8h && d.tex_enabled && (d.tex.psm == 0x1b || d.tex.psm == 0x24 || d.tex.psm == 0x2c)) return true;
            if (skip_rtblend && d.tex_enabled && d.prim_type == 6 && d.alpha_blend && d.raster.alpha.c == 2 && d.tex.psm == 0) return true;
            if (skip_date && d.raster.test.date) return true;
            return false;
        }
        // Census gs_pages=: the 8 KB VRAM pages a host->local / local->local transfer wrote
        // (InvalidateTextures jobs: a1 = dbp bytes, a2 = bytes) and the pages textured draws
        // sample (TEX0 tbp0 / cbp) -- a texture page sampled but never written is the
        // "garbage strip" class (blacklevel notes: pages 448..486 all-zero at frame 600).
        // page_target: every page of the FRAME / ZBUF footprint a draw rendered into (base ..
        // the scissor's last row at FBW pixels of the PSM's byte width). A page sampled but only
        // ever written by draws is a render-to-texture target (gs_pages rtt=), not an orphan: the
        // Binocucom portraits are 128x128 PSMCT32 targets at 0x140000 / 0x150000 (Z 0x170000)
        // drawn ~1000 times a frame and sampled by one sprite each (pages_notes.md).
        std::array<uint32_t, 512> page_written{}, page_sampled{}, page_target{};
        void MarkTarget(uint32_t base, uint32_t fbw, uint8_t psm, uint32_t rows) {
            const uint32_t bpp = ((psm & 0xfu) == 2u || (psm & 0xfu) == 10u) ? 2u : 4u;
            const uint32_t first = (base >> 13u) & 511u;
            const uint32_t last = ((base + std::max(1u, rows * fbw * bpp) - 1u) >> 13u) & 511u;
            for (uint32_t pg = first; pg <= last && pg < 512; ++pg) ++page_target[pg];
        }
        void OnDraw(const ps2r::gs::DrawIr& d) override {
            if (Skipped(d)) return;
            const uint32_t rows = static_cast<uint32_t>(d.raster.scissor.y1) + 1u;
            MarkTarget(d.raster.frame.fbp, d.raster.frame.fbw, d.raster.frame.psm, rows);
            if (!d.raster.zbuf.zmsk) MarkTarget(d.raster.zbuf.zbp, d.raster.frame.fbw, d.raster.zbuf.psm, rows);
            if (d.tex_enabled) {
                const uint32_t tp = (d.tex.tbp >> 13u) & 511u, cp = (d.tex.cbp >> 13u) & 511u;
                ++page_sampled[tp];
                if (d.tex.psm == 0x13 || d.tex.psm == 0x14 || d.tex.psm == 0x1b || d.tex.psm == 0x24 || d.tex.psm == 0x2c) ++page_sampled[cp];
            }
            if (on.load(std::memory_order_relaxed)) {
                const uint32_t idx = index++;
                if (skip_from && idx >= skip_from) return;
                std::fprintf(stderr, "verts: seq=%u prim=%u tme=%d abe=%d fbp=0x%x fbw=%u psm=%u fbmsk=0x%x zbp=0x%x zpsm=%u zmsk=%d zte=%d ztst=%u ate=%d atst=%u aref=%u afail=%u scissor=%u,%u-%u,%u off=%.1f,%.1f tbp=0x%x tbw=%u tpsm=%u tw=%u th=%u tcc=%u tfx=%u cbp=0x%x cpsm=%u csm=%u csa=%u cld=%u fst=%d texa=%u/%u/%d alpha=%u%u%u%u/%u n=%u\n",
                             d.seq, d.prim_type, d.tex_enabled ? 1 : 0, d.alpha_blend ? 1 : 0, d.raster.frame.fbp, d.raster.frame.fbw,
                             d.raster.frame.psm, d.raster.frame.fbmsk, d.raster.zbuf.zbp, d.raster.zbuf.psm, d.raster.zbuf.zmsk ? 1 : 0,
                             d.raster.test.zte ? 1 : 0, d.raster.test.ztst, d.raster.test.ate ? 1 : 0, d.raster.test.atst, d.raster.test.aref,
                             d.raster.test.afail, d.raster.scissor.x0, d.raster.scissor.y0, d.raster.scissor.x1, d.raster.scissor.y1, d.off_x,
                             d.off_y, d.tex.tbp, d.tex.tbw, d.tex.psm, d.tex.tw, d.tex.th, d.tex.tcc, d.tex.tfx, d.tex.cbp, d.tex.cpsm, d.tex.csm,
                             d.tex.csa, d.tex.cld, d.tex.fst ? 1 : 0, d.tex.texa_ta0, d.tex.texa_ta1, d.tex.texa_aem ? 1 : 0, d.raster.alpha.a,
                             d.raster.alpha.b, d.raster.alpha.c, d.raster.alpha.d, d.raster.alpha.fix, d.vertex_count);
                for (size_t i = 0; i < d.verts.size() && i < 8; ++i)
                    std::fprintf(stderr, "verts:   v%zu xyz=%.1f,%.1f,%.0f stq=%g,%g,%g uv=%u,%u rgba=%02x%02x%02x%02x\n", i, d.verts[i].x, d.verts[i].y,
                                 d.verts[i].z, d.verts[i].s, d.verts[i].t, d.verts[i].q, d.verts[i].uv_u, d.verts[i].uv_v, d.verts[i].r, d.verts[i].g,
                                 d.verts[i].b, d.verts[i].a);
            }
            if (next) next->OnDraw(d);
        }
        void OnJob(const ps2r::gs::JobRec& j) override {
            if (j.type == static_cast<uint32_t>(ps2r::gs::JobKind::InvalidateTextures) && j.arg2) {
                const uint32_t first = (j.arg1 >> 13u) & 511u, last = ((j.arg1 + j.arg2 - 1) >> 13u) & 511u;
                for (uint32_t pg = first; pg <= last && pg < 512; ++pg) ++page_written[pg];
            }
            if (on.load(std::memory_order_relaxed))
                std::fprintf(stderr, "verts: job type=%u a1=0x%x a2=0x%x a3=0x%x a4=0x%x\n", j.type, j.arg1, j.arg2, j.arg3, j.arg4);
            if (next) next->OnJob(j);
        }
        void OnFrame(const ps2r::gs::JobRec& v) override { if (next) next->OnFrame(v); }
    } vert_trace;
    const uint32_t vert_trace_key = std::getenv("NATIVEEE_GS_VERTS") ? static_cast<uint32_t>(std::strtoul(std::getenv("NATIVEEE_GS_VERTS"), nullptr, 10)) : 0;
    const char* skip_class_env = std::getenv("NATIVEEE_GS_SKIP_CLASS");
    if (gs_sink_head) {   // always in the chain: the gs_pages= census counts every draw / transfer
        vert_trace.next = gs_sink_head;
        if (const char* env = std::getenv("NATIVEEE_GS_SKIP_FROM")) vert_trace.skip_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
        if (skip_class_env) {
            const std::string c = std::string(",") + skip_class_env + ",";
            vert_trace.skip_m16 = c.find(",m16,") != std::string::npos;
            vert_trace.skip_fbmsk = c.find(",fbmsk,") != std::string::npos;
            vert_trace.skip_t8h = c.find(",t8h,") != std::string::npos;
            vert_trace.skip_rtblend = c.find(",rtblend,") != std::string::npos;
            vert_trace.skip_date = c.find(",date,") != std::string::npos;
        }
        gs_sink_head = &vert_trace;
    }
    ps2r::gs::CountingDrawIrSink draw_sink(gs_sink_head);
    gs_state.SetSink(&draw_sink);
    // The queue stops (drains, joins) before the consumer and the video stack it draws into go
    // (the frame unwinds in reverse order; this guard runs first).
    struct GsQueueStop {
        std::unique_ptr<ps2r::gs::DrawIrThreadQueue>& q;
        ~GsQueueStop() { if (q) q->Stop(); }
    } gs_queue_stop{gs_queue};
    // --dump-frames <dir>: the Verify slot writes f<key>.ppm / p<key>.ppm there (hle/frame_dump.h).
    ps2r::title::sly2::FrameDump frame_dump;
    if (video_live && !frame_dump.Open(dump_frames_dir))
        std::fprintf(stderr, "nativeee: --dump-frames %s: cannot create the directory (no dumps)\n", dump_frames_dir.c_str());
    uint64_t grab_local_memory = 0, grab_fallback = 0, grabs = 0;   // census gs_grab_sources=
    // NATIVEEE_DUMP_EVERY=N (boot triage): write only every Nth present key (1 = all; long runs).
    // NATIVEEE_DUMP_FROM=M (boot triage): no dumps before present key M (a dense window late in a
    // long run without the hundreds of frames before it).
    uint32_t dump_every = 1, dump_from = 0;
    if (const char* env = std::getenv("NATIVEEE_DUMP_EVERY")) dump_every = std::max(1u, static_cast<uint32_t>(std::strtoul(env, nullptr, 10)));
    if (const char* env = std::getenv("NATIVEEE_DUMP_FROM")) dump_from = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    const auto dump_wanted = [dump_every, dump_from](uint32_t n) { return n >= dump_from && n % dump_every == 0; };
    ps2r::gs::GsMmioHandler gs_mmio(gs_state);
    mmio.Register(MmioRouter::kGsPrivBase, MmioRouter::kGsPrivEnd - MmioRouter::kGsPrivBase, gs_mmio, "GS_PRIV");
    GifRegs gif_regs;
    mmio.Register(GifRegs::kBase, GifRegs::kSize, gif_regs, "GIF");
    GsStateSink gs_state_sink(gs_state);
    // NATIVEEE_GIF_TRACE=1: every GIF tag / register write to stderr (boot triage only).
    struct GifTraceSink : GsSink {
        void BeginTag(const GifTag& tag, GifPath path) override {
            std::fprintf(stderr, "gif: tag path=%u nloop=%u flg=%u nreg=%u eop=%u prim=%u pre=%u\n", unsigned(path),
                         unsigned(tag.nloop), unsigned(tag.flg), unsigned(tag.nregs), unsigned(tag.eop), unsigned(tag.prim), unsigned(tag.pre));
        }
        void WriteRegister(uint8_t reg, uint64_t value) override { std::fprintf(stderr, "gif:   reg 0x%02x = 0x%016llx\n", reg, (unsigned long long)value); }
        void PackedRgbaq(uint8_t, uint8_t, uint8_t, uint8_t) override {}
        void PackedSt(float, float, float) override {}
        void PackedUv(uint16_t, uint16_t) override {}
        void PackedFog(uint8_t) override {}
        void KickVertex(float, float, float, uint8_t, bool) override { ++kicks; }
        void ImageData(uint64_t, uint64_t) override { ++image; }
        void EndOfPacket() override { std::fprintf(stderr, "gif:   eop (kicks=%llu image=%llu)\n", (unsigned long long)kicks, (unsigned long long)image); kicks = image = 0; }
        void FinishPath3() override { std::fprintf(stderr, "gif:   path3 finish\n"); }
        unsigned long long kicks = 0, image = 0;
    } gif_trace;
    GsSinkFanout gif_sink({&gs_state_sink});
    if (std::getenv("NATIVEEE_GIF_TRACE")) gif_sink.Add(&gif_trace);
    GifParser gif_parser(gif_sink, gif_regs);
    GifArbiter gif_arbiter(gif_parser, nullptr);
    VuUnit vu0(0);
    VuUnit vu1(1, &gif_arbiter, title.quirks.xgkick_defer);
    Vu0Registry vu0_registry;
    Vif vif(vu0, vu1, gif_arbiter, gif_regs);
    VifMmioGate vif_gate(vif);
    mmio.Register(Vif::kVif0Base, Vif::kRegsSize, vif_gate, "VIF0");
    mmio.Register(Vif::kVif1Base, Vif::kRegsSize, vif_gate, "VIF1");
    mmio.Register(Vif::kVif1FifoBase, Vif::kFifoSize, vif_gate, "VIF1_FIFO");
    map.RegisterHwWindow(0x11000000u, 0x1000u, vu0.state().micro, "VU0_MICRO");
    map.RegisterHwWindow(0x11004000u, 0x1000u, vu0.state().data, "VU0_DATA");
    map.RegisterHwWindow(0x11008000u, 0x4000u, vu1.state().micro, "VU1_MICRO");
    map.RegisterHwWindow(0x1100C000u, 0x4000u, vu1.state().data, "VU1_DATA");
    Dmac dmac(ram, spad, vif, gif_arbiter);
    mmio.Register(0x10008000u, 0x6064u, dmac, "DMAC");
    mmio.Register(0x1000F520u, 0x10u, dmac, "D_ENABLER");
    mmio.Register(0x1000F590u, 0x10u, dmac, "D_ENABLEW");
    dmac.SetGuestCall(guest_call);
    uint64_t dmac_int1_raises = 0;
    dmac.SetInterruptRaise([&dmac_int1_raises] { ++dmac_int1_raises; });
    // _EnableDmac / _DisableDmac are D_STAT.CIM on the hardware: the kernel word and the
    // register are one state (libmpeg enables channel 3 for its fromIPU completion handler).
    SyscallTable::Instance().SetDmacMaskSink([&dmac](int ch, bool enable) { dmac.SetChannelInterruptMask(ch, enable); });
    // The IPU register window + its two DMAC channels (ch4 RAM -> input FIFO, ch3 output FIFO ->
    // RAM; a short drain retries when output appears): the hardware the title's libmpeg drives.
    ps2r::ipu::IpuDevice ipu;
    mmio.Register(0x10002000u, ps2r::ipu::IpuDevice::kWindowBytes, ipu, "IPU");
    // The FIFO ports (IPU_out_FIFO 0x10007000 read, IPU_in_FIFO 0x10007010 write): libipu pushes
    // the quantiser matrices (sceIpuSetIQ/SetVQ) through the write port, not the DMAC.
    ps2r::ipu::IpuFifoPort ipu_fifo(ipu);
    mmio.Register(0x10007000u, ps2r::ipu::IpuFifoPort::kWindowBytes, ipu_fifo, "IPU_FIFO");
    // NATIVEEE_IPU_DUMP=<file>: every byte the toIPU channel feeds, appended (boot triage only).
    std::FILE* ipu_dump = nullptr;
    if (const char* dump = std::getenv("NATIVEEE_IPU_DUMP")) ipu_dump = std::fopen(dump, "wb");
    struct IpuDumpClose { std::FILE*& f; ~IpuDumpClose() { if (f) std::fclose(f); } } ipu_dump_close{ipu_dump};
    dmac.SetIpuFeedInput([&ipu, &ipu_dump](uint8_t* host, size_t bytes) {
        if (ipu_dump) std::fwrite(host, 1, bytes, ipu_dump);
        ipu.FeedInputFifo(host, bytes);
    });
    // NATIVEEE_IPU_OUT_DUMP=<file>: every byte the fromIPU channel drains (BDEC RAW16 / CSC RGB32), appended.
    std::FILE* ipu_out_dump = nullptr;
    if (const char* dump = std::getenv("NATIVEEE_IPU_OUT_DUMP")) ipu_out_dump = std::fopen(dump, "wb");
    struct IpuOutDumpClose { std::FILE*& f; ~IpuOutDumpClose() { if (f) std::fclose(f); } } ipu_out_dump_close{ipu_out_dump};
    dmac.SetIpuDrainOutput([&ipu, &ipu_out_dump](uint8_t* host, size_t bytes) {
        const size_t got = ipu.DrainOutputFifo(host, bytes);
        if (ipu_out_dump && got) std::fwrite(host, 1, got, ipu_out_dump);
        return got;
    });
    ipu.SetOutputReady([&dmac] {
        if (dmac.channel(static_cast<int>(DmacChannelId::IpuFrom)).chcr & DmacChcr::kStr)
            dmac.StartChannel(static_cast<int>(DmacChannelId::IpuFrom));
    });
    // The eight-quadword input FIFO: the toIPU channel moves what fits and resumes as the
    // IPU consumes (libmpeg reads D4_QWC and IPU_CTRL.IFC to find where the stream is).
    dmac.SetIpuInputRoom([&ipu] { return ipu.InputRoomBytes(); });
    ipu.SetInputRoomReady([&dmac] {
        auto& to = dmac.channel(static_cast<int>(DmacChannelId::IpuTo));
        if ((to.chcr & DmacChcr::kStr) && to.qwc != 0) dmac.StartChannel(static_cast<int>(DmacChannelId::IpuTo));
    });
    // COP2: the VU0 macro ops and CMSAR1 reach the units (unbound they park vu_micro_absent).
    VuMacroBridge::Bind(&vu0, &vu1, &vu0_registry);
    struct VuMacroUnbind { ~VuMacroUnbind() { VuMacroBridge::Unbind(); } } vu_macro_unbind;
    // The deferred VIF1 kick's observation points at the frame boundary / sceGsSyncPath.
    GsSync::BindDmac([&dmac] { dmac.ChaseVif1Tail(); }, [&dmac] { dmac.FlushDeferredVif1(); });
    // The kernel's DMAC handler records reach the DMAC's registry before every dispatch.
    auto install_dmac_handlers = [&dmac] {
        std::vector<DmacHandler> records;
        int next_id = 1;
        for (const SyscallTable::KernelHandler& k : SyscallTable::Instance().DmacHandlers()) {
            DmacHandler h;
            h.id = k.id;
            h.cause = k.cause;
            h.pc = k.pc;
            h.gp = k.gp;
            h.arg = k.arg;
            h.flag = k.flag;
            h.active = k.active;
            records.push_back(h);
            if (k.id >= next_id) next_id = k.id + 1;
        }
        dmac.handlers().Install(records, next_id);
    };
    // The GS interrupt: SIGNAL / FINISH count on GsPrivRegs as the general register is written
    // (on the kicking guest thread, so STAT is set in place); the IMR view is the kernel's
    // GsPutIMR word (syscall 113; bit 8 SIGMSK, bit 9 FINISHMSK). Delivered at the next
    // dispatch point: the syscall-entry hook below, a back-edge yield, or the boundary.
    uint64_t gs_irq_events = 0, gs_irq_masked = 0, gs_irq_raises = 0;
    // NATIVEEE_ISR_TRACE=1 (boot triage): one stderr line per GS SIGNAL / FINISH event -- the
    // kicking tid, the kernel's IMR word and whether the cause was raised or masked.
    static const bool isr_trace = std::getenv("NATIVEEE_ISR_TRACE") != nullptr;
    gs_state.Priv().SetInterruptObserver([&](uint32_t csr_event) {
        ++gs_irq_events;
        const bool masked = (SyscallTable::Instance().GsImr() & (static_cast<uint64_t>(csr_event) << 8)) != 0;
        if (isr_trace)
            std::fprintf(stderr, "gs-irq: tid=%d event=0x%x imr=0x%llx %s path=%d p1=%llu p2=%llu p3=%llu\n",
                         EeRunToken::Instance().CurrentTid(), csr_event,
                         static_cast<unsigned long long>(SyscallTable::Instance().GsImr()), masked ? "masked" : "raise",
                         static_cast<int>(gif_parser.ActivePath()),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path1)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path2)),
                         static_cast<unsigned long long>(gif_parser.Receives(GifPath::Path3)));
        if (masked) {
            ++gs_irq_masked;
            return;
        }
        intc.Raise(Intc::kGs);
        ++gs_irq_raises;
    });
    // The interrupt taken at the next kernel call: every syscall's entry dispatches the pending
    // enabled INTC causes (and the DMAC's INT1 handlers) on the calling thread before the
    // syscall's body runs -- a GS FINISH raised in-line by the kick above reaches the game's GS
    // handler before its WaitSema would block forever. Intc's in-dispatch guard keeps an ISR's own
    // kernel calls (iSignalSema) from recursing into the dispatch.
    // A BLOCKING kernel wait is also a DMA observation point (Dmac::ObserveAtKernelWait): the
    // thread that kicked a VIF1 chain / PATH3 upload and now sleeps for the FINISH handler's
    // semaphore gives the bus to the DMA; the parked chain is walked and the arbiter's PATH3
    // backlog reaches the GS. The FINISH the observer raises is then pending in INTC_STAT, and
    // an interrupt runs on WHICHEVER thread is running when it lands: if another thread is READY,
    // the waiter blocks and the ISR is taken at that thread's next dispatch point (its next
    // kernel call, back-edge yield or boundary) -- the DMA's latency shows up as the other
    // threads' run time, as on the console. Taking it here instead would complete the kick
    // instantaneously and return the waiter before it ever blocked: a high-priority render /
    // frame-pacing thread pair then never both wait and the lower-priority main thread starves
    // (Sly 2's level loader after the intro FMV). Only when nothing else is READY -- the kernel's
    // idle loop would take the interrupt -- is it dispatched here, before the block.
    // The entry LOOPS: a handler's exit (EeRunToken::IsrEnd) may hand the token to the thread it
    // readied; when this thread is re-elected it is still at this entry, about to block, and the
    // other threads counted it READY while they deferred -- so the causes raised meanwhile are its
    // to take now. Without the re-check they would stay pending with every thread asleep.
    uint64_t syscall_entry_isrs = 0, syscall_entry_isrs_deferred = 0;
    // NATIVEEE_ISR_TRACE also prints one line per syscall entry that finds an INTC cause
    // pending -- tid, syscall, the pending/mask words and the decision.
    SyscallTable::Instance().SetPreDispatch([&](int num, CpuContext& ctx) {
        const bool blocking = SyscallTable::IsBlockingWait(num);
        if (blocking) dmac.ObserveAtKernelWait();
        for (;;) {
            if (dmac.CheckInterrupt()) {
                install_dmac_handlers();
                dmac.DispatchInterrupt(ctx);
            }
            if (!intc.Pending()) break;
            EeRunToken& token = EeRunToken::Instance();
            const bool defer = blocking && token.AnyReadyBesides(token.CurrentTid());
            if (isr_trace)
                std::fprintf(stderr, "isr-trace: tid=%d syscall=%d stat=0x%x mask=0x%x %s\n", token.CurrentTid(), num,
                             intc.Stat(), intc.Mask(), defer ? "deferred" : "dispatch");
            if (defer) {
                ++syscall_entry_isrs_deferred;
                break;
            }
            const int ran = intc.DispatchPending(ctx);
            syscall_entry_isrs += static_cast<uint64_t>(ran);
            if (ran == 0) break;  // pending but refused (nested) or without a handler: nothing more to do here
        }
    });

    // --- the disc: IsoFs over the image, the sector reader the libcdvd rows copy from ------------
    IsoFs iso_fs;
    const std::string iso_path =
        !cfg.paths.iso.empty() ? cfg.paths.iso : cfg.paths.game_files + "/" + title.disc_image_file;
    std::string iso_error;
    const bool iso_open = !title.disc_image_file.empty() && iso_fs.Open(iso_path, &iso_error);
    if (!iso_open) std::fprintf(stderr, "nativeee: disc image: %s (sceCdRead answers 0)\n", iso_error.c_str());
    ReadEngine read_engine(iso_fs);
    StreamReader stream_reader(iso_open ? &read_engine : nullptr);
    if (auto* meta = host.Meta()) {
        meta->Set("iso_path", iso_open ? iso_path : std::string());
        meta->Set("iso_open", iso_open ? "1" : "0");
    }

    // The kernel answers a SifDmaTransfer list with the transfer id (nonzero when queued; 0 only
    // while the SIF1 channel is busy). The engine's syscall keeps its first title's rule -- 0 for a
    // list without the command attribute (attr & 4) -- which Sly 2's plain data pushes never carry:
    // the IOP-boot function 0x1a7630 polls `while (!sceSifSetDma(&t, 1))` and would spin forever.
    // This hook supplies the id for such lists (upstream candidate: the kernel rule in the engine).
    uint32_t title_sif_dma_id = 0x1000;
    SyscallTable::Instance().SetSifDmaHook([&title_sif_dma_id](CpuContext& ctx) {
        const int32_t count = ctx.cpuRegs.GPR.r[5].SL[0];
        if (ctx.cpuRegs.GPR.r[2].UL[0] == 0 && count > 0) ctx.cpuRegs.GPR.r[2].UD[0] = ++title_sif_dma_id;
    });

    // --- the RPC servers the stock IOP modules would answer (padman, mcserv, sdrdrv) --------------
    // Title-neutral and cheap; they matter once hle/sdk_bindings.tsv binds the sceSif rows (a
    // sceSifBindRpc to one of these ids then routes here instead of parking sif_server_unbound).
    RpcRouter rpc_router;
    McHostStore mc_store(cfg.paths.mc_dir);
    McCommandModel mc_model(cfg.cdvd.mc_latency_polls);
    McservModule mcserv(mc_model, [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); });
    rpc_router.RegisterServer(McservModule::kServerId, mcserv, "MCSERV");
    PadProtocol pad_protocol([](uint32_t a, uint32_t v, int size) {
        if (size == 1) memory::write<uint8_t>(a, static_cast<uint8_t>(v));
        else if (size == 2) memory::write<uint16_t>(a, static_cast<uint16_t>(v));
        else memory::write<uint32_t>(a, v);
    });
    const auto pad_now = [&domain] { return domain.DomainTick(); };
    PadmanModule padman(
        pad_protocol, [](uint32_t a) { return memory::read<uint32_t>(a); },
        [](uint32_t a, uint8_t v) { memory::write<uint8_t>(a, v); },
        [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); }, pad_now);
    rpc_router.RegisterServer(PadmanModule::kCommandServerId, padman, "PADMAN");
    rpc_router.RegisterServer(PadmanModule::kDataServerId, padman, "PADMAN_DATA");
    ps2r::sound::SpuCore spu_core;
    ps2r::sound::Mixer mixer(spu_core);
    ps2r::sound::hle::SdBlockTrans sd_block_trans(cfg.audio.sdr_block_ms, [&mixer] { return mixer.FramesMixed(); });
    ps2r::sound::hle::SdrRemote sdr_remote(spu_core, sd_block_trans, [](uint32_t a) { return memory::read<uint32_t>(a); },
                                           [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); });
    rpc_router.RegisterServer(ps2r::sound::hle::SdrRemote::kServerId, sdr_remote, "SDRDRV");

    // --- play mode: the audio device ----------------------------------------------------------------
    // The mixer's 800-frame output of every boundary is ALSO pushed into the SPSC ring the SDL
    // device callback pulls from (engine/platform/sdl/audio_pull_ring.h: nothing flows back, so
    // the audio lane is byte-identical with or without a device). The device opens in play mode
    // only, and only when audio.enabled (headless / deterministic legs stay muted, the rule in
    // sdl_audio_sink.h); audio.volume (0..100, the session's live volume) is the gain applied to
    // the ring's copy, never to the hashed buffer. Pad-with-tail (the clock contract, rule 4): a
    // late game tick repeats the previous quantum's tail -- never a gap, never a pitch change.
    ps2r::platform::sdl::AudioPullRing audio_ring;
    ps2r::platform::sdl::SdlAudioSink audio_sink;
    std::vector<float> audio_gain_buf;
    uint64_t audio_device_quanta = 0, audio_device_nonsilent = 0;   // census audio_device=
    uint64_t audio_pumped_tick = 0, audio_field_pumps = 0;           // the field pump (Verify slot)
    // NATIVEEE_AUDIO_DUMP=<file.wav> (triage): the DEVICE stream -- what the listener hears, pads
    // and trims included -- written as 48 kHz stereo int16 WAV from the ring's consumer tap (the
    // SDL device thread; the header is patched at close). Not the hashed lane.
    struct AudioDump {
        std::FILE* f = nullptr;
        uint64_t frames = 0;
        std::vector<int16_t> buf;
        static void Tap(void* ctx, const float* in, int n) {
            AudioDump* d = static_cast<AudioDump*>(ctx);
            d->buf.resize(static_cast<size_t>(n) * 2);
            for (size_t i = 0; i < d->buf.size(); ++i) {
                const float v = std::clamp(in[i], -1.0f, 1.0f);
                d->buf[i] = static_cast<int16_t>(v < 0 ? v * 32768.0f : v * 32767.0f);
            }
            std::fwrite(d->buf.data(), sizeof(int16_t), d->buf.size(), d->f);
            d->frames += static_cast<uint64_t>(n);
        }
        void Close() {
            if (!f) return;
            const uint32_t data_bytes = static_cast<uint32_t>(frames * 4), riff = 36 + data_bytes;
            const uint32_t rate = 48000, byte_rate = rate * 4;
            const uint16_t fmt_tag = 1, ch = 2, align = 4, bits = 16;
            const uint32_t fmt_len = 16;
            std::fseek(f, 0, SEEK_SET);
            std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
            std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&fmt_tag, 2, 1, f); std::fwrite(&ch, 2, 1, f);
            std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f);
            std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data_bytes, 4, 1, f);
            std::fclose(f);
            f = nullptr;
        }
    } audio_dump;
    if (play_mode && cfg.audio.enabled) {
        ps2r::platform::AudioSpec spec;   // 48 kHz stereo, 800 frames per quantum
        audio_ring.SetPadWithTail(true);
        audio_ring.Prime();
        if (const char* wav = std::getenv("NATIVEEE_AUDIO_DUMP")) {
            audio_dump.f = std::fopen(wav, "wb");
            if (audio_dump.f) {
                static const uint8_t zero_header[44] = {};
                std::fwrite(zero_header, 1, sizeof(zero_header), audio_dump.f);   // patched at close
                audio_ring.SetPullTap(&AudioDump::Tap, &audio_dump);
            }
        }
        if (!audio_sink.Open(spec, audio_ring)) PS2R_LOG(HOST, Warn, "audio device not opened: running without sound");
        else PS2R_LOG(HOST, Info, "audio device: %d Hz x%d, %d-frame buffer", audio_sink.DeviceSampleRate(), audio_sink.DeviceChannels(), audio_sink.DeviceBufferFrames());
    }

    // --- play mode: the live pad + the session window on main ---------------------------------
    // PlatformInput is constructed on main (its owner); the boundary's Sample() posts to main and
    // waits a bounded 3 ms. SessionRuntime owns the VISIBLE window + its own GL context on main
    // (opened after the video stack's context was released from main): Run() moves the cold boot
    // to a 1 GiB-stack game worker while main pumps SDL, presents the posted frames at
    // video.fps_cap and services the pad. The GS pipeline's presented image (VideoStack::Output,
    // on the consumer thread) is posted to it.
    std::unique_ptr<PlatformInput> live_input;
    std::unique_ptr<ps2r::app::SessionRuntime> session;
    if (play_mode && video_live) {
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of('/');
        const std::string exe_dir = slash == std::string::npos ? std::string(".") : exe.substr(0, slash);
        live_input = std::make_unique<PlatformInput>(PlatformInput::SettingsFrom(cfg, exe_dir));
        session = std::make_unique<ps2r::app::SessionRuntime>(live_input.get());
        std::string err;
        if (!session->Open(cfg.audio.volume, audio_sink.Running(), /*recording=*/false, std::string(), err, cfg.paths.mc_dir)) {
            std::fprintf(stderr, "nativeee: session window: %s\n", err.c_str());
            return 2;
        }
        session->ConfigurePresentation(cfg.video.vsync, cfg.video.fps_cap);
        session->SetWindowTitle(title.display_name.empty() ? title.serial : title.display_name);
        session->SetEventTrace(cfg.dev.log_level == LogLevelSetting::Narrate);
        // The consumer thread's presented image goes to the session window and, with
        // --dump-frames, every dump_every-th one is written as p<n>.ppm with n = the OUTPUT
        // frame ordinal (under frame skip the presents and the EE boundaries no longer pair 1:1,
        // so the play dump is keyed by the picture, not the boundary).
        video.SetOutput([&session, &video, &frame_dump, dump_wanted](std::vector<uint8_t> rgba, int w, int h) {
            const uint32_t n = static_cast<uint32_t>(video.OutputFrames());
            if (frame_dump.Enabled() && dump_wanted(n)) frame_dump.Write("p", n, w, h, rgba.data(), /*flip=*/true);
            session->PostFrame(std::move(rgba), w, h);
        });
    }
    const auto push_audio_ring = [&](const float* out) {
        if (!audio_sink.Running()) return;
        const float gain = static_cast<float>(std::clamp(session ? session->Volume() : cfg.audio.volume, 0, 100)) / 100.0f;
        ++audio_device_quanta;
        for (int i = 0; i < ps2r::sound::Mixer::kFramesPerVsync * 2; ++i)
            if (out[i] != 0.0f) { ++audio_device_nonsilent; break; }
        if (gain == 1.0f) {
            audio_ring.Push(out, ps2r::sound::Mixer::kFramesPerVsync);
        } else {
            audio_gain_buf.assign(out, out + ps2r::sound::Mixer::kFramesPerVsync * 2);
            for (float& v : audio_gain_buf) v *= gain;
            audio_ring.Push(audio_gain_buf.data(), ps2r::sound::Mixer::kFramesPerVsync);
        }
    };

    // --- the IOP arm: the title's embedded modules hosted on the engine's IOP kernel -----------
    // The EE's sceSifSetDma pushes land in IOP RAM; sceSifAllocIopHeap is the sysmem heap; a
    // sceSifLoadModuleBuffer boots the recompiled module whose .iopmod name matches (lgAud,
    // 989snd, spiop) or records the row of a natively served one. The 989snd RPC servers the
    // module registers are put on rpc_router by the host, so snd_Init's sceSifCallRpc reaches them.
    ps2r::title::sly2::IopHost::ExpectedHashes iop_hashes;
    iop_hashes.fileio = PS2R_SLY2_SHA_FILEIO;
    iop_hashes.lgaud = PS2R_SLY2_SHA_LGAUD;
    iop_hashes.snd989 = PS2R_SLY2_SHA_SND989;
    iop_hashes.spiop = PS2R_SLY2_SHA_SPIOP;
    ps2r::title::sly2::IopHost iop_host(cfg, ram, rpc_router, spu_core, iso_open ? &read_engine : nullptr, iop_hashes);
    SyscallTable::Instance().SetSifDmaCopy([&iop_host](uint32_t ee_src, uint32_t iop_dst, uint32_t size) {
        iop_host.SifDmaCopy(ee_src, iop_dst, size);
    });
    // The padman face follows the padman the title loads (padman.irx 4.10 on this disc: the SDK-3
    // command table, GET_MODVER 0x410); the row image's .bss is the actuator-DMA sink.
    padman.SetModuleFacts([&iop_host] {
        PadmanModule::ModuleFacts f;
        if (const auto* m = iop_host.Table().FindModule("padman")) {
            f.version = static_cast<uint16_t>(m->version);
            if (m->text_addr) {
                f.act_sink = m->text_addr + m->text_size + m->data_size;
                f.act_sink_size = m->bss_size;
            }
        }
        return f;
    });
    sequencer.SetStep(BoundarySequencer::Step::IopDispatch, [&iop_host](CpuContext& ctx) {
        iop_host.Table().PumpAtBoundary();
        SceSif::DeliverPendingCommands(ctx);   // SIF commands the IOP sent since the last boundary
        return true;
    });
    // Slot 4 (MaintDuties): the SIF RPC end callbacks. A sceSifCallRpc with an end function in
    // NOWAIT mode (libmc's sceMcXxx: the callback signals the semaphore sceMcSync waits on) is
    // queued by the router and runs here, in order, as a guest call on the boundary's context --
    // the engine's rule (rpc_router.h) and the proven composition's slot-4 duty.
    // Then the pad: one PadProtocol::Stamp per boundary -- the live host input in play mode, the
    // neutral pad (no buttons, centred sticks) otherwise, exactly what the console's padman
    // publishes every frame. Before this the leg never stamped, so the title saw no pad data.
    uint64_t rpc_end_callbacks = 0, pad_stamps = 0;
    // run.meta pad_served=: stamps carrying a press or a deflected stick (the live keyboard / gamepad,
    // the pad script, the key script -- whatever reached the title), and the last such sample.
    uint64_t pad_served_active = 0;
    std::string pad_served_last;
    // NATIVEEE_PAD_SCRIPT=<frame>:<buttons_hex>:<hold>[:<lx>,<ly>[,<rx>,<ry>]][,<frame>:...] (boot triage):
    // press `buttons` (libpad active-high order: CIRCLE 0x2000, CROSS 0x4000, START 0x0008, SQUARE
    // 0x8000, D-pad UP 0x0010 RIGHT 0x0020 DOWN 0x0040 LEFT 0x0080) and, when given, hold the sticks at
    // lx,ly[,rx,ry] (0..255, 128 = centre; e.g. 0,128 = left stick full left) for `hold` boundaries
    // starting at the `frame`-th pad stamp (one per boundary), over the live / neutral pad.
    // Steps overlap by OR (buttons) / last-wins (sticks). Parsed by comma tokens: a token with a
    // ':' starts a step, a bare number continues the previous step's stick tuple -- so a step
    // giving only lx,ly may be followed by another step (an earlier greedy read took the next
    // step's frame as rx and dropped the rest of the script).
    struct PadScriptStep { uint64_t frame; uint16_t buttons; uint64_t hold; bool sticks; uint8_t lx, ly, rx, ry; };
    std::vector<PadScriptStep> pad_script;
    if (const char* env = std::getenv("NATIVEEE_PAD_SCRIPT")) {
        unsigned stick_slot = 4;   // the next stick value to fill: 1 = ly, 2 = rx, 3 = ry, 4 = none
        for (const char* p = env; *p;) {
            const char* comma = std::strchr(p, ',');
            const std::string tok(p, comma ? static_cast<size_t>(comma - p) : std::strlen(p));
            if (tok.find(':') != std::string::npos) {
                char* e = nullptr;
                PadScriptStep st{};
                st.lx = st.ly = st.rx = st.ry = 0x80;
                stick_slot = 4;
                st.frame = std::strtoull(tok.c_str(), &e, 10);
                if (*e != ':') break;
                st.buttons = static_cast<uint16_t>(std::strtoul(e + 1, &e, 16));
                if (*e != ':') break;
                st.hold = std::strtoull(e + 1, &e, 10);
                if (*e == ':') {
                    st.sticks = true;
                    st.lx = static_cast<uint8_t>(std::strtoul(e + 1, &e, 10));
                    stick_slot = 1;
                }
                pad_script.push_back(st);
            } else if (!pad_script.empty() && pad_script.back().sticks && stick_slot < 4) {
                const uint8_t v = static_cast<uint8_t>(std::strtoul(tok.c_str(), nullptr, 10));
                PadScriptStep& st = pad_script.back();
                if (stick_slot == 1) st.ly = v;
                else if (stick_slot == 2) st.rx = v;
                else st.ry = v;
                ++stick_slot;
            } else {
                std::fprintf(stderr, "nativeee: NATIVEEE_PAD_SCRIPT: stray token '%s' (script truncated here)\n", tok.c_str());
                break;
            }
            if (!comma) break;
            p = comma + 1;
        }
    }
    // NATIVEEE_KEY_SCRIPT=<frame>:<sdl_scancode>:<hold>[,...] (boot triage, play mode): hold a KEYBOARD key
    // (SDL scancode: A 4, D 7, S 22, W 26, Space 44, Up 82 ...) from the `frame`-th pad stamp for `hold`
    // boundaries through PlatformInput::HoldKey -- the engine's held-key overlay on the kbm backend, so
    // the press goes through the REAL keyboard -> InputMapper (default_bindings.ini: WASD = left stick,
    // Space = cross ...) -> DualShock sample path, not the pad script's direct stamp.
    struct KeyScriptStep { uint64_t frame; int scancode; uint64_t hold; bool down = false; };
    std::vector<KeyScriptStep> key_script;
    if (const char* env = std::getenv("NATIVEEE_KEY_SCRIPT")) {
        for (const char* p = env; *p;) {
            char* e = nullptr;
            KeyScriptStep st{};
            st.frame = std::strtoull(p, &e, 10);
            if (*e != ':') break;
            st.scancode = static_cast<int>(std::strtol(e + 1, &e, 10));
            if (*e != ':') break;
            st.hold = std::strtoull(e + 1, &e, 10);
            key_script.push_back(st);
            if (*e != ',') break;
            p = e + 1;
        }
    }
    sequencer.SetStep(BoundarySequencer::Step::MaintDuties, [&](CpuContext& ctx) {
        rpc_end_callbacks += static_cast<uint64_t>(rpc_router.CompletePending(
            ctx, [](uint32_t addr, CpuContext& c) { DispatchTable::Invoke(addr, c); },
            [](uint32_t a) { return memory::read<uint32_t>(a); }));
        if (live_input)
            for (KeyScriptStep& st : key_script) {
                const bool want = pad_stamps >= st.frame && pad_stamps < st.frame + st.hold;
                if (want != st.down) {
                    live_input->HoldKey(st.scancode, want);
                    st.down = want;
                }
            }
        PadProtocol::Input in = live_input ? live_input->Sample() : PadProtocol::Input{};
        for (const PadScriptStep& st : pad_script)
            if (pad_stamps >= st.frame && pad_stamps < st.frame + st.hold) {
                in.buttons |= st.buttons;
                if (st.sticks) { in.lx = st.lx; in.ly = st.ly; in.rx = st.rx; in.ry = st.ry; }
            }
        if (in.buttons || in.lx != 0x80 || in.ly != 0x80 || in.rx != 0x80 || in.ry != 0x80) {
            ++pad_served_active;
            pad_served_last = std::to_string(pad_stamps) + ":" + Hex(in.buttons) + ":" + std::to_string(in.lx) + "," + std::to_string(in.ly) + "," +
                              std::to_string(in.rx) + "," + std::to_string(in.ry);
        }
        pad_protocol.Stamp(in, domain.DomainTick());
        ++pad_stamps;
        return true;
    });
    // Slot 0 (PresentRendezvous): the Phase-3 ticket, then the GS frame boundary -- the VSync
    // job the engine does not push itself (nobody else marks the EE game frame): the counting
    // sink hands it down the chain to the consumer (DrawPipeline::OnFrameEnd: the grab, the
    // display quad, the present). Lockstep / inline: the push returns once the frame was
    // consumed, so the Verify slot below reads a complete grab. Then CSR.VSINT. The GS SIGNAL /
    // FINISH causes are raised in-line by the interrupt observer below -- not re-raised here.
    uint64_t gs_frame_boundaries = 0;
    sequencer.SetStep(BoundarySequencer::Step::PresentRendezvous, [&](CpuContext&) {
        const uint32_t ticket = rendezvous.Issue();
        if (!rendezvous.WaitFor(ticket, running)) return false;
        ps2r::gs::JobRec vs{};
        vs.type = static_cast<uint32_t>(ps2r::gs::JobKind::VSync);
        vs.seq = gs_state.Seq();
        vs.src_frame = static_cast<uint32_t>(gs_frame_boundaries);
        vs.arg1 = 1;   // the EE game-frame boundary
        vs.vram_epoch = gs_state.Vram().UploadEpoch();
        draw_sink.OnFrame(vs);
        if (gs_queue && !sequencer.WallLocked()) gs_queue->WaitFrame(gs_queue->FrameSeq());
        ++gs_frame_boundaries;
        gs_state.Priv().SetVsint();
        return true;
    });
    // The SIF command path IOP -> EE: a hosted module's sceSifSendCmd lands in libkernl's command
    // buffer (the title's sceSifInitCmd static) and runs the handler the EE registered with
    // sceSifAddCmdHandler, as a guest call on the EE's context.
    SceSif::SetCmdBuffer(SceSif::CmdBuffer{ps2r::title::sly2::IopModules::kEeSifCmdBuffer,
                                           ps2r::title::sly2::IopModules::kEeSifCmdSlots,
                                           ps2r::title::sly2::IopModules::kEeSifCmdSlotBytes});
    SceSif::SetGuestCall([](uint32_t addr, CpuContext& ctx) { DispatchTable::Invoke(addr, ctx); });
    sequencer.SetStep(BoundarySequencer::Step::EeStreamRpc, [&iop_host](CpuContext&) {
        iop_host.Table().PumpAfterEeService();
        return true;
    });
    // The back-edge yield (recomp/CMakeLists.txt: --yield-backedges): a guest loop that reaches
    // no kernel call trips EeYieldPoint every kYieldBudget back-edges -- one field of emulated
    // time (the protocol's wait hook), alarms, timers, the pending ISRs, the event drain, then
    // the thbase preemption point. Refused while the boundary sequence is in flight.
    constexpr uint32_t kYieldBudget = EeYieldPoint::kDefaultBudget;
    {
        EeYieldPoint::Deps yd;
        yd.advance_field = [hook = protocol.WaitHook()] { hook(0); };
        yd.domain = &domain;
        yd.alarms = &Alarms::Instance();
        yd.timers = &timers;
        yd.intc = &intc;
        // The DMAC's INT1 handlers too: a guest loop spinning on a counter its channel-3
        // completion handler bumps (libmpeg's CSC chunks) has no kernel call to deliver it at.
        yd.dispatch_dmac = [&dmac, &install_dmac_handlers](CpuContext& ctx) {
            if (!dmac.CheckInterrupt()) return;
            install_dmac_handlers();
            dmac.DispatchInterrupt(ctx);
        };
        yd.queue = &EeEventQueue::Instance();
        yd.inhibit = [&sequencer] { return sequencer.InFlight() != 0; };
        yd.pump_devices = [&iop_host](CpuContext& ctx) {
            // The IOP's threads run on their own clock while the EE spins (an RPC status
            // poll into a hosted module needs the module to progress); its SIF commands land.
            iop_host.Table().PumpFromParkLoop();
            SceSif::DeliverPendingCommands(ctx);
        };
        yd.budget = kYieldBudget;
        EeYieldPoint::Instance().Bind(std::move(yd));
    }
    struct YieldUnbind { ~YieldUnbind() { EeYieldPoint::Instance().Unbind(); } } yield_unbind;

    // --- the ELF image + boot environment ----------------------------------------------------------
    {
        std::vector<uint8_t> file;
        ElfLoader::Image image;
        std::string err;
        if (!ElfLoader::ReadFile(elf_path, file, &err) || !ElfLoader::Load(file, ram, image, &err)) {
            std::fprintf(stderr, "nativeee: ELF %s: %s\n", elf_path.c_str(), err.c_str());
            return 2;
        }
        if (image.entry != title.entry) {
            std::fprintf(stderr, "nativeee: ELF entry 0x%08x != profile entry 0x%08x\n", image.entry, title.entry);
            return 2;
        }
        PS2R_LOG(HOST, Info, "ELF loaded: %s entry=0x%08x segments=%zu", elf_path.c_str(), image.entry,
                 image.segments.size());
        if (auto* meta = host.Meta()) {
            meta->Set("elf_path", elf_path);
            meta->Set("elf_entry", Hex(image.entry));
            meta->Set("elf_segments", std::to_string(image.segments.size()));
        }
    }
    BootEnv::Instance().Configure(title.boot);
    BootEnv::Instance().PreSeed();

    // --- generated dispatch + the SDK binding manifest ---------------------------------------------
    std::string iop_modules;          // run.meta iop_modules=: every module load answered, in order
    uint64_t iop_module_loads = 0;
    uint64_t boundary_site_hits = 0;  // run.meta boundary_site_hits=: WaitForVblank mirror entries
    Ps2Float::SetGuestThreadRounding();
    DispatchTable::Initialize();
    {
        SdkBindingManifest& manifest = SdkBindingManifest::Instance();
        manifest.RegisterBuiltins();
        GsSync::Register(manifest);
        // The IOP heap is the host's sysmem heap (counted by SceSif::Heap()); a
        // sceSifLoadModuleBuffer ("buffer@<iop addr> <args>") is answered by the host: the hosted
        // module whose .iopmod name matches is booted (relocated, entry run with its args, threads
        // dispatched to their first block), a natively served one becomes a row; the module id is
        // the table row. run.meta iop_modules= keeps the load order.
        SceSif::IopHeap iop_heap;
        iop_heap.alloc = [&iop_host](uint32_t size) { return iop_host.HeapAlloc(size); };
        iop_heap.free = [&iop_host](uint32_t addr) { iop_host.HeapFree(addr); };
        SceSif::SetModuleLoader([&iop_host, &iop_modules, &iop_module_loads](const std::string& path) {
            const int32_t id = iop_host.LoadModuleBuffer(path);
            iop_module_loads++;
            iop_modules += (iop_modules.empty() ? "" : ",") + path.substr(0, path.find(' ')) + "=" + std::to_string(id);
            PS2R_LOG(HOST, Info, "sceSifLoadModuleBuffer(%s) -> %d", path.c_str(), id);
            return id;
        });
        const int sif_rows = SceSif::Bind(manifest, rpc_router, sif_state, std::move(iop_heap), [](uint32_t handle) {
            if ((handle & 0xFFFF0000u) != 0xDEAD0000u) return 0u;
            const uint32_t low = handle & 0xFFFFu;
            return low ? (0x80000000u | low) : 0u;   // the SDK system servers keep their low half
        });
        if (!manifest.MarkWaitBody("libkernl.a", "sceSifRpcLoop", TSW_SLEEP)) return 2;
        const int cdvd_rows = LibcdvdBindings::Bind(manifest, iso_open ? &read_engine : nullptr, &clocks, &stream_reader);
        // The memory card: the libmc rows over the host store (cfg.paths.mc_dir/mc0 = a formatted
        // 8 MB card in slot 1); this libmc 3.0.0 passes sceMcGetInfo's fifth argument in t0.
        const int libmc_rows = LibmcBindings::Bind(manifest, &mc_model, &mc_store);
        LibmcBindings::SetFormatInT0(cfg.hle.libmc_format_in_t0);

        if (auto* meta = host.Meta()) {
            meta->Set("sif_rows", std::to_string(sif_rows));
            meta->Set("cdvd_rows", std::to_string(cdvd_rows));
            meta->Set("libmc_rows", std::to_string(libmc_rows));
        }

        std::vector<SdkBindingManifest::TitleRow> rows;
        std::string err;
        const std::string tsv = title_dir + "/" + title.sdk_bindings_file;
        if (!SdkBindingManifest::LoadTitleManifest(tsv, rows, &err)) {
            std::fprintf(stderr, "nativeee: manifest %s: %s\n", tsv.c_str(), err.c_str());
            return 2;
        }
        int traps = 0, unresolved = 0;
        const std::vector<SdkBindingManifest::Resolved> resolved = manifest.Resolve(rows);
        static std::deque<uint32_t> wait_entries;   // stable storage for the wait bodies' continuation proofs
        const int installed = manifest.Install(
            resolved, [](uint32_t a, SdkBindingManifest::Body b) { return DispatchTable::Install(a, b); },
            [](uint32_t a) { return DispatchTable::Capture(a); }, &traps, &unresolved,
            [](uint32_t addr, SdkBindingManifest::Body body, int wait_type) {
                wait_entries.push_back(addr);
                DispatchTable::Continuations proof;
                proof.entries = &wait_entries.back();
                proof.count = 1;
                proof.restart_wait = wait_type;
                return DispatchTable::GeneratedBody(addr, [addr, body = std::move(body)](CpuContext& ctx, uint32_t a) {
                    const bool pushed = ShadowCallStack::Push(addr, ctx.cpuRegs.GPR.r[31].UL[0]);
                    body(ctx, a);
                    if (pushed) ShadowCallStack::Pop();
                }, proof);
            });
        // The frame boundary site when the title links no sceGsSyncV (TitleProfile::Gs::vblank_wait_entry):
        // the engine's WaitForVblank mirror (GsSync::WaitForVblank -> BoundarySequencer::RunBoundary)
        // replaces the game's flag + WaitSema vsync wait; its VBLANK INTC handler still runs at the
        // boundary's TickAdvance (Intc::DispatchPending) and does the display flip.
        if (title.gs.vblank_wait_entry) {
            const bool ok = DispatchTable::Install(title.gs.vblank_wait_entry, [&boundary_site_hits](CpuContext& ctx, uint32_t) {
                ++boundary_site_hits;
                GsSync::WaitForVblank(ctx);
            });
            if (!ok) {
                std::fprintf(stderr, "nativeee: vblank_wait_entry 0x%08x is not a generated function start\n",
                             title.gs.vblank_wait_entry);
                return 2;
            }
        }
        // NATIVEEE_FN_TRACE=addr[,addr...]: wrap generated functions with an entry/return print
        // (a0..a3 at entry, v0 at return) -- boot triage only, never a replacement.
        if (const char* env = std::getenv("NATIVEEE_FN_TRACE")) {
            std::string list(env);
            size_t pos = 0;
            while (pos < list.size()) {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) comma = list.size();
                const uint32_t fn = static_cast<uint32_t>(std::strtoul(list.substr(pos, comma - pos).c_str(), nullptr, 16));
                pos = comma + 1;
                DispatchTable::Body original = DispatchTable::Capture(fn);
                if (!original) { std::fprintf(stderr, "fn-trace: 0x%08x is not a function start\n", fn); continue; }
                DispatchTable::Install(fn, [fn, original](CpuContext& ctx, uint32_t entry) {
                    std::fprintf(stderr, "fn-trace: 0x%08x(a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x t0=0x%08x t1=0x%08x) ra=0x%08x\n", fn,
                                 ctx.cpuRegs.GPR.r[4].UL[0], ctx.cpuRegs.GPR.r[5].UL[0], ctx.cpuRegs.GPR.r[6].UL[0],
                                 ctx.cpuRegs.GPR.r[7].UL[0], ctx.cpuRegs.GPR.r[8].UL[0], ctx.cpuRegs.GPR.r[9].UL[0],
                                 ctx.cpuRegs.GPR.r[31].UL[0]);
                    original(ctx, entry);
                    std::fprintf(stderr, "fn-trace: 0x%08x -> v0=0x%08x\n", fn, ctx.cpuRegs.GPR.r[2].UL[0]);
                });
            }
        }
        std::string not_starts;
        for (const auto& r : resolved)
            if (DispatchTable::IdOf(r.title.address) >= DispatchTable::FunctionCount())
                not_starts += (not_starts.empty() ? "" : ",") + r.title.name + "@" + Hex(r.title.address);
        PS2R_LOG(HOST, Info, "sdk manifest %s: %zu rows, %d installed (%d trap rows, %d not function starts: %s)",
                 tsv.c_str(), rows.size(), installed, traps, unresolved, not_starts.c_str());
        if (auto* meta = host.Meta()) {
            meta->Set("title", title.serial);
            meta->Set("functions", std::to_string(DispatchTable::FunctionCount()));
            meta->Set("sdk_rows", std::to_string(rows.size()));
            meta->Set("sdk_rows_installed", std::to_string(installed));
            meta->Set("sdk_rows_trap", std::to_string(traps));
            meta->Set("sdk_rows_not_fn_start", std::to_string(unresolved));
            meta->Set("sdk_rows_not_fn_start_list", not_starts);
        }
    }

    // --- the leg closer, the census rows every exit path writes, the park sink -------------------
    LegCloser closer(host, mmio);
    uint64_t boundaries = 0;
    // run.meta boundary_wall_ms= : the wall clock (ms since the leg's cold boot) at every 100th
    // boundary -- the frame rate of a WINDOW of the run ((b2 - b1) / (t2 - t1)), where wall_ms /
    // frames averages the FMV, the loads and the level together.
    const auto leg_t0 = std::chrono::steady_clock::now();
    std::string boundary_wall;
    // The shutdown's step census (run.meta shutdown= / the stderr line): the join ladder's steps
    // with their wall offsets, and the guest workers it had to abandon ("" = none).
    std::string shutdown_steps, guest_join_abandoned;
    // NATIVEEE_VU1_MODE=interp|cached|block (boot triage): the VU1 fallback tier (engine default Cached;
    // block = the pre-resolved straight-line block tier, VU1 only).
    if (const char* env = std::getenv("NATIVEEE_VU1_MODE"))
        vu1.dispatch().settings().vu1_mode = std::string(env) == "interp"  ? VuTierMode::Interp
                                             : std::string(env) == "block" ? VuTierMode::Block
                                                                            : VuTierMode::Cached;
    // NATIVEEE_VU1_CAP=<dir>:<frame>:<nth>[:<count>] (boot triage): Vu1Case + PATH1 packet capture of
    // VU1 kicks (hle/vu1_probe.h); the probe reads `boundaries` at every kick.
    ps2r::title::sly2::Vu1Probe vu1_probe;
    if (const char* env = std::getenv("NATIVEEE_VU1_CAP"))
        if (!vu1_probe.Install(env, vu1, gif_arbiter, &boundaries, &vif)) std::fprintf(stderr, "nativeee: NATIVEEE_VU1_CAP=%s: malformed or unmakeable (ignored)\n", env);
    // The main thread's context pointer, for the watchdog's park_pc (a racy read is fine: it
    // is a diagnostic of a spinning guest, never a guest-visible effect).
    std::atomic<CpuContext*> main_ctx{nullptr};
    // Every guest thread: status, what it waits on, priority, entry, live ra/pc (racy reads of
    // live contexts; a diagnostic). st: 1 RUN 2 READY 4 WAIT 8 SUSPEND 16 DORMANT; wait: 1 sleep
    // 2 sema 4 event flag.
    auto thread_census = [] {
        std::string threads;
        for (const auto& e : ThreadManager::Instance().SnapshotTable()) {
            const std::shared_ptr<ThreadInfo>& info = e.second;
            if (!info) continue;
            threads += (threads.empty() ? "" : ",") + std::to_string(e.first) + ":st=" + std::to_string(info->status) +
                       ":wait=" + std::to_string(info->wait_type) + "/" + std::to_string(info->wait_id) +
                       ":prio=" + std::to_string(info->current_priority) + ":entry=" + Hex(info->entry);
            if (const CpuContext* c = info->ctx)
                threads += ":ra=" + Hex(c->cpuRegs.GPR.r[31].UL[0]) + ":pc=" + Hex(c->cpuRegs.pc);
        }
        return threads;
    };
    auto write_census = [&] {
        if (auto* meta = host.Meta()) {
            meta->Set("threads", thread_census());
            meta->Set("first_syscalls", first_syscalls);
            meta->Set("syscall_count", std::to_string(syscall_count));
            meta->Set("boundaries", std::to_string(boundaries));
            meta->Set("domain_tick", std::to_string(domain.DomainTick()));
            const SceSif::HeapCensus heap = SceSif::Heap();
            meta->Set("iop_heap_allocs", std::to_string(heap.allocs));
            meta->Set("iop_heap_refused", std::to_string(heap.refused));
            meta->Set("sif_init_rpc_calls", std::to_string(SceSif::InitRpcCalls()));
            meta->Set("sif_dma_bytes", std::to_string(SyscallTable::Instance().SifDmaBytes()));
            meta->Set("iop_module_loads", std::to_string(iop_module_loads));
            meta->Set("iop_modules", iop_modules);
            meta->Set("sif_bound", rpc_router.BoundCensus());
            meta->Set("sif_cmds_delivered", std::to_string(SceSif::CommandsDelivered()));
            meta->Set("sif_cmds_dropped", std::to_string(SceSif::CommandsDropped()));
            meta->Set("sif_cmds_pending", std::to_string(SceSif::PendingCommands()));
            meta->Set("rpc_end_callbacks", std::to_string(rpc_end_callbacks) + ":pending=" + std::to_string(rpc_router.PendingCount()));
            iop_host.WriteCensus(meta);
            meta->Set("cd_read_calls", std::to_string(LibcdvdBindings::ReadCalls()));
            meta->Set("boundary_site", title.gs.vblank_wait_entry ? Hex(title.gs.vblank_wait_entry) : "sceGsSyncV");
            meta->Set("boundary_site_hits", std::to_string(boundary_site_hits));
            meta->Set("sdk_rows_hit", std::to_string(SdkBindingManifest::Instance().HitTotal()));
            meta->Set("sdk_rows_hit_list", SdkBindingManifest::Instance().HitList());
            meta->Set("ee_yield", EeYieldPoint::Instance().Stats().Format());
            meta->Set("ee_yield_budget", std::to_string(kYieldBudget));
            meta->Set("vblank_waits", std::to_string(EeRunToken::Instance().VblankWaits()) + ":idle=" +
                                          std::to_string(EeRunToken::Instance().VblankWaitsIdle()));
            meta->Set("syscall_entry_isrs", std::to_string(syscall_entry_isrs) + ":deferred=" + std::to_string(syscall_entry_isrs_deferred));
            meta->Set("intc_raises", std::to_string(intc.RaiseCount()));
            meta->Set("intc_dispatches", std::to_string(intc.DispatchCount()));
            meta->Set("intc_nested_refusals", std::to_string(intc.NestedRefusals()));
            meta->Set("gs_irq", "events=" + std::to_string(gs_irq_events) + ":masked=" + std::to_string(gs_irq_masked) +
                                    ":raises=" + std::to_string(gs_irq_raises));
            meta->Set("gs_priv_writes", std::to_string(gs_state.Priv().Writes()));
            {
                // gs_pages=written=<n>:sampled=<n>:sampled_never_written=<n>:<page ranges, draws>:rtt=<ranges>:targets=<pages>
                // rtt= : pages sampled and never transferred to but inside a drawn FRAME / ZBUF footprint
                // (render-to-texture; served from the live target). The orphan ranges exclude them.
                uint32_t written = 0, sampled = 0, orphan = 0;
                std::string ranges, rtt;
                const auto orphan_at = [&](uint32_t pg) { return vert_trace.page_sampled[pg] && !vert_trace.page_written[pg] && !vert_trace.page_target[pg]; };
                const auto rtt_at = [&](uint32_t pg) { return vert_trace.page_sampled[pg] && !vert_trace.page_written[pg] && vert_trace.page_target[pg]; };
                for (uint32_t pg = 0; pg < 512; ++pg) {
                    written += vert_trace.page_written[pg] != 0;
                    sampled += vert_trace.page_sampled[pg] != 0;
                    if (orphan_at(pg)) {
                        ++orphan;
                        uint32_t end = pg, draws = 0;
                        while (end < 512 && orphan_at(end)) draws += vert_trace.page_sampled[end++];
                        ranges += ":" + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(draws);
                        orphan += end - 1 - pg;
                        pg = end - 1;
                    } else if (rtt_at(pg)) {
                        uint32_t end = pg, draws = 0;
                        while (end < 512 && rtt_at(end)) draws += vert_trace.page_sampled[end++];
                        rtt += (rtt.empty() ? "" : ",") + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(draws);
                        pg = end - 1;
                    }
                }
                std::string targets;
                for (uint32_t pg = 0; pg < 512; ++pg) {
                    if (!vert_trace.page_target[pg]) continue;
                    uint32_t end = pg;
                    while (end < 512 && vert_trace.page_target[end]) ++end;
                    targets += (targets.empty() ? "" : ",") + std::to_string(pg) + "-" + std::to_string(end - 1) + "x" + std::to_string(vert_trace.page_target[pg]);
                    pg = end - 1;
                }
                meta->Set("gs_pages", "written=" + std::to_string(written) + ":sampled=" + std::to_string(sampled) +
                                          ":sampled_never_written=" + std::to_string(orphan) + ranges + ":rtt=" + rtt + ":targets=" + targets);
            }
            meta->Set("gs_csr", Hex(gs_state.Priv().Read(ps2r::gs::GsPrivRegs::kCsr)));
            meta->Set("gs_draws", std::to_string(draw_sink.Draws()));
            meta->Set("gs_frames", std::to_string(draw_sink.Frames()));
            meta->Set("video", video_live ? "gl" : "stub");
            if (video_live) {
                meta->Set("gl_renderer", video.Renderer());
                meta->Set("gl_version", video.GlVersion());
                meta->Set("gs_window", std::to_string(video.WindowW()) + "x" + std::to_string(video.WindowH()) +
                                           ":rebinds=" + std::to_string(video.Rebinds()) +
                                           ":output_frames=" + std::to_string(video.OutputFrames()));
                if (consumer) {
                    const ps2r::gs::DrawIrQueueConsumer::Counters& c = consumer->Count();
                    meta->Set("gs_consumer", "draws=" + std::to_string(c.draws) + ":jobs=" + std::to_string(c.jobs) +
                                                 ":frames=" + std::to_string(c.frames) + ":uploads=" + std::to_string(c.uploads) +
                                                 ":uploads_dropped=" + std::to_string(c.uploads_dropped) +
                                                 ":resizes=" + std::to_string(c.resizes) + ":presents=" + std::to_string(c.presents) +
                                                 ":grab_empty=" + std::to_string(c.grab_empty));
                }
                if (gs_queue) {
                    const ps2r::gs::DrawIrThreadQueue::Counters& q = gs_queue->Count();
                    meta->Set("gs_queue", std::string(gs_queue->policy() == ps2r::gs::DrawIrThreadQueue::Policy::Lockstep ? "lockstep" : "pipelined") +
                                              (gs_queue->FrameSkip() ? ":frame_skip" : "") +
                                              ":enqueued=" + std::to_string(q.enqueued) + ":consumed=" + std::to_string(q.consumed) +
                                              ":frames=" + std::to_string(q.frames) + ":frames_dropped=" + std::to_string(q.frames_dropped) +
                                              ":frame_waits=" + std::to_string(q.frame_waits) +
                                              ":max_depth=" + std::to_string(q.max_depth) + ":payload_bytes=" + std::to_string(q.payload_bytes) +
                                              ":vram_ops=" + std::to_string(q.vram_ops) + ":snapshots=" + std::to_string(q.snapshots) +
                                              ":l2l=" + std::to_string(q.l2l_ee_served) + "/" + std::to_string(q.l2l_snapshot_served) + "/" +
                                              std::to_string(q.l2l_stale_max) + "/" + std::to_string(q.l2l_unserved) +
                                              ":faulted=" + (gs_queue->Faulted() ? "1" : "0"));
                } else {
                    meta->Set("gs_queue", "inline");
                }
                meta->Set("gs_grab_sources", "grabs=" + std::to_string(grabs) + ",local_memory=" + std::to_string(grab_local_memory) +
                                                 ",fallback=" + std::to_string(grab_fallback));
                meta->Set("dump_frames", frame_dump.Enabled() ? frame_dump.Dir() + ":written=" + std::to_string(frame_dump.Written()) +
                                                                     ":failed=" + std::to_string(frame_dump.Failed())
                                                               : std::string("off"));
            }
            meta->Set("pad_stamps", std::to_string(pad_stamps));
            meta->Set("pad_served", "active=" + std::to_string(pad_served_active) + ":last=" + (pad_served_last.empty() ? "none" : pad_served_last));
            if (live_input) {
                meta->Set("pad_live", live_input->Census());
                meta->Set("input_sources", live_input->Sources());
            }
            if (session) meta->Set("session_presents", std::to_string(session->Presents()));
            // The audio device (play): quanta pushed to the ring / with a non-zero sample, the
            // ring's own counters (frames pushed/pulled, underruns, padded frames, re-latches, trimmed).
            meta->Set("audio_device", audio_sink.Running() || audio_device_quanta
                                          ? "sdl:" + std::to_string(audio_sink.DeviceSampleRate()) + "hz:quanta=" + std::to_string(audio_device_quanta) +
                                                ":nonsilent=" + std::to_string(audio_device_nonsilent) + ":pushed=" + std::to_string(audio_ring.pushed()) +
                                                ":pulled=" + std::to_string(audio_ring.pulled()) + ":underruns=" + std::to_string(audio_ring.underruns()) +
                                                ":padded=" + std::to_string(audio_ring.padded()) + ":relatches=" + std::to_string(audio_ring.relatches()) +
                                                ":trimmed=" + std::to_string(audio_ring.trimmed()) +
                                                ":field_pumps=" + std::to_string(audio_field_pumps) +
                                                ":lead=" + std::to_string(audio_ring.LeadQuanta())
                                          : std::string("none"));
            meta->Set("audio_mixed", "pumps=" + std::to_string(mixer.Pumps()) + ":frames=" + std::to_string(mixer.FramesMixed()));
            // The pace census (play; the pacer's protocol): boundaries that parked for the wall
            // edge / arrived late, the longest frame's work, the first long frames with the
            // TickProfile breakdown (Boundary / GifDrain / ... / other = recompiled code), the
            // per-site totals (ms/calls).
            meta->Set("pace", "parks=" + std::to_string(protocol.PaceParks()) + ":late=" + std::to_string(protocol.PaceLate()) +
                                  ":timeouts=" + std::to_string(protocol.PaceTimeouts()) + ":late_fields=" + std::to_string(sequencer.PaceLateFields()) +
                                  ":slept_ms=" + std::to_string(protocol.PaceSleptUs() / 1000) + ":work_max_ms=" + std::to_string(protocol.WorkMaxUs() / 1000) +
                                  ":long=" + std::to_string(protocol.LongTraceCount()));
            meta->Set("pace_work_max_breakdown", protocol.WorkMaxBreakdown().empty() ? std::string("none") : protocol.WorkMaxBreakdown());
            meta->Set("pace_long_trace", protocol.LongTrace().empty() ? std::string("none") : protocol.LongTrace());
            meta->Set("tick_profile", protocol.SiteTotals());
            meta->Set("boundary_wall_ms", boundary_wall.empty() ? std::string("none") : boundary_wall);
            meta->Set("shutdown", shutdown_steps.empty() ? std::string("none") : shutdown_steps);
            meta->Set("gif_sink", "tags=" + std::to_string(gs_state_sink.Tags()) +
                                  ":p1=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path1)) +
                                  ":p2=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path2)) +
                                  ":p3=" + std::to_string(gs_state_sink.TagsOn(GifPath::Path3)) +
                                  ":regs=" + std::to_string(gs_state_sink.RegisterWrites()) +
                                  ":packed=" + std::to_string(gs_state_sink.PackedWrites()) +
                                  ":kicks=" + std::to_string(gs_state_sink.Kicks()) +
                                  ":image_qw=" + std::to_string(gs_state_sink.ImageQwords()) +
                                  ":eops=" + std::to_string(gs_state_sink.Eops()) +
                                  ":p3_finishes=" + std::to_string(gs_state_sink.Path3Finishes()) +
                                  ":drained=" + std::to_string(gif_arbiter.DrainedPackets()) +
                                  ":pending=" + std::to_string(gif_arbiter.PendingBytes()));
            meta->Set("dmac_int1_raises", std::to_string(dmac_int1_raises));
            {
                const DmacTransferStats& ts = dmac.TransferStats();
                std::string spr;
                for (int m = 0; m < 4; ++m)
                    spr += (m ? ",from" : "from") + std::to_string(m) + "=" + std::to_string(ts.spr_from[m].starts) + "/" +
                           std::to_string(ts.spr_from[m].completed) + "/" + std::to_string(ts.spr_from[m].rejected) +
                           ",to" + std::to_string(m) + "=" + std::to_string(ts.spr_to[m].starts) + "/" +
                           std::to_string(ts.spr_to[m].completed) + "/" + std::to_string(ts.spr_to[m].rejected);
                meta->Set("dmac_spr", spr);  // per MOD: starts/completed/rejected
            }
            std::string dmac_chcr;
            for (int ch = 0; ch < kDmacChannelCount; ++ch)
                dmac_chcr += (ch ? "," : "") + std::to_string(ch) + "=" + Hex(dmac.channel(ch).chcr) + "/" +
                             std::to_string(dmac.channel(ch).qwc);
            meta->Set("dmac_chcr_qwc", dmac_chcr);
            std::string ipu_cmds;
            for (uint8_t code = 0; code < 10; ++code)
                if (ipu.CommandCount(code))
                    ipu_cmds += (ipu_cmds.empty() ? "" : ",") + std::to_string(code) + "=" + std::to_string(ipu.CommandCount(code)) +
                                "/" + std::to_string(ipu.UnsupportedCommandCount(code));
            meta->Set("ipu_commands", ipu_cmds.empty() ? "none" : ipu_cmds);
            meta->Set("ipu_fifo", "in=" + std::to_string(ipu.InputSize()) + ":out=" + std::to_string(ipu.OutputSize()));
            meta->Set("ipu_fifo_port", "in_bytes=" + std::to_string(ipu_fifo.InBytes()) + ":out_bytes=" + std::to_string(ipu_fifo.OutBytes()) + ":unhandled=" + std::to_string(ipu_fifo.Unhandled()));
            meta->Set("vu_macro", "vu0_calls=" + std::to_string(VuMacroBridge::Stats().vu0_calls) +
                                      ":vu1_kicks=" + std::to_string(VuMacroBridge::Stats().vu1_kicks));
            // The VU1 fallback tier's work: bundles retired by the cached tier (the title's
            // microprograms interpreted on the kicking EE thread), the unit's cycle count.
            meta->Set("vu1_tier", "bundles=" + std::to_string(vu1.dispatch().bundles()) + ":cycle=" + std::to_string(vu1.cycle()) +
                                      ":dropped_kicks=" + std::to_string(vu1.DroppedKicks()));
        }
    };
    using ps2r::triage::TriageBundle;
    TriageBundle::InstallTerminateHook([&](const char* what) {
        write_census();
        closer.Close("exception", 0, ThreadManager::Instance().CurrentTid(), what);
    });
    static int terminate_hook_token = 0;
    const std::unique_ptr<int, void (*)(int*)> terminate_hook_reset(&terminate_hook_token,
                                                                    [](int*) { TriageBundle::ClearTerminateHook(); });
    GuestPark::SetSink([&](const ParkRecord& r) {
        TriageBundle::NoteThrowSite(r.detail ? r.detail : r.reason);
        write_census();
        closer.Close(r.reason, r.pc, ThreadManager::Instance().CurrentTid(), r.detail);
        // Play: the session closes cleanly around the park (the window goes, the workers are
        // joined); a headless leg's park is its result -- leave at once, census written.
        if (session) {
            session->RequestClose();
            throw ThreadExitUnwind{};
        }
        std::_Exit(0);
    });

    // The IOP reboot image (booted here, after the park sink exists, so an unresolved import of a
    // hosted module parks visibly): the stock modules the engine serves natively need no bytes; FILEIO the stock modules the engine serves natively need no bytes; FILEIO
    // (the EE's libfileio server) is hosted from the image's own module, read from the disc.
    {
        IsoFs::Entry rp;
        std::vector<uint8_t> rp_bytes;
        bool booted = false;
        if (iso_open && iso_fs.Find(ps2r::title::sly2::IopModules::kRebootImage, rp) && !rp.directory && rp.size) {
            rp_bytes.resize(rp.size);
            const size_t got = iso_fs.ReadBytes(static_cast<uint64_t>(rp.lba) * IsoFs::kSectorSize, rp.size, rp_bytes.data());
            if (got == rp.size) booted = iop_host.BootRebootImageModules(rp_bytes.data(), rp_bytes.size());
        }
        if (!booted) std::fprintf(stderr, "nativeee: the reboot image's FILEIO module was not hosted (%s)\n",
                                  iso_open ? "not found or refused" : "no disc image");
        if (auto* meta = host.Meta()) meta->Set("iop_reboot_image_hosted", booted ? "1" : "0");
    }

    // --- slot 7 (Verify): the lanes, the present / frame dump, the frames cap --------------------
    // The grab this boundary reads: the consumer's reference when the boundary waited for it
    // (lockstep / inline); under the wall lock (play: the consumer runs concurrently, pipelined)
    // the published HEADER + hash (no pixel copy crosses the threads; the consumer hashed the
    // grab at the present scale). When grabbed: the frames lane line (host.OnPresent /
    // OnPresentHashed) and, with --dump-frames in a headless leg, f<key>.ppm (the grab) +
    // p<key>.ppm (the presented window image, Backend::ReadPresented). In play the p<n> dump is
    // the consumer thread's (VideoStack::Output above).
    // (frame_dump / grab counters are declared with the GS block above: the census reads them.)
    const std::vector<ps2r::verify::MemorySpan> verify_spans = {{ram.Data(), ram.Size()}, {spad.Data(), spad.Size()}};
    // The play-mode field ticker (started with the cold boot below; declared here so the clean
    // leg end can stop it).
    TickThread tick_thread(tick_source, sequencer.Ticks(), protocol);
    // The clean leg end (the frames cap, the window close): every guest worker is cancelled
    // (ThreadManager::TerminateAll + the kernel waits' condition variables + the pacer woken) and
    // the thread at the boundary unwinds through ThreadExitUnwind; the cold-boot thread catches
    // its own unwind and joins the rest (headless) or the session's `stopped` ladder does (play).
    ps2r::app::SessionWorkers session_workers(ThreadManager::Instance(), running, [&tick_thread] { tick_thread.Stop(); },
                                              [&protocol] { protocol.NotifyTick(); });
    auto end_leg_from_boundary = [&](const char* detail) {
        write_census();
        closer.Close("none", 0, ThreadManager::Instance().CurrentTid(), detail);
        std::fflush(stdout);
        if (session) session->RequestClose();   // main: hooks.stop -> the cancel below
        else session_workers.Cancel();
        throw ThreadExitUnwind{};
    };
    // --set harness.close_at_frame=N [harness.close_after_ms=M]: the window's close (a real SDL_QUIT
    // into SDL's queue, from main) at the first boundary >= N -- the user's red X without the
    // finger; the shutdown pin (RootExitClean's shape). --set harness.inject_key=<KEY>@N[-M][,...]:
    // a synthetic press through the REAL key route (SDL's queue -> the main pump -> SessionGui ->
    // SessionControls / the pad's kbm backend) at the first boundary >= N, released at M (or the
    // next boundary); KEY = F1..F12 or an SDL key name (Escape, Space ...). The validator pinned
    // the format; an unknown name is refused here.
    struct HarnessKeyPress { int scancode; uint64_t frame, until; bool pressed = false, released = false; std::string name; };
    std::vector<HarnessKeyPress> harness_keys;
    for (std::string rest = cfg.harness.inject_key; !rest.empty();) {
        const size_t comma = rest.find(',');
        const std::string item = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const size_t at = item.find('@');
        HarnessKeyPress k{};
        k.name = item.substr(0, at);
        const std::string frames = item.substr(at + 1);
        const size_t dash = frames.find('-');
        k.frame = std::strtoull(frames.substr(0, dash).c_str(), nullptr, 10);
        k.until = dash == std::string::npos ? k.frame : std::strtoull(frames.substr(dash + 1).c_str(), nullptr, 10);
        k.scancode = static_cast<int>(SDL_GetScancodeFromName(k.name.c_str()));
        if (k.scancode == SDL_SCANCODE_UNKNOWN) {
            std::fprintf(stderr, "nativeee: harness.inject_key: unknown key name '%s'\n", k.name.c_str());
            return 2;
        }
        harness_keys.push_back(k);
    }
    bool close_pushed = false;
    auto harness_verbs = [&](uint64_t at_boundary) {
        if (!session) return;
        for (HarnessKeyPress& k : harness_keys) {
            if (!k.pressed && at_boundary >= k.frame) {
                k.pressed = true;
                std::fprintf(stderr, "nativeee: harness.inject_key %s at boundary %llu: %s\n", k.name.c_str(),
                             static_cast<unsigned long long>(at_boundary),
                             session->InjectKey(k.scancode, true) ? "SDL_KEYDOWN pushed into SDL's queue" : "SDL_PushEvent refused");
            }
            if (k.pressed && !k.released && at_boundary > k.until) {
                k.released = true;
                session->InjectKey(k.scancode, false);
                std::fprintf(stderr, "nativeee: harness.inject_key %s released at boundary %llu\n", k.name.c_str(),
                             static_cast<unsigned long long>(at_boundary));
            }
        }
        if (cfg.harness.close_at_frame > 0 && !close_pushed && at_boundary >= static_cast<uint64_t>(cfg.harness.close_at_frame)) {
            close_pushed = true;
            if (cfg.harness.close_after_ms > 0) {
                session->ArmCloseAfter(uint64_t(cfg.harness.close_after_ms) * 1000000ull);
                std::fprintf(stderr, "nativeee: harness.close_at_frame at boundary %llu: SDL_QUIT armed on main in %d ms\n",
                             static_cast<unsigned long long>(at_boundary), cfg.harness.close_after_ms);
            } else {
                std::fprintf(stderr, "nativeee: harness.close_at_frame at boundary %llu: %s\n", static_cast<unsigned long long>(at_boundary),
                             session->InjectClose() ? "SDL_QUIT pushed into SDL's queue" : "SDL_PushEvent refused");
            }
        }
    };
    sequencer.SetStep(BoundarySequencer::Step::Verify, [&](CpuContext&) {
        harness_verbs(boundaries);
        // The play-mode FIELD PUMP (the first title's rule, ee_bootstrap_play.cpp): the SPU2 mixes
        // 48000 frames a second whether or not the EE passes a boundary -- a late boundary delivered
        // the elapsed fields (DomainTick ran ahead of the boundary count), so the mixer is caught up
        // to one quantum per domain tick, each pushed to the DEVICE ring only; the boundary's own
        // pump below is the hashed one (the lane keeps exactly 800 frames per boundary). The device
        // then hears the game's audio at its own rate instead of a padded tail. Play only: the
        // headless lane is the boundary-only pump it always was.
        if (audio_sink.Running()) {
            const uint64_t now = domain.DomainTick();
            if (audio_pumped_tick == 0) audio_pumped_tick = now;   // the first boundary anchors
            while (audio_pumped_tick + 1 < now) {
                mixer.Pump();
                push_audio_ring(mixer.Output());
                ++audio_pumped_tick;
                ++audio_field_pumps;
            }
            audio_pumped_tick = now;
        }
        mixer.Pump();
        push_audio_ring(mixer.Output());
        const uint64_t boundary = host.OnBoundary(mixer.Output(), ps2r::sound::Mixer::kFramesPerVsync, verify_spans);
        boundaries = boundary + 1;
        if (boundaries % 100 == 0)
            boundary_wall += (boundary_wall.empty() ? "" : ",") + std::to_string(boundaries) + ":" +
                             std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - leg_t0).count());
        RingLog::SetFrame(static_cast<uint32_t>(boundaries));
        if (consumer) {
            const uint32_t present_key = host.PresentKey(0, boundary);
            if (vert_trace_key) vert_trace.on.store(present_key + 1 == vert_trace_key);
            const bool wall_locked = sequencer.WallLocked();
            ps2r::gs::DrawPipeline::Frame wall_locked_grab;
            const ps2r::gs::DrawPipeline::Frame* fr = &consumer->LastFrame();
            if (wall_locked) {
                wall_locked_grab = consumer->LastFrameHeaderCopy();
                fr = &wall_locked_grab;
            }
            if (fr->grabbed) {
                ++grabs;
                if (fr->source == ps2r::gs::ScanoutComposer::Source::Mirror) ++grab_local_memory;
                if (fr->source_fallback) ++grab_fallback;
                if (wall_locked) host.OnPresentHashed(present_key, fr->hashed ? fr->hash : 0, video.Renderer());
                else host.OnPresent(present_key, fr->rgba.data(), fr->w, fr->h, video.Scale(), video.Renderer());
                if (!wall_locked && frame_dump.Enabled() && dump_wanted(present_key)) {
                    frame_dump.Write("f", present_key, fr->w, fr->h, fr->rgba.data(), /*flip=*/false);
                    std::vector<uint8_t> presented;
                    int pw = 0, ph = 0;
                    if (video.Device().ReadPresented(&presented, &pw, &ph)) frame_dump.Write("p", present_key, pw, ph, presented.data(), /*flip=*/true);
                }
            } else {
                host.OnPresentHashed(present_key, 0, video.Renderer());
            }
            // NATIVEEE_GS_ROUTES=<key>[,<key>...] (boot triage): the pipeline's per-draw route trace of
            // that frame + the CRTC registers and the grab's source as <dump dir or harness out>/routes_<key>.txt.
            static const char* routes_env = std::getenv("NATIVEEE_GS_ROUTES");
            if (routes_env && !wall_locked) {
                const std::string want = std::string(",") + routes_env + ",";
                if (want.find("," + std::to_string(present_key) + ",") != std::string::npos) {
                    const std::string dir = frame_dump.Enabled() ? frame_dump.Dir() : cfg.harness.out_dir;
                    if (std::FILE* rf = std::fopen((dir + "/routes_" + std::to_string(present_key) + ".txt").c_str(), "w")) {
                        using P = ps2r::gs::GsPrivRegs;
                        const P& p = gs_state.Priv();
                        std::fprintf(rf, "key=%u pmode=%s dispfb1=%s display1=%s dispfb2=%s display2=%s smode2=%s\n", present_key,
                                     Hex(p.Read(P::kPmode)).c_str(), Hex(p.Read(P::kDispfb1)).c_str(), Hex(p.Read(P::kDisplay1)).c_str(),
                                     Hex(p.Read(P::kDispfb2)).c_str(), Hex(p.Read(P::kDisplay2)).c_str(), Hex(p.Read(P::kSmode2)).c_str());
                        const ps2r::gs::ScanoutComposer& sc = video.Pipe()->Scanout();
                        std::fprintf(rf, "scanout_fbp=0x%x dispfb_fbw=%u psm=%u content=%dx%d grab=%s@0x%x fallback=%d %dx%d\n",
                                     sc.ScanoutFbp(), sc.DispfbFbw(), sc.DispfbPsm(), sc.ContentW(), sc.ContentH(),
                                     fr->source == ps2r::gs::ScanoutComposer::Source::Target ? "target"
                                     : fr->source == ps2r::gs::ScanoutComposer::Source::Mirror ? "local_memory" : "none",
                                     fr->source_fbp, fr->source_fallback ? 1 : 0, fr->w, fr->h);
                        video.Pipe()->RouteTrace().Write(rf);
                        std::fclose(rf);
                    }
                    // The GS local-memory image at that frame (4 MiB, native layout), for the texel check.
                    if (std::FILE* vf = std::fopen((dir + "/vram_" + std::to_string(present_key) + ".bin").c_str(), "wb")) {
                        std::fwrite(gs_state.Vram().Bytes(), 1, ps2r::gs::VramImage::kBytes, vf);
                        std::fclose(vf);
                    }
                }
            }
        }
        if (cfg.harness.frames > 0 && boundary + 1 >= static_cast<uint64_t>(cfg.harness.frames)) end_leg_from_boundary("frames_cap");
        // The session's boundary (pause / step / the close): false = the window was closed --
        // the leg ends here with its census (the stop hook on main may be closing it too; the
        // closer closes exactly once) and the guest workers unwind.
        if (session && !session->Boundary(boundaries)) end_leg_from_boundary("session_closed");
        return true;
    });
    // The join ladder, in dependency order: the guest workers first (they produce for every
    // consumer below; bounded -- a worker parked in a wait the cancel cannot reach is abandoned
    // after the deadline and NAMED), the pacer, the GS consumer's backlog (Quiesce: the census
    // reads its counters after it drained; the queue's own Stop/join and the video stack's
    // teardown run in the frame's destructor order, on main), the audio device, the census.
    constexpr uint64_t kGuestJoinDeadlineMs = 5000;
    auto join_ladder = [&] {
        const auto t0 = std::chrono::steady_clock::now();
        auto note = [&](const char* step) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            shutdown_steps += (shutdown_steps.empty() ? "" : ",") + std::string(step) + "@" + std::to_string(ms) + "ms";
        };
        if (session_workers.Join(kGuestJoinDeadlineMs, &guest_join_abandoned) > 0)
            std::fprintf(stderr, "nativeee: shutdown: guest workers abandoned after %llu ms: %s\n",
                         static_cast<unsigned long long>(kGuestJoinDeadlineMs), guest_join_abandoned.c_str());
        note("guest_join");
        tick_thread.Stop();
        note("pacer");
        if (gs_queue) gs_queue->Quiesce();
        note("gs_quiesce");
        if (audio_sink.Running()) audio_sink.Close();
        audio_ring.SetPullTap(nullptr, nullptr);   // the device callback has returned (Close waits for it)
        audio_dump.Close();
        note("audio_close");
    };
    if (session) {
        ps2r::app::SessionRuntime::Hooks hooks;
        // Main, at the close request: cancel only (no join here -- main keeps pumping the
        // window and enforcing the shutdown deadline); the boundary thread sees
        // Boundary() == false and unwinds, the waiters wake terminated.
        hooks.stop = [&] { session_workers.Cancel(); };
        // The game worker, after the cold boot returned: the join ladder, then the census.
        hooks.stopped = [&] {
            join_ladder();
            write_census();
            closer.Close("none", 0, 0, "window_closed");
            std::fflush(stdout);
        };
        session->SetHooks(std::move(hooks));
    }

    // --- the watchdog: a guest that never reaches a boundary (a raw SIF poll with no IOP, an
    // unmodelled wait) would otherwise only end at the bounded runner's wall timeout with no
    // run.meta park row. After NATIVEEE_WATCHDOG_S seconds (default 60; 0 = off) without a
    // boundary the leg closes as park=watchdog with the main thread's pc -- the FINDING of this
    // stage is exactly where the fallbacks stop carrying the title.
    uint64_t watchdog_s = 60;
    if (const char* env = std::getenv("NATIVEEE_WATCHDOG_S")) watchdog_s = std::strtoull(env, nullptr, 10);
    std::thread watchdog;
    if (watchdog_s > 0) {
        watchdog = std::thread([&, watchdog_s] {
            uint64_t last = 0;
            auto since = std::chrono::steady_clock::now();
            while (running.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (boundaries != last) {
                    last = boundaries;
                    since = std::chrono::steady_clock::now();
                    continue;
                }
                const auto waited = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - since);
                if (static_cast<uint64_t>(waited.count()) < watchdog_s) continue;
                CpuContext* ctx = main_ctx.load();
                const uint32_t pc = ctx ? ctx->cpuRegs.pc : 0;
                const std::string detail = "no boundary for " + std::to_string(watchdog_s) + "s; last syscall #" +
                                           std::to_string(syscall_count);
                write_census();
                if (auto* meta = host.Meta()) {
                    if (ctx) {
                        meta->Set("watchdog_ra", Hex(ctx->cpuRegs.GPR.r[31].UL[0]));
                        meta->Set("watchdog_sp", Hex(ctx->cpuRegs.GPR.r[29].UL[0]));
                        meta->Set("watchdog_a0", Hex(ctx->cpuRegs.GPR.r[4].UL[0]));
                    }
                    meta->Set("threads", thread_census());
                    meta->Set("token_tid", std::to_string(EeRunToken::Instance().OwnerForTest()));
                }
                closer.Close("watchdog", pc, 1, detail.c_str());
                // NATIVEEE_RAM_DUMP=<file>: the 32 MB guest RAM image at the park (boot triage only;
                // racy against still-running guest threads, which is fine for a parked title).
                if (const char* dump = std::getenv("NATIVEEE_RAM_DUMP")) {
                    if (std::FILE* f = std::fopen(dump, "wb")) {
                        std::fwrite(ram.Data(), 1, ram.Size(), f);
                        std::fclose(f);
                        std::fprintf(stderr, "watchdog: RAM dumped to %s\n", dump);
                    }
                }
                std::fflush(stdout);
                std::_Exit(0);
            }
        });
    }

    // --- the cold boot ------------------------------------------------------------------------------
    // Play mode: the wall-locked boundary -- the field ticker notes one tick per 60 Hz wall edge
    // and the boundary's TickAdvance slot parks for it (BindPacer); NATIVEEE_UNPACED=1 keeps the
    // boundary ticking inline (the title's pacer/render thread handshake under instant
    // completions was the sched stage's starvation case -- the knob is the comparison).
    const bool paced = play_mode && video_live && std::getenv("NATIVEEE_UNPACED") == nullptr;
    if (paced) {
        tick_thread.Start();
        sequencer.BindPacer(&protocol);
    }
    if (auto* meta = host.Meta()) meta->Set("play_mode", play_mode ? (paced ? "paced" : "unpaced") : "headless");
    int result = 0;
    uint32_t fault_pc = 0;
    auto cold_boot = [&]() {
        CpuContext ctx;
        CpuContextInit::Reset(ctx, title.entry, title.main_stack_top);
        std::shared_ptr<ThreadInfo> main_info = ThreadManager::Instance().AdoptCurrentThread(1, kMainThreadPriority);
        main_info->ctx = &ctx;
        main_ctx.store(&ctx);
        EeRunToken::Instance().GuestEnter(1, kMainThreadPriority);
        PS2R_LOG(HOST, Info, "cold boot: entry 0x%08x sp 0x%08x", title.entry, title.main_stack_top);
        std::exception_ptr failure;
        try {
            Ps2Float::SetGuestThreadRounding();
            DispatchTable::Invoke(title.entry, ctx);
        } catch (const ThreadExitUnwind&) {
        } catch (...) {
            failure = std::current_exception();
            fault_pc = ctx.cpuRegs.pc;
        }
        main_ctx.store(nullptr);
        std::string release_error;
        if (!ThreadManager::Instance().ReleaseCurrentThread(&release_error) && !failure)
            failure = std::make_exception_ptr(std::runtime_error(release_error));
        running.store(false);
        // Headless: this thread joins the rest (the session's `stopped` ladder does it in play).
        if (!session) join_ladder();
        write_census();
        if (failure) {
            std::string what = "guest fault";
            try { std::rethrow_exception(failure); } catch (const std::exception& e) { what = e.what(); } catch (...) {}
            closer.Close("exception", ctx.cpuRegs.pc, 1, what.c_str());
            return 1;
        }
        closer.Close("none", ctx.cpuRegs.pc, 1, session && session->Closing() ? "session_closed" : "guest_returned");
        return 0;
    };
    if (session) {
        // The game on the session's 1 GiB worker; main pumps the window until it returns or
        // the window closes; the `stopped` ladder joins the workers and writes the census; Run
        // returns on main once the worker is joined -- the frame below unwinds normally (the GS
        // queue's Stop, the video stack, the session window: on main, in reverse construction order).
        result = session->Run(cold_boot, static_cast<unsigned>(cfg.engine.shutdown_wait_s));
        write_census();
        closer.Close("none", fault_pc, fault_pc ? 1 : 0, "session_closed");
    } else {
        result = cold_boot();
    }
    if (watchdog.joinable()) watchdog.join();
    std::fprintf(stderr, "nativeee: leg shutdown: %s%s%s\n", shutdown_steps.empty() ? "(no ladder)" : shutdown_steps.c_str(),
                 guest_join_abandoned.empty() ? "" : " abandoned=", guest_join_abandoned.c_str());
    cap.RunFinalizers();
    if (!guest_join_abandoned.empty()) {
        // An abandoned guest worker is still parked inside objects this frame owns: leave now,
        // census written, without their destructors.
        std::fflush(stderr);
        std::fflush(stdout);
        std::_Exit(result ? result : 3);
    }
    return result;
}

}  // namespace

int TitlePlayMain(int argc, char** argv) {
    Config cfg;
    ConfigLoader loader;
    loader.SetRetainedStartupAvailable(false);   // no IOP in this binary (PS2R_IOP_ORACLE=0)
    loader.SetUserDataRoot(ps2r::app::UserData::PlatformRoot());
    const ConfigLoader::Result r = loader.Load(argc, argv, cfg);
    if (r.show_version) {
        std::printf("%s\n", g_build_id);
        return 0;
    }
    if (r.show_help) {
        std::printf("%s", ConfigLoader::Usage());
        return 0;
    }
    if (!r.ok) {
        std::fprintf(stderr, "nativeee: %s\n", r.error.c_str());
        return 2;
    }
    const TitleProfile& title = ActiveTitleProfile();
    // The CLI route of the first-run extraction (`--extract-disc <image>`): verify the disc image
    // against this build's ELF (TitleProfile::elf_sha256) and lay the game files, memcards,
    // settings and stamp under the resolved user data root, then exit. No window, no game.
    if (!r.extract_disc.empty())
        return ps2r::app::FirstRun::RunCli(ps2r::app::FirstRun::PlanFor(cfg, title, g_build_id, r.extract_disc, true));

    const std::string log_dir = cfg.paths.log_dir.empty() ? std::string("logs") : cfg.paths.log_dir;
    Logger::Instance().Start(cfg.harness.headless ? std::string() : log_dir + "/runtime_log.txt",
                             ToLoggerLevel(cfg.dev.log_level), true);
    const uint32_t mask_normal = RingLog::ParseChannelList(cfg.log.channels.c_str(), ~0u);
    RingLog::Configure(mask_normal, 0, static_cast<uint32_t>(cfg.log.ring_events), log_dir.c_str());
    RingLog::RegisterThread("main");
    ps2r::platform::host::RegisterSelf();
    ps2r::platform::host::PinCurrentThreadInteractive("nativeee-main");
    const std::string crash_ring = log_dir + "/ring_crash.rlog";
    ps2r::platform::CrashHandler::Install(crash_ring.c_str());
    for (const std::string& note : r.notes) std::fprintf(stderr, "[config] %s\n", note.c_str());

    PS2R_LOG(HOST, Info, "nativeee %s title %s (%s) config sources=%zu", g_build_id, title.serial.c_str(),
             PS2R_TITLE_ID, r.sources.size());

    // The launcher (the engine's first-run / settings screen, app/host/launcher.h) runs BEFORE
    // the game on a plain start: no game files under the user data root -> drop the disc image,
    // verify (ISO9660, SCUS_973.16 present, its SHA-256 == the ELF this build recompiled),
    // extract (ELF, disc copy or reference, memcards, settings, stamp), Play; afterwards the
    // settings screen (launcher.show). Any harness flag (--iso, --headless, --frames, --set,
    // --game-files ...) bypasses it, so the harness legs are unchanged; --launcher forces it.
    // Play lands the files in cfg.paths.game_files / cfg.paths.iso, where ResolveElf and the
    // disc open below look first.
    if (ps2r::app::Launcher::Wanted(cfg, title, r.harness_cli, r.launcher)) {
        ps2r::app::Launcher launcher(cfg, title, g_build_id);
        if (!r.launcher_screenshot.empty()) launcher.SetScreenshot(r.launcher_screenshot, r.launcher_then_play);
        else if (r.launcher_then_play) launcher.SetAutoPlay();
        if (!r.launcher_disc.empty()) launcher.SetScriptedDrop(r.launcher_disc);
        if (!r.launcher_page.empty()) launcher.SetHarnessPage(r.launcher_page);
        const ps2r::app::Launcher::Outcome outcome = launcher.Run();
        if (outcome == ps2r::app::Launcher::Outcome::Quit) return 0;
        if (outcome == ps2r::app::Launcher::Outcome::Failed) {
            std::fprintf(stderr, "nativeee: the launcher could not open or present a window; use --extract-disc <image> and --game-files\n");
            return 2;
        }
        PS2R_LOG(HOST, Info, "launcher: Play (game files %s, disc %s)", cfg.paths.game_files.c_str(), cfg.paths.iso.c_str());
    }

    ps2r::verify::VerifyHost host;
    {
        std::string err;
        if (!host.Arm(cfg, g_build_id, PS2R_TITLE_ID, &err)) {
            std::fprintf(stderr, "nativeee: %s\n", err.c_str());
            return 2;
        }
        if (auto* meta = host.Meta()) meta->Set("user_data_root", cfg.paths.user_data);
    }
    ps2r::verify::FrameCap cap;
    cap.Arm(static_cast<uint64_t>(cfg.harness.frames > 0 ? cfg.harness.frames : 0));
    cap.AddFinalizer([&] { host.Finalize(); });
    cap.AddFinalizer([] { RingLog::ExitDump(); });
    cap.AddFinalizer([] { Logger::Instance().Flush(); });

    // The title directory (hle/sdk_bindings.tsv): the source tree's games/<title>/ when this binary
    // runs from its build (PS2R_TITLE_DIR), else the staged copy beside the executable.
    std::string title_dir = PS2R_TITLE_DIR;
    if (!FileExists(title_dir + "/" + title.sdk_bindings_file)) {
        std::string exe_err;
        const std::string exe = ps2r::platform::host::ExecutablePath(&exe_err);
        const size_t slash = exe.find_last_of('/');
        const std::string beside = (slash == std::string::npos ? std::string(".") : exe.substr(0, slash)) + "/title";
        if (FileExists(beside + "/" + title.sdk_bindings_file)) title_dir = beside;
        else std::fprintf(stderr, "nativeee: title directory not found: neither %s nor %s has %s\n", PS2R_TITLE_DIR,
                          beside.c_str(), title.sdk_bindings_file.c_str());
    }
    const std::string elf_path = ResolveElf(cfg, title);
    if (!FileExists(elf_path)) {
        std::fprintf(stderr,
                     "nativeee: ELF not found at %s (give --game-files <dir> holding %s/%s, or build with "
                     "NATIVEEE_DISC / NATIVEEE_TITLE_ELF so the build's extracted copy exists)\n",
                     elf_path.c_str(), title.elf_dir.c_str(), title.elf_path.c_str());
        cap.RunFinalizers();
        return 2;
    }
    return RunLeg(cfg, host, cap, title, elf_path, title_dir);
}
