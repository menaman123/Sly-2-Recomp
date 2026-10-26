// VertTraceSink: always at the head of GsState's DrawIR chain (gs_pages= counts every draw and transfer),
// with three debugging knobs:
//   NATIVEEE_GS_VERTS=<key>     every draw of the frame that ENDS at present key <key> (the draws between
//                               boundaries key-1 and key) to stderr with its raster state and vertices
//                               (NATIVEEE_GS_VERTS_CAP=<n> vertices per draw, default 64);
//   NATIVEEE_GS_SKIP_FROM=<n>   with GS_VERTS: the traced frame's draws from index n on are NOT forwarded
//   [NATIVEEE_GS_SKIP_TO=<m>]   to the renderer (a draw-order bisect of that frame's picture); with SKIP_TO
//                               only the draws with index in [n, m) are dropped (the frame's picture is the
//                               NEXT present: the title displays the buffer a frame finished one vsync later);
//   NATIVEEE_GS_SKIP_CLASS=<c>[,<c>...]  every frame: drop draws of a class -- m16 (a 16-bit FRAME over
//                               the buffers), fbmsk (a partial FBMSK other than alpha-only), t8h (a PSMT8H /
//                               PSMT4HH / PSMT4HL read), rtblend (a textured sprite blending a buffer with
//                               FIX), date (DATE-tested draws).
// Census gs_pages=: the 8 KB VRAM pages transfers wrote and the pages textured draws sampled. A page
// sampled but never written is suspect (garbage texture); a page sampled but only written by draws is a
// render-to-texture target (rtt=), e.g. the Binocucom portraits, 128x128 PSMCT32 targets at 0x140000 /
// 0x150000 sampled by one sprite each.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r::title::sly2 {

struct VertTraceSink : ps2r::gs::DrawIrSink {
    ps2r::gs::DrawIrSink* next = nullptr;
    std::atomic<bool> on{false};
    uint32_t skip_from = 0, skip_to = 0, index = 0;
    uint32_t vert_cap = 64;   // NATIVEEE_GS_VERTS_CAP: vertices printed per traced draw
    bool skip_m16 = false, skip_fbmsk = false, skip_t8h = false, skip_rtblend = false, skip_date = false;
    // Triage classes of the title's two-pass textured objects: `addpass` = the second pass
    // (ALPHA A=Cs B=0 C=As D=Cd, additive with the fragment alpha), `lerppass` = the first
    // (ALPHA A=Cs B=Cd C=As D=Cd).
    bool skip_addpass = false, skip_lerppass = false;
    // `tbp=<hex>` -- drop every textured draw reading TEX0.TBP0 at that byte address (which object is it?).
    std::vector<uint32_t> skip_tbp;
    std::array<uint32_t, 512> page_written{}, page_sampled{}, page_target{};

    // Parses the NATIVEEE_GS_SKIP_CLASS list (nullptr = unset) into the skip_* flags.
    void SetSkipClasses(const char* list);
    bool Skipped(const ps2r::gs::DrawIr& d) const;
    void MarkTarget(uint32_t base, uint32_t fbw, uint8_t psm, uint32_t rows);
    void OnDraw(const ps2r::gs::DrawIr& d) override;
    // The owned form forwards by move (the queue copies the vertex vector otherwise).
    void OnDrawOwned(ps2r::gs::DrawIr&& d) override;
    void OnJob(const ps2r::gs::JobRec& j) override;
    void OnFrame(const ps2r::gs::JobRec& v) override;

    // The census / trace of one draw; false = skipped (not forwarded).
    bool Observe(const ps2r::gs::DrawIr& d);
};

}  // namespace ps2r::title::sly2
