#!/usr/bin/env python3
"""judge_solidity.py -- the Sly 2 SOLIDITY gate (round H item 1: the D1 / D3 / D7 see-through class).

One presented frame of ours against its PCSX2 2.7.329 twin (the same save, the same hub, the same idle
camera: the warp + PINE recipe), inside three regions of the fixture (verify/solidity/fixtures.json --
numbers only: boxes in the 512x224 frame):
  sly        -- the player character,
  wall       -- a near wall / roof sheet (the D3 / D7 surfaces),
  background -- scenery next to the character (what shows THROUGH him when he is see-through).

Metrics (both frames reduced to 512x224 Rec.601 luminance; the twin is aligned to ours by the best
small shift over the whole frame, |dx|,|dy| <= 3 -- PCSX2's SaveFrame is the 513x226 CRTC field):
  lum_wall           -- mean |Y_ours - Y_twin| inside the wall mask (static geometry, 0..255)   PASS <= 6
  lum_sly            -- the same inside the sly mask, reported; the two emulators are never at the same
                        animation phase, so the VERDICT uses lum_sly_w1 = the Wasserstein-1 distance between
                        the two masks' luminance distributions (mean |quantile_ours - quantile_twin|,
                        0..255: pose-tolerant, a see-through character still lifts the distribution)  PASS <= 6
  see_through        -- the background showing THROUGH the character, pose-tolerant (the two emulators
                        are never at the same animation phase): the twin's background mask gives the
                        background palette, the twin's sly mask minus background-like pixels the character
                        palette; a sly-mask pixel's background weight t = d_sly / (d_sly + d_bg) (RGB L1
                        distances to the nearest palette colours: 0 = a character colour, 1 = a background
                        colour, a 50% blend ~0.5). see_through = the excess of our mean t over the twin's
                        own (its silhouette surroundings), normalised by the twin's character share
                        (1 - t_twin): 0 = as solid as the twin, ~0.5 = half transparent.   PASS <= 0.1
The thresholds are the round's; PASS needs all three per fixture. A contact sheet (ours | twin | |diff| x3,
masks outlined) goes under <artefacts>/<fixture>.png and one table row per fixture to stdout.

Exit 0 PASS, 1 FAIL, 77 SKIP (an input file is missing; the ctest row runs without a disc). `--selftest`
exercises the metrics on synthetic frames (no fixtures).

Usage:
  judge_solidity.py --fixture <name> --ours <frame.ppm> --twin <pcsx2 frame.png> [--artefacts <dir>] [--summary]
  judge_solidity.py --selftest
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from judge_textures import Img, read_png, read_pnm, write_png  # noqa: E402

W, H = 512, 224
MAX_SHIFT = 3
LUM_LIMIT = 6.0
SEE_THROUGH_LIMIT = 0.1


def lum(r, g, b):
    return (77 * r + 150 * g + 29 * b) >> 8


def to_frame(w, h, ch, px):
    """Any RGB(A) image -> (rgb tuples flat list) at 512x224: rows pair-averaged when the source is
    line-doubled (448 rows), the 513x226 CRTC field cropped to its 512x224 content (x 0..511, y 1..224)."""
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


def load_ours(path):
    kind, w, h, body = read_pnm(path)
    if kind != "rgb":
        raise ValueError("ours must be a P6 PPM")
    return to_frame(w, h, 3, body)


def load_twin(path):
    w, h, ch, px = read_png(path)
    if ch < 3:
        raise ValueError("twin must be RGB / RGBA")
    return to_frame(w, h, ch, px)


def shifted(frame, dx, dy):
    """The twin moved by (dx, dy); pixels that leave the frame are clamped to the edge."""
    out = [None] * (W * H)
    for y in range(H):
        sy = min(max(y - dy, 0), H - 1)
        for x in range(W):
            sx = min(max(x - dx, 0), W - 1)
            out[y * W + x] = frame[sy * W + sx]
    return out


def align(ours, twin):
    """Best small shift of the twin onto ours by the whole-frame mean |Y| difference (the 513x226 CRTC
    field of PCSX2 against our raw target; a half-line DY offset is the usual answer)."""
    lo = [lum(*p) for p in ours]
    best = None
    for dy in range(-MAX_SHIFT, MAX_SHIFT + 1):
        for dx in range(-MAX_SHIFT, MAX_SHIFT + 1):
            tot = 0
            n = 0
            for y in range(MAX_SHIFT, H - MAX_SHIFT, 2):
                sy = y - dy
                for x in range(MAX_SHIFT, W - MAX_SHIFT, 2):
                    p = twin[sy * W + x - dx]
                    tot += abs(lo[y * W + x] - lum(*p))
                    n += 1
            m = tot / max(n, 1)
            if best is None or m < best[0]:
                best = (m, dx, dy)
    return best[1], best[2], best[0]


def box_pixels(box):
    x0, y0, x1, y1 = box
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            yield y * W + x


def lum_diff(ours, twin, box):
    tot = n = 0
    for i in box_pixels(box):
        tot += abs(lum(*ours[i]) - lum(*twin[i]))
        n += 1
    return tot / max(n, 1)


def lum_w1(ours, twin, box):
    a = sorted(lum(*ours[i]) for i in box_pixels(box))
    b = sorted(lum(*twin[i]) for i in box_pixels(box))
    n = min(len(a), len(b))
    return sum(abs(a[i] - b[i]) for i in range(n)) / max(n, 1)


def mean_rgb(frame, box):
    s = [0, 0, 0]
    n = 0
    for i in box_pixels(box):
        p = frame[i]
        s[0] += p[0]; s[1] += p[1]; s[2] += p[2]
        n += 1
    n = max(n, 1)
    return (s[0] / n, s[1] / n, s[2] / n)


def palette(frame, box, step):
    pts = list(box_pixels(box))
    return [frame[i] for i in pts[::step]]


def nearest(p, pal):
    best = 1 << 30
    for q in pal:
        d = abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2])
        if d < best:
            best = d
    return best


def background_weight(frame, box, bg_pal, sly_pal):
    tot = n = 0.0
    for i in box_pixels(box):
        p = frame[i]
        ds, db = nearest(p, sly_pal), nearest(p, bg_pal)
        tot += ds / (ds + db) if ds + db > 0 else 0.0
        n += 1
    return tot / max(n, 1)


def see_through(ours, twin, sly_box, bg_box):
    """The round's see-through metric (module doc): pose-tolerant palette classification."""
    bg_pal = palette(twin, bg_box, 37)
    # The twin's character palette: its sly-mask pixels that are not background-like themselves.
    sly_pal = [p for p in palette(twin, sly_box, 7) if nearest(p, bg_pal) > 48]
    if not sly_pal:
        return 1.0
    f_twin = background_weight(twin, sly_box, bg_pal, sly_pal)
    f_ours = background_weight(ours, sly_box, bg_pal, sly_pal)
    return max(0.0, f_ours - f_twin) / max(1e-6, 1.0 - f_twin)


