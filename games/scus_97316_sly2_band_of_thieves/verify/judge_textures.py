#!/usr/bin/env python3
"""judge_textures.py -- the Sly 2 TEXTURE gate: every distinct texture the engine decoded in a leg,
judged against two oracles.

  Oracle B (self-consistency, computed by the engine at dump time): the FAST decode tier
  (BlockDecode::Decode, page LUT + NEON) must equal the per-pixel PsmLayout reference byte for
  byte -- the `oracle_b` field of each <hash>.json written by NATIVEEE_TEX_DUMP=<from>,<to>
  (engine TextureDumpSink, <harness-out or dump dir>/textures/).
  Oracle A (hardware, PCSX2 2.7.329 HW renderer): PCSX2's per-draw texture dumps
  (DumpGSData + SaveTexture + SaveAlpha + SaveInfo: `<draw>_f<frame>_itex_<gs|tgt>_<TBP0>(<src>)_<PSM>_
  <WMS><WMT>_<MINU>_<MAXU>_<MINV>_<MAXV>.png` + `_alpha.png` + `<draw>_context.txt`) matched to ours by
  the TEX0 tuple (TBP0, PSM, TBW, TW, TH, CBP, CPSM, CSM, CSA); the decoded images compared per texel.

PASS = 0 textures differ under Oracle B and 0 matched textures differ beyond tolerance under Oracle A
(mean abs error <= 1/255 per channel over RGB + alpha, no structural periodicity in the error mask).
The 16-bit expansion conventions differ (ours replicates the 5-bit channel, PCSX2 shifts: GS/GSBlock.h
Expand16to32 `c << 3`), so colours sourced from a 16-bit texel or a 16-bit CLUT entry are compared at
5-bit precision. Textures PCSX2 did not sample in its dumped frames are "unmatched" (not a failure;
the dumps are frame samples). A read PCSX2 served from a render target ("tgt") that we decoded from
memory, or a "gs" read we served from a target (serves.tsv), is reported as class `source` --
informational, not failing: both texture caches decide that by heuristics.

On FAIL every finding prints hash, PSM/CPSM/CSA/TBW/TW/TH, the error class, the first draw / frame
that used it, and a contact sheet PNG (ours | oracle | diff) is written under <artefacts>/textures/.
Error classes: swizzle(P) = the error mask is periodic at P in {8,16,32,64} px (block / column / page
fault); clut = every one of our colours maps to exactly one oracle colour (palette-wide: index / alpha
convention / CSA); format_tbw = the mask is whole-row or whole-column stripes, or ours equals the
oracle shifted by half its width (TBW / format fault); source = a wholly different image; value =
small bounded differences everywhere else (TEXA / alpha rule).

Exit 0 PASS, 1 FAIL, 77 SKIP (no textures directory / no <hash>.json inside; the ctest row runs
without a disc). `--selftest` exercises the classifier on synthetic images (no fixtures).

Usage:
  judge_textures.py --textures <leg>/textures [--pcsx2 <hwdump dir>] [--artefacts <dir>] [--summary]
  judge_textures.py --selftest
"""
import argparse
import glob
import json
import os
import re
import struct
import sys
import zlib

INDEXED = {0x13, 0x14, 0x1B, 0x24, 0x2C}
SIXTEEN = {0x02, 0x0A, 0x32, 0x3A}
PSM_NAMES = {"C_32": 0x00, "C_24": 0x01, "C_16": 0x02, "C_16S": 0x0A, "P_8": 0x13, "P_4": 0x14, "P_8H": 0x1B,
             "P_4HL": 0x24, "P_4HH": 0x2C, "Z_32": 0x30, "Z_24": 0x31, "Z_16": 0x32, "Z_16S": 0x3A}
MAE_TOLERANCE = 1.0          # mean abs error per channel, /255
DIFF_THRESHOLD = 8           # a texel counts as "different" above this per-channel error
ITEX_RE = re.compile(r"^(\d+)_f(\d+)_itex_(gs|tgt)_([0-9a-f]+)\(([0-9a-f]+)\)_([A-Z]_[0-9A-Z]+)_(\d)(\d)_([0-9a-f]+)_([0-9a-f]+)_([0-9a-f]+)_([0-9a-f]+)(_alpha)?\.png$")


