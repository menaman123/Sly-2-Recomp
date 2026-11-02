#!/usr/bin/env python3
"""iop_guest_arity.py -- derive, from a recompiled IOP module's generated TU, how many argument registers
(a0-a3) each guest function READS, for iop_trace_compare.py --guest-arity.

    iop_guest_arity.py <build>/generated/games/<title>/iop/<id>/iop_recompiled.cpp [more TUs...] > arity.txt

A MIPS caller loads only the registers it passes; a0-a3 the callee never reads carry the caller's leftovers,
and a native body leaves different leftovers than the recompiled one -- the call trace's E rows then differ
in registers nobody reads. This derives the read set per function from the generated body (the ground truth
of this disc's module): a register is an ARGUMENT when the body reads it before writing it (textual order of
the emitted instructions, which is the execution order within a basic block; delay slots are emitted in
place), or passes it unwritten to a callee that reads it (fixpoint over the direct JAL sites; an import's
read set is the stock library's arity, unknown imports read all four). Over-approximate on branches (a
register written on one path and read on another counts as read): the output is conservative -- it never
hides a real argument. Output: `<addr-hex> <n>` where n = 1 + the highest argument register read (0 = none).

    --exports TRACE             learn which guest function each hosted module's export is (an `I lib:idx` row
                                immediately followed by the `E mod addr` row it entered: snd989:93 -> 33000 ...)
                                so an import of another hosted module takes that function's arity instead of
                                the unknown-import default of four.
    --exports-map FILE          the same map given statically: `lib:idx=addr-hex` lines (read off the hosted
                                modules' export tables -- the 0x41c00000 library headers in the IOP RAM image).
    --variadic ADDR[,ADDR...]   printf-like guest functions (a format + arguments the body only spills and
                                prints: 989snd's fn_35140 report): their arity is pinned to 1 and they stop the
                                fixpoint -- otherwise every caller that passes fewer arguments than four (the
                                deadlock reports in the lock) is pulled to 4 and the compare judges leftovers
                                that only ever became text. stdio:4 (printf) is treated the same way.
"""
import re
import sys

