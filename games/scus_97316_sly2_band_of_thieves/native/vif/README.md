# `native/vif/` -- Sly 2's VIF1 DMA chains on the title's native path (tracks `vif` + `dmac`)

The engine models a VIF1 chain kick as `ChainWalker::WalkVifChain` (the DMAtag walk) feeding every span to
`Vif::ProcessData` (the whole-span VIFcode parser, `VifUnpack` with its NEON lanes). Sly 2 kicks ONE chain
per frame (~2300-4300 tags: CNT / REFS / CALL / RET / END) carrying its whole display list; this directory
executes that chain in the title, through the engine seam `ChainWalker::SetVifChainExecutor`.

| file | role |
|---|---|
| `native_vif.{h,cpp}` | `NativeVif`: installs the ONE executor; `title.native.dmac` = the chain executor, `title.native.vif` = the typed UNPACK loops (implies the executor); `NATIVEEE_VIF_CENSUS=<file>` = the read-only census through the same hook; run.meta `native_vif=` + the `native=` details |
| `vif_native_chains.{h,cpp}` | `VifNativeChains`: the walker's tag rules restated for the whole chain (TTE, ASR stack, IRQ+TIE, the tag guard); per span: the unit idle + the span admitted (payloads inside, no DIRECT) -> executed here, else `Vif::ProcessData` (the walker's own call = the oracle) |
| `vif_unpack_native.{h,cpp}` | `VifUnpackNative`: typed loops for every defined UNPACK format under CL >= WL, straight into VU1 data memory with the engine's W / peek / parity / MASK / MODE rules |
| `vif_chain_tags.{h,cpp}` | `VifChainTags`: DMAtag decode + span pointers with the walker's address rules |
| `vif_census.{h,cpp}` | `VifChainCensus`: histograms of tags, VIFcodes (format / mask / USN / TOPS / MODE / CL-WL / VU address) and chain TEMPLATES (tag + code shape up to the span holding the VU kick) |
| `test_native_vif.cpp` | ctest `Sly2_NativeVifChains`: an oracle rig (walker alone) and a native rig over the same chains -> identical VU1 memory, VIF registers + unit state, channel registers, walk result, GIF ops, i-bit raises, RAM |

**Where the VU1 and GIF native paths meet.** The unpack destination IS VU1 data memory (`VuUnit::state().data`,
the quadword layout the microprograms read: one 16-byte vector per quadword at `(addr + TOPS) & 0x3FF`); a native
VU1 kick handler reads it with no second copy. The MSCAL goes through `Vif::ExecuteCommand` -> `Vif::KickVu1`
(the double-buffer swap, `SetVifTop`, `VuUnit::Execute`) -> `VuTierDispatch` -- the VU1 track's hook is reached
unchanged. DIRECT (PATH2) spans stay the engine's (`GifArbiter`), as the walker fed them.

**The i bit.** A code carrying it raises INT|VIS and runs the composition's stall hook (`Vif::RaiseIBit` ->
`LegVifIrq`) right after its effect, before the next word is read -- the VIF1 interrupt handler that fills the
colour payloads further down the chain runs then; later spans are read from memory after it (never cached).

**Scope.** The inline feed only (the lane: MTVU off). Under the MTVU staged feed the walker never consults the
executor and the engine's model runs unchanged (the worker owns the span replay: `MtvuWorker::ReplaySpan`).
