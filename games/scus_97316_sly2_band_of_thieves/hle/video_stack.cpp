#include "games/scus_97316_sly2_band_of_thieves/hle/video_stack.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>

#include "engine/core/log/log.h"

namespace ps2r::title::sly2 {

namespace {
// The pipeline's native display rectangle (NTSC 640x448; DrawPipeline's own default): the
// device's capture / fallback viewport until the first DISPLAY job sets the window size.
constexpr int kNativeWindowW = 640;
constexpr int kNativeWindowH = 448;
// The hidden window's own size (never shown; the presented image is captured, not read
// from its default framebuffer).
constexpr int kHiddenWindowW = 64;
constexpr int kHiddenWindowH = 64;
}  // namespace

VideoStack::VideoStack() = default;
VideoStack::~VideoStack() { Shutdown(); }

bool VideoStack::Create(const VideoConfig& video, const char* title, gs::VramImage& vram, bool play) {
    if (Active()) return true;
    // video.backend=auto: in PLAY, Metal when the host has an Apple-silicon-class Metal device
    // (MTLGPUFamilyApple7+: the arm the Metal backend is built and measured for), else GL; the
    // headless / dump lanes resolve to GL (the frame-hash gates' arm). Explicit values as given.
    VideoBackend chosen = video.backend;
    backend_auto_ = chosen == VideoBackend::Auto;
    if (backend_auto_) {
        chosen = VideoBackend::Gl;
#if defined(PS2R_HAS_METAL)
        if (play && render::MetalDevice::Recommended()) chosen = VideoBackend::Metal;
#else
        (void)play;
#endif
    }
    const bool ok = chosen == VideoBackend::Metal ? CreateMetal(video) : CreateContext(video, title);
    if (!ok) {
        Shutdown();
        return false;
    }
    int scale = video.scale;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    BuildScene(scale, vram);
    return true;
}

bool VideoStack::CreateContext(const VideoConfig& video, const char* title) {
    if (!win_.Create(true, kHiddenWindowW, kHiddenWindowH, title)) {
        PS2R_LOG(HOST, Warn, "video: no window (stub present)");
        return false;
    }
    if (!ctx_.Create(win_)) {
        PS2R_LOG(HOST, Warn, "video: no GL 4.1 context (stub present)");
        return false;
    }
    ctx_.SetSwapInterval(video.vsync ? 1 : 0);
    if (!gl_.Init([this] { PresentWindow(); })) {
        PS2R_LOG(HOST, Warn, "video: GlDevice::Init failed (stub present)");
        return false;
    }
    be_ = &gl_;
    metal_active_ = false;
    window_w_ = kNativeWindowW;
    window_h_ = kNativeWindowH;
    be_->SetWindowSize(window_w_, window_h_);
    // Every Present draws into the owned capture framebuffer and reads it back: the
    // image the session window shows and the --dump-frames p<key>.ppm.
    be_->ArmPresentCapture(true);
    current_thread_ = std::this_thread::get_id();
    PS2R_LOG(HOST, Info, "video: GL %s / %s (hidden window, capture %dx%d)", ctx_.Version(), ctx_.Renderer(),
             window_w_, window_h_);
    return true;
}

// video.backend=metal: the Metal device (no window, no context; any thread may drive it).
bool VideoStack::CreateMetal(const VideoConfig& video) {

#if defined(PS2R_HAS_METAL)
    render::MetalDeviceSettings ms;
    ms.gpu_timing = metal_options_.gpu_timing;
    if (metal_options_.commit_draws) ms.commit_draws = *metal_options_.commit_draws;
    // video.metal_hw_depth (the rasterizer's depth: HSR / early-Z) or the fragment-written depth; the
    // NATIVEEE_METAL_HW_DEPTH env knob forces the rasterizer's for an A/B.
    ms.shader_depth = !(video.metal_hw_depth || metal_options_.hw_depth);
    ms.exact_arms = metal_options_.exact_arms;
    ms.private_attachments = metal_options_.private_rt;
    metal_ = std::make_unique<render::MetalDevice>(ms);
    if (!metal_->Init([this] { PresentWindow(); })) {
        PS2R_LOG(HOST, Warn, "video: MetalDevice::Init failed (stub present)");
        metal_.reset();
        return false;
    }
    be_ = metal_.get();
    metal_active_ = true;
    renderer_name_ = std::string("Metal ") + metal_->DeviceName();
    window_w_ = kNativeWindowW;
    window_h_ = kNativeWindowH;
    be_->SetWindowSize(window_w_, window_h_);
    be_->ArmPresentCapture(true);
    current_thread_ = std::this_thread::get_id();
    PS2R_LOG(HOST, Info, "video: %s (offscreen, capture %dx%d)", renderer_name_.c_str(), window_w_, window_h_);
    return true;
#else
    PS2R_LOG(HOST, Warn, "video: video.backend=metal is not built on this host (stub present)");
    return false;
#endif
}

void VideoStack::BuildScene(int scale, gs::VramImage& vram) {
    scale_factor_ = scale;
    vram_ = &vram;
    scale_ = std::make_unique<gs::ScalePolicy>(gs::ScaleSettings{scale}, &layout_);
    cache_ = std::make_unique<gs::RenderTargetCache>(*be_, *vram_, *scale_);
    refresher_ = std::make_unique<gs::TargetRefresher>(*be_, *vram_, *cache_);
    resolver_ = std::make_unique<gs::TextureResolver>(*be_, *vram_, *cache_, *refresher_, layout_);
    scanout_ = std::make_unique<gs::ScanoutComposer>(*be_, *cache_);
    presenter_ = std::make_unique<gs::Presenter>(*scanout_);
    gs::DrawPipeline::Deps deps;
    deps.be = be_;
    deps.vram = vram_;
    deps.cache = cache_.get();
    deps.refresher = refresher_.get();
    deps.resolver = resolver_.get();
    deps.scanout = scanout_.get();
    deps.presenter = presenter_.get();
    deps.layout = &layout_;
    deps.registry = nullptr;   // no class renderers: every draw is DrawGeneric
    deps.sequence = nullptr;
    pipe_ = std::make_unique<gs::DrawPipeline>(deps);
    // LIVE: the VRAM image is the console's local memory -- the resolver's VRAM-decode
    // tiers serve textured draws (the replay driver leaves this off).
    pipe_->SetLiveSnapshot(true);
    // LIVE: no window seed (the first DISPLAY job sets the window and content dims); the
    // scanout base is unset until the first DISPFB job, so the display quad is skipped
    // until then.
    scanout_->SetScanoutFbp(gs::DrawPipeline::kNoFbp);
}

void VideoStack::ApplyScale(int scale) {
    if (!scale_) return;
    scale_->SetFactor(scale);
    scale_factor_ = scale_->Factor();
}

void VideoStack::EnsureCurrent() {
    if (!ctx_.IsCreated()) return;   // Metal: no context to bind
    const std::thread::id me = std::this_thread::get_id();
    if (me == current_thread_ && SDL_GL_GetCurrentWindow() == win_.Handle()) return;
    if (ctx_.MakeCurrent(win_)) {
        current_thread_ = me;
        rebinds_++;
    }
}

void VideoStack::ReleaseCurrent() {
    if (win_.Handle() && SDL_GL_GetCurrentWindow() == win_.Handle()) SDL_GL_MakeCurrent(win_.Handle(), nullptr);
    current_thread_ = {};
}

void VideoStack::PresentWindow() {
    if (output_) {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (be_ && be_->ReadPresented(&rgba, &w, &h) && w > 0 && h > 0) {
            ++output_frames_;
            output_(std::move(rgba), w, h);
        }
    }
    if (swap_hidden_ && !metal_active_) win_.Swap();
    else if (be_) be_->FlushCommands();
}

namespace {
// p50 / p95 / max of a per-frame series, as "p50/p95/max" with `div` applied (ms from ns, KB from bytes).
template <typename T>
std::string Quantiles(std::vector<T> v, double div, bool p95) {
    if (v.empty()) return "0/0/0";
    std::sort(v.begin(), v.end());
    auto fmt = [&](T x) {
        char b[32];
        if (div == 1.0) std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(x));
        else std::snprintf(b, sizeof b, "%.2f", static_cast<double>(x) / div);
        return std::string(b);
    };
    const T p50 = v[v.size() / 2];
    const T p95v = v[std::min(v.size() - 1, (v.size() * 95) / 100)];
    const T mx = v.back();
    return p95 ? fmt(p50) + "/" + fmt(p95v) + "/" + fmt(mx) : fmt(p50) + "/" + fmt(mx);
}
}  // namespace