IMPORT_ARITY = {
    'thbase:4': 1, 'thbase:5': 1, 'thbase:6': 2, 'thbase:8': 0, 'thbase:9': 0, 'thbase:10': 1, 'thbase:14': 2,
    'thbase:16': 1, 'thbase:20': 0, 'thbase:22': 2, 'thbase:24': 0, 'thbase:25': 1, 'thbase:26': 1, 'thbase:33': 1,
    'thbase:39': 2,
    'thsemap:4': 1, 'thsemap:5': 1, 'thsemap:6': 1, 'thsemap:7': 1, 'thsemap:8': 1, 'thsemap:9': 1,
    'thevent:4': 1, 'thevent:5': 1, 'thevent:6': 2, 'thevent:7': 2, 'thevent:8': 2, 'thevent:9': 2, 'thevent:10': 4,
    'thevent:11': 4, 'thevent:12': 2,
    'thmsgbx:4': 1, 'thmsgbx:5': 1, 'thmsgbx:6': 2, 'thmsgbx:7': 2, 'thmsgbx:8': 2, 'thmsgbx:9': 2, 'thmsgbx:11': 2,
    'intrman:8': 0, 'intrman:9': 0, 'intrman:17': 1, 'intrman:18': 1,
    'timrman:4': 3, 'timrman:6': 1, 'timrman:20': 4, 'timrman:22': 4, 'timrman:23': 1, 'timrman:24': 1,
    'sifman:5': 0, 'sifman:7': 2, 'sifman:8': 1, 'sifman:29': 0, 'sifman:32': 4,
    'sifcmd:12': 3, 'sifcmd:13': 3, 'sifcmd:14': 1, 'sifcmd:15': 2, 'sifcmd:16': 4, 'sifcmd:17': 4, 'sifcmd:19': 2,
    'sifcmd:22': 1, 'sifcmd:23': 4, 'sifcmd:24': 2, 'sifcmd:25': 1,
    'sysmem:4': 3, 'sysmem:5': 1, 'sysmem:6': 0, 'sysmem:7': 0, 'sysmem:8': 0, 'sysmem:9': 1, 'sysmem:10': 1,
    'loadcore:5': 0, 'loadcore:6': 1, 'loadcore:7': 1, 'loadcore:12': 0,
    'cdvdman:4': 1, 'cdvdman:6': 4, 'cdvdman:8': 0, 'cdvdman:10': 2, 'cdvdman:11': 1, 'cdvdman:12': 0, 'cdvdman:13': 1,
    'cdvdman:14': 2, 'cdvdman:15': 0, 'cdvdman:37': 1, 'cdvdman:39': 0, 'cdvdman:75': 1,
    'modload:17': 2, 'modload:18': 3,
    # sysclib 1.04 (engine/iop/hle/sysclib/sysclib_module.cpp kOrdinals), usbd 1.01, ioman 1.04, loadcore:12
    'sysclib:7': 1, 'sysclib:12': 3, 'sysclib:13': 3, 'sysclib:14': 3, 'sysclib:16': 3, 'sysclib:17': 2, 'sysclib:20': 2,
    'sysclib:22': 2, 'sysclib:23': 2, 'sysclib:27': 1, 'sysclib:29': 3, 'sysclib:34': 2, 'sysclib:36': 3, 'sysclib:40': 3,
    'usbd:4': 1, 'usbd:5': 1, 'usbd:6': 3, 'usbd:7': 2, 'usbd:8': 1, 'usbd:9': 2, 'usbd:10': 1, 'usbd:11': 4, 'usbd:12': 2,
    'usbd:13': 2, 'usbd:18': 4,
    'ioman:4': 3, 'ioman:5': 1, 'ioman:6': 3, 'ioman:7': 3, 'ioman:8': 3, 'ioman:9': 3, 'ioman:10': 1, 'ioman:11': 2,
    'ioman:12': 1, 'ioman:13': 1, 'ioman:14': 1, 'ioman:15': 2, 'ioman:16': 2, 'ioman:17': 3, 'ioman:18': 4, 'ioman:20': 1,
    'ioman:21': 1, 'ioman:25': 2, 'ioman:26': 1, 'ioman:27': 2, 'ioman:28': 4, 'ioman:29': 1, 'ioman:30': 4, 'ioman:31': 4,
    'ioman:32': 2, 'ioman:33': 3, 'ioman:34': 4,
    'loadcore:12': 1,
    # stdio:4 printf: the format only (its register arguments are text, never state; the module's own variadic
    # reporters are --variadic): the stock table's arity 4 would judge leftovers that only ever became text.
    'stdio:4': 1,
    'libsd:4': 1, 'libsd:5': 2, 'libsd:6': 1, 'libsd:7': 2, 'libsd:8': 1, 'libsd:9': 2, 'libsd:10': 1, 'libsd:11': 2,
    'libsd:13': 1, 'libsd:15': 3, 'libsd:17': 4, 'libsd:18': 4, 'libsd:19': 2, 'libsd:20': 2, 'libsd:23': 2,
    'libsd:25': 2, 'libsd:26': 2,
}

FUNC_RE = re.compile(r'^// Function at 0x([0-9a-f]+) - 0x([0-9a-f]+)')
WRITE_RE = re.compile(r'^\s*iop_ctx\.gpr\[(\d+)\]\s*=\s*(.*);')
READ_RE = re.compile(r'iop_ctx\.gpr\[(\d+)\]')
CALL_RE = re.compile(r'^\s*iop_func_([0-9a-f]+)\(iop_ctx\);')
IMPORT_RE = re.compile(r'import_call\("([a-z0-9_]+)",\s*(\d+),')
ARGS = (4, 5, 6, 7)


