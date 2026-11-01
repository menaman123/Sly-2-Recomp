# How the recompiled EE code is produced

`recomp/CMakeLists.txt` generates the code at build time into `${PS2R_RECOMP_GEN_DIR}`
(`<build>/generated/games/scus_97316_sly2_band_of_thieves/recomp`). None of it is committed;
`MANIFEST.sha256` pins the result.

| Item | Value |
|---|---|
| Tools | `ps2r_recompiler_v2` and `ps2r_continuations`, built from `engine/tools/recompiler_v2/` |
| Input | `SCUS_973.16`, extracted from the disc at build time (sha256 `a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73`, 2,597,048 bytes) |
| Stub files | none (`inputs/tiny_stubs.txt` and `inputs/tiny_returners.txt` are absent) |
| Working directory | `<build>/games/scus_97316_sly2_band_of_thieves/recomp/tool_cwd` |

## Command

```
ps2r_recompiler_v2 --input <gen>/../inputs/SCUS_973.16 --output-header <gen>/recompiled.h --output-cpp <gen>/recompiled.cpp \
  --shards 32 --init-parts 32 --direct-calls --no-legacy-title-stubs --yield-backedges
ps2r_continuations <gen> --audits engine/tools/recompiler_v2/continuation_audits.json
```

This is pure discovery: no `--ghidra-input`, `--overrides` or `--manual-json`. `--yield-backedges` puts a
budget check and `ps2r_ee_yield_point` on each of the 2,729 loop back-edges.

## Expected report

```
[v2] discovery over .../SCUS_973.16 (.text 1416688 bytes, 5 data sections, entry 0x00100008)
[v2] emitted recompiled.h + recompiled.cpp (32 shard(s))
continuations: 6575 bodies, 72351 exact entries, 2 restartable wait thunks
```

The output is deterministic: a run from a different working directory and input path produces identical
files. There are no "Unhandled OP Code" sites in the shards.

## `--vu0-native` (CMake `PS2R_SLY2_VU0_NATIVE`, default ON since STAGE NATIVE-2; OFF = the scalar emission, the oracle)

Adds `--vu0-native` to the command above: every COP2 (VU0 macro-mode) FMAC op is emitted through the host
vector kernels of `engine/ee/recomp_abi/host_app/vu0_native.h` (NEON / SSE2 / scalar; the bit-for-bit twins
of the scalar `VR_reg` operators, `tests/vu/test_vu0_native_twin.cpp`), the dest mask as a lane blend, and
the MAC/STATUS flag commit folded over each straight-line run (one `vu0n::Commit` before any reader: CFC2 /
CTC2 / VCALLMS / syscall / control flow / block end). The shards differ textually (6,577 commit sites ->
1,559 folded commits), so this setting has its own manifest, `MANIFEST.vu0_native.sha256`, recorded the
same way from a `--vu0-native` generation. The report line is unchanged (6575 bodies, 72351 exact entries).
The two settings must be run-equivalent: the determinism lane's mem/audio/frames hashes are the gate.

## `--mem-window` (CMake `PS2R_SLY2_MEM_WINDOW`, default ON since STAGE PERF-1; OFF = the classic emission, the oracle)

Adds `--mem-window` to the command above (STAGE PERF-1, track ee). Every generated body is emitted as
`void FUN_x(CpuContext& PS2R_RECOMP_RESTRICT ctx, uint32_t addr)` -- the restrict qualifier states what is
true of the engine: guest RAM and the scratchpad never overlap a `CpuContext`, so a guest store (a char-typed
store through the RAM base, which the compiler must otherwise assume may alias anything) cannot change a
guest register, and the compiler keeps register values in host registers across it. Every other path that
changes the running context (a syscall, an interrupt dispatch, a yield point) is a call the body passes `ctx`
to, which the compiler treats as a clobber. The prologue gains one inert local,
`const memory::Window _mw = memory::window();` (the two inline bases of `FastMem`, read once per body), and a
shard post-pass (`Recompiler::rewrite_memory_window_file`) rewrites every `memory::read<T>(..)` /
`memory::write<T>(..)` / `memory::read_quad(..)` / `memory::write_quad(..)` site in the shard to the `Window`
overload of `engine/ee/recomp_abi/host_app/memory.h` -- the same arms (main RAM, scratchpad, slow path) in the
same order as `FastMem::Read/Write/ReadQuad/WriteQuad`, so the bytes the game reads and writes are the classic
form's. The continuation tools accept both signatures and strip the `_mw` prologue token (the entry-switch
audits are unchanged). Manifests: `MANIFEST.vu0_native.sha256` pins the DEFAULT build (`--vu0-native
--mem-window`); `MANIFEST.sha256` pins the all-classic emission (`--no-vu0-native --no-mem-window`); the two
mixed combinations have no pin (the check is skipped with a notice). The report line is unchanged (6575
bodies, 72351 exact entries, 2 restartable wait thunks). The gate: the lane rows (nat2_gate defaults =
baseline) and the Paris hub rows must be the classic emission's.

## `--zero-fold` (CMake `PS2R_SLY2_ZERO_FOLD`, default ON since STAGE PERF-1)

Adds `--zero-fold`: every rvalue read of `$zero` in a generated body (`ctx.cpuRegs.GPR.r[0].<view>[k]`) becomes a
typed literal 0 (shard post-pass `Recompiler::rewrite_zero_fold_file`: 82,020 reads folded, 8 writes kept -- the
four `dsra32`-form pairs `r[0].SD[0] = ...; r[0].UD[0] = 0;`, the only `$zero` writes in the emission, adjacent, no
read between, so the register reads 0 at every read site). The continuation tools' restartable-wait-thunk match
accepts both spellings (still 2 restartable wait thunks). Under `--mem-window` the common TU's shadow call stack is
also ONE `thread_local` object (`EeShadowStack`; the two old names are macros, every other emitted line unchanged):
one TLS address resolution per dispatch instead of up to four.

## `--inline-leaves` (CMake `PS2R_SLY2_INLINE_LEAVES`, default OFF)

Adds `--inline-leaves`: a JAL whose constant target is a trivial leaf (one block, <= 8 whitelisted straight-line
instructions, `jr $ra`) is emitted as `if (g_recomp_fn_hooked[id]) recomp_hooked_dispatch(..); else { <the callee's
instructions in place> }` (2,556 sites over 208 leaves on this ELF; e.g. the identity getter 0x2564b8 = 22 % of all
calls on the lanes). Measured neutral on the Paris hub and a +14 % EE-CPU regression on hub_ep3 (the +1 MB of text
costs more instruction-cache misses than the predicted calls it removes), so it is OFF; kept for a smaller bound.

## Re-recording the manifest

When the generator or its flags change, the build fails the manifest check. Regenerate, review the diff,
then record the new hashes:

```
(cd <gen> && shasum -a 256 recompiled.h recompiled.cpp continuations.h recompiled_shard_*.cpp) > recomp/MANIFEST.sha256
```
