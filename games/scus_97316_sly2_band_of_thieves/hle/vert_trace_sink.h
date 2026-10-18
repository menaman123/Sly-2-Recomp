// games/scus_97316_sly2_band_of_thieves/hle/vert_trace_sink.h -- VertTraceSink: the DrawIR sink the title
// composition keeps at the head of GsState's chain, always (the run.meta gs_pages= census counts every
// draw / transfer), with three boot-triage knobs on top:
//   NATIVEEE_GS_VERTS=<key>     every draw of the frame that ENDS at present key <key> (the draws between
//                               boundaries key-1 and key) to stderr with its raster state and vertices;
//   NATIVEEE_GS_SKIP_FROM=<n>   with GS_VERTS: the traced frame's draws from index n on are NOT forwarded
//                               to the renderer (a draw-order bisect of that frame's picture);
//   NATIVEEE_GS_SKIP_CLASS=<c>[,<c>...]  every frame: drop draws of a class -- m16 (a 16-bit FRAME over
//                               the buffers), fbmsk (a partial FBMSK other than alpha-only), t8h (a PSMT8H /
//                               PSMT4HH / PSMT4HL read), rtblend (a textured sprite blending a buffer with
//                               FIX), date (DATE-tested draws).
// Census gs_pages=: the 8 KB VRAM pages a host->local / local->local transfer wrote (InvalidateTextures
// jobs: a1 = dbp bytes, a2 = bytes) and the pages textured draws sample (TEX0 tbp0 / cbp) -- a texture
// page sampled but never written is the "garbage strip" class (blacklevel notes: pages 448..486
// all-zero at frame 600). page_target: every page of the FRAME / ZBUF footprint a draw rendered into
// (base .. the scissor's last row at FBW pixels of the PSM's byte width). A page sampled but only ever
// written by draws is a render-to-texture target (gs_pages rtt=), not an orphan: the Binocucom portraits
// are 128x128 PSMCT32 targets at 0x140000 / 0x150000 (Z 0x170000) drawn ~1000 times a frame and sampled
// by one sprite each (pages_notes.md).
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
    uint32_t skip_from = 0, index = 0;
    bool skip_m16 = false, skip_fbmsk = false, skip_t8h = false, skip_rtblend = false, skip_date = false;
    // Triage classes of the title's two-pass textured objects: `addpass` = the second pass
    // (ALPHA A=Cs B=0 C=As D=Cd, additive with the fragment alpha), `lerppass` = the first
    // (ALPHA A=Cs B=Cd C=As D=Cd) -- the Paris canopy bisect (ep1 stage).
    bool skip_addpass = false, skip_lerppass = false;
    // `tbp=<hex>` -- drop every textured draw reading TEX0.TBP0 at that byte address (which object is it?).
    std::vector<uint32_t> skip_tbp;
    std::array<uint32_t, 512> page_written{}, page_sampled{}, page_target{};

    // Parses the NATIVEEE_GS_SKIP_CLASS list (nullptr = unset) into the skip_* flags.
    void SetSkipClasses(const char* list);
    bool Skipped(const ps2r::gs::DrawIr& d) const;
    void MarkTarget(uint32_t base, uint32_t fbw, uint8_t psm, uint32_t rows);
    void OnDraw(const ps2r::gs::DrawIr& d) override;
    void OnJob(const ps2r::gs::JobRec& j) override;
    void OnFrame(const ps2r::gs::JobRec& v) override;
};

}  // namespace ps2r::title::sly2
