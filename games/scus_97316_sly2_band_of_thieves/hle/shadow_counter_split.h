// ShadowCounterSplit: rewrites Sly 2's stencil-shadow counter draws into two clamped fixed-function draws.
//
// Sly 2 counts shadow-volume faces in the frame buffer's G byte: untextured, flat-shaded triangles with
// COLCLAMP = 0, FBMSK = ffff00ff and ALPHA (0 - Cs) * FIX + Cd (or (Cs - 0) * FIX + Cd), with G = 2 for one
// face orientation and 0xfe for the other. Each face passing the Z test adds or subtracts 2 (mod 256), so a
// receiver inside a closed volume ends two counts from the start value.
//
// The engine's COLCLAMP = 0 path blends in the fragment program against one copy of the destination per
// draw, so overlapping faces of the same draw all read the pre-draw value and the last face wins (+-2
// instead of the sum). This sink detects the class (ABE on, TME/IIP/COLCLAMP/PABE off, C = FIX, (A, B, D) =
// (0, Cs, Cd) or (Cs, 0, Cd), at least two triangles with overlapping bounds) and forwards it as up to two
// draws with COLCLAMP = 1 and FIX = 0x80: the subtracting triangles (k = (Cs * FIX) >> 7 <= 128, colour k,
// ALPHA 2021) and the adding ones (k > 128 wraps: colour 256 - k, ALPHA 0221). GL fixed function blends
// those per fragment, so overlapping faces sum exactly. Saturating instead of wrapping at 0 / 255 cannot be
// reached by a counter starting at 0x7e or 0x3e. Anything else is forwarded unchanged.
// NATIVEEE_SHADOW_SPLIT=0 disables the rewrite. Census: shadow_split=.
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
    // Split + forward the halves (owned) to `next`; false = declined (counted), nothing forwarded.
    bool SplitAndForward(const ps2r::gs::DrawIr& d);

    void OnDraw(const ps2r::gs::DrawIr& d) override;
    // The owned form forwards a draw it does not split by move.
    void OnDrawOwned(ps2r::gs::DrawIr&& d) override;
    void OnJob(const ps2r::gs::JobRec& j) override;
    void OnFrame(const ps2r::gs::JobRec& v) override;
};

}  // namespace ps2r::title::sly2