def parse_functions(path):
    funcs = {}
    cur = None
    with open(path) as f:
        for line in f:
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1), 16)
                funcs[cur] = []
                continue
            if cur is None:
                continue
            if line.startswith('}'):
                cur = None
                continue
            funcs[cur].append(line)
    return funcs


def analyse(funcs):
    # events per function: ('r', reg) / ('w', reg) / ('c', callee_addr) / ('i', 'lib:idx'), in order
    events = {}
    for addr, lines in funcs.items():
        ev = []
        for line in lines:
            s = line.strip()
            if s.startswith('//') or s.startswith('label_'):
                continue
            m = CALL_RE.match(line)
            if m:
                ev.append(('c', int(m.group(1), 16)))
                continue
            m = IMPORT_RE.search(line)
            if m:
                ev.append(('i', '%s:%s' % (m.group(1), m.group(2))))
                continue
            m = WRITE_RE.match(line)
            if m:
                dst = int(m.group(1))
                for r in READ_RE.findall(m.group(2)):
                    ev.append(('r', int(r)))
                ev.append(('w', dst))
                continue
            for r in READ_RE.findall(line):
                ev.append(('r', int(r)))
        events[addr] = ev
    reads = {addr: set() for addr in funcs}
    changed = True
    while changed:
        changed = False
        for addr, ev in events.items():
            written = set()
            used = set()
            for kind, v in ev:
                if kind == 'r':
                    if v in ARGS and v not in written:
                        used.add(v)
                elif kind == 'w':
                    written.add(v)
                elif kind == 'c':
                    callee_reads = {ARGS[0]} if v in VARIADIC else reads.get(v, set(ARGS))
                    for r in callee_reads:
                        if r not in written:
                            used.add(r)
                elif kind == 'i':
                    if v in EXPORTS:
                        target = EXPORTS[v]
                        callee_reads = {ARGS[0]} if target in VARIADIC else reads.get(target, set(ARGS))
                        for r in callee_reads:
                            if r not in written:
                                used.add(r)
                        continue
                    n = IMPORT_ARITY.get(v, 4)
                    for r in ARGS[:n]:
                        if r not in written:
                            used.add(r)
            if used != reads[addr]:
                reads[addr] = used
                changed = True
    return reads


VARIADIC = set()
EXPORTS = {}   # 'lib:idx' -> guest address (from --exports)


def learn_exports(path):
    prev = None
    with open(path, 'r', errors='replace') as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if prev is not None and parts[0] == 'E' and len(parts) > 3:
                EXPORTS.setdefault(prev, int(parts[3], 16))
            prev = parts[2] if parts[0] == 'I' and len(parts) > 2 else None


def main():
    args = sys.argv[1:]
    paths = []
    while args:
        a = args.pop(0)
        if a == '--exports':
            learn_exports(args.pop(0))
        elif a == '--exports-map':
            with open(args.pop(0)) as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#') or '=' not in line:
                        continue
                    k, _, v = line.partition('=')
                    v = v.strip().lower()
                    EXPORTS[k.strip()] = int(v[2:] if v.startswith('0x') else v, 16)
        elif a == '--variadic':
            for tok in args.pop(0).split(','):
                tok = tok.strip().lower()
                if tok.startswith('0x'):
                    tok = tok[2:]
                if tok:
                    VARIADIC.add(int(tok, 16))
        else:
            paths.append(a)
    if not paths:
        print(__doc__, file=sys.stderr)
        return 2
    funcs = {}
    for path in paths:
        funcs.update(parse_functions(path))
    reads = analyse(funcs)
    print('# guest arity (1 + highest a-register read before written; conservative) from', ' '.join(paths),
          ('variadic=' + ','.join('%x' % a for a in sorted(VARIADIC))) if VARIADIC else '',
          ('exports=%d' % len(EXPORTS)) if EXPORTS else '')
    for addr in sorted(reads):
        n = 1 if addr in VARIADIC else max((r - 3 for r in reads[addr]), default=0)
        print('%x %d' % (addr, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
