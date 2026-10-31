# `native/gif/` -- track gif: Sly 2's GIF packets applied to the GS natively

Part of `ps2r_title_native` (../CMakeLists.txt). The engine's `GifParser` -> `GsSinkFanout` ->
`GsStateSink` -> `GsState::WritePacked` chain stays the fallback and the oracle.

| file | role |
|---|---|
| `gif_shape_census.{h,cpp}` | `GifShapeCensus : GsSink` -- `NATIVEEE_GIF_CENSUS=<tsv>`: the histogram of the GIFtag TEMPLATES the title emits (path, EOP/PRE/PRIM/FLG/NREG + descriptor word, the A+D address sequence, the submitting function, the VU1 program for PATH1), with the quadwords / kicks / IMAGE words each covers. `NATIVEEE_GIF_PKT_CAPTURE=<file>[:<MiB>]` records every delivered packet (the equivalence corpus; game data -> outside every repository). Installed by `hle/leg_devices.cpp` on the parser's fan-out. |
| `native_gif.{h,cpp}` | `NativeGif` -- `title.native.gif`: the whole-packet builder on `GifArbiter::SetPacketInterceptor` (engine/gif/gif_arbiter.h): PACKED all-known rows (dedicated bodies per template, generic row otherwise), PACKED A+D blocks, whole IMAGE tags, NLOOP 0; everything else declined to the parser. Installed by `hle/boot_leg.cpp` RunLeg. |

Seam, decisions, census tables and gate results: `~/ra-orch/notes/nat-gif.md`.
