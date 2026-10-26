#!/usr/bin/env python3
"""judge_cel.py -- the Sly 2 CEL-SHADING gate: ink outlines and toon shading judged against PCSX2.

How Sly 2 cel-shades:
  INK   -- after the scene and the stencil-shadow block, the VU1 silhouette pass emits one GS LINE
           primitive per visible silhouette edge of each character (474 lines on Sly in the Paris twin),
           every line as a draw PAIR over the same two vertices: PRIM 0xc1 (LINE, ABE, AA1) + TEST ATE
           ATST NEVER AFAIL RGB_ONLY ZTE ZTST GREATER (the colour pass: RGBAQ 16/16/16/128 flat, Z not
           written), then PRIM 0x01 + AFAIL ZB_ONLY (the Z pass). No hull mesh, no GS trick: the ink IS
           the line list, one pixel wide, at the body's own depth, tested against the body it outlines.
  SHADE -- every lit object is two textured passes of the same PSMT8 / PSMT4 index texture with TWO
           palettes: the lerp pass (ALPHA 0101, As = 0x80 => replace, ATST GEQUAL 126 FB_ONLY, ZTST
           GREATER, writes Z) samples palette A and modulates it by the Gouraud vertex colour (the
           ambient tint), the add pass (ALPHA 0201, Cd + Cs * As, ATST NEVER FB_ONLY) samples palette B
           and modulates by a grey vertex colour with the per-vertex LIGHT INTENSITY in the vertex alpha.
           The shade is therefore continuous per vertex (no quantisation, no ramp CLUT indexed by
           intensity); the "cel" look is the ink plus the hand-painted two-palette textures. The judge
           measures what the twin actually does (band modes, gradient leak) and asks ours to match it.

Metrics per fixture case (ours vs the PCSX2 twin at matched frames; field 512x224). Each side brings
its OWN draw list -- the twin's SaveInfo per-draw dump (<draw>_context.txt + _vertex.txt), ours the
NATIVEEE_GS_VERTS=<key-1> trace (`verts: seq=...` rows) -- so the masks follow each side's own pose
(the idle animation phase differs between the emulators; only the scripted cinematic is pose-matched).
  character mask M = the pixels covered by the BODY-PASS triangles (textured strips with every vertex
  inside char_box and char_z, ALPHA 0101 ATST GEQUAL = the lerp pass); materials = the body-pass TEX0.TBP0.
  ink set I = the pixels of the INK LINE draws (PRIM LINE, flat colour with L < ink_lum).
  a. OUTLINES (own masks, compared as statistics)
     contrast  = (mean L over (dilate(I, 2) minus dilate(I, 1)) - mean L over I) / that surround mean --
                 how dark the ink is relative to its 2-px surroundings (relative, so the few levels of
                 global tone do not count);  ours / twin in [contrast_lo,
                 contrast_hi] (0.85, 1.25)
     visible   = |near-black & I| / |I| (the part of the line list the Z test lets through);
                 ours >= visible_min (0.90) * twin
     thickness = median run-length thickness of the near-black pixels within 1 px of I; |ours - twin|
                 <= max(thick_px (1), thick_rel (0.2) * twin) -- relative once the ink merges (a far character)
     ring (the task's pixel metric) on the TWIN's mask: ring = dilate(M_twin, ring_px) minus M_twin, T = twin
                 near-black ring pixels, O = ours: coverage = |O & T| / |T| (>= coverage_min 0.90), extra =
                 |O minus dilate(T, 1)| / |T| (<= extra_max 0.10) -- GATED only when the case is pose_matched
                 (the cinematic), otherwise reported (the pose offset alone moves it).
     Other ink carriers: ink-line pixels farther than other_px (8) from the body mask (guards, buildings)
     are counted on each side and reported -- "yes" when they exceed 5 % of the twin's ink (the coloured
     LINE draws of the twin are not ink and are listed apart; the ink of the fixtures' frames sits on the
     characters only -- the ink pass runs per character model, not for the world).
  b. TOON BANDS  per material, the luminance histogram inside M & material on each side: dominant modes =
     peaks of the 5-bin-smoothed histogram >= mode_frac (0.15) of the max, >= mode_sep (8) apart and
     separated by a valley of prominence >= 0.20 max; ours must have the same count (+-0) with positions
     within mode_px (6), and the W1 distance of the two normalised histograms <= band_w1_max (8 levels).
     Gated for materials with >= material_gate_px (800) pixels on both sides (smaller regions -- a few
     hundred pixels of a hat or a glove -- give noisy mode sets; reported). gradient leak = fraction of mask pixels whose 3x3 luminance std lies in
     [leak_lo, step / 4) with step = the twin's median mode gap (or leak_step_default when it has one
     mode; a hard step of `step` gives a 3x3 std of 0.31..0.47 step, a ramp over >= 4 px <= 0.2 step),
     measured on the mask MINUS dilate(own ink lines, 1) (the ink is not shading; its own edge would be
     counted as a step on one side and as a leak on the other when the ink tone differs by a few levels) --
     "smooth where the twin steps": ours <= twin + leak_max (0.02).
  c. RAMP IDENTITY  the character's textures (every body-pass TBP0, BOTH palettes = the lerp and the add
     CLUT) decode byte-identical to the twin's per-draw itex dumps (judge_textures' Oracle A match by TEX0
     tuple): max |diff| == 0 on RGB + alpha.

Exit 0 PASS, 1 FAIL, 77 SKIP (inputs absent). `--selftest`: synthetic frames / draw lists. The
fixture (verify/cel/cel_fixture.json) holds numbers only; frames, dumps and twins are game-derived and
stay outside the repository.

Usage:
  judge_cel.py --fixture <json> --case <name> --ours-frame <ppm> --ours-verts <log> --twin-frame <png>
               --twin-dump <pcsx2 hwdump dir> [--twin-frame-id N] [--ours-textures <dir>]
               [--artefacts <dir>] [--summary] [--label gl|metal]
  judge_cel.py --selftest
"""
import argparse
import glob
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from judge_textures import read_png, read_pnm, write_png, Img  # noqa: E402
import judge_textures as jt  # noqa: E402

W, H = 512, 224


# ----------------------------------------------------------------------------- frames
def lum(r, g, b):
    return (77 * r + 150 * g + 29 * b) >> 8


