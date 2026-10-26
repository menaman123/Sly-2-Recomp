// VideoStack: the live renderer over the engine's generic GS pipeline. video.backend picks GL (a hidden
// SDL window with a GL 4.1 context) or Metal (macOS). There is no class registry and the VRAM layout is
// empty, so every draw takes DrawPipeline::DrawGeneric, the plain-GS path. The scanout base starts unset:
// nothing is presented until the first DISPFB job.
//
// The GL window is always hidden. The player sees SessionRuntime's window on main, fed with the backend's
// presented image (the display quad drawn into an owned framebuffer and read back before the swap); the
// same image is the headless p<key>.ppm. A SetWindow job therefore only sets the device's window size. The
// GL context is current on one thread at a time; EnsureCurrent() re-binds it when the caller changes.
//
// Create() returns false when no window / GL context / device is available (the leg then keeps
// its counting sink and reports video=stub); nothing here fails a leg. Shutdown order: pipeline
// -> presenter -> scanout -> resolver -> refresher -> cache -> scale -> device (context current)
// -> context -> window.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/gs/hle/classrdr/draw_pipeline.h"
#include "engine/gs/hle/present/presenter.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/scale_policy.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/hle/texsrc/texture_resolver.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backend.h"
#include "engine/render/backends/gl/gl_device.h"
#if defined(PS2R_HAS_METAL)
#include "engine/render/backends/metal/metal_device.h"
#endif

namespace ps2r::title::sly2 {

class VideoStack {
public:
    // The presented image of one Present (RGBA8, rows bottom-up as GL reads them).
    using Output = std::function<void(std::vector<uint8_t> rgba, int w, int h)>;

    VideoStack();
    ~VideoStack();
    VideoStack(const VideoStack&) = delete;
    VideoStack& operator=(const VideoStack&) = delete;

    // Hidden window -> GL context -> swap interval from video.vsync -> GlDevice (present
    // capture armed, window size seeded at the pipeline's native 640x448) -> the scene at
    // ScaleSettings{video.scale} over `vram`: the GS's local-memory image (the one its transfer
    // engine writes), which the decode tiers, the target cache and the refresher read. The
    // caller keeps it alive past Shutdown(). False = no GL available (nothing was built).
    // `play`: the windowed play session. video.backend=auto resolves to Metal only there (an
    // Apple-silicon-class device present); the headless / dump lanes keep GL, so every frame-hash
    // gate taken on the GL arm stays comparable across hosts.
    bool Create(const VideoConfig& video, const char* title, gs::VramImage& vram, bool play = false);
    void Shutdown();
    bool Active() const { return pipe_ != nullptr; }

    gs::DrawPipeline* Pipe() { return pipe_.get(); }
    // The active backend (GL or Metal); valid after a successful Create().
    render::Backend& Device() { return *be_; }
    // The backend in force ("metal" / "gl") and whether video.backend=auto chose it.
    const char* ActiveBackend() const { return metal_active_ ? "metal" : "gl"; }
    bool BackendAuto() const { return backend_auto_; }
    const char* Renderer() const { return metal_active_ ? renderer_name_.c_str() : ctx_.Renderer(); }
    const char* GlVersion() const { return metal_active_ ? "Metal" : ctx_.Version(); }
    int Scale() const { return scale_factor_; }

    // The context on the calling thread (a no-op when it already is); the release on
    // the thread that holds it (main, before the consumer thread takes it).
    void EnsureCurrent();
    void ReleaseCurrent();
    uint64_t Rebinds() const { return rebinds_; }

    // Every Present's captured window image goes here (the session window); empty = none.
    void SetOutput(Output output) { output_ = std::move(output); }
    uint64_t OutputFrames() const { return output_frames_; }

    // Play: the device reads each present's window image behind a fence and hands over the
    // PREVIOUS present's (no GPU drain per present; the output lane one present late; a
    // p<key>.ppm of a windowed run is the previous present). Headless keeps the synchronous read.
    void SetPresentCaptureDeferred(bool deferred) { present_capture_deferred_ = deferred; if (be_) be_->SetPresentCaptureDeferred(deferred); }
    // The present capture is read one present late (the picture posted at a present is the previous present's).
    bool PresentCaptureDeferred() const { return present_capture_deferred_; }
    // The prompt capture's bounded wait (video.capture_wait_ms), and the lag of the picture ReadPresented
    // last returned (0 = this present's, 1 = the previous present's).
    void SetPresentCaptureWaitNs(uint64_t ns) { if (be_) be_->SetPresentCaptureWaitNs(ns); }
    void SetPresentCaptureGate(std::function<bool()> gate) { if (be_) be_->SetPresentCaptureGate(std::move(gate)); }
    int PresentedLag() const { return be_ ? be_->PresentedLag() : 0; }
    // The hidden window's present: a swap (SDL_GL_SwapWindow -> CGLFlushDrawable of a drawable nobody
    // sees) or a plain command flush (the default; the captured image is what the session window shows).
    void SetSwapHiddenWindow(bool swap) { swap_hidden_ = swap; }
    // GL arm only: every blended draw through the exact shader blend (the identity oracle).
    void SetGlForceShaderBlend(bool on) { if (!metal_active_) gl_.SetForceShaderBlend(on); }
    // A SetWindow job changed the window size (consumer thread): the device's capture /
    // fallback viewport follows; the hidden OS window is left alone.
    void OnWindowResized(int w, int h);
    int WindowW() const { return window_w_; }
    int WindowH() const { return window_h_; }

private:
    bool CreateContext(const VideoConfig& video, const char* title);
    bool CreateMetal(const VideoConfig& video);
    void BuildScene(int scale, gs::VramImage& vram);
    void PresentWindow();   // the device's swap callback (consumer thread)

    platform::Window win_;
    platform::GlContext ctx_;
    render::GlDevice gl_;
#if defined(PS2R_HAS_METAL)
    std::unique_ptr<render::MetalDevice> metal_;
#endif
    render::Backend* be_ = nullptr;   // gl_ or *metal_
    bool metal_active_ = false;
    bool backend_auto_ = false;
    std::string renderer_name_;
    std::thread::id current_thread_{};
    uint64_t rebinds_ = 0;
    int window_w_ = 0, window_h_ = 0;

    gs::VramLayout layout_{};   // EMPTY: every query answers false (plain GS)
    gs::VramImage* vram_ = nullptr;
    int scale_factor_ = 1;
    std::unique_ptr<gs::ScalePolicy> scale_;
    std::unique_ptr<gs::RenderTargetCache> cache_;
    std::unique_ptr<gs::TargetRefresher> refresher_;
    std::unique_ptr<gs::TextureResolver> resolver_;
    std::unique_ptr<gs::ScanoutComposer> scanout_;
    std::unique_ptr<gs::Presenter> presenter_;
    std::unique_ptr<gs::DrawPipeline> pipe_;

    Output output_;
    uint64_t output_frames_ = 0;
    bool swap_hidden_ = false;
    bool present_capture_deferred_ = false;
};

}  // namespace ps2r::title::sly2
