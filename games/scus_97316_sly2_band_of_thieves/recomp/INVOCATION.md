# INVOCATION -- how the recompiled EE code of games/scus_97316_sly2_band_of_thieves is produced

The output is GENERATED into `${PS2R_RECOMP_GEN_DIR}` (`<build>/generated/games/scus_97316_sly2_band_of_thieves/recomp`)
by recomp/CMakeLists.txt at build time; nothing of it is committed. MANIFEST.sha256 pins the result.

| Item | Value |
|---|---|
| Tool | `ps2r_recompiler_v2` built from `engine/tools/recompiler_v2/` (NativeEE bec0022 + the local generator fixes listed in the repository README: u32-wrap, engine2 guard predecessor hop, ctc1 std::dec, 15 emitters, `--legacy-title-stubs` gate); binary md5 `1f9a887810bbfd8d02661412d129c3f8` (host build, informational) |
| ELF | `SCUS_973.16` extracted from the disc at build time (ps2r_disc_extract, sha256 `a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73`, 2,597,048 bytes) |
| inputs/tiny_stubs.txt, inputs/tiny_returners.txt | none (0 auto-registered stubs) |
| cwd | `<build>/games/scus_97316_sly2_band_of_thieves/recomp/tool_cwd` (the tool writes logs/recompiler_tool_v2/runtime_log.txt and looks for the stub files there) |

## Command (recomp/CMakeLists.txt)

```
ps2r_recompiler_v2 --input <gen>/../inputs/SCUS_973.16 --output-header <gen>/recompiled.h --output-cpp <gen>/recompiled.cpp \
  --shards 32 --init-parts 32 --direct-calls --no-legacy-title-stubs --yield-backedges
python3 tools/recompiler_v2/continuations.py <gen>
```
Pure discovery (no --ghidra-input / --overrides / --manual-json).

`--yield-backedges` (the sched stage): 2,729 loop back-edges get `if (--g_ee_backedge_budget == 0)
ps2r_ee_yield_point(ctx);` before their `goto`; recompiled.h and recompiled.cpp are unchanged, continuations.h
changes only in its shard-hash provenance comments. With the flag OFF the tool's output is byte-identical to the
previous generation (verified: `diff -rq` over the 35 files empty; MANIFEST.sha256 of 2026-10-15 reproduced), so
the first title's regeneration is unaffected. MANIFEST.sha256 now pins the flag-ON output.

## Report lines (first generation, 2026-10-15)

```
[v2] discovery over .../SCUS_973.16 (.text 1416688 bytes, 5 data sections, entry 0x00100008)
[v2] discovered 6574 functions, 265 jump-table sites, 4725 unreached gap words
[v2] delay-slot rule: 0 weak seed(s) rejected, 0 strong entries in a delay slot
[v2] no --ghidra-input: emitting the pure discovery set (no aliases/compat/pins)
[v2] emission model: 6574 functions (0 aliases, 0 pinned), 265 jr table sites
Could not find stub file: tiny_stubs.txt / tiny_returners.txt   (expected: none shipped)
[v2] emitted recompiled.h + recompiled.cpp (32 shard(s))
continuations: 6573 bodies, 72345 exact entries, 2 restartable wait thunks; 32 body shards unchanged
```
Determinism: the standalone probe run (different cwd and --input path) produced byte-identical files (`diff -rq` empty).
"Unhandled OP Code" sites in the shards: 0.

## Regeneration 2026-10-17 (overlays stage, E210)

Tool change `tools/recompiler_v2/recompiler_tool/discovery/{rv2_decode.h,functions.cpp}`: the walk stops at the
kernel's no-return syscalls (`addiu v1,zero,{0x04 Exit, 0x23 ExitThread, 0x24 ExitDeleteThread}; syscall`).
Report: `continuations: 6575 bodies, 72351 exact entries, 2 restartable wait thunks` (was 6574 bodies); the new
function is 0x100228 (crt0's root 0x100220 now ends at 0x100228); recompiled_shard_000.cpp, recompiled.h,
recompiled.cpp and continuations.h changed, the 31 other shards are byte-identical. MANIFEST.sha256 re-recorded.