def to_field(w, h, ch, px):
    """RGB(A) image -> flat list of (r, g, b) at 512x224: pair-averaged rows for a line-doubled 448-row
    frame, the 513x226 CRTC field cropped to its 512x224 content (x 0..511, y 1..224)."""
    def at(x, y):
        i = (y * w + x) * ch
        return px[i], px[i + 1], px[i + 2]
    out = [None] * (W * H)
    if (w, h) == (W, H):
        for y in range(H):
            for x in range(W):
                out[y * W + x] = at(x, y)
    elif w == W and h == 2 * H:
        for y in range(H):
            for x in range(W):
                a, b = at(x, 2 * y), at(x, 2 * y + 1)
                out[y * W + x] = ((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, (a[2] + b[2]) >> 1)
    elif w >= W and h >= H:
        ox, oy = (w - W) // 2, (h - H) // 2
        for y in range(H):
            for x in range(W):
                out[y * W + x] = at(x + ox, y + oy)
    else:
        raise ValueError("unsupported frame size %dx%d" % (w, h))
    return out


def load_frame(path):
    if path.lower().endswith(".png"):
        w, h, ch, px = read_png(path)
        if ch < 3:
            raise ValueError("twin must be RGB / RGBA")
        return to_field(w, h, ch, px)
    kind, w, h, body = read_pnm(path)
    if kind != "rgb":
        raise ValueError("ours must be a P6 PPM")
    return to_field(w, h, 3, body)


def lum_plane(frame):
    return [lum(*p) for p in frame]


# ----------------------------------------------------------------------------- masks
class Mask:
    def __init__(self, data=None):
        self.px = bytearray(data) if data is not None else bytearray(W * H)

    def count(self):
        return sum(self.px)

    def dilate(self, r):
        """Chebyshev dilation by r pixels (separable max filter)."""
        if r <= 0:
            return Mask(self.px)
        tmp = bytearray(W * H)
        src = self.px
        for y in range(H):
            row = y * W
            for x in range(W):
                lo, hi = max(0, x - r), min(W, x + r + 1)
                if any(src[row + lo:row + hi]):
                    tmp[row + x] = 1
        out = bytearray(W * H)
        for x in range(W):
            for y in range(H):
                lo, hi = max(0, y - r), min(H, y + r + 1)
                for yy in range(lo, hi):
                    if tmp[yy * W + x]:
                        out[y * W + x] = 1
                        break
        return Mask(out)

    def __and__(self, o):
        return Mask(bytes(a & b for a, b in zip(self.px, o.px)))

    def __or__(self, o):
        return Mask(bytes(a | b for a, b in zip(self.px, o.px)))

    def minus(self, o):
        return Mask(bytes(a & (1 - b) for a, b in zip(self.px, o.px)))


def raster_triangle(mask, ax, ay, bx, by, cx, cy):
    """Marks the GS sample points (integer (i, j)) inside the triangle, inclusive edges."""
    x0, x1 = max(0, int(math.floor(min(ax, bx, cx)))), min(W - 1, int(math.ceil(max(ax, bx, cx))))
    y0, y1 = max(0, int(math.floor(min(ay, by, cy)))), min(H - 1, int(math.ceil(max(ay, by, cy))))
    if x0 > x1 or y0 > y1:
        return
    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    if abs(area) < 1e-9:
        return
    eps = 1e-6 * abs(area)
    px = mask.px
    for j in range(y0, y1 + 1):
        row = j * W
        for i in range(x0, x1 + 1):
            e0 = (bx - ax) * (j - ay) - (by - ay) * (i - ax)
            e1 = (cx - bx) * (j - by) - (cy - by) * (i - bx)
            e2 = (ax - cx) * (j - cy) - (ay - cy) * (i - cx)
            if area > 0:
                if e0 >= -eps and e1 >= -eps and e2 >= -eps:
                    px[row + i] = 1
            else:
                if e0 <= eps and e1 <= eps and e2 <= eps:
                    px[row + i] = 1


def raster_line(mask, ax, ay, bx, by):
    """The GS one-pixel (DDA) line: along the major axis, every integer step between the endpoints
    takes the one sample nearest the line (|minor offset| <= 0.5) -- what PrimExpand's parallelogram
    draws; a zero-length line is its one nearest pixel."""
    dx, dy = bx - ax, by - ay
    px = mask.px
    if abs(dx) < 1e-9 and abs(dy) < 1e-9:
        i, j = int(round(ax)), int(round(ay))
        if 0 <= i < W and 0 <= j < H:
            px[j * W + i] = 1
        return
    if abs(dx) >= abs(dy):
        if ax > bx:
            ax, ay, bx, by, dx, dy = bx, by, ax, ay, -dx, -dy
        for i in range(max(0, int(math.ceil(ax))), min(W - 1, int(math.floor(bx))) + 1):
            y = ay + (i - ax) * dy / dx
            j = int(math.floor(y + 0.5))
            if 0 <= j < H and abs(j - y) <= 0.5:
                px[j * W + i] = 1
    else:
        if ay > by:
            ax, ay, bx, by, dx, dy = bx, by, ax, ay, -dx, -dy
        for j in range(max(0, int(math.ceil(ay))), min(H - 1, int(math.floor(by))) + 1):
            x = ax + (j - ay) * dx / dy
            i = int(math.floor(x + 0.5))
            if 0 <= i < W and abs(i - x) <= 0.5:
                px[j * W + i] = 1


# ----------------------------------------------------------------------------- the twin's draw list
VERT_TRI_RE = re.compile(r"X:\s*([-\d.]+), Y:\s*([-\d.]+), Z:\s*(\d+), U:\s*([-\d.e+]+), V:\s*([-\d.e+]+), R:\s*(\d+), G:\s*(\d+), B:\s*(\d+), A:\s*(\d+)")
VERT_LINE_RE = re.compile(r"X:\s*([-\d.]+), Y:\s*([-\d.]+), Z:\s*(\d+), R:\s*(\d+), G:\s*(\d+), B:\s*(\d+), A:\s*(\d+)")


def parse_ctx(path):
    sec, cur = {}, None
    for line in open(path, errors="replace"):
        if not line.strip():
            continue
        if not line.startswith(" "):
            cur = line.strip().rstrip(":")
            sec[cur] = {}
        else:
            m = re.match(r"\s*(\w+):\s*([-\w.x]+)", line)
            if m and cur:
                sec[cur][m.group(1)] = m.group(2)
    return sec


def num(s):
    return int(s, 0) if s.startswith("0x") else int(s)


def parse_verts(path):
    """[(x, y, z, r, g, b, a)] -- PCSX2's SaveInfo vertex.txt (XYZ already XYOFFSET-relative, Y - 0.5)."""
    vs = []
    for line in open(path, errors="replace"):
        m = VERT_TRI_RE.search(line)
        if m:
            vs.append((float(m.group(1)), float(m.group(2)), int(m.group(3)), int(m.group(6)), int(m.group(7)), int(m.group(8)), int(m.group(9))))
            continue
        m = VERT_LINE_RE.search(line)
        if m:
            vs.append((float(m.group(1)), float(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7))))
    return vs


