// games/scus_97316_sly2_band_of_thieves/hle/shadow_counter_split.h -- ShadowCounterSplit: the DrawIR
// sink that rewrites the title's stencil-shadow COUNTER draws into two clamped fixed-function draws.
//
// Sly 2 counts shadow-volume faces in the frame buffer's G byte (shadow_notes.md, round H): untextured,
// flat-shaded (IIP = 0) triangle meshes with COLCLAMP = 0, FBMSK = ffff00ff and ALPHA (0 - Cs) * FIX + Cd
// (or (Cs - 0) * FIX + Cd), the kicking vertex's G = 2 for one orientation and 0xfe for the other, so
// the GS computes Cd - 2 or Cd + 2 (mod 256) per face that passes the Z test and a receiver inside a
// closed volume ends two counts away from the start value. The engine's COLCLAMP = 0 route blends in the
// fragment program against ONE copy of the destination per draw: the faces of the SAME draw that overlap
// on screen all read the pre-draw value and the last rasterised face wins (+-2 instead of the sum) -- the
// hall's walls end at "-2" and the floor inside the shaft volumes at 0 (sh_py/sh_raster.py: coverage 2 /
// 4 / 6 / 8 faces per pixel, signed sum 0). This sink detects the class (ABE, TME off, IIP off, COLCLAMP
// off, PABE off, C = FIX, (A, B, D) = (0, Cs, Cd) or (Cs, 0, Cd), triangle class, >= 2 triangles whose
// bounding boxes overlap) and forwards it as up to two draws with COLCLAMP = 1 and FIX = 0x80 -- the
// triangles whose per-channel count k = (Cs * FIX) >> 7 subtracts (k <= 128, colour := k, ALPHA 2021) and
// the ones that add (k > 128 under 2021 = wrap: colour := 256 - k, ALPHA 0221; the mirror for a 0221 draw)
// -- which the backend blends in GL fixed function (REVERSE_SUBTRACT / ADD with the constant FIX), serialised
// per fragment by the GPU, so overlapping faces sum exactly. The only difference from the GS: saturation
// instead of wrap at 0 / 255 -- unreachable for a counter that starts at 0x7e or 0x3e and moves by 2 per
// face. A triangle whose unmasked channels disagree in direction, a non-flat triangle, a draw of more than
// kMaxTriangles or without overlap is forwarded unchanged (the existing route). NATIVEEE_SHADOW_SPLIT=0
// disables the rewrite (RED/GREEN comparisons). Census: shadow_split= draws / sub / add triangles / declined.
#pragma once

#include <atomic>
#include <cstdint>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r::title::sly2 {

struct ShadowCounterSplit : ps2r::gs::DrawIrSink {
    static constexpr size_t kMaxTriangles = 512;

    ps2r::gs::DrawIrSink* next = nullptr;
    bool enabled = true;
    std::atomic<uint64_t> draws_split{0}, tris_sub{0}, tris_add{0}, declined{0};

    // The class test alone (pure): true when `d` is a counter draw this sink would rewrite.
    static bool IsCounterDraw(const ps2r::gs::DrawIr& d);
    // Any two triangles' bounding boxes intersect (pure; false for a single triangle).
    static bool TrianglesOverlap(const ps2r::gs::DrawIr& d);
    // The rewrite (pure): fills `sub` / `add` (either may end empty: verts.empty()); false = declined
    // (a non-flat triangle, or unmasked channels disagreeing in direction) -- forward the original.
    static bool Split(const ps2r::gs::DrawIr& d, ps2r::gs::DrawIr* sub, ps2r::gs::DrawIr* add);

    void OnDraw(const ps2r::gs::DrawIr& d) override;
    void OnJob(const ps2r::gs::JobRec& j) override;
    void OnFrame(const ps2r::gs::JobRec& v) override;
};

}  // namespace ps2r::title::sly2
