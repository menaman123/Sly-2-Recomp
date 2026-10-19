// games/scus_97316_sly2_band_of_thieves/verify/test_shadow_counter_split.cpp -- ShadowCounterSplit unit test
// (ctest Sly2_ShadowCounterSplit): the class test, the overlap test and the rewrite of a wrap-route counter
// draw into the subtract / add fixed-function pair, checked against the GS formula (Cd - Cs mod 256 summed
// over every face) on a synthetic closed volume.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/shadow_counter_split.h"

using ps2r::gs::DrawIr;
using ps2r::gs::VertexRec;
using ps2r::title::sly2::ShadowCounterSplit;

namespace {

int g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; }  \
    } while (0)

VertexRec V(float x, float y, float z, uint8_t g) {
    VertexRec v{};
    v.x = x; v.y = y; v.z = z; v.q = 1.f; v.g = g; v.a = 0x80; v.draw_kick = 1;
    return v;
}

void Tri(DrawIr* d, float x0, float y0, float x1, float y1, float x2, float y2, uint8_t g) {
    d->verts.push_back(V(x0, y0, 1000.f, g));
    d->verts.push_back(V(x1, y1, 1000.f, g));
    d->verts.push_back(V(x2, y2, 1000.f, g));
    d->vertex_count = static_cast<uint32_t>(d->verts.size());
}

// The title's counter draw: untextured flat strip, COLCLAMP off, FBMSK ffff00ff, ALPHA (0-Cs)*0x80+Cd, GREATER.
DrawIr CounterDraw() {
    DrawIr d;
    d.prim_type = 4;
    d.alpha_blend = true;
    d.raster.alpha = {2, 0, 2, 1, 0x80};
    d.raster.frame.fbmsk = 0xffff00ffu;
    d.raster.frame.fbw = 512;
    d.raster.scissor = {0, 0, 511, 223};
    d.raster.test.zte = true;
    d.raster.test.ztst = 3;
    d.raster.colclamp = false;
    return d;
}

// A closed "volume" of two faces covering the same square: front G=0xfe (+2), back G=2 (-2).
DrawIr Volume() {
    DrawIr d = CounterDraw();
    Tri(&d, 10, 10, 50, 10, 10, 50, 0xfe);
    Tri(&d, 50, 10, 50, 50, 10, 50, 0xfe);
    Tri(&d, 10, 10, 50, 10, 10, 50, 2);
    Tri(&d, 50, 10, 50, 50, 10, 50, 2);
    return d;
}

// Software reference: per-pixel GS result over the triangles that pass (all of them here).
int Coverage(const DrawIr& d, float px, float py, int* sum) {
    int cov = 0;
    *sum = 0;
    for (size_t i = 0; i + 2 < d.verts.size(); i += 3) {
        const VertexRec* v = &d.verts[i];
        const float den = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (den == 0.f) continue;
        const float w0 = ((v[1].x - px) * (v[2].y - py) - (v[2].x - px) * (v[1].y - py)) / den;
        const float w1 = ((v[2].x - px) * (v[0].y - py) - (v[0].x - px) * (v[2].y - py)) / den;
        const float w2 = 1.f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        ++cov;
        const int k = (int(v[0].g) * int(d.raster.alpha.fix)) >> 7;
        *sum += (d.raster.alpha.a == 2) ? -k : k;   // 2021 subtracts Cs, 0221 adds it
    }
    return cov;
}

}  // namespace

