#!/usr/bin/env python3
"""judge_shadow.py -- the Sly 2 SHADOW gate (D8): the title's stencil shadows judged against PCSX2.

How this title draws shadows (shadow_notes.md, round H): after the scene it runs one stencil block per
frame on the 32-bit frame buffer F -- (1) the G byte of every pixel is saved into the alpha byte of the
other display buffer S through a 16-bit "texture shuffle" sprite pass, (2) G := 0x7e, (3) the shadow
volumes are drawn untextured with COLCLAMP off, FBMSK = ffff00ff (G only) and ALPHA (0 - Cs) * FIX(0x80)
+ Cd with the per-triangle flat colour G = 2 (front face, -2) or G = 0xfe (back face, +2 by wrap) under
ZTST GEQUAL: a receiver pixel inside a volume ends at 0x80 (set 1) -- then G[0:6] := 0x3e and a second
set of volumes / planes puts bit 6 (0x40) for set 2, (4) G bit 0 := G bit 6, an in-place shuffle moves
G bit 7 -> A bit 7 and G bit 0 -> A bit 0, G is restored from S, and (5) one full-screen sprite reads
F as PSMT8H (the alpha byte = palette index, CLUT at 0x180000: black, alpha rising with the index on
bit 7 / bit 0) and lerps the frame towards black = the shadow. The judge therefore has an EXACT mask on
both sides: the pixels that step (5) darkened (rt before - rt after > 0), from PCSX2's per-draw SaveRT
dump (`<draw>_f<frame>_rt0/rt1_*.png` of the draw whose TEX0 PSM is 0x1b = P_8H reading the FRAME base)
and from ours (NATIVEEE_GS_RT_DUMP=<key>,<fbp>: rt_<fbp>_<ordinal>_pre/post.pam of the indexed-high read,
listed in rt_dump_index.txt with tpsm=1b).

Metrics per case (fixture: verify/shadow/shadow_fixture.json, numbers only -- the frames and dumps are
game-derived and live outside the repository):
  overlap  = |ours ∧ twin| / |twin| over the case's region  -- must be >= overlap_min (0.80)
  spurious = |ours ∧ ¬dilate(twin, dilate_px)| / |twin|     -- must be <= spurious_max
  caster   = darkened pixels inside the caster's screen box: ours <= twin * (1 + caster_tolerance) +
             caster_slack_px (the "drawn over the caster" failure: the character's own texels darkened)
The twin and ours are aligned to within a few frames (the intro cinematic is deterministic from boot on
both sides; the key is picked by correlation), so the masks are compared at field resolution (512x224)
with a dilation tolerance. A frame-level FALLBACK (no per-draw dumps: --ours-frame / --twin-frame) uses
the task's local-darkness mask -- a pixel darker than its (2r+1)^2 neighbourhood mean by `threshold` --
with r / threshold from the fixture; measured on the intro museum twin that operator recalls only ~50 %
of the real shadow pixels (it keys on texture edges; the shadows here are 15-30 % darkenings of large
areas), so it is reported but the per-draw mask is the gate whenever dumps are given.

Exit 0 PASS, 1 FAIL, 77 SKIP (fixture case names dumps that are absent). `--selftest`: synthetic masks.

Usage:
  judge_shadow.py --fixture <json> --case <name> --ours-dump <dir> --twin-dump <pcsx2 hwdump dir>
                  [--ours-frame <ppm> --twin-frame <png>] [--artefacts <dir>] [--summary]
  judge_shadow.py --selftest
"""
import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from judge_textures import read_png, read_pnm, write_png, Img  # noqa: E402

FIELD_W, FIELD_H = 512, 224


