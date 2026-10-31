#!/usr/bin/env python3
"""extract_images.py -- pull Sly 2's VU microprogram images out of the DISC into the build directory.

The game's VU code is the ELF's `.vutext` section (SCUS_973.16, lba 271 of the ISO9660 volume): 8 VU overlay
programs described by the `.DVP.ovlytab` / `.DVP.ovlystrtab` sections (one row per MPG chunk: name
`.DVP.overlay..0x<load>.<program id>.<n>.<index>`, EE vaddr of the chunk, VU load address); each chunk is
preceded in `.vutext` by its VIF MPG code (0x4A <num> <addr>), which this tool cross-checks. The level files on the
disc carry no microcode (native/COVERAGE.md, closure_check.py), so this IS the complete set.

Writes, into OUT (a build directory: game bytes never enter the repository):
  <id>.bin                 the program's bytes laid at their VU load addresses (size = the last chunk's end)
  disc_programs.txt        one row per program: <id> <unit> <size hex> <VuImageHash::Fast of the bytes, 16 hex>
                           <sha256[:16]> <chunks vaddr+size@load,...>
Facts (hashes/sizes/addresses) only go into the repository: closure_check.py compares this table with
native/vu1/images.txt. Usage: extract_images.py (--iso ISO | --elf ELF) --out DIR [--quiet]
Pure Python 3, no third-party modules (Windows portable).
"""
import argparse
import hashlib
import os
import struct
import sys

SECTOR = 2048
MASK64 = (1 << 64) - 1
VU0_PROGRAM_ID = "11753019"   # the one program the game runs on VU0 (VCALLMS entries 0x0..0x1c0)


def vu_image_hash(b):
    """engine/vu/vu_image_hash.cpp VuImageHash::Fast, verbatim: 4-lane FNV-1a over LE 64-bit words."""
    prime = 1099511628211
    h0 = 1469598103934665603
    h1 = h0 ^ 0x9E3779B97F4A7C15
    h2 = (h0 + 0x517CC1B727220A95) & MASK64
    h3 = h0 ^ 0xFF51AFD7ED558CCD
    n = len(b)
    i = 0
    u = struct.unpack_from
    while i < (n & ~31):
        h0 = ((h0 ^ u("<Q", b, i)[0]) * prime) & MASK64
        h1 = ((h1 ^ u("<Q", b, i + 8)[0]) * prime) & MASK64
        h2 = ((h2 ^ u("<Q", b, i + 16)[0]) * prime) & MASK64
        h3 = ((h3 ^ u("<Q", b, i + 24)[0]) * prime) & MASK64
        i += 32
    while i + 8 <= n:
        h0 = ((h0 ^ u("<Q", b, i)[0]) * prime) & MASK64
        i += 8
    h = h0
    h = ((h ^ h1) * prime) & MASK64
    h = ((h ^ h2) * prime) & MASK64
    h = ((h ^ h3) * prime) & MASK64
    return h


