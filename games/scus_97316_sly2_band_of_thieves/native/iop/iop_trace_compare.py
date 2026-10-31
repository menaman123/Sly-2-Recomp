#!/usr/bin/env python3
"""iop_trace_compare.py -- the per-function gate of a native IOP port: diff two NATIVEEE_IOP_TRACE call
streams (native/iop/iop_call_trace.h) and name the first call where they diverge.

    iop_trace_compare.py OFF.trace ON.trace [--ignore-regs ADDR:a2,a3 ...] [--context N] [--no-time]
                         [--imports-only-under ADDR] [--summary]

OFF = the lane with the recompiled bodies (NATIVEEE_IOP_NATIVE=none or title.native.iopsys=off); ON = the
same lane with the native bodies installed. The rows are aligned in order; the body column (n/r) is NOT
compared (that is the difference under test). The first row that differs is printed with its N preceding
rows from both files and the enclosing guest call stack (the open E rows) -- the function whose port is
wrong is the innermost native body on that stack, or the one whose exit (X) row differs.

Rows: E seq mod addr n|r a0 a1 a2 a3 sp ra t_us tid depth / X seq mod addr n|r v0 v1 t_us /
      I seq lib:idx a0 a1 a2 a3 t_us depth / J seq lib:idx v0 t_us.
--ignore-regs ADDR:REGS   do not compare the named entry registers (a0..a3,sp,ra) of calls to ADDR (hex)
                          -- for a callee that reads fewer arguments than the recompiled caller left in
                          a2/a3 (register-file leftovers, nat2-snd.md); `*:ra` ignores $ra everywhere
--ignore-imports LIB:IDX  do not compare the named import's registers (e.g. libsd:10 a2/a3 leftovers): the
                          row must still be there, only its register columns are skipped
--guest-arity FILE        `<addr-hex> <n>` lines: compare only a0..a(n-1) at the entry of guest function
                          <addr> (its real argument count, read off the generated body); others: all four
--all-import-regs         compare all four registers of every import. Default: only the registers the
                          import READS (the stock libraries' arities, IMPORT_ARITY below; unknown = 4) --
                          a MIPS caller loads only the registers it passes, so a2/a3 carry leftovers
--no-time                 do not compare the t_us column (the IOP clock) -- a port that changes the
                          dispatch count moves it; the default compares it
--compare-ra              compare $ra at entry (default off: a native caller leaves $ra as it found it;
                          no body's control flow depends on it)
--compare-v1              compare $v1 at exit (default off: only a 64-bit result uses it; a recompiled
                          body leaves a scratch value there)
--summary                 per-function call counts of both files (native / recompiled) and exit
Exit 0 = identical under the options, 1 = divergence found, 2 = usage / read error.
"""
import argparse
import sys


# The stock libraries' import arities (how many of a0-a3 the HLE body reads): stock_library_binder.cpp.
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
    # sysclib 1.04 (engine/iop/hle/sysclib/sysclib_module.cpp kOrdinals), usbd 1.01, ioman 1.04, loadcore:12 (lgaud track)
    'sysclib:7': 1, 'sysclib:12': 3, 'sysclib:13': 3, 'sysclib:14': 3, 'sysclib:16': 3, 'sysclib:17': 2, 'sysclib:20': 2,
    'sysclib:22': 2, 'sysclib:23': 2, 'sysclib:27': 1, 'sysclib:29': 3, 'sysclib:34': 2, 'sysclib:36': 3, 'sysclib:40': 3,
    'usbd:4': 1, 'usbd:5': 1, 'usbd:6': 3, 'usbd:7': 2, 'usbd:8': 1, 'usbd:9': 2, 'usbd:10': 1, 'usbd:11': 4, 'usbd:12': 2,
    'usbd:13': 2, 'usbd:18': 4,
    'ioman:4': 3, 'ioman:5': 1, 'ioman:6': 3, 'ioman:7': 3, 'ioman:8': 3, 'ioman:9': 3, 'ioman:10': 1, 'ioman:11': 2,
    'ioman:12': 1, 'ioman:13': 1, 'ioman:14': 1, 'ioman:15': 2, 'ioman:16': 2, 'ioman:17': 3, 'ioman:18': 4, 'ioman:20': 1,
    'ioman:21': 1, 'ioman:25': 2, 'ioman:26': 1, 'ioman:27': 2, 'ioman:28': 4, 'ioman:29': 1, 'ioman:30': 4, 'ioman:31': 4,
    'ioman:32': 2, 'ioman:33': 3, 'ioman:34': 4,
    'loadcore:12': 1,
    # libsd (export index): Init 1, SetParam 2, GetParam 1, SetSwitch 2, GetSwitch 1, SetAddr 2, GetAddr 1,
    # SetCoreAttr 2, Note2Pitch 1, ProcBatch 3, VoiceTrans 4, BlockTrans 4, VoiceTransStatus 2, BlockTransStatus 2,
    # SetTransCallback 2, SetEffectAttr 2, GetEffectAttr 2
    'libsd:4': 1, 'libsd:5': 2, 'libsd:6': 1, 'libsd:7': 2, 'libsd:8': 1, 'libsd:9': 2, 'libsd:10': 1, 'libsd:11': 2,
    'libsd:13': 1, 'libsd:15': 3, 'libsd:17': 4, 'libsd:18': 4, 'libsd:19': 2, 'libsd:20': 2, 'libsd:23': 2,
    'libsd:25': 2, 'libsd:26': 2,
}


