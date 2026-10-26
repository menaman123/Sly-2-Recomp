#include "games/scus_97316_sly2_band_of_thieves/hle/shadow_counter_split.h"

#include <algorithm>
#include <utility>

namespace ps2r::title::sly2 {

namespace {

constexpr uint8_t kPrimTriangle = 3, kPrimTriangleStrip = 4, kPrimTriangleFan = 5;

// The three RGB channels FBMSK leaves writable (bit i set = channel i written).
unsigned WritableRgb(uint32_t fbmsk) {
    unsigned m = 0;
    for (int c = 0; c < 3; ++c)
        if (((fbmsk >> (8 * c)) & 0xFFu) != 0xFFu) m |= 1u << c;
    return m;
}

uint8_t Channel(const ps2r::gs::VertexRec& v, int c) { return c == 0 ? v.r : (c == 1 ? v.g : v.b); }
void SetChannel(ps2r::gs::VertexRec& v, int c, uint8_t x) { (c == 0 ? v.r : (c == 1 ? v.g : v.b)) = x; }

}  // namespace

bool ShadowCounterSplit::IsCounterDraw(const ps2r::gs::DrawIr& d) {
    if (d.tex_enabled || !d.alpha_blend || d.gouraud) return false;
    const auto& r = d.raster;
    if (r.colclamp || r.pabe) return false;
    if (r.alpha.c != 2 || r.alpha.d != 1) return false;   // C = FIX, D = Cd
    const bool sub = r.alpha.a == 2 && r.alpha.b == 0;    // (0 - Cs) * FIX + Cd
    const bool add = r.alpha.a == 0 && r.alpha.b == 2;    // (Cs - 0) * FIX + Cd
    if (!sub && !add) return false;
    if (d.prim_type != kPrimTriangle && d.prim_type != kPrimTriangleStrip && d.prim_type != kPrimTriangleFan) return false;
    if (d.verts.size() < 6 || d.verts.size() % 3 != 0 || d.verts.size() / 3 > kMaxTriangles) return false;
    return WritableRgb(r.frame.fbmsk) != 0;
}

bool ShadowCounterSplit::TrianglesOverlap(const ps2r::gs::DrawIr& d) {
    const size_t n = d.verts.size() / 3;
    if (n < 2) return false;
    struct Box { float x0, y0, x1, y1; };
    std::vector<Box> boxes(n);
    for (size_t i = 0; i < n; ++i) {
        const ps2r::gs::VertexRec* v = &d.verts[i * 3];
        boxes[i] = {std::min({v[0].x, v[1].x, v[2].x}), std::min({v[0].y, v[1].y, v[2].y}),
                    std::max({v[0].x, v[1].x, v[2].x}), std::max({v[0].y, v[1].y, v[2].y})};
    }
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (boxes[i].x0 < boxes[j].x1 && boxes[j].x0 < boxes[i].x1 && boxes[i].y0 < boxes[j].y1 &&
                boxes[j].y0 < boxes[i].y1)
                return true;
    return false;
}

bool ShadowCounterSplit::Split(const ps2r::gs::DrawIr& d, ps2r::gs::DrawIr* sub, ps2r::gs::DrawIr* add) {
    const auto& r = d.raster;
    const bool draw_subtracts = r.alpha.a == 2;   // 2021; else 0221
    const unsigned writable = WritableRgb(r.frame.fbmsk);
    // The two batches share every register but ALPHA (FIX 0x80: (Cs * 0x80) >> 7 == Cs) and COLCLAMP.
    auto seed = [&](ps2r::gs::DrawIr* out, bool subtract) {
        *out = d;
        out->verts.clear();
        out->raster.colclamp = true;
        out->raster.alpha.fix = 0x80;
        out->raster.alpha.a = subtract ? 2 : 0;
        out->raster.alpha.b = subtract ? 0 : 2;
    };
    seed(sub, true);
    seed(add, false);
    const size_t n = d.verts.size() / 3;
    for (size_t i = 0; i < n; ++i) {
        const ps2r::gs::VertexRec* v = &d.verts[i * 3];
        int dir = 0;                          // +1 = the batch adds, -1 = subtracts
        uint8_t mag[3] = {0, 0, 0};
        for (int c = 0; c < 3; ++c) {
            if (!(writable & (1u << c))) continue;
            const uint8_t cs = Channel(v[0], c);
            if (Channel(v[1], c) != cs || Channel(v[2], c) != cs) return false;   // not flat
            const int k = (int(cs) * int(r.alpha.fix)) >> 7;                        // the GS product
            if (k == 0) continue;                                                   // this channel is a no-op
            // 2021: Cd - k  -> subtract k (k <= 128) or add 256 - k (k > 128, the wrap); 0221 mirrored.
            int cdir = (k <= 128) ? -1 : +1;
            if (!draw_subtracts) cdir = -cdir;
            const int m = (k <= 128) ? k : 256 - k;
            if (dir != 0 && cdir != dir) return false;                              // channels disagree
            dir = cdir;
            mag[c] = static_cast<uint8_t>(m);
        }
        if (dir == 0) continue;                                                     // the triangle writes nothing
        ps2r::gs::DrawIr* out = dir < 0 ? sub : add;
        for (int k = 0; k < 3; ++k) {
            ps2r::gs::VertexRec o = v[k];
            for (int c = 0; c < 3; ++c)
                if (writable & (1u << c)) SetChannel(o, c, mag[c]);
            out->verts.push_back(o);
        }
    }
    sub->vertex_count = static_cast<uint32_t>(sub->verts.size());
    add->vertex_count = static_cast<uint32_t>(add->verts.size());
    return true;
}

void ShadowCounterSplit::OnDraw(const ps2r::gs::DrawIr& d) {
    if (!next) return;
    if (!enabled || !IsCounterDraw(d) || !TrianglesOverlap(d)) {
        next->OnDraw(d);
        return;
    }
    if (!SplitAndForward(d)) next->OnDraw(d);
}

void ShadowCounterSplit::OnDrawOwned(ps2r::gs::DrawIr&& d) {
    if (!next) return;
    if (!enabled || !IsCounterDraw(d) || !TrianglesOverlap(d)) {
        next->OnDrawOwned(std::move(d));
        return;
    }
    if (!SplitAndForward(d)) next->OnDrawOwned(std::move(d));
}

// The split halves forwarded (owned); false = declined (the caller forwards `d` whole).
bool ShadowCounterSplit::SplitAndForward(const ps2r::gs::DrawIr& d) {
    ps2r::gs::DrawIr sub, add;
    if (!Split(d, &sub, &add)) {
        declined.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    draws_split.fetch_add(1, std::memory_order_relaxed);
    tris_sub.fetch_add(sub.verts.size() / 3, std::memory_order_relaxed);
    tris_add.fetch_add(add.verts.size() / 3, std::memory_order_relaxed);
    if (!sub.verts.empty()) next->OnDrawOwned(std::move(sub));
    if (!add.verts.empty()) next->OnDrawOwned(std::move(add));
    return true;
}

void ShadowCounterSplit::OnJob(const ps2r::gs::JobRec& j) {
    if (next) next->OnJob(j);
}

void ShadowCounterSplit::OnFrame(const ps2r::gs::JobRec& v) {
    if (next) next->OnFrame(v);
}

}  // namespace ps2r::title::sly2
