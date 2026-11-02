# `native/COVERAGE.md` -- closure over the disc

**The question.** For every PS2 unit the native module serves, is the set of inputs *the disc can produce* fully
covered -- proven from the ISO (the complete, finite input), not from what captured runs happened to hit?

**The method.** Enumerate statically, from the disc image, every input each unit can receive; compare with what the
native module serves; name the gaps. `closure_check.py` re-derives the enumeration at test time (ctest
`Sly2_NativeClosure`) and fails when an enumerated input has no native path. Only derived facts (hashes, addresses,
counts) are written here or anywhere in the repository; game bytes are read from the disc and stay in the build
directory (`native/vu1/extract_images.py`).

## The disc (SCUS_973.16, NTSC-U; 1 408 272 sectors)

| Region | Content | How it was established |
|---|---|---|
| ISO9660 directory | 4 files: `SYSTEM.CNF` (lba 270), `SCUS_973.16` (271, 2 597 048 B, sha256 `a511a94b...fec6a73`), `IOPRP300.IMG` (1540, 275 345 B, sha256 `02d31446...c480c82`), `Z6TAIL.DUP` (1 391 880, 32 MiB) | `tools/extract_elf.py --list` (recomp-agent) |
| raw region 0xca0 .. 0x153c88 | the game's LBA-addressed data (sectors 1675..0xc9f are zero) | first non-zero sector scan |
| ELF `.rodata` 0x2a3268..0x2a39e8 | the game's level/stream table: 30 obfuscated 0x40-byte records (`lba = (w0^w13) + 0xca0`, `size = w1^w15`, `id = w2^w14`; the loader 0x20e890 copies a record behind a flags word and xors at +4/+0x38, +8/+0x40): 24 distinct FMVs (PSS program streams, sizes `...004`), 2 compressed level streams (lba 0x51e80 id 0x5d, 9 969 830 B = the trace's 0x1305-sector read; lba 0x557c0 id 0x39, 1 949 759 B), 1 raw 13-sector record (lba 0x11d8aa id 0x7cf) | static disassembly of 0x20e890 / 0x161a58 / 0x1ac260 / 0x2124f0 |
| level streams | 43 compressed streams (sector heads `ff ?? ?? 'FK$X'`, 0x20-sector aligned) at 0x51e80 .. 0xd3d40 inside the raw region, 2.30 GB decompressed, 7 774 `FK$X` object records; each followed by further compressed data and SPU ADPCM audio | sector-head scan; the game's decompressor (SCUS_973.16 0x1179f0: flag byte LSB-first, 1 = literal, 0 = 16-bit ref `count = (v>>13)+3`, absolute 8 KB window offset `v & 0x1fff`) re-expressed in `closure_check.py` |
| FMVs | 24 table entries + 10 further PSS regions at the starts of the untabled gaps (0xbe8b, 0x10cd5, 0x22f22, 0x2b284, 0x33208, 0x3d30b, 0x45285, 0xe25ab, 0x11d8b7, 0x141d2d) | `00 00 01 ba` pack headers |
| audio | SPU ADPCM blocks between the level streams (e.g. 0x54000.., 0x56000..) | ADPCM block-header statistics |

**Key finding.** The decompressed level data of all 43 streams and the raw bytes of the whole raw region carry **no
VU microcode** (0 runs of valid VU bundles with the code shape, 0 VIF `MPG` uploads with a valid payload, 0 bytes of
the ELF's 21 microprogram chunks), **no IRX module** (0 MIPS ELF headers, 0 `iopmod`), **no GIF packets with the
game's descriptor words** (0 of the 11 lane templates; the 295 "GIFtag-shaped" quadwords found are float data:
regs words `0x3e..`/`0x42..`/`0xbe..`), and **no VIF code streams of the chain grammar** (the 234 "MPG + payload"
walks are 0x4A words followed by zero data). The level files hold geometry, textures, scripts and audio. **Every
input of the VU0 / VU1 / GIF / VIF / DMAC units is therefore produced by the EE code and the ELF's `.vutext`
section: the enumeration is a static enumeration over the ELF.** (the full disc scan:
1 462 s over the 43 streams + 86 x 32 MiB raw windows; `closure_check.py --full` repeats it.)

## Per-unit closure table

