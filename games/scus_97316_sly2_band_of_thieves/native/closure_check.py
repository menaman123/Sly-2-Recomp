#!/usr/bin/env python3
"""closure_check.py -- the checkable meaning of "all parts native" for Sly 2 (ctest Sly2_NativeClosure).

Re-derives, from the disc (or the ELF alone), the finite set of inputs the game can hand to each PS2 unit the native
module serves, and FAILS when one of them has no native path:

  VU1  the microprograms = the ELF's .vutext overlay programs (.DVP.ovlytab) -> every VU1 program's CONTENT hash must
       be a row of native/vu1/images.txt AND (with --generated) a generated TU; every MSCAL entry the EE code can form
       (the VU address constants passed to the packet builders' MSCAL helpers) must be a block of every program that
       can hold it; no VCALLMSR-like dynamic VU1 kick exists (MSCNT continuations are the loop's).
  VU0  the VCALLMS immediates of .text must be a subset of the native VU0 entry set; no VCALLMSR; no CTC2 to CMSAR0;
       the VU0 image (program 11753019) must sit where native/vu0 reads it (vaddr/size).
  IOP  the IRX modules the game can load = the ELF's .irx section + IOPRP300.IMG's ROMDIR (--iso): every module name must
       be in the enumerated set below (a new module = a closure failure; the hosting status is COVERAGE.md's).
  DISC with --iso: the level streams ('ff ?? ?? FK$X' sector heads in the raw region) are enumerated; --quick decompresses
       the two streams the ELF's own level table names and scans them for microcode / MPG uploads (must be 0, the disc
       carries none: the VIF chains, GIFtags and microprograms are produced by the EE code and .vutext); --full scans all
       43 streams and the raw region (minutes; the recorded run is in COVERAGE.md).
Exit 0 = closed; 1 = a gap (printed); 2 = usage / missing input. Pure Python 3 (numpy optional, for --full).
Facts only: hashes, addresses, counts -- no game bytes are written anywhere by this tool.
"""
import argparse
import hashlib
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "vu1"))
import extract_images as EI  # noqa: E402  (the same extraction the build runs)

SECTOR = 2048
RAW_START = 0xCA0
VU0_PROGRAM_ID = EI.VU0_PROGRAM_ID
VU0_IMAGE_VADDR = 0x259E00
VU0_IMAGE_SIZE = 0x2A0
VU0_ENTRIES = {0x0, 0x10, 0x60, 0xF0, 0x120, 0x150, 0x1C0}                   # native/vu0 (PS2R_SLY2_VU0_ENTRIES)
MSCAL_HELPERS = {0x226CE0, 0x226D10, 0x226D28}                               # the packet builders' MSCAL/MSCNT helpers
IRX_ELF = {"sio2man", "padman", "mcman", "mcserv", "Sound_Device_Library", "USB_driver", "lgAud", "989snd_Library", "spiop"}
IRX_ROM = {"System_Memory_Manager", "Module_Manager", "IOP_SIF_rpc_interface", "IOP_SIF_manager", "Multi_Thread_Manager",
           "IO/File_Manager", "Moldule_File_loader", "FILEIO_service", "cdvd_driver", "cdvd_ee_driver", "LoadModuleByEE",
           "Timer_Manager", "ROM_file_driver", "SyncEE", "System_C_lib", "Stdio"}
LEVEL_TABLE = (0x2A3268, 0x2A39E8, 0x40)                                     # .rodata obfuscated records (lba=w0^w13+0xca0)


class Report:
    def __init__(self):
        self.fails = []
        self.lines = []

    def ok(self, msg):
        self.lines.append("  ok   " + msg)

    def fail(self, msg):
        self.lines.append("  FAIL " + msg)
        self.fails.append(msg)

    def info(self, msg):
        self.lines.append("  .    " + msg)


# ---------------------------------------------------------------------------------------------------------------
def read_images_txt(path):
    rows = {}
    with open(path) as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            tok = s.split()
            if len(tok) >= 3 and re.fullmatch(r"[0-9a-f]{16}", tok[0]):
                rows[int(tok[0], 16)] = {"id": tok[1], "size": int(tok[2], 16)}
    return rows