# ----------------------------------------------------------------------------- masks
class Mask:
    """w x h booleans as a bytearray (1 = in the mask)."""

    def __init__(self, w, h, data=None):
        self.w, self.h = w, h
        self.px = bytearray(data) if data is not None else bytearray(w * h)

    def count(self, box=None):
        if box is None:
            return sum(self.px)
        x0, y0, x1, y1 = box
        n = 0
        for y in range(max(0, y0), min(self.h, y1)):
            n += sum(self.px[y * self.w + max(0, x0):y * self.w + min(self.w, x1)])
        return n

    def dilate(self, r):
        if r <= 0:
            return Mask(self.w, self.h, self.px)
        out = Mask(self.w, self.h)
        w, h = self.w, self.h
        src = self.px
        for y in range(h):
            for x in range(w):
                if not src[y * w + x]:
                    continue
                for dy in range(-r, r + 1):
                    yy = y + dy
                    if yy < 0 or yy >= h:
                        continue
                    row = yy * w
                    for xx in range(max(0, x - r), min(w, x + r + 1)):
                        out.px[row + xx] = 1
        return out

    def restrict(self, box):
        out = Mask(self.w, self.h)
        x0, y0, x1, y1 = box
        for y in range(max(0, y0), min(self.h, y1)):
            s = y * self.w
            out.px[s + max(0, x0):s + min(self.w, x1)] = self.px[s + max(0, x0):s + min(self.w, x1)]
        return out


def rgb_rows(img_w, img_h, ch, data, w, h):
    """The top-left w x h window of an image as a list of (r, g, b) rows (gray / RGB / RGBA input)."""
    rows = []
    for y in range(min(h, img_h)):
        row = []
        base = y * img_w * ch
        for x in range(min(w, img_w)):
            p = base + x * ch
            if ch == 1:
                v = data[p]
                row.append((v, v, v))
            else:
                row.append((data[p], data[p + 1], data[p + 2]))
        rows.append(row)
    return rows


def darken_mask(before, after, w=FIELD_W, h=FIELD_H):
    """Pixels whose RGB sum dropped from `before` to `after` (both row lists)."""
    m = Mask(w, h)
    for y in range(min(h, len(before), len(after))):
        rb, ra = before[y], after[y]
        for x in range(min(w, len(rb), len(ra))):
            if sum(rb[x]) > sum(ra[x]):
                m.px[y * w + x] = 1
    return m


def read_pam(path):
    """PAM (P7) RGB / RGB_ALPHA 8-bit: returns (w, h, depth, bytes)."""
    data = open(path, "rb").read()
    hdr = {}
    pos = 0
    while True:
        end = data.index(b"\n", pos)
        line = data[pos:end].decode().strip()
        pos = end + 1
        if line == "ENDHDR":
            break
        if " " in line:
            k, v = line.split(" ", 1)
            hdr[k] = v
    w, h, d = int(hdr["WIDTH"]), int(hdr["HEIGHT"]), int(hdr["DEPTH"])
    return w, h, d, data[pos:pos + w * h * d]


def read_frame_rows(path):
    """A frame image (PPM / PNG / PAM) as rows of (r, g, b) at field resolution: a 448-line PRESENTED
    frame (PPM / PNG) is read as its 224 even rows; a target dump (PAM, 512x448 = the whole buffer) as
    its first 224 rows (the field the title draws under its scissor)."""
    if path.endswith(".pam"):
        w, h, d, body = read_pam(path)
        return rgb_rows(w, h, d, body, FIELD_W, FIELD_H)
    elif path.endswith(".png"):
        w, h, ch, body = read_png(path)
        rows = rgb_rows(w, h, ch, body, FIELD_W, h)
    else:
        kind, w, h, body = read_pnm(path)
        rows = rgb_rows(w, h, 3 if kind == "rgb" else 1, body, FIELD_W, h)
    if len(rows) >= 2 * FIELD_H:
        rows = rows[0::2]
    return rows[:FIELD_H]


# ----------------------------------------------------------------------------- the two dumps
def parse_draw_context(path):
    """TEX0 / FRAME / ALPHA fields of a PCSX2 <draw>_context.txt as {section: {field: int}} (TBP0 and
    FBP are printed by PCSX2 in the same 256-byte unit, so they compare directly)."""
    out = {}
    sec = None
    for line in open(path, errors="replace"):
        if not line.startswith(" ") and line.strip().endswith(":"):
            sec = line.strip()[:-1]
            continue
        if sec in ("TEX0", "FRAME", "ALPHA"):
            m = re.match(r"\s+(\w+):\s*(0x[0-9a-fA-F]+|\d+)", line)
            if m:
                out.setdefault(sec, {})[m.group(1)] = int(m.group(2), 0)
    return out