| Unit | Enumerated set (from the disc) | Served natively (`native/`) | Gap | Verdict |
|---|---|---|---|---|
| **VU1 microprograms** | the ELF's `.vutext` (0x259df0, 0x8540 B) = 8 VU overlay programs (`.DVP.ovlytab`, 21 MPG chunks each prefixed by its VIF `MPG` code): **7 VU1 programs** -- `376110651` 0x3c60 B (content hash `0bcaf2408301ebce`, the main renderer), `188054619` 0x1e60 (`bbb90259e965db36`), `376109499` 0xec0 (`c9d3cd4589a7ea02`), `188054331` 0x920 (`81b9f647569fbfc2`), `188055835` 0x8c0 (`8d086b61837f9626`), `752221467` 0x5d0 (`6fe004542a6c2f6e`), `376110587` 0x10 (`5a41f38bf6affc01`, the 2-bundle E-bit stub); 0 programs in the level data | `native/vu1`: 7 generated TUs keyed by **content hash** (`images.txt`); the corpus's 10 whole-memory images map onto exactly these 7 (`188055835` with three residues, `188054331` with two); run-time identity = hash of the first `size` bytes, aliased at the first kick (`Vu1AotTier::SetUnknownImageResolver` / `SetNativeExportsAlias`) -- a program loaded over any residue runs the same code, 0 fallback kicks | none at the program level. Per block: the generated closure = corpus entries + the EE code's 16 static MSCAL entries (below) + fall-through/static targets; a JR/JALR target no case reached runs on the specialised table inside the native path (24 of 1.55 G ops on the Paris lane before this change) | **CLOSED** (checked: `closure_check.py` VU1 rows) |
| **VU1 kick entries** | the EE code forms MSCAL codes by passing VU byte addresses (DVP `lui 0 + addiu` relocations) to its packet helpers 0x226ce0 / 0x226d10 / 0x226d28: **16 static entries** 0x0 0xc0 0x100 0x390 0x738 0x870 0x1910 0x1aa0 0x2eb0 0x2ee8 0x2ef8 0x2f80 0x2fc8 0x3358 0x3628 0x3ab0; all 27 VU address constants (incl. data addresses): 0 30 60 90 c0 100 140 390 650 738 7f0 870 1000 1910 1a68 1aa0 2e20 2e68 2eb0 2ee8 2ef8 2f80 2fc8 3358 3628 37a8 3ab0 | every constant inside a program's extent is a closure entry of that program's TU (`vu1_native_gen --programs`), so 0x390 -- kicked on neither lane -- has its blocks | the corpus entries 0x1c48 (`188054619`) and 0x10 (`752221467`) are **computed** (not literals: MSCNT / a derived address): the generator takes them from the corpus; a computed entry the corpus lacks would run on the specialised table (native path, not generated code) | **CLOSED for the static set**; computed entries bounded by the corpus |
| **VU0** | one program `11753019` at vaddr 0x259e00, 0x2a0 B (= `.vutext` + 0x10); the VCALLMS immediates of `.text`: {0x10 x1, 0x60 x4, 0xf0 x1, 0x120 x2, 0x150 x1, 0x1c0 x3} (= the 12 `g_vu0.Execute` sites of the shards); **0 VCALLMSR, 0 CTC2 to CMSAR0** (the only CTC2s write FBRST); no other VU0 `MPG` on the disc (the `.vutext` header `1000002b .. 4a540000` is the one VIF0 upload) | `native/vu0`: 7 entries {0, 0x10, 0x60, 0xf0, 0x120, 0x150, 0x1c0} generated from that image (entry 0 = the SDK stub) | none | **CLOSED** (checked) |
| **GIF packet shapes** | producers: (a) the 7 VU1 programs' XGKICKs with tags the EE unpacks into VU1 data (the `V4-32 [1/1] @0` headers), (b) EE PATH3/PATH2 A+D + IMAGE heads; EE constant tag heads in `.text`: A+D `nreg=1` (15 sites), `nreg=1 pre prim=1`, IMAGE `nreg=3`/`nreg=5` (flg 2), PACKED `nreg=2..9`, `nreg=6 pre prim=1`, `nreg=8 pre`; **no FLG=3 tag head anywhere** (the 19 "flg=3" constants are UNPACK codes, the 3 data qwords are code bytes); the level data holds none. Observed on the two lanes: 107 distinct templates (62 boot / 98 Paris) | `native/gif`: PACKED all descriptors incl. register-as-data, A+D incl. SIGNAL/FINISH/LABEL, REGLIST, IMAGE whole + continuations, split payloads | declined: FLG 3 (not producible: no constant), NLOOP-0-without-EOP forms (NLOOP is a run-time field: not statically decidable; 0 occurrences on both lanes), a packet with a trailing partial quadword | **CLOSED on the producible set**; NLOOP-0 bounded by observation only |
| **VIF / DMAC** | the chain grammar is produced by EE code (0 VIF code streams in the level data; the VIF codes are formed at run time -- MSCAL/STCYCL/UNPACK are not literals, only the VU addresses are); formats observed over both lanes: V4-32 V4-16 V4-8 V3-32 V3-16 V3-8 V2-32 V2-16 V4-5 S-8 S-16 (+ masked), modes 0-3, STROW/STCOL, CL/WL 1-4; tags CNT REFS CALL RET END (+1 TTE chain); DIRECT (PATH2 uploads), MPG (the 7 programs: 36 249 uploads / 68 MB per 600 Paris frames) | `native/vif`: typed loops for **every defined UNPACK format** (format-complete by construction, not by observation); DIRECT, MPG, split payloads, TTE native; `spans=.../0 declined=0/0` on both lanes | none known: an UNPACK format outside the PS2's defined set cannot exist; a VIF code outside {NOP STCYCL OFFSET BASE ITOP STMOD MSKPATH3 MARK FLUSHE FLUSH FLUSHA MSCAL MSCALF MSCNT STMASK STROW STCOL MPG DIRECT DIRECTHL UNPACK} is an undefined code the engine's executor already owns | **CLOSED** (by format completeness; the EE's run-time code set is bounded by the hardware's) |
| **IOP modules** | `IOPRP300.IMG` ROMDIR: 16 kernel IRX (SYSMEM LOADCORE SIFCMD SIFMAN THREADMAN IOMAN MODLOAD FILEIO CDVDMAN CDVDFSV LOADFILE TIMEMANI ROMDRV EESYNC SYSCLIB STDIO); the ELF's `.irx` section (0x472200, 0x73c40 B): 9 game IRX -- sio2man (sha256 `edf2a493…`), padman (`2bb5d36c…`), mcman (`0827c332…`), mcserv (`b62ee045…`), Sound_Device_Library = libsd (`1218041a…`), USB_driver (`8d47afc1…`), lgAud (`27ab3c42…`), 989snd_Library (`9b58b673…`), spiop (`f597c47d…`); **0 IRX in the raw region / level streams** | engine: the kernel + cdvdman/libsd hosted; title `iop/`: lgaud recompiled; `native/iop`: the 989snd RPC server (`native/snd/README.md`) | 989snd + spiop stream/SFX/bank/tick paths still forwarded to the hosted module (in progress) | **ENUMERATION CLOSED** (the module set is exactly these 25; checked); hosting status = the sound driver's |
| **FMV / IPU** | 24 tabled + 10 untabled PSS streams; the IPU commands the retail libmpeg issues on the boot movie: BCLR 302, BDEC 48 425, VDEC 235 051, FDEC 1 057 742, SETIQ 20, SETVQ 1, CSC 264, SETTH 1 -- **no IDEC, no PACK** (the two partially modelled commands) | the engine's `IpuDevice` executes each command in host C++ (13818-2 VLC tables, inverse quantisation, IDCT, CSC) -- a command processor, not an instruction interpreter: nothing is interpreted per frame; `engine/ipu/hle` holds an alternative libavcodec path (not on this lane) | none for the disc's command set | **CLOSED** (the model is host-native; the disc never issues IDEC/PACK) |