def parse(path):
    rows = []
    with open(path, 'r', errors='replace') as f:
        for line in f:
            if not line or line[0] == '#':
                continue
            parts = line.split()
            if not parts:
                continue
            rows.append(parts)
    return rows


guest_arity = {}


def key(row, opts, ignore_regs, ignore_imports, next_row=None):
    """The comparable tuple of a row (the body column dropped; ignored columns blanked). `next_row`: the row
    after this one -- an import of a HOSTED module's export (snd989:93, lgaud:18 ...) is masked by the guest
    arity of the export it enters (the E row that follows), since the stock table cannot know it."""
    kind = row[0]
    if kind == 'E':
        # E seq mod addr body a0 a1 a2 a3 sp ra t tid depth
        _, _seq, mod, addr, _body, a0, a1, a2, a3, sp, ra, t, tid, depth = row[:14]
        regs = {'a0': a0, 'a1': a1, 'a2': a2, 'a3': a3, 'sp': sp, 'ra': ra}
        for name in ignore_regs.get(addr.lower(), set()) | ignore_regs.get('*', set()):
            regs[name] = '-'
        arity = guest_arity.get(addr.lower())
        if arity is not None:
            for i in range(arity, 4):
                regs['a%d' % i] = '-'
        return ('E', mod, addr, regs['a0'], regs['a1'], regs['a2'], regs['a3'], regs['sp'],
                regs['ra'] if opts.ra else '-', t if opts.time else '-', tid, depth)
    if kind == 'X':
        _, _seq, mod, addr, _body, v0, v1, t = row[:8]
        return ('X', mod, addr, v0, v1 if opts.v1 else '-', t if opts.time else '-')
    if kind == 'I':
        _, _seq, imp, a0, a1, a2, a3, t, depth = row[:9]
        regs = [a0, a1, a2, a3]
        if imp in ignore_imports:
            regs = ['-'] * 4
        elif not opts.all_import_regs:
            arity = IMPORT_ARITY.get(imp)
            if arity is None and next_row is not None and next_row[0] == 'E' and len(next_row) > 3:
                arity = guest_arity.get(next_row[3].lower())
            if arity is None:
                arity = 4
            regs = regs[:arity] + ['-'] * (4 - arity)
        return ('I', imp, regs[0], regs[1], regs[2], regs[3], t if opts.time else '-', depth)
    if kind == 'J':
        _, _seq, imp, v0, t = row[:5]
        if imp in ignore_imports:
            v0 = '-'
        return ('J', imp, v0, t if opts.time else '-')
    return tuple(row)


def stack_at(rows, upto):
    """The open E rows (guest call stack) of the thread running at row `upto` (per-tid stacks: the
    coroutines interleave at their parks; an X row pops the innermost E of its address on the last
    thread that entered it)."""
    stacks = {}
    owner = {}
    cur = None
    for r in rows[:upto + 1]:
        if r[0] == 'E':
            tid = r[12] if len(r) > 12 else '?'
            stacks.setdefault(tid, []).append(r)
            owner[r[3]] = tid
            cur = tid
        elif r[0] == 'X':
            tid = owner.get(r[3])
            st = stacks.get(tid, [])
            for i in range(len(st) - 1, -1, -1):
                if st[i][3] == r[3]:
                    del st[i]
                    break
            cur = tid
    return stacks.get(cur, [])