def twin_darken_mask(dump_dir, frame):
    """PCSX2 SaveRT dump: the darkening draw of frame `frame` = the P_8H (0x1b) read of the FRAME base
    with a plain lerp (ALPHA 0 1 0 1) into a 32-bit frame; returns (mask, draw_number) or (None, why)."""
    tag = "_f%05d_" % frame
    found = None
    for ctx in sorted(glob.glob(os.path.join(dump_dir, "*_context.txt")), key=lambda p: int(os.path.basename(p).split("_")[0])):
        n = int(os.path.basename(ctx).split("_")[0])
        if not glob.glob(os.path.join(dump_dir, "%d%srt1_*.png" % (n, tag))):
            continue
        c = parse_draw_context(ctx)
        t, f, al = c.get("TEX0", {}), c.get("FRAME", {}), c.get("ALPHA", {})
        if t.get("PSM") == 0x1b and f.get("PSM", 0) == 0 and t.get("TBP0", -1) == f.get("FBP", -2) \
                and (al.get("A"), al.get("B"), al.get("C"), al.get("D")) == (0, 1, 0, 1):
            found = n
            break
    if found is None:
        return None, "no P_8H-of-FRAME lerp draw with rt0/rt1 for frame %d in %s" % (frame, dump_dir)
    rt0 = [p for p in glob.glob(os.path.join(dump_dir, "%d%srt0_*.png" % (found, tag))) if "_alpha" not in p]
    rt1 = [p for p in glob.glob(os.path.join(dump_dir, "%d%srt1_*.png" % (found, tag))) if "_alpha" not in p]
    if not rt0 or not rt1:
        return None, "draw %d lacks rt0/rt1 images" % found
    w0, h0, c0, d0 = read_png(rt0[0])
    w1, h1, c1, d1 = read_png(rt1[0])
    before = rgb_rows(w0, h0, c0, d0, FIELD_W, FIELD_H)
    after = rgb_rows(w1, h1, c1, d1, FIELD_W, FIELD_H)
    return darken_mask(before, after), found


def ours_darken_mask(dump_dir):
    """Our NATIVEEE_GS_RT_DUMP directory: the indexed-high (tpsm=1b) read's pre/post PAMs."""
    index = os.path.join(dump_dir, "rt_dump_index.txt")
    if not os.path.exists(index):
        return None, "no rt_dump_index.txt in " + dump_dir
    pre = post = None
    ordinal = None
    for line in open(index):
        if " tpsm=1b" not in line:
            continue
        m = re.match(r"ordinal=(\d+) (pre|post) seq=\d+ file=(\S+)", line)
        if not m:
            continue
        if m.group(2) == "pre":
            pre, ordinal = m.group(3), int(m.group(1))
        elif ordinal is not None and int(m.group(1)) == ordinal:
            post = m.group(3)
            break
    if not pre or not post:
        return None, "no tpsm=1b pre/post pair in " + index
    before = read_frame_rows(os.path.join(dump_dir, pre))
    after = read_frame_rows(os.path.join(dump_dir, post))
    return darken_mask(before, after), ordinal


def local_dark_mask(rows, radius, threshold, w=FIELD_W, h=FIELD_H):
    """The task's frame-level definition: luminance below (1 - threshold) x the mean luminance of the
    (2r+1)^2 neighbourhood (edge-clamped box, summed-area table)."""
    lum = [[0.299 * r + 0.587 * g + 0.114 * b for (r, g, b) in row[:w]] for row in rows[:h]]
    hh, ww = len(lum), len(lum[0]) if lum else 0
    sat = [[0.0] * (ww + 1) for _ in range(hh + 1)]
    for y in range(hh):
        acc = 0.0
        for x in range(ww):
            acc += lum[y][x]
            sat[y + 1][x + 1] = sat[y][x + 1] + acc
    m = Mask(w, h)
    for y in range(hh):
        y0, y1 = max(0, y - radius), min(hh, y + radius + 1)
        for x in range(ww):
            x0, x1 = max(0, x - radius), min(ww, x + radius + 1)
            mean = (sat[y1][x1] - sat[y0][x1] - sat[y1][x0] + sat[y0][x0]) / ((y1 - y0) * (x1 - x0))
            if lum[y][x] < mean * (1.0 - threshold):
                m.px[y * w + x] = 1
    return m