int main() {
    // 1. The class test.
    DrawIr d = Volume();
    CHECK(ShadowCounterSplit::IsCounterDraw(d));
    CHECK(ShadowCounterSplit::TrianglesOverlap(d));
    DrawIr t = d; t.tex_enabled = true; CHECK(!ShadowCounterSplit::IsCounterDraw(t));
    t = d; t.gouraud = true; CHECK(!ShadowCounterSplit::IsCounterDraw(t));
    t = d; t.raster.colclamp = true; CHECK(!ShadowCounterSplit::IsCounterDraw(t));
    t = d; t.raster.alpha = {0, 1, 0, 1, 0}; CHECK(!ShadowCounterSplit::IsCounterDraw(t));   // the lerp
    t = d; t.raster.alpha.c = 0; CHECK(!ShadowCounterSplit::IsCounterDraw(t));                 // C = As
    t = d; t.raster.frame.fbmsk = 0x00ffffffu; CHECK(!ShadowCounterSplit::IsCounterDraw(t));  // RGB all masked
    t = d; t.prim_type = 6; CHECK(!ShadowCounterSplit::IsCounterDraw(t));                      // sprites
    t = d; t.raster.alpha = {0, 2, 2, 1, 0x80}; CHECK(ShadowCounterSplit::IsCounterDraw(t));   // the 0221 pair member
    // No overlap: two disjoint triangles.
    DrawIr nd = CounterDraw();
    Tri(&nd, 0, 0, 10, 0, 0, 10, 2);
    Tri(&nd, 100, 100, 110, 100, 100, 110, 0xfe);
    CHECK(!ShadowCounterSplit::TrianglesOverlap(nd));

    // 2. The rewrite: the +2 faces become the add batch with colour 2 (256 - 0xfe), the -2 faces the sub batch.
    DrawIr sub, add;
    CHECK(ShadowCounterSplit::Split(d, &sub, &add));
    CHECK(sub.verts.size() == 6 && add.verts.size() == 6);
    CHECK(sub.raster.colclamp && add.raster.colclamp);
    CHECK(sub.raster.alpha.a == 2 && sub.raster.alpha.b == 0 && sub.raster.alpha.c == 2 && sub.raster.alpha.d == 1 && sub.raster.alpha.fix == 0x80);
    CHECK(add.raster.alpha.a == 0 && add.raster.alpha.b == 2 && add.raster.alpha.c == 2 && add.raster.alpha.d == 1 && add.raster.alpha.fix == 0x80);
    for (const VertexRec& v : sub.verts) CHECK(v.g == 2 && v.r == 0 && v.b == 0);
    for (const VertexRec& v : add.verts) CHECK(v.g == 2);
    CHECK(sub.raster.frame.fbmsk == 0xffff00ffu && add.raster.test.ztst == 3 && add.prim_type == 4);
    // The GS reference and the pair agree at every covered pixel: Cd - 2 - 2 + 2 + 2 = Cd (closed volume).
    for (int y = 0; y < 60; y += 7) {
        for (int x = 0; x < 60; x += 7) {
            int ref_sum = 0, ssub = 0, sadd = 0;
            const int cov = Coverage(d, x + 0.5f, y + 0.5f, &ref_sum);
            Coverage(sub, x + 0.5f, y + 0.5f, &ssub);   // 2021: -k per face
            Coverage(add, x + 0.5f, y + 0.5f, &sadd);   // 0221: +k per face
            // The GS: -2 per G=2 face and -0xfe == +2 (mod 256) per G=0xfe face; the pair: -2 and +2.
            const int ref_mod = ((ref_sum % 256) + 256) % 256;
            const int pair_mod = (((ssub + sadd) % 256) + 256) % 256;
            CHECK(ref_mod == pair_mod);
            if (cov == 4) CHECK(pair_mod == 0);
        }
    }

    // 3. A 0221 draw with G=0xfe: Cd + 0xfe == Cd - 2 -> the sub batch with colour 2.
    DrawIr a = CounterDraw();
    a.raster.alpha = {0, 2, 2, 1, 0x80};
    Tri(&a, 10, 10, 50, 10, 10, 50, 0xfe);
    Tri(&a, 10, 10, 50, 10, 10, 50, 2);
    CHECK(ShadowCounterSplit::Split(a, &sub, &add));
    CHECK(sub.verts.size() == 3 && sub.verts[0].g == 2 && add.verts.size() == 3 && add.verts[0].g == 2);

    // 4. FIX scales the count: FIX 0x40 with G=4 -> k = 2 (subtract 2).
    DrawIr f = Volume();
    f.raster.alpha.fix = 0x40;
    for (VertexRec& v : f.verts) v.g = (v.g == 2) ? 4 : 0xfc;   // 0xfc*0x40>>7 = 0x7e -> subtract 126 (not a wrap)
    CHECK(ShadowCounterSplit::Split(f, &sub, &add));
    CHECK(add.verts.empty() && sub.verts.size() == 12);
    int g2 = 0, g126 = 0;
    for (const VertexRec& v : sub.verts) { g2 += v.g == 2; g126 += v.g == 126; }
    CHECK(g2 == 6 && g126 == 6);

    // 5. Declines: a non-flat triangle; unmasked channels disagreeing in direction.
    DrawIr nf = Volume();
    nf.verts[1].g = 0x10;
    CHECK(!ShadowCounterSplit::Split(nf, &sub, &add));
    DrawIr dis = Volume();
    dis.raster.frame.fbmsk = 0xff0000ffu;   // G and B writable
    for (VertexRec& v : dis.verts) v.b = (v.g == 2) ? 0xfe : 2;   // B says the opposite of G
    CHECK(!ShadowCounterSplit::Split(dis, &sub, &add));
    // A no-op channel (k == 0) does not vote.
    DrawIr nop = Volume();
    nop.raster.frame.fbmsk = 0xff0000ffu;
    for (VertexRec& v : nop.verts) v.b = 0;
    CHECK(ShadowCounterSplit::Split(nop, &sub, &add) && sub.verts.size() == 6 && add.verts.size() == 6);

    // 6. The sink: a counter draw arrives as two draws, a plain draw unchanged, disabled = pass-through.
    struct TestShadowCounterSplit : ps2r::gs::DrawIrSink {
        std::vector<DrawIr> got;
        void OnDraw(const DrawIr& x) override { got.push_back(x); }
        void OnJob(const ps2r::gs::JobRec&) override {}
        void OnFrame(const ps2r::gs::JobRec&) override {}
    } rec;
    ShadowCounterSplit sink;
    sink.next = &rec;
    sink.OnDraw(d);
    CHECK(rec.got.size() == 2 && sink.draws_split.load() == 1 && sink.tris_sub.load() == 2 && sink.tris_add.load() == 2);
    sink.OnDraw(nd);
    CHECK(rec.got.size() == 3 && !rec.got[2].raster.colclamp);
    sink.OnDraw(nf);
    CHECK(rec.got.size() == 4 && sink.declined.load() == 1);
    sink.enabled = false;
    sink.OnDraw(d);
    CHECK(rec.got.size() == 5 && rec.got[4].verts.size() == 12);

    std::printf("test_shadow_counter_split: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
