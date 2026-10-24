#!/usr/bin/env python3
"""judge_cane.py -- the Sly 2 CHARGED-CANE EFFECT gate (round 6, track cane).

Hold SQUARE in the Paris hub and the cane head carries a bright orange additive glow with yellow spark
streaks and orange particle dots (the particle systems: kind-12 draw entries). Before the round every
particle emitter failed the VU0 macro-mode frustum test (the generated COP2 FMAC ops never committed the
MAC flags the test reads with cfc2 $vi17), so the effect -- and every other particle system -- was absent.

Fixture (verify/cane/fixtures.json, numbers only): the state, the pad script (`frame:buttons:hold`), the
dumped frame keys of the hold, the cane-head box of ours and of the PCSX2 twin (boxes [x0, y0, x1, y1) in
the 512x224 frame; the twin's SaveFrame is the 513x226 CRTC field, cropped to its centre).

Metrics per frame inside the box:
  orange   -- pixels with R > 200, G > 120, B < 120 (the glow / sparks / dots palette)
  bright   -- pixels with Rec.601 luminance > 170 (the additive sum saturating)
The VERDICT is the MEDIAN over the hold frames of ours: PASS needs orange >= 100 AND bright >= 100 (ours
before the fix: 12 / 33 on every hold frame; after: 260..465 / 200..322; the twin inside its own box:
174..339 / 100..202). `--before <dir>` (optional) reports the same medians for a pre-fix dump and asserts
they stay below the limits (the judge separates the two); `--twin <png>` reports the twin's counts and
asserts its presence (>= the limits). A contact sheet (ours median frame | before | twin, boxes outlined)
goes under <artefacts>/<fixture>.png.

Exit 0 PASS, 1 FAIL, 77 SKIP (an input is missing; the ctest row runs without a disc). `--selftest`
exercises the metrics on synthetic frames (no fixtures).

Usage:
  judge_cane.py --fixture <name> --frames <dump dir with f<key>.ppm> [--before <dump dir>] [--twin <png>]
                [--artefacts <dir>] [--summary]
  judge_cane.py --selftest
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from judge_solidity import W, H, load_twin, lum, to_frame  # noqa: E402
from judge_textures import Img, read_pnm, write_png  # noqa: E402

ORANGE_LIMIT = 100
BRIGHT_LIMIT = 100


def load_ours(path):
    """Our --dump-frames PPM: 512x224, or 512x448 -- the field in the upper half over a black lower half (the
    headless dump of a 448-line target the title draws one field into) is cropped, a line-doubled one is
    pair-averaged (judge_solidity.to_frame)."""
    kind, w, h, body = read_pnm(path)
    if kind != "rgb":
        raise ValueError("ours must be a P6 PPM")
    if (w, h) == (W, 2 * H) and not any(body[W * H * 3:]):
        body = body[:W * H * 3]
        h = H
    return to_frame(w, h, 3, body)


def counts(frame, box):
    """(orange, bright) inside box = [x0, y0, x1, y1) of a 512x224 rgb-tuple frame."""
    x0, y0, x1, y1 = box
    orange = bright = 0
    for y in range(y0, y1):
        row = y * W
        for x in range(x0, x1):
            r, g, b = frame[row + x]
            if r > 200 and g > 120 and b < 120:
                orange += 1
            if lum(r, g, b) > 170:
                bright += 1
    return orange, bright


def median(values):
    s = sorted(values)
    return s[len(s) // 2] if s else 0


def put(img, x, y, rgb):
    i = (y * img.w + x) * 4
    img.px[i], img.px[i + 1], img.px[i + 2], img.px[i + 3] = rgb[0], rgb[1], rgb[2], 255


def outline(img, box, colour, ox):
    x0, y0, x1, y1 = box
    for x in range(x0, x1):
        put(img, ox + x, y0, colour)
        put(img, ox + x, y1 - 1, colour)
    for y in range(y0, y1):
        put(img, ox + x0, y, colour)
        put(img, ox + x1 - 1, y, colour)


def contact_sheet(path, panels):
    """panels = [(frame, box, colour)]; side by side, the box outlined."""
    sheet = Img(W * len(panels), H)
    for i, (frame, box, colour) in enumerate(panels):
        for y in range(H):
            for x in range(W):
                put(sheet, i * W + x, y, frame[y * W + x])
        if box:
            outline(sheet, box, colour, i * W)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    write_png(path, sheet)


def judge(fixture_name, fx, frames_dir, before_dir, twin_path, artefacts, summary, out=sys.stdout):
    keys = fx["hold_keys"]
    ours = []
    for k in keys:
        p = os.path.join(frames_dir, "f%d.ppm" % k)
        if not os.path.exists(p):
            out.write("SKIP %s: missing %s\n" % (fixture_name, p))
            return 77
        ours.append((k, load_ours(p)))
    box = fx["box"]
    per = [(k, counts(f, box)) for k, f in ours]
    med_o = median([o for _, (o, _) in per])
    med_b = median([b for _, (_, b) in per])
    ok = med_o >= ORANGE_LIMIT and med_b >= BRIGHT_LIMIT
    row = "%-12s ours median orange=%d bright=%d (limits >= %d / >= %d)" % (fixture_name, med_o, med_b, ORANGE_LIMIT, BRIGHT_LIMIT)
    before_frame = None
    if before_dir:
        bper = []
        for k in keys:
            p = os.path.join(before_dir, "f%d.ppm" % k)
            if os.path.exists(p):
                f = load_ours(p)
                bper.append(counts(f, box))
                if before_frame is None:
                    before_frame = f
        if bper:
            bo, bb = median([o for o, _ in bper]), median([b for _, b in bper])
            row += " | before orange=%d bright=%d" % (bo, bb)
            if bo >= ORANGE_LIMIT and bb >= BRIGHT_LIMIT:
                row += " (!before already passes: the judge does not separate)"
                ok = False
    twin_frame = None
    if twin_path:
        if not os.path.exists(twin_path):
            out.write("SKIP %s: missing twin %s\n" % (fixture_name, twin_path))
            return 77
        twin_frame = load_twin(twin_path)
        to, tb = counts(twin_frame, fx["twin_box"])
        row += " | twin orange=%d bright=%d" % (to, tb)
        if not (to >= ORANGE_LIMIT and tb >= BRIGHT_LIMIT):
            row += " (!twin shows no effect)"
            ok = False
    row += "  -> %s" % ("PASS" if ok else "FAIL")
    out.write(row + "\n")
    if not summary:
        for k, (o, b) in per:
            out.write("  f%d orange=%d bright=%d\n" % (k, o, b))
    if artefacts:
        mid = sorted(per, key=lambda kv: kv[1][0])[len(per) // 2][0]
        mid_frame = dict(ours)[mid]
        panels = [(mid_frame, box, (255, 255, 0))]
        if before_frame is not None:
            panels.append((before_frame, box, (255, 0, 0)))
        if twin_frame is not None:
            panels.append((twin_frame, fx["twin_box"], (0, 255, 255)))
        contact_sheet(os.path.join(artefacts, fixture_name + ".png"), panels)
    return 0 if ok else 1


def selftest():
    # A dark scene with a 30x30 orange glow patch inside the box passes; without it fails; a white
    # (bright, not orange) patch alone fails the orange limit.
    box = [150, 20, 330, 120]

    def scene(glow, white=False):
        f = [(20, 24, 40)] * (W * H)
        if glow:
            for y in range(50, 80):
                for x in range(220, 250):
                    f[y * W + x] = (255, 255, 255) if white else (255, 170, 40)
        return f

    o, b = counts(scene(True), box)
    assert o == 900 and b == 900, (o, b)
    o, b = counts(scene(False), box)
    assert o == 0 and b == 0, (o, b)
    o, b = counts(scene(True, white=True), box)
    assert o == 0 and b == 900, (o, b)
    assert median([3, 1, 2]) == 2 and median([]) == 0
    # the verdict rule
    assert (900 >= ORANGE_LIMIT and 900 >= BRIGHT_LIMIT) and not (0 >= ORANGE_LIMIT)
    print("judge_cane selftest PASS")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixture")
    ap.add_argument("--fixtures-json", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "cane", "fixtures.json"))
    ap.add_argument("--frames")
    ap.add_argument("--before")
    ap.add_argument("--twin")
    ap.add_argument("--artefacts")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.fixture or not a.frames:
        ap.error("--fixture and --frames are required (or --selftest)")
    with open(a.fixtures_json) as f:
        fixtures = json.load(f)
    if a.fixture not in fixtures:
        ap.error("unknown fixture %s" % a.fixture)
    if not os.path.isdir(a.frames):
        print("SKIP: no frames dir %s" % a.frames)
        return 77
    return judge(a.fixture, fixtures[a.fixture], a.frames, a.before, a.twin, a.artefacts, a.summary)


if __name__ == "__main__":
    sys.exit(main())