# ----------------------------------------------------------------------------- the judge
def judge_masks(ours, twin, case, region, caster_ours, caster_twin):
    """The three metrics over the fixture region; returns (ok, dict)."""
    o = ours.restrict(region)
    t = twin.restrict(region)
    tn = t.count()
    both = sum(1 for i in range(len(o.px)) if o.px[i] and t.px[i])
    td = t.dilate(int(case.get("dilate_px", 2)))
    spur = sum(1 for i in range(len(o.px)) if o.px[i] and not td.px[i])
    overlap = both / tn if tn else 0.0
    spurious = spur / tn if tn else (1.0 if o.count() else 0.0)
    c_ours = ours.count(caster_ours)
    c_twin = twin.count(caster_twin)
    c_limit = c_twin * (1.0 + float(case.get("caster_tolerance", 0.05))) + int(case.get("caster_slack_px", 20))
    ok = overlap >= float(case.get("overlap_min", 0.8)) and spurious <= float(case.get("spurious_max", 0.3)) \
        and c_ours <= c_limit
    return ok, dict(twin_px=tn, ours_px=o.count(), overlap=overlap, spurious=spurious, caster_ours=c_ours,
                    caster_twin=c_twin, caster_limit=c_limit)


def mask_sheet(path, ours, twin, region):
    """ours | twin | overlay (green = both, red = ours only, blue = twin only) at field resolution."""
    w, h = ours.w, ours.h
    img = Img(w * 3, h)
    for y in range(h):
        for x in range(w):
            o, t = ours.px[y * w + x], twin.px[y * w + x]
            inr = region[0] <= x < region[2] and region[1] <= y < region[3]
            for k, v in ((0, o), (1, t)):
                p = (y * img.w + k * w + x) * 4
                c = 255 if v else (40 if inr else 0)
                img.px[p] = img.px[p + 1] = img.px[p + 2] = c
                img.px[p + 3] = 255
            p = (y * img.w + 2 * w + x) * 4
            img.px[p] = 255 if (o and not t) else 0
            img.px[p + 1] = 255 if (o and t) else 0
            img.px[p + 2] = 255 if (t and not o) else 0
            img.px[p + 3] = 255
    write_png(path, img)


def run_case(fixture, name, ours_dump, twin_dump, ours_frame, twin_frame, artefacts, out):
    case = fixture["cases"][name]
    region = case["region"]
    caster_ours = case.get("caster_box_ours", case.get("caster_box"))
    caster_twin = case.get("caster_box_twin", case.get("caster_box"))
    mode = "darken_pass"
    if ours_dump and twin_dump:
        twin, tinfo = twin_darken_mask(twin_dump, int(case["twin_frame"]))
        if twin is None:
            out.write("SKIP %s: %s\n" % (name, tinfo))
            return 77
        ours, oinfo = ours_darken_mask(ours_dump)
        if ours is None:
            out.write("SKIP %s: %s\n" % (name, oinfo))
            return 77
        source = "twin draw %s / ours ordinal %s" % (tinfo, oinfo)
    elif ours_frame and twin_frame:
        mode = "local_dark"
        ld = case.get("local_dark", fixture.get("local_dark", {"radius": 48, "threshold": 0.08}))
        twin = local_dark_mask(read_frame_rows(twin_frame), int(ld["radius"]), float(ld["threshold"]))
        ours = local_dark_mask(read_frame_rows(ours_frame), int(ld["radius"]), float(ld["threshold"]))
        source = "frames r=%d thr=%.2f" % (int(ld["radius"]), float(ld["threshold"]))
    else:
        out.write("SKIP %s: give --ours-dump + --twin-dump or --ours-frame + --twin-frame\n" % name)
        return 77
    ok, m = judge_masks(ours, twin, case, region, caster_ours, caster_twin)
    gate = bool(case.get("gate", True))   # gate=false: reported, never the verdict (frames not camera-matched)
    verdict = ("PASS" if ok else "FAIL") if gate else ("pass" if ok else "fail") + "(report-only)"
    out.write("%s %s [%s] twin_px=%d ours_px=%d overlap=%.3f (min %.2f) spurious=%.3f (max %.2f) "
              "caster ours=%d twin=%d limit=%.0f  %s\n" % (verdict, name, mode, m["twin_px"], m["ours_px"], m["overlap"],
                                                            float(case.get("overlap_min", 0.8)), m["spurious"],
                                                            float(case.get("spurious_max", 0.3)), m["caster_ours"],
                                                            m["caster_twin"], m["caster_limit"], source))
    if artefacts:
        os.makedirs(artefacts, exist_ok=True)
        mask_sheet(os.path.join(artefacts, "shadow_%s_%s.png" % (name, mode)), ours, twin, region)
    return 0 if (ok or not gate) else 1


