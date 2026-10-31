# spiop -- Sly 2's streaming IOP module (0x5a000..0x5f700, 0x5700 B) as host C++

The module behind 989snd's external command 'SUKR' (ops 0x00..0x0d): three stream slots of up to five
interleaved VAG voices each, refilled from the disc through the hosted cdvdman under a disc semaphore,
polled by one stream thread per slot every 5 ms; a tray-poll thread (op 0x0c's reply word) and a
Logitech USB-headset monitor (lgaud:18 every 500 ms). `spiop_state.h` names every word of its RAM layout.

| file | what |
|---|---|
| `spiop_native.{h,cpp}` | 69 function bodies: the SUKR handlers, the context, the slots, the stream state machine, the refill reads, the allocator shims -- every function any lane reaches. `Register()` adds them all. |
| `spiop_libc.{h,cpp}` | 14 more: gcc fp-bit single-precision soft float, `sqrtf`, the allocation-failure `exit` -- the headset monitor's microphone-RMS arm and operator new's failure path; no lane reaches them (no USB device, no heap exhaustion). |
| `test_spiop_libc.cpp` | their oracle: ctest `Sly2_SpiopLibcDifferential` runs each through the recompiled body and the native body on the same IOP RAM image (74320 random + edge cases: v0, sp, s0-s7, module bytes, fp records, every stack byte equal). |
| `spiop_threads.{h,cpp}` | the headset monitor (fn_5b0cc) and feeder (fn_5b2a0) threads in the step form (`NativeIopModule::AddThread`, `iop_step.h`). The tray thread is `../spiop_tray_thread.cpp` (iopsys). |

Census on the lanes (`native_iop=spiop:83/89:<native calls>/0`): boot 600 boot.pad 4736/0, Paris hub 900
31025/0, binocucom cutscene 900 35078/0, loader route 1300 21281/0 -- every call native; the trace compare
(`iop_trace_compare.py`, OFF = `NATIVEEE_IOP_NATIVE=none`) IDENTICAL on each.

Not native (6 of 89): the double-precision soft-float helpers 0x5da78, 0x5f034, 0x5db7c, 0x5dd40, 0x5de88,
0x5e960 -- statically dead (reachable only from the unreferenced `__extendsfdf2` tail at 0x5daa8); the
recompiled bodies stay in the generated TU.

Threads: tray (fn_5b438) and headset monitor/feeder run as step bodies (no coroutine). The three slot stream
threads (fn_5be48) stay coroutines: their state functions call 989snd exports (snd989:49/99/25/29 ...) whose
bodies take the 989snd global lock (fn_42f58: `WaitSema` on semaphore 8) -- a park inside the hosted callee, which
a step body cannot return (`iop_kernel.cpp LeaveThread` reports such a call); the slot semaphore (fn_5bff8) and
the disc semaphore (fn_5c0c4/fn_5c794) are two more nested sites. Measured on the OFF traces (a blocking import =
an I row not followed by its own J row): on boot.pad 600 and the Paris hub 900 the stream threads park ONLY on
their own 5 ms `DelayThread` (572 switches each on boot, ~3000 each on the hub); not one nested `WaitSema` of
theirs ever blocked (the sid-8 lock blocks belong to snd989's threads 46520/47fec and the EE-side handler). So a
step form that calls the callees as they are would be exact on these lanes but not by construction: the exact
form is the lock pre-acquisition protocol (poll the semaphore; return `Park::Sema` when it would block; on resume
hand the callee a `{tid, sid}` token its `WaitSema` site consumes instead of waiting), which needs the token
honoured at every nested site: the three spiop sites above plus snd989's fn_42f58 and fn_4c1ac (the voice-table
lock) -- the snd989 track's bodies. Until then the coroutine form stays (exact; ~3.7 ms per 600 boot frames, ~20 ms
per 900 hub frames at 3.27 us per switch).

Rules learned here (beside README.md's loop): the original's frame is mirrored (same `$sp` move, the same
saved-register slots and parameter blocks -- imports read them); every void body sets the recompiled body's
leftover `$v0` at each exit (the compare judges X rows); a sibling call goes through `iop_stubs::iop_resolve`
(the seam: native when installed, the recompiled body otherwise = the oracle at every call).
