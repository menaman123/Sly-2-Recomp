// games/scus_97316_sly2_band_of_thieves/hle/video_stack.cpp -- VideoStack (see video_stack.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/video_stack.h"

#include <SDL.h>

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
    (void)video;
#if defined(PS2R_HAS_METAL)
    metal_ = std::make_unique<render::MetalDevice>();
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

void VideoStack::OnWindowResized(int w, int h) {
    if (!be_ || w <= 0 || h <= 0) return;
    window_w_ = w;
    window_h_ = h;
    be_->SetWindowSize(w, h);
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