def summary(rows, label):
    counts = {}
    for r in rows:
        if r[0] != 'E':
            continue
        k = (r[2], r[3])
        c = counts.setdefault(k, [0, 0])
        c[0 if r[4] == 'n' else 1] += 1
    print(f"# {label}: {len(rows)} rows, {sum(1 for r in rows if r[0] == 'E')} guest calls, "
          f"{sum(1 for r in rows if r[0] == 'I')} imports")
    for (mod, addr), (n, rc) in sorted(counts.items(), key=lambda kv: -(kv[1][0] + kv[1][1])):
        print(f"  {mod} {addr}: native={n} recompiled={rc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('off')
    ap.add_argument('on')
    ap.add_argument('--ignore-regs', action='append', default=[], metavar='ADDR:REGS')
    ap.add_argument('--ignore-imports', action='append', default=[], metavar='LIB:IDX')
    ap.add_argument('--context', type=int, default=8)
    ap.add_argument('--no-time', dest='time', action='store_false')
    ap.add_argument('--compare-ra', dest='ra', action='store_true')
    ap.add_argument('--compare-v1', dest='v1', action='store_true')
    ap.add_argument('--all-import-regs', action='store_true')
    ap.add_argument('--guest-arity', metavar='FILE')
    ap.add_argument('--summary', action='store_true')
    opts = ap.parse_args()
    ignore_regs = {}
    for spec in opts.ignore_regs:
        addr, _, regs = spec.partition(':')
        a = addr.lower()
        if a.startswith('0x'):
            a = a[2:]
        ignore_regs.setdefault(a, set()).update(x.strip() for x in regs.split(',') if x.strip())
    ignore_imports = set(opts.ignore_imports)
    if opts.guest_arity:
        with open(opts.guest_arity) as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2 and not parts[0].startswith('#'):
                    a = parts[0].lower()
                    guest_arity[a[2:] if a.startswith('0x') else a] = int(parts[1])
    try:
        a = parse(opts.off)
        b = parse(opts.on)
    except OSError as e:
        print(f"iop_trace_compare: {e}", file=sys.stderr)
        return 2
    if opts.summary:
        summary(a, opts.off)
        summary(b, opts.on)
    n = min(len(a), len(b))
    for i in range(n):
        ka = key(a[i], opts, ignore_regs, ignore_imports, a[i + 1] if i + 1 < len(a) else None)
        kb = key(b[i], opts, ignore_regs, ignore_imports, b[i + 1] if i + 1 < len(b) else None)
        if ka == kb:
            continue
        print(f"DIVERGE at row {i} (seq OFF={a[i][1]} ON={b[i][1]})")
        lo = max(0, i - opts.context)
        print(f"--- {opts.off} rows {lo}..{i}")
        for r in a[lo:i + 1]:
            print('   ' + ' '.join(r))
        print(f"--- {opts.on} rows {lo}..{i}")
        for r in b[lo:i + 1]:
            print('   ' + ' '.join(r))
        st = stack_at(b, i)
        print("--- open guest calls (ON) at the divergence, innermost last:")
        for r in st:
            print(f"   {r[2]} {r[3]} ({'native' if r[4] == 'n' else 'recompiled'}) a0={r[5]} a1={r[6]} a2={r[7]} a3={r[8]}")
        if b[i][0] == 'X':
            print(f"=> the exit of {b[i][2]} {b[i][3]} differs: its {'native' if b[i][4] == 'n' else 'recompiled'} body is the suspect")
        elif st:
            native = [r for r in st if r[4] == 'n']
            if native:
                print(f"=> innermost native body on the stack: {native[-1][2]} {native[-1][3]}")
            else:
                print("=> no native body on the stack: a state difference left by an earlier native call")
        return 1
    if len(a) != len(b):
        print(f"DIVERGE at row {n}: {opts.off} has {len(a)} rows, {opts.on} has {len(b)}")
        shorter, longer, name = (a, b, opts.on) if len(a) < len(b) else (b, a, opts.off)
        print(f"--- {name} continues with:")
        for r in longer[n:n + opts.context]:
            print('   ' + ' '.join(r))
        return 1
    print(f"IDENTICAL: {n} rows ({sum(1 for r in a if r[0] == 'E')} guest calls, {sum(1 for r in a if r[0] == 'I')} imports)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