def iso_read_file(iso_path, name):
    """The bytes of a root-directory file of an ISO9660 volume (no Joliet needed: the disc has 4 files)."""
    with open(iso_path, "rb") as f:
        f.seek(16 * SECTOR)
        pvd = f.read(SECTOR)
        if pvd[:6] != b"\x01CD001":
            raise SystemExit("%s: no ISO9660 primary volume descriptor" % iso_path)
        root = pvd[156:156 + 34]
        root_lba = struct.unpack_from("<I", root, 2)[0]
        root_size = struct.unpack_from("<I", root, 10)[0]
        f.seek(root_lba * SECTOR)
        d = f.read((root_size + SECTOR - 1) // SECTOR * SECTOR)
        off = 0
        while off < len(d):
            length = d[off]
            if length == 0:
                off = (off // SECTOR + 1) * SECTOR
                continue
            lba = struct.unpack_from("<I", d, off + 2)[0]
            size = struct.unpack_from("<I", d, off + 10)[0]
            nlen = d[off + 32]
            fname = d[off + 33:off + 33 + nlen].decode("ascii", "replace").split(";")[0]
            if fname == name:
                f.seek(lba * SECTOR)
                return f.read(size), lba
            off += length
    raise SystemExit("%s: %s not in the root directory" % (iso_path, name))


def elf_sections(data):
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF")
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    raw = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    names_off = raw[shstrndx][4]
    out = {}
    for s in raw:
        start = names_off + s[0]
        name = data[start:data.index(b"\0", start)].decode("ascii", "replace")
        out[name] = {"addr": s[3], "offset": s[4], "size": s[5]}
    return out


def programs_from_elf(data):
    """[(id, [(vaddr, size, load)], bytes)] in .DVP.ovlytab order."""
    sec = elf_sections(data)
    for n in (".vutext", ".DVP.ovlytab", ".DVP.ovlystrtab"):
        if n not in sec:
            raise SystemExit("the ELF has no %s section" % n)
    vt = sec[".vutext"]
    st = sec[".DVP.ovlystrtab"]
    ot = sec[".DVP.ovlytab"]
    names = data[st["offset"]:st["offset"] + st["size"]]
    rows = struct.unpack_from("<%dI" % (ot["size"] // 4), data, ot["offset"])
    groups = {}
    order = []
    for i in range(0, len(rows), 3):
        name_off, vaddr, load = rows[i:i + 3]
        name = names[name_off:names.index(b"\0", name_off)].decode()
        parts = name.split(".")   # ['', 'DVP', 'overlay', '', '0x<load>', '<id>', '<n>', '<index>']
        pid = parts[5]
        if pid not in groups:
            groups[pid] = []
            order.append(pid)
        off = vt["offset"] + (vaddr - vt["addr"])
        mpg = struct.unpack_from("<I", data, off - 4)[0]
        if (mpg >> 24) & 0x7F != 0x4A:
            raise SystemExit("no VIF MPG code before the chunk at %08x (%08x)" % (vaddr, mpg))
        num = (mpg >> 16) & 0xFF or 256
        if (mpg & 0xFFFF) * 8 != load:
            raise SystemExit("MPG load address %x != ovlytab %x at %08x" % ((mpg & 0xFFFF) * 8, load, vaddr))
        groups[pid].append((vaddr, num * 8, load))
    out = []
    for pid in order:
        chunks = groups[pid]
        total = max(l + s for _, s, l in chunks)
        img = bytearray(total)
        for vaddr, size, load in chunks:
            off = vt["offset"] + (vaddr - vt["addr"])
            img[load:load + size] = data[off:off + size]
        out.append((pid, chunks, bytes(img)))
    return out


def vu_address_constants(data):
    """Every VU address the EE code materialises as a `lui rt, 0` + `addiu rd, rt, imm` pair (the DVP linker's
    HI16/LO16 relocations against VU symbols: the MSCAL entries the game's packet builders pass to their
    helpers, VU data addresses, ...). Returns the sorted set of immediates (bytes) -- the static superset of
    the kick entries (a computed entry, e.g. MSCNT, is not in it)."""
    sec = elf_sections(data)
    t = sec[".text"]
    n = t["size"] // 4
    words = struct.unpack_from("<%dI" % n, data, t["offset"])
    out = set()
    for k, w in enumerate(words):
        if (w >> 26) == 0x0F and (w & 0xFFFF) == 0:
            rt = (w >> 16) & 31
            for j in range(1, 5):
                if k + j >= n:
                    break
                w2 = words[k + j]
                if (w2 >> 26) == 0x09 and ((w2 >> 21) & 31) == rt:
                    out.add(w2 & 0xFFFF)
                    break
    return sorted(v for v in out if v < 0x4000 and (v & 7) == 0)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", help="the disc image (SCUS_973.16 is read from its root directory)")
    ap.add_argument("--elf", help="the ELF file itself (alternative to --iso)")
    ap.add_argument("--out", required=True, help="output directory (build tree)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    if a.iso:
        data, lba = iso_read_file(a.iso, "SCUS_973.16")
        src = "%s:SCUS_973.16@lba%d" % (os.path.basename(a.iso), lba)
    elif a.elf:
        with open(a.elf, "rb") as f:
            data = f.read()
        src = os.path.basename(a.elf)
    else:
        ap.error("--iso or --elf")
    os.makedirs(a.out, exist_ok=True)
    progs = programs_from_elf(data)
    rows = ["# disc_programs.txt -- Sly 2's VU microprograms extracted from %s (sha256 %s) by native/vu1/extract_images.py" % (
        src, hashlib.sha256(data).hexdigest()),
            "# id unit size_hex content_hash(VuImageHash::Fast over the bytes at their load addresses) sha256_16 chunks(vaddr+size@load,...)"]
    for pid, chunks, img in progs:
        unit = "vu0" if pid == VU0_PROGRAM_ID else "vu1"
        with open(os.path.join(a.out, "%s.bin" % pid), "wb") as f:
            f.write(img)
        rows.append("%s %s %x %016x %s %s" % (pid, unit, len(img), vu_image_hash(img), hashlib.sha256(img).hexdigest()[:16],
                                           ",".join("%x+%x@%x" % c for c in chunks)))
    ents = vu_address_constants(data)
    rows.append("# vu address constants of the EE code (lui 0 + addiu: the static superset of the MSCAL entries), bytes:")
    rows.append("vuaddrs " + " ".join("%x" % v for v in ents))
    with open(os.path.join(a.out, "disc_programs.txt"), "w") as f:
        f.write("\n".join(rows) + "\n")
    if not a.quiet:
        print("\n".join(rows))
        print("extract_images.py: %d programs -> %s" % (len(progs), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
