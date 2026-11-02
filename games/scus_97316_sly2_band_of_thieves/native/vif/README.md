# `native/vif/` -- Sly 2's VIF1 stream on the title's native path (`title.native.vif` + `title.native.dmac`)

The engine models a VIF1 chain kick as `ChainWalker::WalkVifChain` (the DMAtag walk) feeding every span to
`Vif::ProcessData` (the whole-span VIFcode parser `VifFastSpan` + the word latch, `VifUnpack` with its NEON
lanes). Sly 2 kicks ONE chain per frame (~2300-4300 tags: CNT / REFS / CALL / RET / END) carrying its whole
display list. This directory executes it in the title through two engine seams:

* `ChainWalker::SetVifChainExecutor` (`title.native.dmac`): the tag walk. Consulted on the inline walk (the lane)
  AND the staged walk (the MTVU worker); every span and TTE quadword goes through the walker's own feed
  (`ChainWalker::FeedVif1`: `Vif::ProcessData` inline, the worker's staged copy under MTVU).
* `Vif::SetSpanExecutor` (`title.native.vif`): the span parse + execution. Consulted first in `Vif::ProcessData`
  on the FEEDING thread -- the EE / DMAC thread inline, the MTVU worker's thread under the staged feed -- so the
  worker's replay (`MtvuWorker::ReplaySpan -> Vif::ProcessVif1`) is native too.

| file | role |
|---|---|
| `native_vif.{h,cpp}` | `NativeVif`: installs the two executors; `NATIVEEE_VIF_CENSUS=<file>` = the read-only census through the chain seam; run.meta `native_vif=` + the `native=` details |
| `vif_native_chains.{h,cpp}` | `VifNativeChains`: the walker's tag rules restated for the whole chain (TTE, ASR stack, IRQ+TIE, the tag guard); every span fed through `ChainWalker::FeedVif1` |
| `vif_native_spans.{h,cpp}` | `VifNativeSpans`: the whole-span parser restated with the title's loops -- NOP/STCYCL/STMOD/STMASK/STROW/STCOL written here, UNPACK through `VifUnpackNative`, MPG into VU1 micro memory, DIRECT/DIRECTHL to PATH2 (`Vif::SubmitDirectSpan`), the rest (MSCAL*, FLUSH*, OFFSET, BASE, ITOP, MARK, MSKPATH3) through `Vif::ExecuteCommand`; a payload or DIRECT stream crossing the span end parked with the engine's own members (`Vif::ParkPayload` / `ParkDirect`) and resumed natively from the held words at the next span (`ResumePending`, `ContinueDirect`); a span that does not complete it either latched (`Vif::LatchWords`); the TTE quadword decoded with the latch's pad-skip rule |
| `vif_unpack_native.{h,cpp}` | `VifUnpackNative`: typed loops for every defined UNPACK format under CL >= WL, straight into VU1 data memory with the engine's W / peek / parity / MASK / MODE rules (the filling write CL < WL through the engine's `VifUnpack`) |
| `vif_chain_tags.{h,cpp}` | `VifChainTags`: DMAtag decode + span pointers with the walker's address rules |
| `vif_census.{h,cpp}` | `VifChainCensus`: histograms of tags, VIFcodes (format / mask / USN / TOPS / MODE / CL-WL / VU address) and chain TEMPLATES (tag + code shape up to the span holding the VU kick) |
| `test_native_vif.cpp` | ctest `Sly2_NativeVifChains`: an oracle rig (walker + engine parser alone) and a native rig over the same chains -> identical VU1 memory, VIF registers + unit state, channel registers, walk result, GIF ops, i-bit raises, RAM; the random chains cover TTE, DIRECT (in-span and streamed), payloads crossing one and several spans |

**Where the VU1 and GIF native paths meet.** The unpack destination IS VU1 data memory (`VuUnit::state().data`,
the quadword layout the microprograms read: one 16-byte vector per quadword at `(addr + TOPS) & 0x3FF`); a native
VU1 kick handler reads it with no second copy. The MSCAL goes through `Vif::ExecuteCommand` -> `Vif::KickVu1`
(the double-buffer swap, `SetVifTop`, `VuUnit::Execute`) -> `VuTierDispatch` -- the native VU1 path's hook is reached
unchanged. DIRECT (PATH2) payloads reach the GIF arbiter as one packet per DIRECT, where `NativeGif` takes them.
MPG writes VU1 micro memory through `VuUnit::WriteMicroMem` (the generation bump the VU1 registry keys on).

**The i bit.** A code carrying it raises INT|VIS and runs the composition's stall hook (`Vif::RaiseIBit` ->
`LegVifIrq`) right after its effect, before the next word is read -- inline that runs the VIF1 interrupt handler
(the colour payloads further down the chain are read from memory after it, never cached); under MTVU it parks
the worker inside the span (`MtvuWorker::EnterIrqStall`), the EE services the interrupt in its next sync wait.

**Fallback.** `VifNativeSpans` declines a span only in states no feed path leaves the unit in (a partial word
latch; a TTE quadword with the unit mid-quadword); the engine's parse then runs it. The census row counts them
(`spans=<native>/<declined>:declined=<tte_busy>/<latch>`); on Sly 2's disc both are 0.
