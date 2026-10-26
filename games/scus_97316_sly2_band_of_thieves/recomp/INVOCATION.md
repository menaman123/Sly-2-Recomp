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

## Re-recording the manifest

When the generator or its flags change, the build fails the manifest check. Regenerate, review the diff,
then record the new hashes:

```
(cd <gen> && shasum -a 256 recompiled.h recompiled.cpp continuations.h recompiled_shard_*.cpp) > recomp/MANIFEST.sha256
```
