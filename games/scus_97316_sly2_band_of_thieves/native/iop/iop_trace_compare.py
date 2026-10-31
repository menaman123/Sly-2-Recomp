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


def key(row, opts, ignore_regs, ignore_imports):
    """The comparable tuple of a row (the body column dropped; ignored columns blanked)."""
    kind = row[0]
    if kind == 'E':
        # E seq mod addr body a0 a1 a2 a3 sp ra t tid depth
        _, _seq, mod, addr, _body, a0, a1, a2, a3, sp, ra, t, tid, depth = row[:14]
        regs = {'a0': a0, 'a1': a1, 'a2': a2, 'a3': a3, 'sp': sp, 'ra': ra}
        for name in ignore_regs.get(addr.lower(), set()) | ignore_regs.get('*', set()):
            regs[name] = '-'
        return ('E', mod, addr, regs['a0'], regs['a1'], regs['a2'], regs['a3'], regs['sp'],
                regs['ra'] if opts.ra else '-', t if opts.time else '-', tid, depth)
    if kind == 'X':
        _, _seq, mod, addr, _body, v0, v1, t = row[:8]
        return ('X', mod, addr, v0, v1 if opts.v1 else '-', t if opts.time else '-')
    if kind == 'I':
        _, _seq, imp, a0, a1, a2, a3, t, depth = row[:9]
        if imp in ignore_imports:
            a0 = a1 = a2 = a3 = '-'
        return ('I', imp, a0, a1, a2, a3, t if opts.time else '-', depth)
    if kind == 'J':
        _, _seq, imp, v0, t = row[:5]
        if imp in ignore_imports:
            v0 = '-'
        return ('J', imp, v0, t if opts.time else '-')
    return tuple(row)


def stack_at(rows, upto):
    """The open E rows (guest call stack) before row index `upto`."""
    stack = []
    for r in rows[:upto]:
        if r[0] == 'E':
            stack.append(r)
        elif r[0] == 'X' and stack:
            # pop the innermost E of the same address (a body that parked and resumed keeps its order)
            for i in range(len(stack) - 1, -1, -1):
                if stack[i][3] == r[3]:
                    del stack[i]
                    break
    return stack


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
        ka = key(a[i], opts, ignore_regs, ignore_imports)
        kb = key(b[i], opts, ignore_regs, ignore_imports)
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
