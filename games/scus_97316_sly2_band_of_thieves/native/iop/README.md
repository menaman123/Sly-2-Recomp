# `native/iop/` -- the hosted IOP modules as native host code, one function at a time

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
(the rule: exact by construction over the module's RAM layout), so a native body and the
recompiled body are interchangeable at any call.

## The frame

| file | what |
|---|---|
| `native_iop_module.{h,cpp}` | `NativeIopModule`: one hosted module; `Add(addr, body, name)` registers a native body for the recompiled function at `addr`; `Install(filter)` puts the admitted ones into the bound slot table; `Census()`; `RemainingCalls()`. `NativeIopFilter`: `NATIVEEE_IOP_NATIVE` (all / none / `mod[:addr,..];..` / `-mod..`) -- the per-function gate for bisecting on the lane. |
| `native_iop_frame.{h,cpp}` | `NativeIopFrame`: the four modules bound to the generated tables (`IopHost::SeamTables()`), the knob `title.native.iopsys`, the trace, the census rows. |
| `native_iop_bodies.cpp` | the registry: one `Register...(frame.<Module>())` line per module. |
| `iop_services.{h,cpp}` | `IopServices`: the stock libraries' services by export ordinal, typed, over the body's own `IopContext` -- the SAME `IopAbiSurface::ImportCall` the recompiled `import_call` reaches (kernel-side and register-side effects identical: a `WaitSema` parks the coroutine where the recompiled one did). `CallGuest(ctx, addr)` runs another guest function through its wrapper. `IopRam` reads/writes the module's state words. `IopStackFrame` for a callee with stack arguments. |
| `iop_call_trace.{h,cpp}` | `NATIVEEE_IOP_TRACE=<file>`: every guest function entry/exit (a0-a3 / v0, $sp, $ra, the IOP clock, tid) and every import entry/exit, bound on the seam's two hooks. |
| `iop_trace_compare.py` | the per-function gate: diff the OFF (recompiled) and ON (native) traces; the first divergent row names the body whose port is wrong. Compares an import's registers up to the stock library's arity and (with `--guest-arity`) a guest function's up to its own: a MIPS caller loads only the registers it passes, the rest are leftovers a native body leaves differently. |
| `iop_guest_arity.py` | derives the guest arity file from the generated TUs (which of a0-a3 each function reads before writing, fixpoint over the JAL sites; conservative). |
| `snd989_lock.{h,cpp}` | the first port and the shared service: the 989snd global lock (fn_42f58 Acquire / fn_430dc Release) over the module's words (owner 0x5229c, sema 0x52528, depth 0x52214, tag stack 0x52218). |

## Porting a function (the loop)

1. Read the function in the generated TU (`<build>/generated/games/<title>/iop/<id>/iop_recompiled.cpp`,
   `// Function at 0x... (N instructions)`): it is the ground truth, byte-faithful to this disc's module.
2. Write the body as `void Body(IopContext& ctx)` over `IopRam` words and `IopServices` calls: every write
   to the module's STATE (its .data/.bss words, the heap buffers it owns, the RPC reply buffer -- the
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

## Closure per module (the ports' own accounting; the census rows are `run.meta native_iop=` / `iop_threads=`)

Lanes: boot 600 (no pad + boot.pad), the Paris hub 900 (hub_ep1_fix + sm_paris.pad), the binocucom cutscene 900
(ep1_binoc_call, no pad), the loader route 1300 (cold boot, mc_pipe card, ep1_a.pad). On every one of them, ON
(`--set title.native.iopsys=on`) = the OFF rows, and the call trace compares IDENTICAL row for row (boot.pad 219324
rows, Paris 1444840, binocucom 1246610, loader route 1145490). Sizes are the generated TUs' function extents.

**FILEIO_service** (`native_fileio.{h,cpp}`, `native_fileio_threads.cpp`): 53 functions / 10976 B. Native 19 /
5976 B. Reached by any lane: 11 / 3304 B -- the entry 0x20000 and its init helpers (0x204a8/0x204ec/0x20528/0x2061c),
the two RPC thread entries (0x21e68 handler, 0x22384 server loop, 0x227d4/0x2290c heap server), the power-off thread
0x22298, the per-fd thread 0x21b78 and 0x21ca4's dispatch: all native (census `fileio:19/53:12/0` on the boot and
loader lanes, `8/0` on the state lanes; with the four thread entries as step bodies the native-call count drops by the
entries, which bypass the seam). Unreached-dead: none provable (every fno shim is live code behind the EE's libfileio).
Unreached-unexercised: 8 ported / 2672 B (close/read/write/lseek/ioctl, buffer alloc/free/reset, record alloc/free) --
the EE's libfileio links fnos 255 0 1 2 3 5 but SCUS_973.16 issues only fno 255 (init) and fno 0 (open "rom0:ROMVER")
on every lane, including the level load (the EE reads levels through cdvdman): no leg on this disc exercises them, the
oracle is the recompiled body behind the knob. Not ported: 34 / 5000 B = the one-shot fno shims 6..28 (ioman remove /
mkdir / dopen / ... / devctl) and the IOP-heap server handler + helpers (0x80000003 sees 0 calls: the engine HLEs
sceSifAllocIopHeap) -- no lane, no leg. Threads: 4 of 4 in step form (`iop_threads=` fileio t1/t2/t3/t17: coro=0).

**lgAud** (`native_lgaud.{h,cpp}`): 140 functions / 35568 B. Native 15 / 2760 B. Reached by any lane: 8 / 2292 B --
the entry 0x299f4 (argv parse, ReferModuleStatus, the image hash 0x303a8/0x302b0/0x30398, the usbd version scan,
RegisterLibraryEntries, sceUsbdRegisterLdd, CreateSema) and export 18 = 0x2a3b8 (the device-state poll spiop's
headset monitor makes every 500 ms): all native (`lgaud:15/140:8929/0` boot, `8952/0` loader route, `31/0` on the
state lanes). Unreached-unexercised: 7 ported / 468 B (unload, the allocator pair, min/max/abs/muldiv) -- reached
only from the headset path. Not ported: 125 / 32808 B = the USB headset driver proper (LDD probe/connect/disconnect
0x28000..0x29238, the isochronous pipes, the VAG decode/mixer 0x2c464..0x30160, exports 4..22 except 18, the 'BLIP'
RPC thread 0x29ed8): the engine's usbd is an idle bus and Sly 2 loads the module with rpc=0, so no leg on this disc
reaches them (a USB headset would be the only one). Threads: none on this title (rpc=0 creates no thread).

**spiop**: see `spiop/README.md` -- 89 functions / 21084 B, native 83, reached 48 (all native, 0 recompiled calls
on every lane), 35 ported-unexercised (the headset arm + the soft-float libc; ctest `Sly2_SpiopLibcDifferential` is
their oracle), 6 statically dead; threads: tray + headset monitor + feeder in step form, the three stream threads
coroutines (measured: no nested park ever blocks on the lanes; see the README for the exact-form requirement).