def twin_draws(dump_dir, frame_id):
    """Every SaveInfo draw of PCSX2 frame `frame_id` (by the f<frame> of its texture / rt files, or every
    draw when the dump holds a single frame): [(idx, ctx, verts)]."""
    ids = set()
    frame_of = {}
    for name in os.listdir(dump_dir):
        m = re.match(r"(\d+)_(?:context\.txt|f(\d+)_)", name)
        if not m:
            continue
        i = int(m.group(1))
        ids.add(i)
        if m.group(2):
            frame_of[i] = int(m.group(2))
    ids = sorted(ids)
    if frame_id is not None and frame_of:
        # Draws without a dumped file (untextured) belong to the frame of their neighbours.
        keep, cur = [], None
        for i in ids:
            if i in frame_of:
                cur = frame_of[i]
            if cur == frame_id or (cur is None and frame_id == min(frame_of.values())):
                keep.append(i)
        ids = keep
    out = []
    for i in ids:
        cp, vp = os.path.join(dump_dir, "%d_context.txt" % i), os.path.join(dump_dir, "%d_vertex.txt" % i)
        if not (os.path.exists(cp) and os.path.exists(vp)):
            continue
        out.append((i, parse_ctx(cp), parse_verts(vp)))
    return out


OURS_DRAW_RE = re.compile(r"verts: seq=(\d+) prim=(\d+) tme=(\d) abe=(\d) .*? ztst=(\d) ate=(\d) atst=(\d) aref=(\d+) .*? tbp=0x([0-9a-f]+) .*? alpha=(\d)(\d)(\d)(\d)/(\d+) n=(\d+)")
OURS_VERT_RE = re.compile(r"verts:\s+v\d+ xyz=([-\d.]+),([-\d.]+),([-\d.]+) .*? rgba=([0-9a-f]{8})")


