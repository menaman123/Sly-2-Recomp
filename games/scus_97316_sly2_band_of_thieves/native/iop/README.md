# `native/iop/` -- the hosted IOP modules as native host code, one function at a time (STAGE NATIVE-3)

The hosted IOP of this title runs four modules as recompiled MIPS (`iop/iop_modules.h`): FILEIO_service
(0x20000), lgAud (0x28000), 989snd_Library (0x33000), spiop (0x5a000). Everything else on the IOP -- the
IOPRP300 kernel, sio2man/padman/mcman/mcserv/libsd/usbd/cdvdman -- is already the engine's C++. This
directory is the FRAME in which those four become host C++ too, function by function, with the recompiled
body as the fallback and the oracle at every step.

## The seam (engine `tools/iop_recompiler --seam`, `engine/iop/abi/host_app/iop_seam.h`)

Every function of a `--seam` module is emitted as a static recompiled body plus the `iop_func_<addr>`
wrapper that every JAL site, every JR/JALR (through `iop_resolve`), every thread entry (thbase), RPC
handler (sifcmd / IopRpcServer), hard-timer beat (timrman) and DMA / CD callback reaches. The wrapper is

    iop_seam::Call(iop_fn_slots[k], &iop_body_<addr>, iop_ctx);

over the module's slot table `{addr, end, native, self, native_calls, recompiled_calls}` in address order.
A slot with a native body runs it INSTEAD of the recompiled one -- on the same `IopContext` (a0-a3 in
gpr[4..7], the result in gpr[2]), the same IOP RAM, the same guest stack -- otherwise the recompiled body
runs. Nothing is re-registered: the hosted kernel keeps every thread, semaphore, event flag, RPC record and
timer; only the body at an address is swapped. The module's RAM region stays allocated and IS the state
(the snd track's rule: exact by construction over the module's RAM layout), so a native body and the
recompiled body are interchangeable at any call.

## The frame

| file | what |
|---|---|
| `native_iop_module.{h,cpp}` | `NativeIopModule`: one hosted module; `Add(addr, body, name)` registers a native body for the recompiled function at `addr`; `Install(filter)` puts the admitted ones into the bound slot table; `Census()`; `RemainingCalls()`. `NativeIopFilter`: `NATIVEEE_IOP_NATIVE` (all / none / `mod[:addr,..];..` / `-mod..`) -- the per-function gate for bisecting on the lane. |
| `native_iop_frame.{h,cpp}` | `NativeIopFrame`: the four modules bound to the generated tables (`IopHost::SeamTables()`), the knob `title.native.iopsys`, the trace, the census rows. |
| `native_iop_bodies.cpp` | the registry: one `Register...(frame.<Module>())` line per module track. |
| `iop_services.{h,cpp}` | `IopServices`: the stock libraries' services by export ordinal, typed, over the body's own `IopContext` -- the SAME `IopAbiSurface::ImportCall` the recompiled `import_call` reaches (kernel-side and register-side effects identical: a `WaitSema` parks the coroutine where the recompiled one did). `CallGuest(ctx, addr)` runs another guest function through its wrapper. `IopRam` reads/writes the module's state words. `IopStackFrame` for a callee with stack arguments. |
| `iop_call_trace.{h,cpp}` | `NATIVEEE_IOP_TRACE=<file>`: every guest function entry/exit (a0-a3 / v0, $sp, $ra, the IOP clock, tid) and every import entry/exit, bound on the seam's two hooks. |
| `iop_trace_compare.py` | the per-function gate: diff the OFF (recompiled) and ON (native) traces; the first divergent row names the body whose port is wrong. Compares an import's registers up to the stock library's arity and (with `--guest-arity`) a guest function's up to its own: a MIPS caller loads only the registers it passes, the rest are leftovers a native body leaves differently. |
| `iop_guest_arity.py` | derives the guest arity file from the generated TUs (which of a0-a3 each function reads before writing, fixpoint over the JAL sites; conservative). |
| `snd989_lock.{h,cpp}` | the first port and the shared service: the 989snd global lock (fn_42f58 Acquire / fn_430dc Release) over the module's words (owner 0x5229c, sema 0x52528, depth 0x52214, tag stack 0x52218). |

## Porting a function (the module tracks' loop)

1. Read the function in the generated TU (`<build>/generated/games/<title>/iop/<id>/iop_recompiled.cpp`,
   `// Function at 0x... (N instructions)`): it is the ground truth, byte-faithful to this disc's module.
2. Write the body as `void Body(IopContext& ctx)` over `IopRam` words and `IopServices` calls: every write
   to the module's STATE (its .data/.bss words, the heap buffers it owns, the RPC reply buffer -- nat2-snd's
   rule: the reply buffer is state) reproduced; every kernel/library call made through `IopServices` (same
   ordinal, same arguments, same order); v0 set as the recompiled body leaves it at EVERY exit. Register
   spills to the body's own stack frame are not reproduced (no oracle reads a module's stack).
3. `module.Add(addr, &NativeIopModule::Thunk<T, &T::Body>, "name", &object)` in your module's register
   function (one line in `native_iop_bodies.cpp`).
4. Gate: the lane with `--set title.native.iopsys=on` must equal the baseline rows (nat2_gate.sh); for a
   divergence, record `NATIVEEE_IOP_TRACE` with `NATIVEEE_IOP_NATIVE=none` and with
   `NATIVEEE_IOP_NATIVE=<id>:<addr>` and run
   `iop_guest_arity.py <build>/generated/.../iop/*/iop_recompiled.cpp > arity.txt;
    iop_trace_compare.py OFF ON --guest-arity arity.txt`.
   The lock's proof: 600 boot.pad frames, OFF vs ON = `IDENTICAL: 219324 rows (71957 guest calls, 37720
   imports)` -- the whole call stream of the hosted IOP, every argument a callee reads, every import in
   order, every return value, the IOP clock at every row.
5. The census (`run.meta native_iop=<id>:<native fns>/<total>:<native calls>/<recompiled calls>`,
   `native_iop_remaining=<id>:<n>fns:<addr>=<calls>...`) says what the lane still reaches as recompiled.

A body that must park (WaitSema / SleepThread / sceSifRpcLoop / DelayThread) parks through `IopServices`
inside the coroutine the kernel runs it on -- a native thread body is written exactly like the recompiled
one, as a loop. The coroutine-free thread form (the kernel's `StartHostThread` step bodies) is the end
state once every function of a module is native; see `engine/iop/README.md`.

Portability: every TU here is portable C++17 (no POSIX / _WIN32 / __APPLE__); the compare tool is Python 3.