# ----------------------------------------------------------------------------- selftest
def selftest():
    ok = True
    w, h = FIELD_W, FIELD_H
    region = [0, 100, w, h]
    case = {"overlap_min": 0.8, "spurious_max": 0.3, "dilate_px": 2, "caster_tolerance": 0.05, "caster_slack_px": 20}
    twin = Mask(w, h)
    for y in range(140, 200):
        for x in range(100, 300):
            twin.px[y * w + x] = 1
    # identical -> PASS
    r, m = judge_masks(twin, twin, case, region, [200, 60, 260, 140], [200, 60, 260, 140])
    ok &= r and abs(m["overlap"] - 1.0) < 1e-9 and m["spurious"] == 0
    # shifted by 1 px -> still PASS (dilation tolerance)
    sh = Mask(w, h)
    for y in range(140, 200):
        for x in range(101, 301):
            sh.px[y * w + x] = 1
    r, m = judge_masks(sh, twin, case, region, [200, 60, 260, 140], [200, 60, 260, 140])
    ok &= r and m["overlap"] > 0.99 and m["spurious"] == 0
    # half the shadow -> FAIL on overlap
    half = Mask(w, h)
    for y in range(140, 170):
        for x in range(100, 300):
            half.px[y * w + x] = 1
    r, m = judge_masks(half, twin, case, region, [200, 60, 260, 140], [200, 60, 260, 140])
    ok &= (not r) and abs(m["overlap"] - 0.5) < 1e-9
    # shadow elsewhere -> FAIL on spurious
    els = Mask(w, h, twin.px)
    for y in range(100, 130):
        for x in range(350, 500):
            els.px[y * w + x] = 1
    r, m = judge_masks(els, twin, case, region, [200, 60, 260, 140], [200, 60, 260, 140])
    ok &= (not r) and m["overlap"] == 1.0 and m["spurious"] > 0.3
    # darkening over the caster -> FAIL on caster
    cas = Mask(w, h, twin.px)
    for y in range(60, 140):
        for x in range(200, 260):
            cas.px[y * w + x] = 1
    r, m = judge_masks(cas, twin, case, region, [200, 60, 260, 140], [200, 60, 260, 140])
    ok &= (not r) and m["caster_ours"] == 80 * 60 and m["caster_twin"] == 0
    # darken_mask: a 2-row before/after
    before = [[(100, 100, 100), (50, 50, 50)], [(10, 10, 10), (0, 0, 0)]]
    after = [[(90, 100, 100), (50, 50, 50)], [(10, 10, 10), (0, 0, 0)]]
    dm = darken_mask(before, after, 2, 2)
    ok &= dm.count() == 1 and dm.px[0] == 1
    # local_dark: a dark square in a flat field
    rows = [[(120, 120, 120)] * w for _ in range(h)]
    for y in range(150, 170):
        for x in range(200, 240):
            rows[y][x] = (60, 60, 60)
    ld = local_dark_mask(rows, 24, 0.08)
    ok &= ld.px[160 * w + 220] == 1 and ld.px[50 * w + 50] == 0
    print("judge_shadow selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fixture")
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument("--ours-dump")
    ap.add_argument("--twin-dump")
    ap.add_argument("--ours-frame")
    ap.add_argument("--twin-frame")
    ap.add_argument("--artefacts")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.fixture or not a.case:
        ap.error("--fixture and --case are required (or --selftest)")
    fixture = json.load(open(a.fixture))
    rc = 0
    for name in a.case:
        if name not in fixture["cases"]:
            sys.stdout.write("SKIP %s: not in the fixture\n" % name)
            rc = max(rc, 77) if rc == 0 else rc
            continue
        r = run_case(fixture, name, a.ours_dump, a.twin_dump, a.ours_frame, a.twin_frame, a.artefacts, sys.stdout)
        if r == 1:
            rc = 1
        elif r == 77 and rc == 0:
            rc = 77
    return rc


if __name__ == "__main__":
    sys.exit(main())