# ----------------------------------------------------------------------------- images
class Img:
    """w x h RGBA8 as a flat bytearray (row-major, 4 bytes per texel)."""

    def __init__(self, w, h, data=None):
        self.w, self.h = w, h
        self.px = bytearray(data) if data is not None else bytearray(w * h * 4)

    def crop(self, x0, y0, w, h):
        out = Img(w, h)
        for y in range(h):
            s = ((y0 + y) * self.w + x0) * 4
            out.px[y * w * 4:(y + 1) * w * 4] = self.px[s:s + w * 4]
        return out


def read_pnm(path):
    data = open(path, "rb").read()
    # Header: magic, width, height, maxval, each followed by ONE whitespace byte; the body
    # may start with whitespace-valued bytes, so the tokens are scanned, never split.
    pos = 0
    tokens = []
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    pos += 1   # the single whitespace after maxval
    magic, w, h = tokens[0], int(tokens[1]), int(tokens[2])
    body = data[pos:]
    if magic == b"P6":
        return "rgb", w, h, body[:w * h * 3]
    if magic == b"P5":
        return "gray", w, h, body[:w * h]
    raise ValueError("not a binary PPM/PGM: " + path)


def read_ours(base):
    kind, w, h, rgb = read_pnm(base + ".ppm")
    im = Img(w, h)
    alpha = None
    if os.path.exists(base + ".alpha.pgm"):
        _, aw, ah, alpha = read_pnm(base + ".alpha.pgm")
        if (aw, ah) != (w, h):
            alpha = None
    px = im.px
    for i in range(w * h):
        px[i * 4] = rgb[i * 3]
        px[i * 4 + 1] = rgb[i * 3 + 1]
        px[i * 4 + 2] = rgb[i * 3 + 2]
        px[i * 4 + 3] = alpha[i] if alpha else 0x80
    return im


