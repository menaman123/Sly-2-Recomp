// games/scus_97316_sly2_band_of_thieves/hle/vert_trace_sink.cpp -- VertTraceSink (vert_trace_sink.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/vert_trace_sink.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "engine/gs/hle/ir/job_rec.h"

namespace ps2r::title::sly2 {

void VertTraceSink::SetSkipClasses(const char* list) {
    if (!list) return;
    const std::string c = std::string(",") + list + ",";
    skip_m16 = c.find(",m16,") != std::string::npos;
    skip_fbmsk = c.find(",fbmsk,") != std::string::npos;
    skip_t8h = c.find(",t8h,") != std::string::npos;
    skip_rtblend = c.find(",rtblend,") != std::string::npos;
    skip_date = c.find(",date,") != std::string::npos;
    skip_addpass = c.find(",addpass,") != std::string::npos;
    skip_lerppass = c.find(",lerppass,") != std::string::npos;
}

bool VertTraceSink::Skipped(const ps2r::gs::DrawIr& d) const {
    if (skip_m16 && (d.raster.frame.psm == 2 || d.raster.frame.psm == 10)) return true;
    if (skip_fbmsk && d.raster.frame.fbmsk != 0 && d.raster.frame.fbmsk != 0xff000000u) return true;
    if (skip_t8h && d.tex_enabled && (d.tex.psm == 0x1b || d.tex.psm == 0x24 || d.tex.psm == 0x2c)) return true;
    if (skip_rtblend && d.tex_enabled && d.prim_type == 6 && d.alpha_blend && d.raster.alpha.c == 2 && d.tex.psm == 0) return true;
    if (skip_date && d.raster.test.date) return true;
    if (skip_addpass && d.tex_enabled && d.alpha_blend && d.raster.alpha.a == 0 && d.raster.alpha.b == 2 && d.raster.alpha.c == 0 && d.raster.alpha.d == 1) return true;
    if (skip_lerppass && d.tex_enabled && d.alpha_blend && d.raster.alpha.a == 0 && d.raster.alpha.b == 1 && d.raster.alpha.c == 0 && d.raster.alpha.d == 1) return true;
    return false;
}

void VertTraceSink::MarkTarget(uint32_t base, uint32_t fbw, uint8_t psm, uint32_t rows) {
    const uint32_t bpp = ((psm & 0xfu) == 2u || (psm & 0xfu) == 10u) ? 2u : 4u;
    const uint32_t first = (base >> 13u) & 511u;
    const uint32_t last = ((base + std::max(1u, rows * fbw * bpp) - 1u) >> 13u) & 511u;
    for (uint32_t pg = first; pg <= last && pg < 512; ++pg) ++page_target[pg];
}

void VertTraceSink::OnDraw(const ps2r::gs::DrawIr& d) {
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

void VertTraceSink::OnJob(const ps2r::gs::JobRec& j) {
    if (j.type == static_cast<uint32_t>(ps2r::gs::JobKind::InvalidateTextures) && j.arg2) {
        const uint32_t first = (j.arg1 >> 13u) & 511u, last = ((j.arg1 + j.arg2 - 1) >> 13u) & 511u;
        for (uint32_t pg = first; pg <= last && pg < 512; ++pg) ++page_written[pg];
    }
    if (on.load(std::memory_order_relaxed))
        std::fprintf(stderr, "verts: job type=%u a1=0x%x a2=0x%x a3=0x%x a4=0x%x\n", j.type, j.arg1, j.arg2, j.arg3, j.arg4);
    if (next) next->OnJob(j);
}

void VertTraceSink::OnFrame(const ps2r::gs::JobRec& v) { if (next) next->OnFrame(v); }

}  // namespace ps2r::title::sly2