void VideoStack::GpuRows(std::string* gpu_frame, std::string* gpu_pass) const {
    if (gpu_frame) *gpu_frame = "off";
    if (gpu_pass) *gpu_pass = "off";
#if defined(PS2R_HAS_METAL)
    if (!metal_active_ || !metal_) return;
    const std::vector<render::MetalFrameStats> frames = metal_->FrameStats();
    if (gpu_frame && !frames.empty()) {
        std::vector<uint64_t> gpu, span, dc, pb, fb, tb, up, rb, rbk, cp, sb, cl, gw, fr, vx, bl, fi;
        uint64_t gw_sum = 0;
        for (const render::MetalFrameStats& f : frames) {
            gpu.push_back(f.gpu_ns); span.push_back(f.gpu_span_ns); fi.push_back(f.frag_invocations);
            dc.push_back(f.draw_calls); pb.push_back(f.program_binds); fb.push_back(f.framebuffer_binds);
            tb.push_back(f.texture_binds); up.push_back(f.upload_bytes); rb.push_back(f.readbacks); rbk.push_back(f.readback_bytes);
            cp.push_back(f.copies); sb.push_back(f.submits); cl.push_back(f.clears); gw.push_back(f.gpu_wait_ns); gw_sum += f.gpu_wait_ns;
            fr.push_back(f.pass_frag_ns); vx.push_back(f.pass_vtx_ns); bl.push_back(f.pass_blit_ns);
        }
        const double ms = 1e6, kb = 1024.0;
        std::string r = "frames=" + std::to_string(frames.size()) + ":gpu_ms=" + Quantiles(gpu, ms, true) +
                        ":gpu_span_ms=" + Quantiles(span, ms, true) +
                        ":draw_calls=" + Quantiles(dc, 1.0, false) + ":pipeline_switches=" + Quantiles(pb, 1.0, false) +
                        ":rt_passes=" + Quantiles(fb, 1.0, false) + ":tex_binds=" + Quantiles(tb, 1.0, false) +
                        ":upload_kb=" + Quantiles(up, kb, false) + ":readbacks=" + Quantiles(rb, 1.0, false) +
                        ":readback_kb=" + Quantiles(rbk, kb, false) + ":copies=" + Quantiles(cp, 1.0, false) +
                        ":submits=" + Quantiles(sb, 1.0, false) + ":clears=" + Quantiles(cl, 1.0, false) +
                        ":gpu_wait_ms=" + Quantiles(gw, ms, true) + "/sum" + std::to_string(gw_sum / 1000000u);
        if (metal_->GpuTimingActive())
            r += ":frag_ms=" + Quantiles(fr, ms, true) + ":vtx_ms=" + Quantiles(vx, ms, true) + ":blit_ms=" + Quantiles(bl, ms, true) +
                 ":frag_minv=" + Quantiles(fi, 1e6, true);
        *gpu_frame = r;
    }
    if (gpu_pass && metal_->GpuTimingActive()) {
        std::vector<render::MetalPassStats> ps = metal_->PassStats();
        std::sort(ps.begin(), ps.end(), [](const render::MetalPassStats& a, const render::MetalPassStats& b) {
            return a.frag_ns + a.vtx_ns > b.frag_ns + b.vtx_ns;
        });
        const double nf = frames.empty() ? 1.0 : static_cast<double>(frames.size());
        std::string r = "frames=" + std::to_string(frames.size()) + ":unsampled=" + std::to_string(metal_->PassesUnsampled());
        size_t shown = 0;
        for (const render::MetalPassStats& p : ps) {
            if (shown++ >= 14) break;
            std::string name;
            if (p.kind == 2) name = "present";
            else if (p.kind == 3) name = "blit";
            else if (p.kind == 1) name = "fullscreen";
            else {
                name = "surf" + std::to_string(p.surface);
                if (cache_) cache_->ForEach([&](const gs::RenderTarget& t) {
                    if (t.views.color.id == p.surface) {
                        char b[16];
                        std::snprintf(b, sizeof b, "fbp%x", t.identity.fbp);
                        name = b;
                    } else if (t.views.alt16.id == p.surface) {
                        char b[20];
                        std::snprintf(b, sizeof b, "fbp%x.alt16", t.identity.fbp);
                        name = b;
                    }
                });
            }
            char b[220];
            // frag_minv = fragment invocations per frame (millions); gfrag_s = invocations / fragment-stage span (the achieved fill).
            std::snprintf(b, sizeof b, ",%s:%ux%u:passes=%.1f:draws=%.0f:frag_ms=%.2f:vtx_ms=%.2f:frag_minv=%.1f:gfrag_s=%.2f",
                          name.c_str(), p.width, p.height, p.passes / nf, p.draws / nf, p.frag_ns / 1e6 / nf, p.vtx_ns / 1e6 / nf,
                          p.frag_invocations / 1e6 / nf, p.frag_ns ? static_cast<double>(p.frag_invocations) / static_cast<double>(p.frag_ns) : 0.0);
            r += b;
        }
        *gpu_pass = r;
    }
#endif
}