def contact_sheet(path, ours, twin, boxes):
    sheet = Img(W * 3, H)
    px = sheet.px
    for y in range(H):
        for x in range(W):
            i = y * W + x
            o, t = ours[i], twin[i]
            d = min(255, 3 * abs(lum(*o) - lum(*t)))
            for col, p in ((0, o), (1, t), (2, (d, d, d))):
                j = (y * W * 3 + col * W + x) * 4
                px[j], px[j + 1], px[j + 2], px[j + 3] = p[0], p[1], p[2], 255
    colours = {"sly": (255, 255, 0), "wall": (0, 255, 255), "background": (255, 0, 255)}
    for name, box in boxes.items():
        c = colours.get(name, (255, 255, 255))
        x0, y0, x1, y1 = box
        for col in range(3):
            for x in range(max(0, x0), min(W, x1)):
                for y in (y0, y1 - 1):
                    if 0 <= y < H:
                        j = (y * W * 3 + col * W + x) * 4
                        px[j:j + 3] = bytes(c)
            for y in range(max(0, y0), min(H, y1)):
                for x in (x0, x1 - 1):
                    if 0 <= x < W:
                        j = (y * W * 3 + col * W + x) * 4
                        px[j:j + 3] = bytes(c)
    write_png(path, sheet)