def text_words(data):
    sec = EI.elf_sections(data)
    t = sec[".text"]
    return t["addr"], struct.unpack_from("<%dI" % (t["size"] // 4), data, t["offset"])


def vcallms_sites(words):
    imm = {}
    msr = 0
    cmsar0 = 0
    for w in words:
        if (w & 0xFE00003F) == 0x4A000038:
            imm[((w >> 6) & 0x7FFF) * 8] = imm.get(((w >> 6) & 0x7FFF) * 8, 0) + 1
        elif (w & 0xFE00003F) == 0x4A000039:
            msr += 1
        elif (w & 0xFFE007FF) == 0x48C00000 and ((w >> 11) & 31) == 27:  # ctc2 rt, vi27 (CMSAR0)
            cmsar0 += 1
    return imm, msr, cmsar0


def mscal_entries(tbase, words):
    """VU addresses passed (lui 0 + addiu) to the MSCAL helpers within 8 instructions: {addr: sites}."""
    out = {}
    n = len(words)
    for k, w in enumerate(words):
        if (w >> 26) == 0x0F and (w & 0xFFFF) == 0:
            rt = (w >> 16) & 31
            for j in range(1, 5):
                if k + j >= n:
                    break
                w2 = words[k + j]
                if (w2 >> 26) == 0x09 and ((w2 >> 21) & 31) == rt:
                    for m in range(0, 9):
                        if k + j + m < n and (words[k + j + m] >> 26) == 3:
                            target = ((words[k + j + m] & 0x3FFFFFF) << 2) | (tbase & 0xF0000000)
                            if target in MSCAL_HELPERS:
                                out.setdefault(w2 & 0xFFFF, []).append(tbase + 4 * k)
                            break
                    break
    return out


def tu_blocks(path):
    with open(path, "r", errors="replace") as f:
        return set(int(m.group(1), 16) for m in re.finditer(r"\bBlk_([0-9a-f]+)\b", f.read()))


def irx_names(blob):
    """Module names of every IRX (ELF with an .iopmod section) in BLOB."""
    names = []
    p = blob.find(b"\x7fELF")
    while p >= 0:
        try:
            if struct.unpack_from("<H", blob, p + 18)[0] == 8:
                shoff = struct.unpack_from("<I", blob, p + 32)[0]
                shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, p + 46)
                names_off = struct.unpack_from("<I", blob, p + shoff + shstrndx * shentsize + 16)[0]
                for i in range(shnum):
                    sn, _, _, _, soff, ssize = struct.unpack_from("<IIIIII", blob, p + shoff + i * shentsize)
                    sname = blob[p + names_off + sn:blob.index(b"\0", p + names_off + sn)].decode()
                    if sname == ".iopmod":
                        mod = blob[p + soff:p + soff + ssize]
                        names.append(mod[26:mod.index(b"\0", 26)].decode())
                        break
        except (struct.error, ValueError, UnicodeDecodeError):
            pass
        p = blob.find(b"\x7fELF", p + 1)
    return names


def level_table(data):
    sec = EI.elf_sections(data)
    ro = sec[".rodata"]
    lo, hi, stride = LEVEL_TABLE
    rows = []
    for a in range(lo, hi, stride):
        w = struct.unpack_from("<16I", data, ro["offset"] + (a - ro["addr"]))
        rows.append({"rec": a, "lba": (w[0] ^ w[13]) + RAW_START, "size": w[1] ^ w[15], "id": w[2] ^ w[14]})
    return rows


# --- the game's decompressor (SCUS_973.16 0x1179f0) and the microcode scans, pure Python ------------------------
def decompress(src, max_out=None):
    window = bytearray(0x2000 + 16)
    out = bytearray()
    pos = 0
    i = 0
    n = len(src)
    flags = 0
    bits = 0
    while i < n:
        if bits == 0:
            flags = src[i]
            i += 1
            bits = 8
        bit = flags & 1
        flags >>= 1
        bits -= 1
        if bit:
            window[pos] = src[i]
            i += 1
            pos += 1
        else:
            if i + 1 >= n:
                break
            v = src[i] | (src[i + 1] << 8)
            i += 2
            count = (v >> 13) + 3
            o = v & 0x1FFF
            for _ in range(count):
                window[pos] = window[o & 0x1FFF]
                o += 1
                pos += 1
        if pos >= 0x2000:
            out += window[:0x2000]
            spill = pos - 0x2000
            window[:spill] = window[0x2000:0x2000 + spill]
            pos = spill
            if max_out is not None and len(out) >= max_out:
                return bytes(out[:max_out])
    out += window[:pos]
    return bytes(out)


_UP_OK = set(range(0x30))
_SP2_UP = set(range(0x2B)) | {0x2C, 0x2D, 0x2E, 0x2F}
_LO_OK = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C,
          0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F}
_SP1_LO = {0x30, 0x31, 0x32, 0x34, 0x35}
_SP2_LO = {0x30, 0x31, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43,
           0x64, 0x68, 0x69, 0x6C, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E}