void VideoStack::OnWindowResized(int w, int h) {
    if (!be_ || w <= 0 || h <= 0) return;
    window_w_ = w;
    window_h_ = h;
    ApplyCaptureSize();
}

void VideoStack::SetPresentOutput(int w, int h) {
    if (!be_) return;
    present_w_ = w > 0 && h > 0 ? w : 0;
    present_h_ = w > 0 && h > 0 ? h : 0;
    if (scanout_) scanout_->SetPresentOutput(present_w_, present_h_);
    ApplyCaptureSize();
}

void VideoStack::ApplyCaptureSize() {
    if (present_w_ > 0 && present_h_ > 0) be_->SetWindowSize(present_w_, present_h_);
    else be_->SetWindowSize(window_w_, window_h_);
}

void VideoStack::Shutdown() {
    if (ctx_.IsCreated() && gl_.IsInitialized()) EnsureCurrent();
    pipe_.reset();
    presenter_.reset();
    scanout_.reset();
    resolver_.reset();
    refresher_.reset();
    cache_.reset();
    scale_.reset();
    vram_ = nullptr;
    output_ = {};
    if (gl_.IsInitialized()) gl_.Shutdown();
#if defined(PS2R_HAS_METAL)
    metal_.reset();
#endif
    be_ = nullptr;
    metal_active_ = false;
    ctx_.Destroy();
    win_.Destroy();
}

}  // namespace ps2r::title::sly2