def read_png(path):
    """Minimal PNG reader: 8-bit gray / RGB / RGBA, non-interlaced. Returns (w, h, channels, bytes)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)
    pos = 8
    idat = []
    w = h = bitdepth = ctype = None
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        t = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h, bitdepth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bitdepth != 8 or interlace != 0:
                raise ValueError("unsupported PNG (bitdepth %d interlace %d): %s" % (bitdepth, interlace, path))
        elif t == b"IDAT":
            idat.append(body)
        elif t == b"IEND":
            break
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(b"".join(idat))
    stride = w * ch
    out = bytearray(w * h * ch)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, bytes(out)


def write_png(path, img):
    raw = b"".join(b"\x00" + bytes(img.px[y * img.w * 4:(y + 1) * img.w * 4]) for y in range(img.h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", img.w, img.h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_pcsx2_texture(path_rgb, path_alpha):
    w, h, ch, rgb = read_png(path_rgb)
    im = Img(w, h)
    alpha = None
    if path_alpha and os.path.exists(path_alpha):
        aw, ah, ach, a = read_png(path_alpha)
        if (aw, ah) == (w, h):
            alpha = a if ach == 1 else bytes(a[i * ach] for i in range(w * h))
    px = im.px
    for i in range(w * h):
        px[i * 4] = rgb[i * ch]
        px[i * 4 + 1] = rgb[i * ch + 1] if ch >= 3 else rgb[i * ch]
        px[i * 4 + 2] = rgb[i * ch + 2] if ch >= 3 else rgb[i * ch]
        px[i * 4 + 3] = alpha[i] if alpha else 0x80
    return im


# ----------------------------------------------------------------------------- comparison
def compare(ours, oracle, five_bit_rgb):
    """Per-texel comparison. Returns (mae per channel [4], diff mask list of 0/1, palette_like, n).
    palette_like: among the differing texels, every one of our colours maps to exactly ONE oracle
    colour, the colours involved number at most 256 and each is reused (>= 4 texels on average) --
    the signature of a palette-wide fault rather than of two unrelated images."""
    n = ours.w * ours.h
    sums = [0, 0, 0, 0]
    mask = bytearray(n)
    colour_map = {}
    consistent = True
    marked = 0
    a, b = ours.px, oracle.px
    for i in range(n):
        o = i * 4
        d = [0, 0, 0, 0]
        for c in range(3):
            x, y = a[o + c], b[o + c]
            if five_bit_rgb:
                x, y = x >> 3, y >> 3
                d[c] = abs(x - y) * 8
            else:
                d[c] = abs(x - y)
        d[3] = abs(a[o + 3] - b[o + 3])
        for c in range(4):
            sums[c] += d[c]
        if max(d) > DIFF_THRESHOLD:
            mask[i] = 1
            marked += 1
            key = bytes(a[o:o + 4])
            val = bytes(b[o:o + 4])
            if colour_map.setdefault(key, val) != val:
                consistent = False
    mae = [s / n for s in sums] if n else [0, 0, 0, 0]
    palette_like = consistent and 0 < len(colour_map) <= 256 and marked >= 4 * len(colour_map)
    return mae, mask, palette_like, n


def shift_explained(ours, oracle, mask):
    """Best (lag, fraction) over lags 1..64 (powers of two) along x and y: the fraction of the
    differing texels whose OUR colour equals the ORACLE colour at +lag or -lag -- a swizzle fault
    (block / column / page order) moves texels by a fixed power-of-two stride and is explained
    by one lag; unrelated images and value faults are not."""
    w, h = ours.w, ours.h
    marked = [i for i in range(w * h) if mask[i]]
    if not marked:
        return None, 0.0
    sample = marked if len(marked) <= 4096 else marked[::max(1, len(marked) // 4096)]
    best = (None, 0.0)
    for axis in ("x", "y"):
        extent = w if axis == "x" else h
        for p in (1, 2, 4, 8, 16, 32, 64):
            if p >= extent:
                continue
            hit = 0
            for i in sample:
                x, y = i % w, i // w
                me = ours.px[i * 4:i * 4 + 3]
                for sgn in (1, -1):
                    nx, ny = (x + sgn * p, y) if axis == "x" else (x, y + sgn * p)
                    if 0 <= nx < w and 0 <= ny < h:
                        j = ny * w + nx
                        if oracle.px[j * 4:j * 4 + 3] == me:
                            hit += 1
                            break
            frac = hit / len(sample)
            if frac > best[1]:
                best = ("%s%d" % (axis, p), frac)
    return best


def periodicity(mask, w, h):
    """Best (period, score) of the error mask along x then y for periods 8/16/32/64: score = the
    fraction of marked texels whose neighbour at +period is also marked, minus the same for a
    non-period offset (the mask's own density), so a random mask scores ~0 and a swizzled mask ~1."""
    total = sum(mask)
    if total == 0:
        return None, 0.0
    density = total / (w * h)
    best = (None, 0.0)
    for axis in ("x", "y"):
        extent = w if axis == "x" else h
        for p in (8, 16, 32, 64):
            if p * 2 > extent:
                continue
            hit = cnt = 0
            for y in range(h):
                for x in range(w):
                    if not mask[y * w + x]:
                        continue
                    cnt += 1
                    nx, ny = (x + p, y) if axis == "x" else (x, y + p)
                    if nx < w and ny < h and mask[ny * w + nx]:
                        hit += 1
            if cnt == 0:
                continue
            score = hit / cnt - density
            if score > best[1]:
                best = ("%s%d" % (axis, p), score)
    return best


def stripes(mask, w, h):
    """Whole rows or whole columns alternating fully-marked / clean."""
    rows = [sum(mask[y * w:(y + 1) * w]) for y in range(h)]
    cols = [sum(mask[y * w + x] for y in range(h)) for x in range(w)]
    full_rows = sum(1 for r in rows if r == w)
    clean_rows = sum(1 for r in rows if r == 0)
    full_cols = sum(1 for c in cols if c == h)
    clean_cols = sum(1 for c in cols if c == 0)
    return (full_rows + clean_rows == h and 0 < full_rows < h) or (full_cols + clean_cols == w and 0 < full_cols < w)


def half_shift(ours, oracle):
    """ours == oracle shifted by half its width (a doubled / TBW-halved image)."""
    w, h = ours.w, ours.h
    if w < 16:
        return False
    hw = w // 2
    same = 0
    for y in range(h):
        for x in range(hw):
            if ours.px[(y * w + x) * 4:(y * w + x) * 4 + 3] == oracle.px[(y * w + x + hw) * 4:(y * w + x + hw) * 4 + 3]:
                same += 1
    return same >= 0.9 * hw * h


def classify(ours, oracle, mae, mask, palette_like):
    n = len(mask)
    marked = sum(mask)
    if max(mae) <= MAE_TOLERANCE and marked <= n * 0.002:
        return "pass", ""
    lag, frac = shift_explained(ours, oracle, mask)
    if marked > n * 0.01 and lag and frac >= 0.5:
        return "swizzle", "%s explains %.0f%%" % (lag, 100 * frac)
    period, score = periodicity(mask, ours.w, ours.h)
    if marked > n * 0.01 and marked < n * 0.6 and period and score >= 0.5:
        return "swizzle", "mask periodic at %s" % period
    if stripes(mask, ours.w, ours.h) or half_shift(ours, oracle):
        return "format_tbw", ""
    if palette_like and marked > n * 0.02:
        return "clut", "palette-wide"
    if marked > n * 0.6:
        return "source", ""
    return "value", "bounded differences"


def contact_sheet(path, ours, oracle, mask):
    w, h = ours.w, ours.h
    sheet = Img(w * 3 + 8, h)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            d = (y * sheet.w + x) * 4
            sheet.px[d:d + 3] = ours.px[o:o + 3]
            sheet.px[d + 3] = 255
            d2 = (y * sheet.w + x + w + 4) * 4
            sheet.px[d2:d2 + 3] = oracle.px[o:o + 3]
            sheet.px[d2 + 3] = 255
            d3 = (y * sheet.w + x + 2 * w + 8) * 4
            if mask[y * w + x]:
                sheet.px[d3:d3 + 4] = b"\xff\x00\x00\xff"
            else:
                g = (ours.px[o] + ours.px[o + 1] + ours.px[o + 2]) // 6
                sheet.px[d3:d3 + 4] = bytes([g, g, g, 255])
        for x in range(w, w + 4):
            for xx in (x, x + w + 4):
                d = (y * sheet.w + xx) * 4
                sheet.px[d:d + 4] = b"\x40\x40\x40\xff"
    write_png(path, sheet)


# ----------------------------------------------------------------------------- inputs
def load_ours(tex_dir):
    out = []
    for jp in sorted(glob.glob(os.path.join(tex_dir, "*.json"))):
        j = json.load(open(jp))
        j["_base"] = jp[:-5]
        out.append(j)
    serves = []
    sp = os.path.join(tex_dir, "serves.tsv")
    if os.path.exists(sp):
        with open(sp) as f:
            head = f.readline().rstrip("\n").split("\t")
            for line in f:
                row = dict(zip(head, line.rstrip("\n").split("\t")))
                serves.append(row)
    return out, serves


def parse_context(path):
    """TEX0 fields of a PCSX2 <draw>_context.txt."""
    fields = {}
    sec = None
    for line in open(path, errors="replace"):
        if not line.startswith(" ") and line.strip().endswith(":"):
            sec = line.strip()[:-1]
            continue
        if sec == "TEX0":
            m = re.match(r"\s+(\w+):\s*(0x[0-9a-fA-F]+|\d+)", line)
            if m:
                fields[m.group(1)] = int(m.group(2), 0)
    return fields


def load_pcsx2(dump_dir):
    """Every `itex` dump as a record keyed by TEX0 tuple; images deduplicated by file content."""
    recs = []
    seen_content = {}
    ctx_cache = {}
    for name in sorted(os.listdir(dump_dir)):
        m = ITEX_RE.match(name)
        if not m or m.group(13):
            continue
        draw, frame, src, tbp0, psm_name = int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4), 16), m.group(6)
        psm = PSM_NAMES.get(psm_name)
        if psm is None:
            continue
        ctx_path = os.path.join(dump_dir, "%d_context.txt" % draw)
        if ctx_path not in ctx_cache:
            ctx_cache[ctx_path] = parse_context(ctx_path) if os.path.exists(ctx_path) else {}
        tex0 = ctx_cache[ctx_path]
        rgb = os.path.join(dump_dir, name)
        alpha = rgb[:-4] + "_alpha.png"
        key = (tbp0 * 256, psm, tex0.get("TBW", 0) * 64, tex0.get("TW"), tex0.get("TH"), tex0.get("CBP", 0) * 256,
               tex0.get("CPSM", 0), tex0.get("CSM", 0), tex0.get("CSA", 0))
        content = zlib.crc32(open(rgb, "rb").read())
        if os.path.exists(alpha):
            content = zlib.crc32(open(alpha, "rb").read(), content)
        dedup = (key, src, content)
        if dedup in seen_content:
            continue
        seen_content[dedup] = True
        recs.append({"draw": draw, "frame": frame, "src": src, "key": key, "rgb": rgb, "alpha": alpha,
                     "wms": int(m.group(7)), "wmt": int(m.group(8)), "minu": int(m.group(9), 16), "maxu": int(m.group(10), 16),
                     "minv": int(m.group(11), 16), "maxv": int(m.group(12), 16)})
    return recs