def valid_bundle(lo, up):
    """VU bundle validity (opcode tables + field rules; real code passes, random data ~5 %)."""
    if (up >> 25) & 3 or (up >> 28) & 3:
        return False
    op = up & 0x3F
    if op < 0x3C:
        if op not in _UP_OK or ((up >> 21) & 0xF) == 0:
            return False
    elif ((((up >> 6) & 0x1F) << 2) | (op & 3)) not in _SP2_UP:
        return False
    if up & 0x80000000:
        return True
    lop = lo >> 25
    if lop == 0x40:
        fn = lo & 0x3F
        if fn < 0x3C:
            return fn in _SP1_LO and ((lo >> 21) & 0xF) == 0
        return ((((lo >> 6) & 0x1F) << 2) | (fn & 3)) in _SP2_LO
    if lop not in _LO_OK:
        return False
    ldest = (lo >> 21) & 0xF
    if lop == 0x20:
        return ((lo >> 11) & 0x3FFF) == 0
    if lop == 0x21:
        return ldest == 0 and ((lo >> 11) & 0x1F) == 0
    if lop in (0x24, 0x25, 0x18, 0x1A, 0x1B):
        return ldest == 0 and (lo & 0x7FF) == 0
    if lop in (0x28, 0x29):
        return ldest == 0
    if 0x2C <= lop <= 0x2F:
        return ldest == 0 and ((lo >> 16) & 0x1F) == 0
    if 0x14 <= lop <= 0x17:
        return ((lo >> 22) & 7) == 0
    if lop in (0x00, 0x01):
        return ldest != 0
    if lop in (0x04, 0x05):
        return ldest in (1, 2, 4, 8)
    return True


def microcode_scan(data, min_run=16):
    """(vu_runs, mpg) -- runs of >= MIN_RUN valid non-zero bundles whose lower words are >= 10 % NOP (the code shape)
    and >= 3 distinct upper opcodes; MPG codes whose payload is >= 95 % valid with the code shape."""
    n8 = len(data) // 8
    runs = []
    for phase in (0, 4):
        run = 0
        nops = 0
        ups = set()
        nz = 0
        i = phase
        while i + 8 <= len(data):
            lo, up = struct.unpack_from("<II", data, i)
            if valid_bundle(lo, up):
                if lo or up:
                    nz += 1
                    nops += lo == 0x8000033C
                    ups.add(up & 0x3F)
                run += 1
            else:
                if run >= min_run and nz >= min_run and nops * 10 >= nz and len(ups) >= 3:
                    runs.append((i - 8 * run, run))
                run = 0
                nops = 0
                ups = set()
                nz = 0
            i += 8
    mpg = []
    for off in range(0, len(data) - 4, 4):
        if data[off + 3] & 0x7F != 0x4A:
            continue
        code = struct.unpack_from("<I", data, off)[0]
        num = ((code >> 16) & 0xFF) or 256
        if off + 4 + 8 * num > len(data):
            continue
        ok = 0
        nops = 0
        nz = 0
        for k in range(num):
            lo, up = struct.unpack_from("<II", data, off + 4 + 8 * k)
            if valid_bundle(lo, up):
                ok += 1
            if lo or up:
                nz += 1
                nops += lo == 0x8000033C
        if ok * 100 >= 95 * num and nz and nops * 10 >= nz:
            mpg.append((off, num, (code & 0xFFFF) * 8))
    return runs, mpg


