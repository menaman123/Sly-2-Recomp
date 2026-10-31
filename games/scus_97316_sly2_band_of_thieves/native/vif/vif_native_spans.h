#pragma once
// VifNativeSpans: Sly 2's VIF1 stream executed by the title, one DMA span at a time,
// on the engine's span seam (Vif::SetSpanExecutor). Every VIF1 span reaches it on
// the FEEDING thread -- the EE / DMAC thread on the inline walk (the lane), the MTVU
// worker under the staged feed (play) -- with the unit in whatever state the previous
// span left it; it consumes the span and leaves the unit (VifRegs incl. STAT, the
// held quadword / FIFO / pending command, VU1 data and micro memory, PATH2 submits,
// the i-bit raises) exactly as the engine's parse would have. The engine's parse
// (VifFastSpan + the word latch) stays the fallback and the oracle; the shapes
// declined are counted (`declined=`) and, on Sly 2's disc, do not occur.
//
// What runs natively (engine/vif/vif.cpp ExecuteCommand + vif_fast_span.cpp restated):
//   NOP / STCYCL / STMOD / STMASK / STROW / STCOL      the register writes
//   UNPACK                                              the typed loops (VifUnpackNative) straight into VU1 data memory
//                                                       (the filling write CL < WL through the engine's VifUnpack)
//   MPG                                                 VuUnit::WriteMicroMem (the image identity is the VU1 registry's)
//   DIRECT / DIRECTHL                                   the PATH2 submit (Vif::SubmitDirectSpan) -- the GIF side is NativeGif's
//   OFFSET / BASE / ITOP / MARK / FLUSH* / MSCAL*       through Vif::ExecuteCommand (MSCAL = the double-buffer swap + the kick)
//   a payload or DIRECT stream crossing the span end    parked with the engine's own members (Vif::ParkPayload / ParkDirect),
//                                                       resumed natively from the held words at the next span
//   the TTE quadword                                    decoded with the pad-skip rule (a payload that does not fit starts on the next span)
// The i bit: raised by Vif::RaiseIBit right after the command's effect, before the
// next word is read -- inline that runs the handler (the colour payloads further down
// the chain are read from memory after it); under MTVU it parks the worker inside
// this span (MtvuWorker::EnterIrqStall through the same hook).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Vif;

class VifNativeSpans {
public:
    // `typed_unpack` = the title's UNPACK loops; off = every UNPACK through Vif::ExecuteCommand.
    VifNativeSpans(Vif& vif, bool typed_unpack);
    ~VifNativeSpans();
    VifNativeSpans(const VifNativeSpans&) = delete;
    VifNativeSpans& operator=(const VifNativeSpans&) = delete;

    // The executor body (Vif::SpanExecutor): true = consumed. VIF1 only.
    bool Process(int vif, const uint8_t* data, size_t size);

    std::string Row() const;
    uint64_t spans_native() const { return spans_native_; }
    uint64_t spans_declined() const { return spans_declined_; }
    uint64_t unpack_typed() const { return unpack_typed_; }
    uint64_t unpack_engine() const { return unpack_engine_; }

private:
    static bool Thunk(void* user, int vif, const uint8_t* data, size_t size);
    // The fresh parse from span word `w` (the unit idle, nothing held).
    void Parse(const uint8_t* data, size_t nwords, size_t w);
    // The TTE quadword (4 words, pad-skip rule) from word `w`, the unit idle.
    void ParseTte(const uint8_t* data, size_t w);
    // A span arriving with a parked command: completes it from the held words + the
    // span; returns the span word the fresh parse resumes at, or SIZE_MAX to decline.
    size_t ResumePending(const uint8_t* data, size_t nwords);
    // One UNPACK with its payload in `payload` (`words` data words, the lookahead rule applied by the caller).
    void Unpack(uint32_t code, const uint8_t* payload, size_t words, bool edge, uint8_t phase);
    // One command whose payload (if any) is at hand: the register writes natively, the rest through the engine.
    void Command(uint32_t code, const uint8_t* payload, size_t words, uint8_t phase);

    Vif& vif_;
    bool typed_unpack_ = false;
    bool in_process_ = false;        // recursion guard: a fallback ProcessData from inside must reach the engine

    uint64_t spans_native_ = 0;      // spans consumed here
    uint64_t spans_declined_ = 0;    // spans left to the engine's parse (shapes below)
    uint64_t declined_tte_busy_ = 0; // a TTE quadword with the unit in a state no feed path leaves it in
    uint64_t declined_latch_ = 0;    // a partial word latch in flight (never on whole-quadword feeds)
    uint64_t deep_split_ = 0;        // a parked payload still incomplete after a whole span: the span latched (Vif::LatchWords)
    uint64_t tte_quadwords_ = 0;
    uint64_t unpack_typed_ = 0;      // UNPACKs through the typed loops
    uint64_t unpack_engine_ = 0;     // UNPACKs through Vif::ExecuteCommand (the filling write, undefined formats)
    uint64_t codes_native_ = 0;      // NOP / STCYCL / STMOD / STMASK / STROW / STCOL / MPG written here
    uint64_t codes_engine_ = 0;      // the other codes through Vif::ExecuteCommand (MSCAL*, FLUSH*, OFFSET, BASE, ITOP, MARK, ...)
    uint64_t mpg_ = 0;
    uint64_t mpg_bytes_ = 0;
    uint64_t direct_ = 0;            // DIRECT / DIRECTHL submitted (in-span + completed streams)
    uint64_t direct_qw_ = 0;
    uint64_t parked_ = 0;            // payloads / DIRECT streams parked at a span end
    uint64_t resumed_ = 0;           // ... resumed natively at the next span
    uint64_t ibit_raised_ = 0;
    std::vector<uint8_t> scratch_;   // a payload assembled from the held words + the span, or + a zero lookahead
};