def judge_frame(name, ours, twin, fx, artefacts, out=sys.stdout):
    dx, dy, frame_diff = align(ours, twin)
    tw = shifted(twin, dx, dy)
    ls = lum_diff(ours, tw, fx["sly"])
    lsw = lum_w1(ours, tw, fx["sly"])
    lw = lum_diff(ours, tw, fx["wall"])
    st = see_through(ours, tw, fx["sly"], fx["background"])
    ok = lsw <= LUM_LIMIT and lw <= LUM_LIMIT and st <= SEE_THROUGH_LIMIT
    out.write("%-11s shift=(%+d,%+d) frame=%.2f lum_sly=%.2f lum_sly_w1=%.2f lum_wall=%.2f see_through=%.3f %s\n" %
              (name, dx, dy, frame_diff, ls, lsw, lw, st, "PASS" if ok else "FAIL"))
    if artefacts:
        contact_sheet(os.path.join(artefacts, name + ".png"), ours, tw,
                      {"sly": fx["sly"], "wall": fx["wall"], "background": fx["background"]})
    return ok, {"shift": [dx, dy], "frame": frame_diff, "lum_sly": ls, "lum_sly_w1": lsw, "lum_wall": lw, "see_through": st}


def synth(sly_alpha, shift=(0, 0)):
    """A grey street with a dark wall band and a blue 'character' square blended at sly_alpha over it."""
    frame = [None] * (W * H)
    for y in range(H):
        for x in range(W):
            xs, ys = x - shift[0], y - shift[1]
            base = (120 + (xs * 7 + ys * 3) % 40, 110 + (xs * 5) % 30, 100)
            if xs < 120:
                base = (40 + (ys % 7), 40, 45)
            if 200 <= xs < 280 and 60 <= ys < 200:
                a = sly_alpha
                base = tuple(int(a * c + (1 - a) * b) for c, b in zip((40, 60, 200), base))
            frame[y * W + x] = base
    return frame


def selftest():
    fx = {"sly": [205, 70, 275, 195], "wall": [10, 30, 110, 200], "background": [300, 60, 480, 200]}
    fails = 0
    twin = synth(1.0)
    ok, m = judge_frame("solid", synth(1.0), twin, fx, None)
    fails += not (ok and m["see_through"] < 0.01)
    ok, m = judge_frame("half", synth(0.5), twin, fx, None)
    fails += not (not ok and 0.35 < m["see_through"] < 0.65 and m["lum_sly_w1"] > LUM_LIMIT)
    ok, m = judge_frame("shifted", synth(1.0, shift=(1, -2)), twin, fx, None)
    fails += not (ok and m["shift"] == [1, -2])
    ok, m = judge_frame("ghost", synth(0.75), twin, fx, None)
    fails += not (not ok and m["see_through"] > SEE_THROUGH_LIMIT)
    print("judge_solidity selftest:", "PASS" if fails == 0 else "FAIL (%d)" % fails)
    return 0 if fails == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fixture", help="fixture name in verify/solidity/fixtures.json")
    ap.add_argument("--ours", help="our presented frame (P6 PPM, 512x448 or 512x224)")
    ap.add_argument("--twin", help="the PCSX2 SaveFrame PNG of the same scene")
    ap.add_argument("--fixtures", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "solidity", "fixtures.json"))
    ap.add_argument("--artefacts", default=None)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not (a.fixture and a.ours and a.twin):
        ap.error("--fixture, --ours and --twin are required (or --selftest)")
    if not (os.path.exists(a.ours) and os.path.exists(a.twin)):
        print("SKIP: missing input (%s / %s)" % (a.ours, a.twin))
        return 77
    fixtures = json.load(open(a.fixtures))
    if a.fixture not in fixtures:
        print("unknown fixture %s (have %s)" % (a.fixture, ", ".join(sorted(fixtures))))
        return 2
    fx = fixtures[a.fixture]
    if a.artefacts:
        os.makedirs(a.artefacts, exist_ok=True)
    ok, m = judge_frame(a.fixture, load_ours(a.ours), load_twin(a.twin), fx, a.artefacts)
    if not a.summary:
        print(json.dumps({a.fixture: m}, indent=1))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