# ---------------------------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", help="the disc image")
    ap.add_argument("--elf", help="the ELF (SCUS_973.16) when no --iso")
    ap.add_argument("--generated", help="<build>/generated/games/<title>/native/vu1 (the TUs the build generated)")
    ap.add_argument("--images", default=os.path.join(HERE, "vu1", "images.txt"))
    ap.add_argument("--quick", action="store_true", help="with --iso: decompress + scan the ELF-tabled level streams")
    ap.add_argument("--full", action="store_true", help="with --iso: every level stream + the raw region (minutes)")
    a = ap.parse_args(argv)
    r = Report()
    if a.iso:
        data, lba = EI.iso_read_file(a.iso, "SCUS_973.16")
        r.info("ELF SCUS_973.16 from %s (lba %d, %d bytes, sha256 %s)" % (os.path.basename(a.iso), lba, len(data), hashlib.sha256(data).hexdigest()[:16]))
    elif a.elf:
        with open(a.elf, "rb") as f:
            data = f.read()
        r.info("ELF %s (%d bytes, sha256 %s)" % (a.elf, len(data), hashlib.sha256(data).hexdigest()[:16]))
    else:
        ap.error("--iso or --elf")

    # ---- VU programs ------------------------------------------------------------------------------------------
    progs = EI.programs_from_elf(data)
    images = read_images_txt(a.images)
    vu1 = [(pid, ch, img) for pid, ch, img in progs if pid != VU0_PROGRAM_ID]
    vu0 = [(pid, ch, img) for pid, ch, img in progs if pid == VU0_PROGRAM_ID]
    r.info("VU programs in .vutext: %d (%d VU1, %d VU0)" % (len(progs), len(vu1), len(vu0)))
    for pid, ch, img in vu1:
        h = EI.vu_image_hash(img)
        row = images.get(h)
        if row is None:
            r.fail("VU1 program %s (%d bytes, content %016x) has no row in images.txt: no native body" % (pid, len(img), h))
        elif row["size"] != len(img) or row["id"] != pid:
            r.fail("VU1 program %s: images.txt row %016x disagrees (size %x vs %x, id %s)" % (pid, h, row["size"], len(img), row["id"]))
        else:
            r.ok("VU1 program %-9s %5x bytes content %016x listed" % (pid, len(img), h))
    for h, row in images.items():
        if not any(EI.vu_image_hash(img) == h for _, _, img in vu1):
            r.fail("images.txt row %016x (%s) is not a program of this ELF (stale recipe)" % (h, row["id"]))
    if len(vu0) != 1:
        r.fail("expected exactly one VU0 program (%s), found %d" % (VU0_PROGRAM_ID, len(vu0)))
    else:
        pid, ch, img = vu0[0]
        if ch[0][0] != VU0_IMAGE_VADDR or len(img) != VU0_IMAGE_SIZE:
            r.fail("VU0 program at %x (%x bytes); native/vu0 reads %x (%x bytes)" % (ch[0][0], len(img), VU0_IMAGE_VADDR, VU0_IMAGE_SIZE))
        else:
            r.ok("VU0 program %s at %x, %x bytes = native/vu0's image" % (pid, ch[0][0], len(img)))

    # ---- VU0 entries ------------------------------------------------------------------------------------------
    tbase, words = text_words(data)
    imm, msr, cmsar0 = vcallms_sites(words)
    extra = sorted(set(imm) - VU0_ENTRIES)
    if extra:
        r.fail("VCALLMS entries without a native VU0 program: %s" % ", ".join("0x%x" % e for e in extra))
    else:
        r.ok("VCALLMS entries %s (sites %s) all native" % (",".join("0x%x" % e for e in sorted(imm)), ",".join(str(imm[e]) for e in sorted(imm))))
    if msr:
        r.fail("%d VCALLMSR site(s): dynamic VU0 entries" % msr)
    else:
        r.ok("no VCALLMSR; CTC2 to CMSAR0: %d" % cmsar0)
    if cmsar0:
        r.fail("CTC2 writes CMSAR0 (%d): a computed VU0 entry" % cmsar0)

    # ---- VU1 entries vs the generated blocks ------------------------------------------------------------------
    ents = mscal_entries(tbase, words)
    r.info("MSCAL entries the EE code forms (lui 0 + addiu -> helpers %s): %s" % (
        ",".join("0x%x" % h for h in sorted(MSCAL_HELPERS)), " ".join("0x%x" % e for e in sorted(ents))))
    if a.generated:
        for pid, ch, img in vu1:
            h = EI.vu_image_hash(img)
            tu = os.path.join(a.generated, "vu1_%016x.cpp" % h)
            if not os.path.isfile(tu):
                r.fail("no generated TU for VU1 program %s (%016x): %s" % (pid, h, tu))
                continue
            blocks = tu_blocks(tu)
            missing = []
            for e in ents:
                if e >= len(img) or (e & 7):
                    continue
                if e in blocks:
                    continue
                lo, up = struct.unpack_from("<II", img, e)
                if lo == 0 and up == 0:
                    continue  # a NOP pair: the loop's sled, never emitted
                missing.append(e)
            if missing:
                r.fail("program %s: MSCAL entries without a generated block: %s" % (pid, " ".join("0x%x" % e for e in missing)))
            else:
                r.ok("program %-9s TU has %4d blocks; every static MSCAL entry inside its %5x bytes is a block" % (pid, len(blocks), len(img)))
    else:
        r.info("(no --generated: the generated TUs are not checked)")

    # ---- IOP modules ------------------------------------------------------------------------------------------
    sec = EI.elf_sections(data)
    irx = sec.get(".irx")
    names = irx_names(data[irx["offset"]:irx["offset"] + irx["size"]]) if irx else []
    unknown = set(names) - IRX_ELF
    if unknown:
        r.fail("IRX in the ELF's .irx not in the enumerated set: %s" % ", ".join(sorted(unknown)))
    elif set(names) != IRX_ELF:
        r.fail("IRX in the ELF's .irx: %s (expected %s)" % (sorted(names), sorted(IRX_ELF)))
    else:
        r.ok("ELF .irx carries exactly the 9 enumerated IRX: %s" % ", ".join(names))
    if a.iso:
        rom, rom_lba = EI.iso_read_file(a.iso, "IOPRP300.IMG")
        rnames = irx_names(rom)
        unknown = set(rnames) - IRX_ROM
        if unknown:
            r.fail("IRX in IOPRP300.IMG not in the enumerated set: %s" % ", ".join(sorted(unknown)))
        else:
            r.ok("IOPRP300.IMG (lba %d, %d bytes, sha256 %s) carries %d IRX, all enumerated" % (rom_lba, len(rom), hashlib.sha256(rom).hexdigest()[:16], len(rnames)))

    # ---- the disc: level streams ------------------------------------------------------------------------------
    if a.iso:
        import mmap
        f = open(a.iso, "rb")
        m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        nsect = len(m) // SECTOR
        table = level_table(data)
        lz_rows = [t for t in table if m[t["lba"] * SECTOR] == 0xFF and m[t["lba"] * SECTOR + 3:t["lba"] * SECTOR + 7] == b"FK$X"]
        pss_rows = [t for t in table if m[t["lba"] * SECTOR:t["lba"] * SECTOR + 4] == b"\x00\x00\x01\xba"]
        r.info("ELF level table: %d records (%d PSS movies, %d LZ level streams, %d other)" % (len(table), len(pss_rows), len(lz_rows), len(table) - len(pss_rows) - len(lz_rows)))
        heads = []
        for lba in range(RAW_START, nsect):
            if m[lba * SECTOR] == 0xFF and m[lba * SECTOR + 3:lba * SECTOR + 7] == b"FK$X":
                heads.append(lba)
        r.info("LZ level-stream heads in the raw region: %d (first 0x%x, last 0x%x)" % (len(heads), heads[0] if heads else 0, heads[-1] if heads else 0))
        irx_in_raw = 0
        if a.quick or a.full:
            todo = sorted(set((t["lba"], t["size"]) for t in lz_rows)) if a.quick else [(h, None) for h in heads]
            for k, (lba, size) in enumerate(todo):
                if size is None:
                    nxt = heads[k + 1] if k + 1 < len(heads) else nsect
                    src = m[lba * SECTOR:nxt * SECTOR]
                else:
                    src = m[lba * SECTOR:lba * SECTOR + size]
                out = decompress(src, 256 << 20)
                if a.full:
                    try:
                        import numpy  # noqa: F401
                        # an optional numpy-based scanner (sigscan.py) next to this script or on $SLY2_SIGSCAN_DIR
                        sys.path.insert(0, os.environ.get("SLY2_SIGSCAN_DIR", os.path.dirname(os.path.abspath(__file__))))
                        import sigscan
                        runs = [x for x in sigscan.vu_runs(out) if sigscan.vu_run_plausible(out, x[0], x[1])[0]]
                        mpg = sigscan.mpg_candidates(out)
                    except ImportError:
                        runs, mpg = microcode_scan(out)
                else:
                    runs, mpg = microcode_scan(out)
                irx_in_raw += len(irx_names(out))
                if runs or mpg:
                    r.fail("level stream at lba 0x%x (%d bytes decompressed): %d VU code run(s), %d MPG upload(s) -- microcode outside .vutext" % (lba, len(out), len(runs), len(mpg)))
                else:
                    r.ok("level stream at lba 0x%x: %9d bytes decompressed, %4d FK$X records, 0 VU code runs, 0 MPG uploads" % (lba, len(out), out.count(b"FK$X")))
            if irx_in_raw:
                r.fail("%d IRX inside the level streams (modules outside the ELF/IOPRP)" % irx_in_raw)
        else:
            r.info("(no --quick/--full: the level streams are enumerated, not decompressed)")
        m.close()
        f.close()

    print("Sly2_NativeClosure -- the disc-derived input sets vs the native paths")
    print("\n".join(r.lines))
    if r.fails:
        print("CLOSURE FAILED: %d gap(s)" % len(r.fails))
        return 1
    print("CLOSED: every enumerated input has a native path")
    return 0


if __name__ == "__main__":
    sys.exit(main())
