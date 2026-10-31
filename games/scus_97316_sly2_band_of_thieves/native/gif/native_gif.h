// native_gif.h -- NativeGif: Sly 2's GIF packets applied to the GS state natively.
//
// The seam: GifArbiter::SetPacketInterceptor (engine/gif/gif_arbiter.h). The drain owner offers
// every packet that carries data -- while the parser is between streams (no buffered tail, no
// tag in flight, PATH3 not masked) -- to Intercept(), which either consumes the WHOLE packet or
// declines it; a declined packet reaches GifParser::ReceiveData untouched (the fallback and the
// oracle). Nothing between the arbiter and ps2r::gs::GsState runs for a consumed packet: not the
// parser's tag / descriptor state machine, not GsSinkFanout, not GsStateSink's PACKED re-pack,
// not GsState::WritePacked's decode -- the builder writes the attribute registers and kicks the
// vertices through the façade's members directly, one specialised row body per GIFtag TEMPLATE
// (the descriptor word + FLG/NREG/PRE/PRIM; NLOOP is the row count). The GS mutations and their
// order are the parser's exactly (gif_equiv proves it on recorded packets; the lane's hashes on
// the game).
//
// What it serves (the census of ~/ra-orch/notes/nat-gif.md, the shapes covering the quadwords):
//   PACKED tags whose descriptors are in the decoded set {PRIM RGBAQ ST UV XYZF2 XYZ2 FOG XYZF3
//     XYZ3 NOP} or A+D -- the VU1 programs' XGKICK geometry (the cel programs' {ST RGBAQ NOP XYZF2}
//     / {ST NOP RGBAQ XYZF2} rows = 63 % of the lane's quadwords, the ink {A+D XYZF2 XYZF2} pairs,
//     position-only strips), the register blocks (every descriptor A+D: texture / test / alpha /
//     the transfer head) and the EE's sprite packets; the templates seen most get a dedicated row
//     body, the rest of the class a generic row over the same member calls; declined when an A+D
//     item names SIGNAL / FINISH / LABEL (the interrupt observer reads the parser's active path
//     for those: rare, left to the parser);
//   IMAGE tags whose payload is whole inside the packet -- one WriteImage span;
//   NLOOP = 0 tags (EOP flush only; the forms that make the parser eat the next quadword declined).
// Declined: REGLIST, FLG 3, a descriptor outside the sets above (the register-as-data form), a tag
// whose payload is not whole in the packet (PATH3 IMAGE continuations, split PATH1/2 streams), a
// packet with a trailing partial unit. Every decision is per PACKET (all its tags validate first;
// nothing is applied before the packet is accepted). After a served packet the GIF register file's
// transfer view (GIF_TAG0-3 / CNT / STAT.OPH) reads as the parser would have left it.
//
// Counts: NativeCensus Served/Fallback(NativeUnit::Gif) per packet; the detail string names the
// template bodies that fired. The run.meta `gif_sink=` row (GsStateSink's counters) legitimately
// drops for the served packets: it counts what reached the parser's sink.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/gif/gif_tag.h"

class GifArbiter;
class GifRegs;
namespace ps2r::gs {
class GsState;
}

namespace ps2r::title::sly2 {

class NativeGif {
public:
    // Installs the interceptor on `arbiter` (null = none: the equivalence tool calls Intercept
    // itself); the packets land in `gs`; `regs` (optional) is the GIF register file whose
    // transfer view (GIF_TAG0-3 / GIF_CNT readback) is left as the parser would have left it.
    NativeGif(GifArbiter* arbiter, ps2r::gs::GsState& gs, GifRegs* regs = nullptr);
    ~NativeGif();
    NativeGif(const NativeGif&) = delete;
    NativeGif& operator=(const NativeGif&) = delete;

    // The whole-packet entry (also the equivalence tool's): true = consumed.
    bool Intercept(GifPath path, const uint8_t* data, size_t size);

    struct Counters {
        uint64_t packets_served = 0;
        uint64_t packets_declined = 0;
        uint64_t tags = 0;
        // The dedicated row bodies, by the census rank of their template (nat-gif.md):
        uint64_t rows_4f12 = 0;      // {ST RGBAQ NOP XYZF2}      the cel programs' first template (31.8 % of the quadwords)
        uint64_t rows_41f2 = 0;      // {ST NOP RGBAQ XYZF2}      their second (30.9 %)
        uint64_t rows_5 = 0;         // {XYZ2}                    position-only strips (4.8 %)
        uint64_t rows_41 = 0;        // {RGBAQ XYZF2}             (2.7 %)
        uint64_t rows_44e = 0;       // {A+D XYZF2 XYZF2}         the ink line pairs (2.8 %)
        uint64_t rows_4fff = 0;      // {NOP NOP NOP XYZF2}       (1.5 %)
        uint64_t rows_512 = 0;       // {ST RGBAQ XYZ2}           the engine's strip3 class
        uint64_t rows_412 = 0;       // {ST RGBAQ XYZF2}
        uint64_t rows_generic = 0;   // any other row of known / A+D descriptors
        uint64_t ad_items = 0;
        uint64_t image_qw = 0;
        uint64_t declined_reglist = 0, declined_disabled = 0, declined_partial = 0, declined_ad_signal = 0,
                 declined_mixed = 0;
    };
    const Counters& counters() const { return c_; }
    // "strip3=123:ad=45:img=6" for the census detail.
    std::string Detail() const;

private:
    static bool Trampoline(void* user, GifPath path, const uint8_t* data, size_t size);
    bool Validate(const uint8_t* data, size_t size);
    void Apply(const uint8_t* data, size_t size);
    void ApplyPacked(const GifTag& tag, const uint8_t* p);
    void ApplyAd(const GifTag& tag, const uint8_t* p);
    void ApplyAdItem(const uint8_t* p);

    GifArbiter* arbiter_;
    ps2r::gs::GsState& gs_;
    GifRegs* regs_;
    Counters c_;
};

}  // namespace ps2r::title::sly2