## What the check verifies (ctest `Sly2_NativeClosure`, 13 s with the disc)

`closure_check.py --iso <disc> --generated <build>/generated/.../native/vu1 --quick`:
the 8 `.vutext` programs vs `images.txt` (content hash, size, id) and the generated TUs; the VU0 image's vaddr/size and
the VCALLMS set vs the native entries, no VCALLMSR / CMSAR0; every static MSCAL entry inside a program's extent is a
block of its TU (or a NOP pair); the ELF `.irx` and `IOPRP300.IMG` module names vs the enumerated set; the level
table and the 43 stream heads; the ELF-tabled streams decompressed and scanned (0 VU code runs, 0 MPG uploads, 0 IRX).
Negative control: removing an `images.txt` row fails the check. `--full` adds every stream and the raw region
(the recorded run: 0 / 0 / 0 everywhere).

## Gates on this branch (2026-10-31)

- `Vu1NativeCorpus`: 10 811 / 10 811 cases identical through the content-identity alias path, `native_ops == bundles`
  (6 024 357), cap sweep 7 / 7 programs.
- `nat_gate` OFF = the stage baseline; `title.native.vu1=on` = baseline rows (mem `0df0a2278a02` / pad `1b9cd487a3bc`,
  audio `797c19071ebf`, frames `2a66c7ab3d3d`, domain_tick 1013, syscalls 2 153 418, gs_draws 585 846; env_hash
  `0xea5e98963e61c0d7`), census `vu1=on:296433/0:images=7.aliases=6...refused=0`; all five knobs on = baseline
  (env_hash `0x010ead36c213ccf2`).

## Not verified / open

- The GIF NLOOP-0-without-EOP forms and the split IMAGE heads are bounded by observation (0 on two lanes), not by
  the static scan: NLOOP is a run-time field of tags the EE builds.
- The computed VU1 entries (MSCNT / derived addresses: 0x1c48, 0x10 seen in the corpus) are bounded by the corpus;
  a static proof needs the packet builders decompiled (0x226ce0 family).
- The content-identity run was gated on the boot lane only (the Paris lockstep leg of the native VU1 path was not re-run here).
- Never executed on Windows (the tools are pure Python 3 / C++17 with no OS calls; `extract_images.py` and
  `closure_check.py` use only `struct`, `mmap`, `hashlib`).