def ours_draws(log_path):
    """Our NATIVEEE_GS_VERTS trace of one frame as the twin's shape: [(seq, ctx, verts)]; verts
    (x, y, z, r, g, b, a) -- the kick output after XYOFFSET, triangles listed 3 per primitive."""
    out = []
    cur = None
    for line in open(log_path, errors="replace"):
        if not line.startswith("verts:"):
            continue
        m = OURS_DRAW_RE.match(line)
        if m:
            ctx = {"PRIM": {"PRIM": m.group(2), "TME": m.group(3), "ABE": m.group(4)},
                   "TEST": {"ZTST": m.group(5), "ATE": m.group(6), "ATST": m.group(7), "AREF": m.group(8)},
                   "TEX0": {"TBP0": "0x%x" % (int(m.group(9), 16) // 256)},
                   "ALPHA": {"A": m.group(10), "B": m.group(11), "C": m.group(12), "D": m.group(13), "FIX": m.group(14)}}
            cur = (int(m.group(1)), ctx, [])
            out.append(cur)
            continue
        m = OURS_VERT_RE.match(line)
        if m and cur is not None:
            c = int(m.group(4), 16)
            cur[2].append((float(m.group(1)), float(m.group(2)), int(float(m.group(3))), (c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255))
    return out


def is_body_pass(ctx, verts, box, zr):
    P, A, T = ctx.get("PRIM", {}), ctx.get("ALPHA", {}), ctx.get("TEST", {})
    if P.get("PRIM") not in ("3", "4", "5") or P.get("TME") != "1" or not verts:
        return False
    if (A.get("A"), A.get("B"), A.get("C"), A.get("D")) != ("0", "1", "0", "1"):
        return False
    if T.get("ATST") != "5":  # GEQUAL: the opaque body pass (the add pass is ATST NEVER)
        return False
    # The lerp pass tests GEQUAL 126 (the docstring's "ATST GEQUAL 126 FB_ONLY"); the particle sprites the
    # round-6 macro-flag fix brought back are GEQUAL 128 with the same ALPHA 0101 and must not widen the body.
    if num(T.get("AREF", "126")) != 126:
        return False
    for v in verts:
        if not (box[0] <= v[0] <= box[2] and box[1] <= v[1] <= box[3] and zr[0] <= v[2] <= zr[1]):
            return False
    return True


def strip_triangles(verts):
    """PCSX2's vertex.txt lists the kicked primitives already assembled: a '# TRIANGLE' list of 3n."""
    tris = []
    n = len(verts) // 3
    for k in range(n):
        tris.append(verts[3 * k:3 * k + 3])
    return tris


def character_masks(draws, box, zr):
    """(mask M, {tbp0: Mask}, n_draws) from the twin's body-pass draws inside the box / z range."""
    M = Mask()
    mats = {}
    n = 0
    for _, ctx, verts in draws:
        if not is_body_pass(ctx, verts, box, zr):
            continue
        n += 1
        tbp = num(ctx["TEX0"]["TBP0"]) * 256
        mm = mats.setdefault(tbp, Mask())
        for a, b, c in strip_triangles(verts):
            raster_triangle(M, a[0], a[1], b[0], b[1], c[0], c[1])
            raster_triangle(mm, a[0], a[1], b[0], b[1], c[0], c[1])
    return M, mats, n


def line_mask(draws, ink_lum):
    """(ink pixels, ink line draws, other-colour line draws, colour census) over every LINE draw whose
    flat colour is near-black (L < ink_lum): the ink. Coloured lines (lasers, beacons) are counted apart."""
    Lm = Mask()
    n_ink = n_other = 0
    colours = {}
    for _, ctx, verts in draws:
        if ctx.get("PRIM", {}).get("PRIM") not in ("1", "2") or len(verts) < 2:
            continue
        a0 = verts[0]
        colours[(a0[3], a0[4], a0[5], a0[6])] = colours.get((a0[3], a0[4], a0[5], a0[6]), 0) + 1
        if lum(a0[3], a0[4], a0[5]) >= ink_lum:
            n_other += 1
            continue
        n_ink += 1
        for k in range(0, len(verts) - 1, 1 if ctx["PRIM"]["PRIM"] == "2" else 2):
            a, b = verts[k], verts[k + 1]
            raster_line(Lm, a[0], a[1], b[0], b[1])
    return Lm, n_ink, n_other, colours


def mean_lum(lp, mask):
    n = t = 0
    for i, m in enumerate(mask.px):
        if m:
            n += 1
            t += lp[i]
    return t / n if n else 0.0


# ----------------------------------------------------------------------------- metrics
def ink_mask(lp, ink_lum):
    return Mask(bytes(1 if v < ink_lum else 0 for v in lp))


def run_thickness(ink, zone):
    """Per ink pixel inside `zone`: min(horizontal run, vertical run) of ink through it; the list."""
    px, zp = ink.px, zone.px
    hrun = [0] * (W * H)
    for y in range(H):
        row = y * W
        x = 0
        while x < W:
            if px[row + x]:
                x0 = x
                while x < W and px[row + x]:
                    x += 1
                for k in range(x0, x):
                    hrun[row + k] = x - x0
            else:
                x += 1
    vrun = [0] * (W * H)
    for x in range(W):
        y = 0
        while y < H:
            if px[y * W + x]:
                y0 = y
                while y < H and px[y * W + x]:
                    y += 1
                for k in range(y0, y):
                    vrun[k * W + x] = y - y0
            else:
                y += 1
    return [min(hrun[i], vrun[i]) for i in range(W * H) if px[i] and zp[i]]


def median(xs):
    if not xs:
        return 0.0
    s = sorted(xs)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def histogram(lp, mask):
    h = [0] * 256
    for i, m in enumerate(mask.px):
        if m:
            h[lp[i]] += 1
    return h


def modes(hist, frac, sep, prom=0.20, win=2):
    """Dominant modes of the (2*win+1)-bin box-smoothed histogram: local maxima >= frac * max, kept
    greedily in height order when >= sep levels from every kept mode AND separated from each of them
    by a valley at least prom * max below the candidate (prominence: a shoulder on a broad hump is not
    a mode). Returns sorted positions."""
    sm = [0.0] * 256
    for i in range(256):
        lo, hi = max(0, i - win), min(256, i + win + 1)
        sm[i] = sum(hist[lo:hi]) / (hi - lo)
    mx = max(sm) if sm else 0
    if mx <= 0:
        return []
    # Local maxima; a plateau of equal values is one candidate at its centre.
    cands = []
    i = 0
    while i < 256:
        j = i
        while j + 1 < 256 and sm[j + 1] == sm[i]:
            j += 1
        l = sm[i - 1] if i > 0 else -1
        r = sm[j + 1] if j < 255 else -1
        if sm[i] >= frac * mx and sm[i] > l and sm[i] > r:
            cands.append((sm[i], (i + j) // 2))
        i = j + 1
    cands.sort(reverse=True)
    kept = []
    for hgt, pos in cands:
        ok = True
        for k in kept:
            if abs(pos - k) < sep:
                ok = False
                break
            valley = min(sm[min(pos, k):max(pos, k) + 1])
            if valley > hgt - prom * mx:
                ok = False
                break
        if ok:
            kept.append(pos)
    return sorted(kept)


def wasserstein1(ha, hb):
    """W1 distance (levels) between two luminance histograms as distributions."""
    na, nb = sum(ha), sum(hb)
    if not na or not nb:
        return 0.0
    ca = cb = 0.0
    d = 0.0
    for v in range(256):
        ca += ha[v] / na
        cb += hb[v] / nb
        d += abs(ca - cb)
    return d


def local_std(lp, i):
    x, y = i % W, i // W
    vals = []
    for dy in (-1, 0, 1):
        yy = y + dy
        if yy < 0 or yy >= H:
            continue
        for dx in (-1, 0, 1):
            xx = x + dx
            if 0 <= xx < W:
                vals.append(lp[yy * W + xx])
    m = sum(vals) / len(vals)
    return math.sqrt(sum((v - m) ** 2 for v in vals) / len(vals))


def gradient_leak(lp, mask, lo, hi):
    n = leak = 0
    for i, m in enumerate(mask.px):
        if not m:
            continue
        n += 1
        s = local_std(lp, i)
        if lo <= s < hi:
            leak += 1
    return leak / n if n else 0.0, n


# ----------------------------------------------------------------------------- ramp identity (Oracle A)
def ramp_identity(ours_tex_dir, twin_dump, tbps):
    """Our dumped textures with TBP0 in `tbps` vs the twin's itex dumps (judge_textures' matching):
    [(tbp0, cbp, matched, max_abs_diff, note)]."""
    rows = []
    if not ours_tex_dir or not os.path.isdir(ours_tex_dir):
        return rows, "no ours textures"
    if not glob.glob(os.path.join(twin_dump, "*_itex_*.png")):
        return rows, "the twin dump has no per-draw texture files (SaveTexture off)"
    ours, _ = jt.load_ours(ours_tex_dir)
    pcsx2 = jt.load_pcsx2(twin_dump)
    by_key = {}
    for r in pcsx2:
        by_key.setdefault(jt.pcsx2_key_for_match(r["key"], r["key"][1]), []).append(r)
    for j in ours:
        if j["tbp0"] not in tbps:
            continue
        cands = [c for c in by_key.get(jt.pcsx2_key_for_match(jt.our_key(j), j["psm"]), []) if c["src"] == "gs"]
        if not cands:
            rows.append((j["tbp0"], j["cbp"], False, None, "no twin texture with this TEX0 tuple"))
            continue
        a = jt.read_ours(j["_base"])
        best = None
        for c in cands:
            b = jt.read_pcsx2_texture(c["rgb"], c["alpha"])
            ox, oy = (j["x0"], j["y0"]) if j.get("sub") else (0, 0)
            if ox + a.w > b.w or oy + a.h > b.h:
                continue
            bc = b.crop(ox, oy, a.w, a.h) if (ox, oy, a.w, a.h) != (0, 0, b.w, b.h) else b
            mx = 0
            for i in range(a.w * a.h * 4):
                d = abs(a.px[i] - bc.px[i])
                if d > mx:
                    mx = d
            if best is None or mx < best[0]:
                best = (mx, c)
        if best is None:
            rows.append((j["tbp0"], j["cbp"], False, None, "size mismatch"))
        else:
            rows.append((j["tbp0"], j["cbp"], True, best[0], "vs PCSX2 draw %d" % best[1]["draw"]))
    return rows, "%d of our textures in the character set" % len(rows)


# ----------------------------------------------------------------------------- artefacts
def sheet(path, ours_lp, twin_lp, M, ring, ours_ink, twin_ink, box):
    """ours L | twin L | overlay: mask grey, ring dark, ink: green both, red ours only, blue twin only."""
    x0, y0, x1, y1 = [int(v) for v in box]
    x0, y0 = max(0, x0 - 8), max(0, y0 - 8)
    x1, y1 = min(W, x1 + 8), min(H, y1 + 8)
    cw, chh = x1 - x0, y1 - y0
    S = 3
    img = Img(cw * S * 3 + 2 * S, chh * S)
    px = img.px
    for yy in range(chh):
        for xx in range(cw):
            i = (y0 + yy) * W + (x0 + xx)
            cols = [(ours_lp[i],) * 3, (twin_lp[i],) * 3]
            o, t = ours_ink.px[i] and ring.px[i], twin_ink.px[i] and ring.px[i]
            if o and t:
                ov = (0, 200, 0)
            elif o:
                ov = (230, 40, 40)
            elif t:
                ov = (60, 90, 255)
            elif M.px[i]:
                ov = (110, 110, 110)
            elif ring.px[i]:
                ov = (50, 50, 50)
            else:
                ov = (20, 20, 20)
            cols.append(ov)
            for k, c in enumerate(cols):
                for sy in range(S):
                    for sx in range(S):
                        X = k * (cw * S + S) + xx * S + sx
                        Y = yy * S + sy
                        o4 = (Y * img.w + X) * 4
                        px[o4], px[o4 + 1], px[o4 + 2], px[o4 + 3] = c[0], c[1], c[2], 255
    write_png(path, img)


# ----------------------------------------------------------------------------- one case
def shift_draws(draws, dx, dy):
    if dx == 0 and dy == 0:
        return draws
    return [(i, c, [(v[0] + dx, v[1] + dy) + tuple(v[2:]) for v in vs]) for i, c, vs in draws]


def align_geometry(lp, draws, ink_l, steps=(-1.0, -0.5, 0.0, 0.5, 1.0)):
    """The draw list's vertex convention vs the frame's pixel grid (PCSX2's vertex.txt prints Y - 0.5;
    the CRTC field crop may sit one row off): the half-pixel shift (dx, dy) that puts the most ink-line
    pixels on near-black frame pixels. Returns (dx, dy, visible)."""
    dark = ink_mask(lp, ink_l)
    best = (0.0, 0.0, -1.0)
    for dy in steps:
        for dx in steps:
            Lm, n, _, _ = line_mask(shift_draws(draws, dx, dy), ink_l)
            if not Lm.count():
                return (0.0, 0.0, 0.0)
            vis = (dark & Lm).count() / Lm.count()
            if vis > best[2] + 1e-9 or (abs(vis - best[2]) <= 1e-9 and abs(dx) + abs(dy) < abs(best[0]) + abs(best[1])):
                best = (dx, dy, vis)
    return best


def side_stats(lp, draws, case, label, out):
    """One side: masks, materials, ink set and the statistics the gates compare."""
    box, zr = case["char_box"], case["char_z"]
    ink_l = case.get("ink_lum", 40)
    dx, dy, _ = align_geometry(lp, draws, ink_l) if case.get("align", True) else (0.0, 0.0, 0.0)
    draws = shift_draws(draws, dx, dy)
    M, mats, nbody = character_masks(draws, box, zr)
    Lm, n_ink, n_other, colours = line_mask(draws, ink_l)
    dark = ink_mask(lp, ink_l)
    st = {"body_draws": nbody, "mask_px": M.count(), "ink_lines": n_ink, "other_lines": n_other, "ink_px": Lm.count(),
          "shift": (dx, dy), "line_colours": {"%d,%d,%d,%d" % k: v for k, v in colours.items()}}
    if Lm.count():
        d1, d2 = Lm.dilate(1), Lm.dilate(2)
        sur = mean_lum(lp, d2.minus(d1))
        st["contrast"] = (sur - mean_lum(lp, Lm)) / sur if sur > 0 else 0.0
        st["visible"] = (dark & Lm).count() / Lm.count()
        st["thick"] = median(run_thickness(dark & d1, d2))
        st["ink_px_char"] = (Lm & M.dilate(2)).count()
        st["ink_px_other"] = Lm.minus(M.dilate(2)).count()
        st["ink_px_far"] = Lm.minus(M.dilate(case.get("other_px", 8))).count()   # anything but this character
    else:
        st.update({"contrast": 0.0, "visible": 0.0, "thick": 0.0, "ink_px_char": 0, "ink_px_other": 0, "ink_px_far": 0})
    st["M"], st["mats"], st["Lm"], st["dark"] = M, mats, Lm, dark
    print("  %s: geometry shift (%+.1f, %+.1f); %d body draws -> mask %d px, %d materials; %d ink LINE draws (%d other-colour lines) -> %d ink px (%d on the character, %d elsewhere); contrast %.3f, visible %.3f, thickness %.1f; line colours %s" % (
        label, dx, dy, nbody, M.count(), len(mats), n_ink, n_other, Lm.count(), st["ink_px_char"], st["ink_px_other"], st["contrast"], st["visible"], st["thick"], st["line_colours"]), file=out)
    return st


def judge_case(case, ours_lp, twin_lp, ours_dl, twin_dl, ours_tex_dir, twin_dump, artefacts, label, out):
    res = {}
    verdicts = []
    tw = side_stats(twin_lp, twin_dl, case, "twin", out)
    ou = side_stats(ours_lp, ours_dl, case, "ours", out)
    for k in ("body_draws", "mask_px", "ink_lines", "other_lines", "ink_px", "ink_px_char", "ink_px_other", "ink_px_far", "contrast", "visible", "thick", "line_colours", "shift"):
        res[k + "_twin"], res[k + "_ours"] = tw[k], ou[k]
    if tw["body_draws"] == 0 or tw["mask_px"] < 50:
        print("  twin: no body-pass draws inside char_box %s z %s -- cannot build the mask" % (case["char_box"], case["char_z"]), file=out)
        return False, res
    if ou["body_draws"] == 0 or ou["mask_px"] < 50:
        print("  ours: no body-pass draws inside char_box %s z %s -- the verts trace does not show the character" % (case["char_box"], case["char_z"]), file=out)
        return False, res
    # a. outlines -- statistics over each side's own ink set.
    c_lo, c_hi = case.get("contrast_lo", 0.85), case.get("contrast_hi", 1.25)
    c_ratio = ou["contrast"] / tw["contrast"] if tw["contrast"] > 0 else (1.0 if ou["contrast"] <= 0 else 99.0)
    ok_c = tw["ink_lines"] == 0 or (c_lo <= c_ratio <= c_hi)
    v_ratio = ou["visible"] / tw["visible"] if tw["visible"] > 0 else 1.0
    ok_v = tw["ink_lines"] == 0 or v_ratio >= case.get("visible_min", 0.90)
    # |d| <= thick_px, or <= thick_rel of the twin's when the ink is dense enough to merge (a far character:
    # 1000 silhouette lines on a 70x48 px body give run thicknesses of ~10 on both sides).
    thick_tol = max(case.get("thick_px", 1.0), case.get("thick_rel", 0.2) * tw["thick"])
    ok_th = abs(ou["thick"] - tw["thick"]) <= thick_tol
    n_ratio = ou["ink_lines"] / tw["ink_lines"] if tw["ink_lines"] else (0.0 if ou["ink_lines"] else 1.0)
    res.update({"contrast_ratio": c_ratio, "visible_ratio": v_ratio, "ink_lines_ratio": n_ratio})
    far_frac = tw["ink_px_far"] / tw["ink_px"] if tw["ink_px"] else 0.0
    print("  outlines: contrast ours/twin %.3f (in [%.2f, %.2f]) %s; visible ours/twin %.3f (>= %.2f) %s; thickness ours %.1f twin %.1f (|d| <= %.1f) %s; ink line count ours/twin %.3f; ink px > %d px from the body mask: ours %d twin %d (%.1f%% of the twin's ink) -> other carriers (guards / buildings) in the twin frame: %s" % (
        c_ratio, c_lo, c_hi, "PASS" if ok_c else "FAIL", v_ratio, case.get("visible_min", 0.90), "PASS" if ok_v else "FAIL", ou["thick"], tw["thick"],
        thick_tol, "PASS" if ok_th else "FAIL", n_ratio, case.get("other_px", 8), ou["ink_px_far"], tw["ink_px_far"], 100.0 * far_frac, "yes" if far_frac > 0.05 else "no"), file=out)
    verdicts += [ok_c, ok_v, ok_th]
    # the task's ring metric on the twin's mask (gated when pose-matched).
    M = tw["M"]
    ring = M.dilate(case.get("ring_px", 2)).minus(M)
    T, O = tw["dark"] & ring, ou["dark"] & ring
    nT = T.count()
    cov = (O & T).count() / nT if nT else 0.0
    extra_strict = O.minus(T).count() / nT if nT else 0.0
    extra = O.minus(T.dilate(1)).count() / nT if nT else 0.0
    pose = bool(case.get("pose_matched", False))
    ok_cov, ok_extra = cov >= case.get("coverage_min", 0.90), extra <= case.get("extra_max", 0.10)
    res.update({"ring_twin_ink": nT, "ring_ours_ink": O.count(), "coverage": cov, "extra": extra, "extra_strict": extra_strict, "pose_matched": pose})
    print("  ring (twin mask, %s): twin ring ink %d px, ours %d; coverage %.3f (>= %.2f) %s; extra %.3f (<= %.2f, strict %.3f) %s" % (
        "GATED: pose-matched case" if pose else "reported only: poses differ", nT, O.count(), cov, case.get("coverage_min", 0.90),
        "PASS" if ok_cov else "FAIL", extra, case.get("extra_max", 0.10), extra_strict, "PASS" if ok_extra else "FAIL"), file=out)
    if pose:
        verdicts += [ok_cov, ok_extra]
    # b. toon bands per material, each side inside its own mask.
    bands = []
    ok_bands = True
    tw_steps = []
    for tbp in sorted(tw["mats"]):
        mt_m = tw["mats"][tbp] & M
        mo_m = (ou["mats"].get(tbp) or Mask()) & ou["M"]
        if mt_m.count() < case.get("material_min_px", 60):
            continue
        if mo_m.count() < case.get("material_min_px", 60):
            bands.append({"tbp0": "%06x" % tbp, "px_twin": mt_m.count(), "px_ours": mo_m.count(), "ok": False, "note": "material absent in ours"})
            ok_bands = False
            print("  bands tbp0=%06x: twin %d px, ours %d px -- material absent in ours FAIL" % (tbp, mt_m.count(), mo_m.count()), file=out)
            continue
        ho, ht = histogram(ours_lp, mo_m), histogram(twin_lp, mt_m)
        mo, mt = modes(ho, case.get("mode_frac", 0.15), case.get("mode_sep", 8)), modes(ht, case.get("mode_frac", 0.15), case.get("mode_sep", 8))
        w1 = wasserstein1(ho, ht)
        same_n = len(mo) == len(mt)
        pos_ok = same_n and all(abs(a - b) <= case.get("mode_px", 6) for a, b in zip(mo, mt))
        w1_ok = w1 <= case.get("band_w1_max", 8.0)
        gated = min(mt_m.count(), mo_m.count()) >= case.get("material_gate_px", 800)
        if len(mt) >= 2:
            tw_steps += [b - a for a, b in zip(mt, mt[1:])]
        bands.append({"tbp0": "%06x" % tbp, "px_twin": mt_m.count(), "px_ours": mo_m.count(), "modes_ours": mo, "modes_twin": mt, "w1": w1, "gated": gated, "ok": pos_ok and w1_ok})
        if gated:
            ok_bands = ok_bands and pos_ok and w1_ok
        print("  bands tbp0=%06x (twin %d px, ours %d px, %s): modes ours %s twin %s %s; W1 %.2f (<= %.1f) %s" % (
            tbp, mt_m.count(), mo_m.count(), "GATED" if gated else "reported: < material_gate_px", mo, mt, "PASS" if pos_ok else "FAIL", w1, case.get("band_w1_max", 8.0), "PASS" if w1_ok else "FAIL"), file=out)
    step = median(tw_steps) if tw_steps else case.get("leak_step_default", 16)
    leak_o, n_o = gradient_leak(ours_lp, ou["M"].minus(ou["Lm"].dilate(1)), case.get("leak_lo", 0.5), step / 4.0)
    leak_t, n_t = gradient_leak(twin_lp, M.minus(tw["Lm"].dilate(1)), case.get("leak_lo", 0.5), step / 4.0)
    ok_leak = leak_o <= leak_t + case.get("leak_max", 0.02)
    res.update({"bands": bands, "band_step": step, "leak_ours": leak_o, "leak_twin": leak_t})
    print("  gradient leak (3x3 std in [%.1f, %.1f) over %d / %d ink-free mask px): ours %.3f twin %.3f (ours <= twin + %.2f) %s" % (
        case.get("leak_lo", 0.5), step / 4.0, n_o, n_t, leak_o, leak_t, case.get("leak_max", 0.02), "PASS" if ok_leak else "FAIL"), file=out)
    verdicts += [ok_bands, ok_leak]
    # c. ramp identity.
    rows, note = ramp_identity(ours_tex_dir, twin_dump, set(tw["mats"]))
    if rows:
        ok_ramp = True
        for tbp, cbp, matched, mx, n in rows:
            ok = matched and mx == 0
            ok_ramp = ok_ramp and ok
            print("  ramp tbp0=%06x cbp=%06x: %s%s %s" % (tbp, cbp, "max|diff|=%s " % mx if matched else "", n, "PASS" if ok else "FAIL"), file=out)
        verdicts.append(ok_ramp)
        res["ramp"] = [{"tbp0": "%06x" % t, "cbp": "%06x" % c, "matched": m, "max_diff": x} for t, c, m, x, _ in rows]
    else:
        print("  ramp identity: skipped (%s)" % note, file=out)
        res["ramp"] = None
    if artefacts:
        os.makedirs(artefacts, exist_ok=True)
        sheet(os.path.join(artefacts, "cel_%s_%s.png" % (case.get("name", "case"), label)), ours_lp, twin_lp, M, ring, ou["dark"], tw["dark"], case["char_box"])
    return all(verdicts), res


def run(args):
    fx = json.load(open(args.fixture))
    case = dict(fx["cases"][args.case])
    case["name"] = args.case
    for k, v in fx.get("defaults", {}).items():
        case.setdefault(k, v)
    for p in (args.ours_frame, args.twin_frame):
        if not p or not os.path.exists(p):
            print("SKIP: missing frame %s" % p)
            return 77
    if not args.twin_dump or not os.path.isdir(args.twin_dump):
        print("SKIP: missing twin dump %s" % args.twin_dump)
        return 77
    if not args.ours_verts or not os.path.exists(args.ours_verts):
        print("SKIP: missing ours verts trace %s" % args.ours_verts)
        return 77
    ours, twin = load_frame(args.ours_frame), load_frame(args.twin_frame)
    ours_lp, twin_lp = lum_plane(ours), lum_plane(twin)
    fid = args.twin_frame_id if args.twin_frame_id is not None else case.get("twin_frame")
    twin_dl = twin_draws(args.twin_dump, fid)
    ours_dl = ours_draws(args.ours_verts)
    print("judge_cel %s [%s]: ours %s (%d draws in %s) twin %s (%d draws of frame %s)" % (
        args.case, args.label, os.path.basename(args.ours_frame), len(ours_dl), os.path.basename(args.ours_verts), os.path.basename(args.twin_frame), len(twin_dl), fid))
    ok, res = judge_case(case, ours_lp, twin_lp, ours_dl, twin_dl, args.ours_textures, args.twin_dump, args.artefacts, args.label, sys.stdout)
    if args.artefacts:
        os.makedirs(args.artefacts, exist_ok=True)
        json.dump(res, open(os.path.join(args.artefacts, "cel_%s_%s.json" % (args.case, args.label)), "w"), indent=1, default=str)
    gate = case.get("gate", True)
    print("judge_cel %s: %s%s" % (args.case, "PASS" if ok else "FAIL", "" if gate else " (report-only case)"))
    return 0 if (ok or not gate) else 1


# ----------------------------------------------------------------------------- selftest
def selftest():
    import io
    import tempfile
    fails = 0

    def check(c, msg):
        nonlocal fails
        if not c:
            fails += 1
            print("FAIL: " + msg)

    # Synthetic twin: a body (grey 120 and 180 halves = two bands) in a box, 1-px black ink around it,
    # a dark background. Ours = the same; variants: no ink, blurred bands, shifted ink by 1 px.
    def frame(ink, blur, shift):
        """A body (two luminance bands) with a 1-px ink frame, the whole character moved by `shift` px."""
        f = [(90, 90, 90)] * (W * H)
        bx0, by0, bx1, by1 = 200 + shift, 100 + shift, 260 + shift, 160 + shift
        for y in range(by0, by1):
            for x in range(bx0, bx1):
                v = 120 if x < 230 + shift else 180
                if blur and 226 + shift <= x < 234 + shift:
                    v = 120 + (x - 226 - shift) * 60 // 8
                f[y * W + x] = (v, v, v)
        if ink:
            for y in range(by0 - 1, by1 + 1):
                for x in range(bx0 - 1, bx1 + 1):
                    if x in (bx0 - 1, bx1) or y in (by0 - 1, by1):
                        f[y * W + x] = (16, 16, 16)
        return f

    def draws(shift=0):
        ctx = {"PRIM": {"PRIM": "4", "TME": "1"}, "ALPHA": {"A": "0", "B": "1", "C": "0", "D": "1"}, "TEST": {"ATST": "5"}, "TEX0": {"TBP0": "0x2980"}}
        q = [(200, 100), (259.5, 100), (200, 159.5), (259.5, 100), (259.5, 159.5), (200, 159.5)]
        verts = [(x + shift, y + shift, 60000, 0, 0, 0, 128) for x, y in q]
        lctx = {"PRIM": {"PRIM": "1", "TME": "0", "ABE": "1"}, "ALPHA": {"A": "0", "B": "1", "C": "0", "D": "1"}, "TEST": {"ATST": "0"}, "TEX0": {"TBP0": "0x0"}}
        lines = [(1, lctx, [(199 + shift, 99 + shift, 60000, 16, 16, 16, 128), (260 + shift, 99 + shift, 60000, 16, 16, 16, 128)])]
        return [(0, ctx, verts)] + lines

    case = {"name": "synthetic", "char_box": [190, 90, 270, 170], "char_z": [50000, 70000], "pose_matched": True}
    twin = lum_plane(frame(True, False, 0))
    buf = io.StringIO()
    ok, res = judge_case(case, lum_plane(frame(True, False, 0)), twin, draws(), draws(), None, None, None, "gl", buf)
    check(ok, "identical frames pass: " + buf.getvalue())
    check(res["coverage"] >= 0.99 and res["extra"] == 0.0 and abs(res["contrast_ratio"] - 1) < 1e-9, "identical: coverage 1, extra 0, contrast 1 (%s)" % buf.getvalue())
    check(len(res["bands"]) == 1 and len(res["bands"][0]["modes_twin"]) == 2, "two bands found in the twin: %s" % res["bands"])
    check(res["ink_lines_twin"] == 1 and res["ink_px_other_twin"] == 0, "one ink line on the character, none elsewhere")
    buf = io.StringIO()
    ok, res = judge_case(case, lum_plane(frame(False, False, 0)), twin, draws(), draws(), None, None, None, "gl", buf)
    check(not ok and res["coverage"] < 0.1 and res["contrast_ratio"] < 0.5 and res["visible_ratio"] < 0.5, "no ink fails on coverage, contrast and visibility (%s)" % buf.getvalue())
    buf = io.StringIO()
    ok, res = judge_case(case, lum_plane(frame(True, True, 0)), twin, draws(), draws(), None, None, None, "gl", buf)
    check(not ok and res["leak_ours"] > res["leak_twin"] + 0.02, "a blurred band step fails on gradient leak (%.3f vs %.3f)" % (res["leak_ours"], res["leak_twin"]))
    buf = io.StringIO()
    ok, res = judge_case(case, lum_plane(frame(True, False, 1)), twin, draws(1), draws(), None, None, None, "gl", buf)
    check(res["extra"] <= 0.10 and res["thick_ours"] == res["thick_twin"], "a 1-px shift stays within the extra tolerance (extra %.3f strict %.3f)" % (res["extra"], res["extra_strict"]))
    # Not pose-matched: the ring is reported only; a 1-px shifted character still passes the statistics.
    case2 = dict(case); case2["pose_matched"] = False
    buf = io.StringIO()
    ok, res = judge_case(case2, lum_plane(frame(True, False, 2)), twin, draws(2), draws(), None, None, None, "gl", buf)
    check(ok and res["coverage"] < 0.5, "pose-tolerant statistics pass on a 2-px shifted character while the ring overlap drops: " + buf.getvalue())
    # Our verts trace parser.
    with tempfile.TemporaryDirectory() as d:
        lp = os.path.join(d, "v.log")
        with open(lp, "w") as f:
            f.write("verts: seq=47455 prim=1 tme=0 abe=1 fbp=0x70000 fbw=512 psm=0 fbmsk=0x0 zbp=0xe0000 zpsm=0 zmsk=0 zte=1 ztst=3 ate=1 atst=0 aref=0 afail=3 scissor=0,0-511,223 off=1792.0,1936.5 tbp=0x0 tbw=0 tpsm=0 tw=0 th=0 tcc=0 tfx=0 cbp=0x0 cpsm=0 csm=0 csa=0 cld=0 fst=1 texa=0/128/0 alpha=0101/0 n=2\n")
            f.write("verts:   v0 xyz=200.2,143.0,57928 stq=512.5,224.5,1 uv=8200,3592 rgba=10101080 fog=0\n")
            f.write("verts:   v1 xyz=204.7,141.8,57449 stq=512.5,224.5,1 uv=8200,3592 rgba=10101080 fog=0\n")
            f.write("verts: seq=47457 prim=4 tme=1 abe=1 fbp=0x70000 fbw=512 psm=0 fbmsk=0x0 zbp=0xe0000 zpsm=0 zmsk=0 zte=1 ztst=3 ate=1 atst=5 aref=126 afail=1 scissor=0,0-511,223 off=1792.0,1936.5 tbp=0x298000 tbw=256 tpsm=19 tw=8 th=7 tcc=1 tfx=0 cbp=0x28e500 cpsm=0 csm=0 csa=0 cld=1 fst=0 texa=0/128/0 alpha=0101/0 n=3\n")
            for k in range(3):
                f.write("verts:   v%d xyz=%d.0,%d.0,60000 stq=0,0,1 uv=0,0 rgba=34507b80 fog=254\n" % (k, 200 + k, 100))
        dl = ours_draws(lp)
        check(len(dl) == 2 and dl[0][1]["PRIM"]["PRIM"] == "1" and dl[0][2][0] == (200.2, 143.0, 57928, 16, 16, 16, 128), "ours line draw parsed: %s" % (dl[0],))
        check(dl[1][1]["TEX0"]["TBP0"] == "0x2980" and dl[1][1]["ALPHA"]["A"] == "0" and dl[1][1]["TEST"]["ATST"] == "5" and dl[1][2][2][3:] == (0x34, 0x50, 0x7b, 0x80), "ours body draw parsed: %s" % (dl[1],))
    # Rasteriser sanity: a 4-px horizontal line marks 4..5 samples; a triangle covers its interior.
    m = Mask()
    raster_line(m, 200.3, 142.75, 204.8, 142.75)
    check(4 <= m.count() <= 5, "line raster marks 4..5 px (%d)" % m.count())
    m = Mask()
    raster_triangle(m, 0, 0, 10, 0, 0, 10)
    check(m.count() == 66, "inclusive triangle raster (0,0)-(10,0)-(0,10) = 66 samples (%d)" % m.count())
    # modes(): two peaks; one peak.
    h = [0] * 256
    for k in range(-3, 4):
        h[100 + k] = 50 - 5 * abs(k)
        h[150 + k] = 60 - 6 * abs(k)
    check(modes(h, 0.15, 8) == [100, 150], "two modes found: %s" % modes(h, 0.15, 8))
    h[125] = h[126] = 40  # a shoulder-free third bump with a shallow valley: not prominent -> not a mode
    for k in range(101, 150):
        h[k] = max(h[k], 45)
    check(modes(h, 0.15, 8) == [150], "a hump without a valley is one mode: %s" % modes(h, 0.15, 8))
    h = [0] * 256
    h[77] = 10
    check(modes(h, 0.15, 8) == [77], "one mode")
    # File round trip: PPM / PNG frames.
    with tempfile.TemporaryDirectory() as d:
        img = Img(W, H)
        for i in range(W * H):
            img.px[i * 4:i * 4 + 4] = bytes((i % 251, (i // 7) % 253, i % 17, 255))
        write_png(os.path.join(d, "t.png"), img)
        f = load_frame(os.path.join(d, "t.png"))
        check(f[5] == (5, 0, 5), "png frame round trip %s" % (f[5],))
        with open(os.path.join(d, "o.ppm"), "wb") as fp:
            fp.write(b"P6\n512 448\n255\n")
            fp.write(bytes(((i // 3) % 7) for i in range(W * 448 * 3)))
        f = load_frame(os.path.join(d, "o.ppm"))
        check(len(f) == W * H, "448-row ppm folds to the field")
    print("judge_cel selftest: %s" % ("PASS" if fails == 0 else "FAIL (%d)" % fails))
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fixture")
    ap.add_argument("--case")
    ap.add_argument("--ours-frame")
    ap.add_argument("--ours-verts", help="NATIVEEE_GS_VERTS=<key-1> stderr log of the judged frame")
    ap.add_argument("--twin-frame")
    ap.add_argument("--twin-dump")
    ap.add_argument("--twin-frame-id", type=int)
    ap.add_argument("--ours-textures")
    ap.add_argument("--artefacts")
    ap.add_argument("--label", default="gl")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not (args.fixture and args.case):
        ap.error("--fixture and --case (or --selftest)")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
