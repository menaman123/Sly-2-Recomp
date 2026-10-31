#!/usr/bin/env python3
"""iop_trace_blocks.py TRACE: which thread parks where, read off a NATIVEEE_IOP_TRACE file (the OFF arm, or any arm).
A blocking import = an I row whose next row is not its own J row (the cooperative kernel switched threads in between).
Attribution = the last E row's tid, reset to None after every block (a resumed thread's rows carry no tid until it enters a
function again); reliable attributions only, the rest listed by lib. The input for the step-form question: a thread whose
only blocking imports are its own (DelayThread / SleepThread / its own WaitSema) can be written as a step body with
StepPark; a nested block (inside a callee, e.g. the 989snd lock fn_42f58 under a snd989 export) needs the token protocol."""
import sys, collections
rows = [l.split() for l in open(sys.argv[1]) if not l.startswith('#')]
cur = None; stack = collections.defaultdict(list)
blk = collections.Counter(); unk = collections.Counter(); where = collections.Counter()
for i, r in enumerate(rows):
    k = r[0]
    if k == 'E':
        cur = int(r[12]); stack[cur].append(r[3])
    elif k == 'X':
        if cur is not None and stack[cur]: stack[cur].pop()
    elif k == 'I':
        lib = r[2]; nxt = rows[i+1] if i+1 < len(rows) else None
        if not (nxt and nxt[0] == 'J' and nxt[2] == lib):
            if cur is None: unk[lib] += 1
            else:
                blk[(cur, lib, r[3] if lib == 'thsemap:8' else '')] += 1
                where[(cur, lib, '>'.join(stack[cur][-3:]))] += 1
            cur = None
print("blocking imports (tid, lib, sid):")
for key, n in sorted(blk.items()): print("  ", key, n)
print("unattributed blocking imports by lib:", dict(unk))
print("contexts of blocking imports on threads whose stack has 5be48:")
for (t, lib, st), n in sorted(where.items()):
    if '5be48' in st: print(f"  tid {t} {lib} n={n} stack={st}")