def our_key(j):
    return (j["tbp0"], j["psm"], j["tbw"], j["tw"], j["th"], j["cbp"] if j["psm"] in INDEXED else 0,
            j["cpsm"] if j["psm"] in INDEXED else 0, j["csm"] if j["psm"] in INDEXED else 0, j["csa"] if j["psm"] in INDEXED else 0)


def pcsx2_key_for_match(k, psm):
    if psm in INDEXED:
        return k
    return (k[0], k[1], k[2], k[3], k[4], 0, 0, 0, 0)


def five_bit(j):
    return j["psm"] in SIXTEEN or (j["psm"] in INDEXED and j["cpsm"] in SIXTEEN)


# ----------------------------------------------------------------------------- the judge
def judge(tex_dir, pcsx2_dir, artefacts, summary_only=False, out=sys.stdout):
    ours, serves = load_ours(tex_dir)
    if not ours:
        print("SKIP: no <hash>.json under %s" % tex_dir, file=out)
        return 77
    pcsx2 = load_pcsx2(pcsx2_dir) if pcsx2_dir and os.path.isdir(pcsx2_dir) else []
    by_key = {}
    for r in pcsx2:
        by_key.setdefault(pcsx2_key_for_match(r["key"], r["key"][1]), []).append(r)
    findings = []
    counts = {"checked": len(ours), "oracle_b_bad": 0, "matched": 0, "unmatched": 0, "size_mismatch": 0, "pass": 0,
              "swizzle": 0, "clut": 0, "format_tbw": 0, "source": 0, "value": 0, "source_tier": 0}
    sheet_dir = os.path.join(artefacts, "textures") if artefacts else None
    for j in ours:
        h = j["hash"]
        where = "psm=%02x cpsm=%02x csm=%d csa=%d tbw=%u tw=%u th=%u tbp0=%06x cbp=%06x first_draw=%u first_frame=%u" % (
            j["psm"], j["cpsm"], j["csm"], j["csa"], j["tbw"], j["tw"], j["th"], j["tbp0"], j["cbp"], j["first_draw"], j["first_frame"])
        if j.get("oracle_b") != "equal":
            counts["oracle_b_bad"] += 1
            findings.append((h, "oracle_b", "fast != reference: %s diffs first_xy=%s" % (j.get("oracle_b_diffs"), j.get("oracle_b_first_xy")), where))
            if sheet_dir and os.path.exists(j["_base"] + ".ref.ppm"):
                a = read_ours(j["_base"])
                _, w, hh, rgb = read_pnm(j["_base"] + ".ref.ppm")
                b = Img(w, hh)
                for i in range(w * hh):
                    b.px[i * 4:i * 4 + 3] = rgb[i * 3:i * 3 + 3]
                    b.px[i * 4 + 3] = a.px[i * 4 + 3]
                mae, mask, _, _ = compare(a, b, False)
                contact_sheet(os.path.join(sheet_dir, h + "_oracle_b.png"), a, b, mask)
        if not pcsx2:
            continue
        cands = by_key.get(pcsx2_key_for_match(our_key(j), j["psm"]), [])
        gs_cands = [c for c in cands if c["src"] == "gs"]
        tgt_cands = [c for c in cands if c["src"] == "tgt"]
        if not gs_cands:
            if tgt_cands:
                counts["source_tier"] += 1
                findings.append((h, "source(info)", "PCSX2 served this read from a render target (tgt @%05x, draw %d f%d); we decoded memory" % (
                    tgt_cands[0]["key"][0] // 256, tgt_cands[0]["draw"], tgt_cands[0]["frame"]), where))
            else:
                counts["unmatched"] += 1
            continue
        a = read_ours(j["_base"])
        full_w, full_h = 1 << j["tw"], 1 << j["th"]
        best = None
        for c in gs_cands:
            b = read_pcsx2_texture(c["rgb"], c["alpha"])
            # Our image may be a UV sub-rect of the level; PCSX2's may be region-clamped (REGION_CLAMP = 2).
            ox, oy = (j["x0"], j["y0"]) if j.get("sub") else (0, 0)
            bx = by = 0
            if (b.w, b.h) != (full_w, full_h):
                if c["wms"] == 2:
                    bx = c["minu"]
                if c["wmt"] == 2:
                    by = c["minv"]
            x0, y0 = ox - bx, oy - by
            if x0 < 0 or y0 < 0 or x0 + a.w > b.w or y0 + a.h > b.h:
                continue
            bc = b.crop(x0, y0, a.w, a.h) if (x0, y0, a.w, a.h) != (0, 0, b.w, b.h) else b
            mae, mask, palette_like, n = compare(a, bc, five_bit(j))
            score = max(mae)
            if best is None or score < best[0]:
                best = (score, c, bc, mae, mask, palette_like)
        if best is None:
            counts["size_mismatch"] += 1
            findings.append((h, "size", "no PCSX2 candidate covers our %dx%d (sub=%d at %d,%d); PCSX2 sizes %s" % (
                a.w, a.h, j.get("sub", 0), j.get("x0", 0), j.get("y0", 0), sorted(set((read_png(c["rgb"])[0], read_png(c["rgb"])[1]) for c in gs_cands))), where))
            continue
        counts["matched"] += 1
        score, c, bc, mae, mask, palette_like = best
        cls, detail = classify(a, bc, mae, mask, palette_like)
        counts[cls] += 1
        if cls != "pass":
            findings.append((h, cls, "%s mae=%.2f/%.2f/%.2f/%.2f marked=%.1f%% vs PCSX2 draw %d f%d" % (
                detail, mae[0], mae[1], mae[2], mae[3], 100.0 * sum(mask) / len(mask), c["draw"], c["frame"]), where))
            if sheet_dir:
                contact_sheet(os.path.join(sheet_dir, h + "_" + cls + ".png"), a, bc, mask)
    failing = [f for f in findings if f[1] not in ("source(info)",)]
    # Served-from-target reads PCSX2 read from memory (the inverse source class).
    served_info = 0
    if pcsx2 and serves:
        for s in serves:
            if s.get("tier") not in ("target", "selfread", "depth"):
                continue
            k = (int(s["tbp"], 16), int(s["psm"], 16), int(s["tbw"]), int(s["tw"]), int(s["th"]), 0, 0, 0, 0)
            psm = k[1]
            if psm in INDEXED:
                k = (k[0], psm, k[2], k[3], k[4], int(s["cbp"], 16), int(s["cpsm"], 16), int(s["csm"]), int(s["csa"]))
            if any(c["src"] == "gs" for c in by_key.get(k, [])):
                served_info += 1
                findings.append(("-", "source(info)", "we served tier=%s from a target; PCSX2 read memory (gs)" % s["tier"],
                                 "psm=%s tbp0=%s tw=%s th=%s frame=%s draw=%s" % (s["psm"], s["tbp"], s["tw"], s["th"], s["frame"], s["draw"])))
    counts["source_tier"] += served_info
    # Report.
    print("judge_textures: %s%s" % (tex_dir, (" vs PCSX2 " + pcsx2_dir) if pcsx2 else " (no PCSX2 dumps: Oracle B only)"), file=out)
    print("  checked=%d oracle_b_bad=%d | oracle A: pcsx2_textures=%d matched=%d unmatched=%d size_mismatch=%d pass=%d" % (
        counts["checked"], counts["oracle_b_bad"], len(pcsx2), counts["matched"], counts["unmatched"], counts["size_mismatch"], counts["pass"]), file=out)
    print("  mismatched by class: swizzle=%d clut=%d format_tbw=%d source=%d value=%d | source-tier (info)=%d" % (
        counts["swizzle"], counts["clut"], counts["format_tbw"], counts["source"], counts["value"], counts["source_tier"]), file=out)
    if not summary_only:
        for h, cls, detail, where in findings:
            print("  %-7s %s %s  [%s]" % (cls, h, detail, where), file=out)
    verdict = "PASS" if not failing else "FAIL"
    print("judge_textures: %s (%d failing findings)" % (verdict, len(failing)), file=out)
    return 0 if not failing else 1


# ----------------------------------------------------------------------------- selftest
def selftest():
    import tempfile
    tmp = tempfile.mkdtemp(prefix="judge_textures_")
    w = h = 64
    base = Img(w, h)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            base.px[o:o + 4] = bytes([(x * 4) & 255, (y * 4) & 255, ((x ^ y) * 3) & 255, 0x80])
    ok = True

    def check(cond, msg):
        nonlocal ok
        if not cond:
            ok = False
            print("SELFTEST FAIL:", msg)
    # identical -> pass
    mae, mask, cons, n = compare(base, base, False)
    check(classify(base, base, mae, mask, cons)[0] == "pass", "identical images pass")
    # 16-bit expansion difference (replicate vs shift) within tolerance at 5-bit precision
    shifted = Img(w, h, base.px)
    for i in range(w * h):
        for c in range(3):
            v = shifted.px[i * 4 + c] >> 3
            shifted.px[i * 4 + c] = v << 3
    mae, mask, cons, n = compare(base, shifted, True)
    check(classify(base, shifted, mae, mask, cons)[0] == "pass", "5-bit comparison tolerates the expansion convention")
    mae, mask, cons, n = compare(base, shifted, False)
    check(max(mae) > 1.0, "8-bit comparison sees the expansion difference (mae %.2f)" % max(mae))
    # swizzle fault: every other 8-px block column swapped with its neighbour
    sw = Img(w, h)
    for y in range(h):
        for x in range(w):
            sx = x ^ 8
            sw.px[(y * w + x) * 4:(y * w + x) * 4 + 4] = base.px[(y * w + sx) * 4:(y * w + sx) * 4 + 4]
    mae, mask, cons, n = compare(base, sw, False)
    cls, det = classify(base, sw, mae, mask, cons)
    check(cls == "swizzle", "block swap classified swizzle (got %s %s)" % (cls, det))
    # clut fault: a 16-colour paletted image whose palette is shifted
    pal = [bytes([(k * 37) & 255, (k * 59) & 255, (k * 83) & 255, 0x80]) for k in range(16)]
    idx = Img(w, h)
    cl = Img(w, h)
    for i in range(w * h):
        k = ((i % w) // 4 + (i // w) // 4) % 16
        idx.px[i * 4:i * 4 + 4] = pal[k]
        cl.px[i * 4:i * 4 + 4] = pal[(k + 5) % 16]
    mae, mask, cons, n = compare(idx, cl, False)
    cls, det = classify(idx, cl, mae, mask, cons)
    check(cls == "clut", "palette-wide remap classified clut (got %s %s)" % (cls, det))
    # format/tbw: alternating whole rows wrong
    st = Img(w, h, base.px)
    for y in range(0, h, 2):
        for x in range(w):
            o = (y * w + x) * 4
            st.px[o:o + 3] = bytes([255 - st.px[o], 255 - st.px[o + 1], 255 - st.px[o + 2]])
    mae, mask, cons, n = compare(base, st, False)
    cls, det = classify(base, st, mae, mask, cons)
    check(cls in ("format_tbw", "swizzle"), "row stripes classified format_tbw/swizzle (got %s %s)" % (cls, det))
    # source: a different image
    other = Img(w, h)
    for i in range(w * h):
        other.px[i * 4:i * 4 + 4] = bytes([(i * 7) & 255, (i * 13) & 255, (i * 29) & 255, 0x80])
    mae, mask, cons, n = compare(base, other, False)
    cls, det = classify(base, other, mae, mask, cons)
    check(cls == "source", "different image classified source (got %s %s)" % (cls, det))
    # PNG round trip + contact sheet
    p = os.path.join(tmp, "a.png")
    write_png(p, base)
    rw, rh, ch, data = read_png(p)
    check((rw, rh, ch) == (w, h, 4) and bytes(base.px) == data, "PNG writer / reader round trip")
    contact_sheet(os.path.join(tmp, "sheet.png"), base, sw, mask)
    check(os.path.exists(os.path.join(tmp, "sheet.png")), "contact sheet written")
    # The end-to-end path on a synthetic textures dir: SKIP without json, PASS with an equal oracle_b record.
    tdir = os.path.join(tmp, "textures")
    os.makedirs(tdir)
    check(judge(tdir, None, None, out=open(os.devnull, "w")) == 77, "empty textures dir -> 77")
    with open(os.path.join(tdir, "00ff.ppm"), "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h) + bytes(b for i in range(w * h) for b in base.px[i * 4:i * 4 + 3]))
    json.dump({"hash": "00ff", "tbp0": 0, "tbw": 64, "psm": 0x13, "tw": 6, "th": 6, "cbp": 8192, "cpsm": 0, "csm": 0, "csa": 0,
               "w": w, "h": h, "sub": 0, "x0": 0, "y0": 0, "first_frame": 1, "first_draw": 2, "oracle_b": "equal"},
              open(os.path.join(tdir, "00ff.json"), "w"))
    check(judge(tdir, None, None, out=open(os.devnull, "w")) == 0, "one equal record -> PASS")
    json.dump({"hash": "00fe", "tbp0": 0, "tbw": 64, "psm": 0x13, "tw": 6, "th": 6, "cbp": 8192, "cpsm": 0, "csm": 0, "csa": 0,
               "w": w, "h": h, "sub": 0, "x0": 0, "y0": 0, "first_frame": 1, "first_draw": 3, "oracle_b": "differs",
               "oracle_b_diffs": 5, "oracle_b_first_xy": [1, 2]}, open(os.path.join(tdir, "00fe.json"), "w"))
    check(judge(tdir, None, None, out=open(os.devnull, "w")) == 1, "an oracle_b differs record -> FAIL")
    # A PCSX2 dump dir with one matching gs texture (identical) and one tgt record.
    pdir = os.path.join(tmp, "hwdump")
    os.makedirs(pdir)
    write_png(os.path.join(pdir, "00010_f00100_itex_gs_00000(00000)_P_8_00_00_00_00_00.png"), base)
    alpha = Img(w, h)
    for i in range(w * h):
        alpha.px[i * 4:i * 4 + 4] = bytes([0x80, 0x80, 0x80, 255])
    write_png(os.path.join(pdir, "00010_f00100_itex_gs_00000(00000)_P_8_00_00_00_00_00_alpha.png"), alpha)
    with open(os.path.join(pdir, "10_context.txt"), "w") as f:
        f.write("TEX0:\n    TBP0: 0x0\n    TBW: 1\n    PSM: 0x13 # P_8\n    TW: 6\n    TH: 6\n    TCC: 1\n    TFX: 0\n    CBP: 0x20\n    CPSM: 0x0\n    CSM: 0\n    CSA: 0\n    CLD: 1\n\nTEX1:\n    LCM: 0\n")
    os.remove(os.path.join(tdir, "00fe.json"))
    recs = load_pcsx2(pdir)
    check(len(recs) == 1 and recs[0]["key"] == (0, 0x13, 64, 6, 6, 8192, 0, 0, 0), "PCSX2 record parsed: %s" % str(recs[0]["key"] if recs else None))
    check(judge(tdir, pdir, tmp, out=open(os.devnull, "w")) == 0, "matched identical texture -> PASS")
    # The same tuple with a swizzled oracle -> FAIL with a contact sheet.
    write_png(os.path.join(pdir, "00010_f00100_itex_gs_00000(00000)_P_8_00_00_00_00_00.png"), sw)
    check(judge(tdir, pdir, tmp, out=open(os.devnull, "w")) == 1, "swizzled oracle -> FAIL")
    check(os.path.exists(os.path.join(tmp, "textures", "00ff_swizzle.png")), "contact sheet for the finding")
    print("judge_textures selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--textures", help="the leg's textures/ directory (NATIVEEE_TEX_DUMP output)")
    ap.add_argument("--pcsx2", help="PCSX2 HWDumpDirectory with itex dumps (+ context.txt)")
    ap.add_argument("--artefacts", help="where <artefacts>/textures/<hash>_<class>.png contact sheets go")
    ap.add_argument("--summary", action="store_true", help="the table only, no per-finding lines")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.textures or not os.path.isdir(a.textures):
        print("SKIP: no textures directory (%s)" % a.textures)
        return 77
    return judge(a.textures, a.pcsx2, a.artefacts, a.summary)


if __name__ == "__main__":
    sys.exit(main())
