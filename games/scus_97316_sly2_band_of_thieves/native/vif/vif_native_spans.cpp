#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_native_spans.h"

#include <cstring>

#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_unpack_native.h"

namespace {

using ps2r::title::sly2::NativeCensus;
using ps2r::title::sly2::NativeUnit;

constexpr uint32_t kCmdNop = 0x00;
constexpr uint32_t kCmdStcycl = 0x01;
constexpr uint32_t kCmdStmod = 0x05;
constexpr uint32_t kCmdStmask = 0x20;
constexpr uint32_t kCmdStrow = 0x30;
constexpr uint32_t kCmdStcol = 0x31;
constexpr uint32_t kCmdMpg = 0x4A;
constexpr uint32_t kCmdDirect = 0x50;
constexpr uint32_t kCmdDirecthl = 0x51;

constexpr size_t kDecline = static_cast<size_t>(-1);

inline uint32_t Word(const uint8_t* data, size_t w) {
    uint32_t x;
    std::memcpy(&x, data + w * 4, 4);
    return x;
}

inline void AppendWord(std::vector<uint8_t>& out, uint32_t w) {
    uint8_t b[4];
    std::memcpy(b, &w, 4);
    out.insert(out.end(), b, b + 4);
}

// Payload bytes a code word needs after itself (engine/vif/vif.cpp Vif::BytesNeeded restated;
// 0 = none). `use_mask` for an UNPACK.
inline uint32_t BytesNeeded(const VifRegs& r, uint32_t cmd, uint32_t num, bool* use_mask) {
    *use_mask = false;
    if (VifUnpack::IsUnpackCmd(cmd)) {
        *use_mask = (cmd & 0x10) != 0;
        return VifUnpack::GetUnpackSize(cmd, num, VifRegsCl(r), VifRegsWl(r));
    }
    if (cmd == kCmdMpg) return (num == 0 ? 256 : num) * 8;
    if (cmd == kCmdStmask) return 4;
    if (cmd == kCmdStrow || cmd == kCmdStcol) return 16;
    return 0;
}

inline bool UnitIdle(const VifUnitState& u) {
    return u.state == VifState::Idle && u.latch_fill == 0 && u.word_index >= 4 && u.fifo.empty();
}

}  // namespace

VifNativeSpans::VifNativeSpans(Vif& vif, bool typed_unpack) : vif_(vif), typed_unpack_(typed_unpack) {
    vif_.SetSpanExecutor(&VifNativeSpans::Thunk, this);
}

VifNativeSpans::~VifNativeSpans() { vif_.SetSpanExecutor(nullptr, nullptr); }

bool VifNativeSpans::Thunk(void* user, int vif, const uint8_t* data, size_t size) {
    return static_cast<VifNativeSpans*>(user)->Process(vif, data, size);
}

bool VifNativeSpans::Process(int vif, const uint8_t* data, size_t size) {
    if (vif != 1 || in_process_) return false;
    const VifUnitState& u = vif_.unit(1);
    if (u.latch_fill != 0 || (size & 3) != 0) {
        declined_latch_++;
        spans_declined_++;
        NativeCensus::Instance().Fallback(NativeUnit::Vif);
        return false;
    }
    const size_t nwords = size / 4;
    const uint64_t ut = unpack_typed_, ue = unpack_engine_;
    in_process_ = true;
    bool consumed = true;
    if (u.tte_feed) {
        // The TTE quadword (always 16 bytes from the walker). A parked payload or DIRECT stream
        // consumes the quadword's words -- the pad included -- as data first (the latch path's
        // rule); the pad-skip rule applies to the commands decoded after it.
        if (nwords != 4 || u.latch_fill != 0) {
            declined_tte_busy_++;
            consumed = false;
        } else if (u.state == VifState::DirectStream) {
            bool streaming = false;
            const size_t w = vif_.ContinueDirect(1, data, nwords, &streaming);
            if (!streaming) {
                resumed_++;
                direct_++;
                ParseTte(data, w);
            }
            tte_quadwords_++;
        } else if (u.state == VifState::WaitData) {
            const size_t w = ResumePending(data, nwords);
            if (w == kDecline) {
                // Still short: every word to the latch (the engine's own buffering, no parse).
                vif_.LatchWords(1, data, nwords);
                deep_split_++;
            } else {
                ParseTte(data, w);
            }
            tte_quadwords_++;
        } else if (!UnitIdle(u)) {
            declined_tte_busy_++;
            consumed = false;
        } else {
            ParseTte(data, 0);
            tte_quadwords_++;
        }
    } else if (u.state == VifState::DirectStream) {
        bool streaming = false;
        const size_t w = vif_.ContinueDirect(1, data, nwords, &streaming);
        if (!streaming) {
            resumed_++;
            direct_++;
            Parse(data, nwords, w);
        }
    } else if (u.state == VifState::WaitData) {
        const size_t w = ResumePending(data, nwords);
        if (w == kDecline) {
            vif_.LatchWords(1, data, nwords);
            deep_split_++;
        } else {
            Parse(data, nwords, w);
        }
    } else if (!UnitIdle(u)) {
        declined_latch_++;
        consumed = false;
    } else {
        Parse(data, nwords, 0);
    }
    in_process_ = false;
    NativeCensus& census = NativeCensus::Instance();
    if (consumed) {
        spans_native_++;
        census.Served(NativeUnit::Vif);
    } else {
        spans_declined_++;
        census.Fallback(NativeUnit::Vif);
    }
    // The UNPACK detail rides the Dmac row's counters: typed / engine loops are counted per span.
    (void)ut;
    (void)ue;
    return consumed;
}

// The whole-span parse (engine/vif/vif_fast_span.cpp VifFastSpan::Process's loop restated):
// one code word at a time from `w`; a payload inside the span executes in place (an
// UNPACK's lookahead word is the next span word, or zero at a quadword edge); a payload
// or DIRECT crossing the span end is parked with the engine's own members.
void VifNativeSpans::Parse(const uint8_t* data, size_t nwords, size_t w) {
    VifRegs& r = vif_.regs(1);
    while (w < nwords) {
        const size_t p = w;
        const uint32_t code = Word(data, p);
        const bool irq = (code >> 31) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        r.code = code;
        w++;

        if (cmd == kCmdDirect || cmd == kCmdDirecthl) {
            const uint32_t qwc = (imm == 0) ? 0x10000u : imm;
            const size_t s = (p + 4) & ~static_cast<size_t>(3);  // the payload starts on the next quadword
            const size_t need_words = static_cast<size_t>(qwc) * 4;
            const bool hl = cmd == kCmdDirecthl;
            direct_qw_ += qwc;
            if (need_words <= nwords - s) {
                vif_.SubmitDirectSpan(1, data + s * 4, need_words * 4, hl, irq);
                direct_++;
                if (irq) ibit_raised_++;
                w = s + need_words;
                continue;
            }
            vif_.ParkDirect(1, qwc, hl, irq, data, nwords, s);
            parked_++;
            return;
        }

        bool use_mask = false;
        const uint32_t bytes_needed = BytesNeeded(r, cmd, num, &use_mask);
        if (bytes_needed == 0) {
            Command(code, nullptr, 0, 0);
            continue;
        }
        const size_t words = (bytes_needed + 3) / 4;
        const size_t s = w;
        if (words > nwords - s) {
            vif_.ParkPayload(1, cmd, imm, num, bytes_needed, irq, use_mask, data, nwords, s);
            parked_++;
            return;
        }
        const uint8_t phase = static_cast<uint8_t>(s & 1);
        if (VifUnpack::IsUnpackCmd(cmd)) {
            Unpack(code, data + s * 4, words, ((s + words) & 3) == 0, phase);
        } else {
            Command(code, data + s * 4, words, phase);
        }
        w = s + words;
    }
}

// The TTE quadword: the tag's two upper words then two zero pad words, decoded by the
// engine's word latch (engine/vif/vif.cpp ProcessFifo) with `tte_feed` set: an UNPACK /
// MPG payload that does not fit in the words left of this quadword starts on the next
// span (the pad is skipped: word_index := 4, the command parked); every other payload
// is taken from the words that follow, the pad included, and parked when they run out.
void VifNativeSpans::ParseTte(const uint8_t* data, size_t w) {
    VifRegs& r = vif_.regs(1);
    while (w < 4) {
        const uint32_t code = Word(data, w);
        const bool irq = (code >> 31) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        r.code = code;
        w++;
        if (cmd == kCmdDirect || cmd == kCmdDirecthl) {
            // The payload starts on the next quadword: the stream opens with nothing buffered.
            const uint32_t qwc = (imm == 0) ? 0x10000u : imm;
            direct_qw_ += qwc;
            vif_.ParkDirect(1, qwc, cmd == kCmdDirecthl, irq, data, 4, 4);
            parked_++;
            return;
        }
        bool use_mask = false;
        const uint32_t bytes_needed = BytesNeeded(r, cmd, num, &use_mask);
        if (bytes_needed == 0) {
            Command(code, nullptr, 0, 0);
            continue;
        }
        const size_t words = (bytes_needed + 3) / 4;
        const bool force_align = VifUnpack::IsUnpackCmd(cmd) || cmd == kCmdMpg;
        if (force_align && words > 4 - w) {
            // Pad skipped: parked with nothing held (the latch path's word_index := 4).
            vif_.ParkPayload(1, cmd, imm, num, bytes_needed, irq, use_mask, data, 4, 4);
            parked_++;
            return;
        }
        if (words > 4 - w) {
            // STMASK / STROW / STCOL: the pad words are payload; the rest comes with the next span.
            vif_.ParkPayload(1, cmd, imm, num, bytes_needed, irq, use_mask, data, 4, w);
            parked_++;
            return;
        }
        const size_t s = w;
        const uint8_t phase = static_cast<uint8_t>(s & 1);
        if (VifUnpack::IsUnpackCmd(cmd)) {
            Unpack(code, data + s * 4, words, ((s + words) & 3) == 0, phase);
        } else {
            Command(code, data + s * 4, words, phase);
        }
        w = s + words;
    }
}

// A span arriving while a command waits for its payload (engine/vif/vif.cpp ProcessFifo's
// WaitData branch restated): the payload is the held quadword's remaining words, then the
// FIFO's quadwords (the previous span's tail), then this span's words; its phase is the held
// cursor's parity (0 when nothing is held); an UNPACK's lookahead word is the span word after
// the payload, or zero when the payload ends on a quadword edge. The unit then goes idle and
// the parse resumes at the next span word. Declined (nothing touched) when the span does not
// complete the payload either (the engine latches it: not a shape Sly 2's chains produce).
size_t VifNativeSpans::ResumePending(const uint8_t* data, size_t nwords) {
    const VifUnitState& u = vif_.unit(1);
    const uint32_t cmd = u.pending_cmd;
    const uint32_t imm = u.pending_imm;
    const uint32_t num = u.pending_num;
    const bool irq = u.pending_irq;
    const bool use_mask = u.pending_use_mask;
    const size_t words_needed = (u.pending_bytes + 3) / 4;
    size_t head = 0;
    if (u.word_index < 4) head += static_cast<size_t>(4 - u.word_index);
    head += u.fifo.size() * 4;
    if (words_needed <= head) return kDecline;  // the latch path would have completed it already
    const size_t from_span = words_needed - head;
    if (from_span > nwords) return kDecline;
    const uint8_t phase = static_cast<uint8_t>(u.word_index < 4 ? (u.word_index & 1) : 0);

    scratch_.clear();
    scratch_.reserve(words_needed * 4 + 4);
    if (u.word_index < 4) {
        for (int i = u.word_index; i < 4; ++i) AppendWord(scratch_, u.current_qw[static_cast<size_t>(i)]);
    }
    for (const auto& qw : u.fifo) {
        for (uint32_t x : qw) AppendWord(scratch_, x);
    }
    scratch_.insert(scratch_.end(), data, data + from_span * 4);
    const bool edge = (from_span & 3) == 0;
    const uint32_t code = (irq ? 0x80000000u : 0u) | (cmd << 24) | (num << 16) | imm;
    // The parked command's code word is already in CODE (written at its decode). STAT while the
    // command executes (what an inline i-bit handler reads): VPS = transfer, FQC = the quadwords
    // the latch held when the payload completed (none on the whole-span parser's edge-parked
    // form), as engine/vif/vif.cpp ProcessFifo / vif_fast_span.cpp set it before executing.
    {
        VifRegs& r = vif_.regs(1);
        uint32_t fqc = 0;
        if (!(u.word_index >= 4 && u.fifo.empty())) {
            const size_t quads = u.fifo.size() + (from_span + 3) / 4;
            fqc = static_cast<uint32_t>(quads / 16);
            if (fqc > VifStat::kFqcMask) fqc = VifStat::kFqcMask;
        }
        r.stat = (r.stat & ~(VifStat::kVpsMask | (VifStat::kFqcMask << VifStat::kFqcShift))) | VifStat::kVpsTransfer |
                 (fqc << VifStat::kFqcShift);
    }
    if (VifUnpack::IsUnpackCmd(cmd)) {
        // Unpack() appends the lookahead itself from `edge` and the span; hand it a copy whose
        // "next span word" is data[from_span] -- assembled here since the payload is not in place.
        if (!edge) AppendWord(scratch_, Word(data, from_span));
        else AppendWord(scratch_, 0);
        const size_t words = words_needed;
        VifRegs& r = vif_.regs(1);
        uint32_t cl = VifRegsCl(r), wl = VifRegsWl(r);
        if (cl == 0) cl = 1;
        if (wl == 0) wl = 1;
        vif_.SetUnpackPhase(1, phase);
        VifUnpackNative::Command c;
        c.format = cmd & 0xF;
        c.masked = use_mask;
        c.usn = (imm & 0x4000) != 0;
        c.addr_qw = imm & 0x3FFu;
        if (imm & 0x8000) c.addr_qw = (c.addr_qw + r.tops) & 0x3FFu;
        c.n = num == 0 ? 256 : num;
        c.cl = cl;
        c.wl = wl;
        c.mode = r.mode;
        c.phase = phase;
        c.src = scratch_.data();
        c.src_bytes = scratch_.size();
        // The command (and its i bit) run with the unit still in WaitData, then it goes idle:
        // the latch path's order (ExecuteCommand, then state := Idle / UpdateStat).
        if (typed_unpack_ && VifUnpackNative::Supports(c)) {
            VifUnpackNative::Run(c, r, vif_.CtxFor(1).state().data);
            unpack_typed_++;
            if (irq) {
                ibit_raised_++;
                vif_.RaiseIBit(1);
            }
        } else {
            vif_.ExecuteCommand(1, cmd, imm, num, scratch_.data(), scratch_.size(), irq, use_mask);
            unpack_engine_++;
            if (irq) ibit_raised_++;
        }
        (void)words;
    } else {
        Command(code, scratch_.data(), words_needed, phase);
    }
    vif_.CompletePending(1);
    resumed_++;
    return from_span;
}

// One UNPACK whose `words` payload words are at `payload` IN PLACE in the span: the
// title's typed loop where one exists, else the engine's executor with the payload as
// the engine's parser hands it (the data words + one lookahead word: the next span word
// when the payload ends mid-quadword, zero at an edge -- PayloadInPlace's rule).
void VifNativeSpans::Unpack(uint32_t code, const uint8_t* payload, size_t words, bool edge, uint8_t phase) {
    const bool irq = (code >> 31) != 0;
    const uint32_t cmd = (code >> 24) & 0x7F;
    const uint32_t num = (code >> 16) & 0xFF;
    const uint32_t imm = code & 0xFFFF;
    const bool use_mask = (cmd & 0x10) != 0;
    VifRegs& r = vif_.regs(1);
    uint32_t cl = VifRegsCl(r), wl = VifRegsWl(r);
    if (cl == 0) cl = 1;
    if (wl == 0) wl = 1;
    vif_.SetUnpackPhase(1, phase);
    VifUnpackNative::Command c;
    c.format = cmd & 0xF;
    c.masked = use_mask;
    c.usn = (imm & 0x4000) != 0;
    c.addr_qw = imm & 0x3FFu;
    if (imm & 0x8000) c.addr_qw = (c.addr_qw + r.tops) & 0x3FFu;
    c.n = num == 0 ? 256 : num;
    c.cl = cl;
    c.wl = wl;
    c.mode = r.mode;
    c.phase = phase;
    c.src = payload;
    c.src_bytes = words * 4;
    const bool typed = typed_unpack_ && VifUnpackNative::Supports(c);
    if (!typed || VifUnpackNative::NeedsLookahead(c.format)) {
        if (!edge) {
            c.src_bytes += 4;  // the lookahead word is the next span word, in place
        } else {
            scratch_.assign(payload, payload + words * 4);
            scratch_.insert(scratch_.end(), 4, 0);
            c.src = scratch_.data();
            c.src_bytes = scratch_.size();
        }
    }
    if (typed) {
        VifUnpackNative::Run(c, r, vif_.CtxFor(1).state().data);
        unpack_typed_++;
        if (irq) {
            ibit_raised_++;
            vif_.RaiseIBit(1);
        }
    } else {
        vif_.ExecuteCommand(1, cmd, imm, num, c.src, c.src_bytes, irq, use_mask);
        unpack_engine_++;
        if (irq) ibit_raised_++;
    }
}

// One non-UNPACK command with its payload (if any) at hand (engine/vif/vif.cpp
// ExecuteCommand's cases restated for the register writes and MPG; MSCAL / MSCALF /
// MSCNT / FLUSH* / OFFSET / BASE / ITOP / MARK / MSKPATH3 and the undefined codes
// through the engine's executor). The i bit follows the command, as there.
void VifNativeSpans::Command(uint32_t code, const uint8_t* payload, size_t words, uint8_t phase) {
    const bool irq = (code >> 31) != 0;
    const uint32_t cmd = (code >> 24) & 0x7F;
    const uint32_t num = (code >> 16) & 0xFF;
    const uint32_t imm = code & 0xFFFF;
    VifRegs& r = vif_.regs(1);
    if (words) vif_.SetUnpackPhase(1, phase);  // the whole-span parser's phase latch, for every payload
    switch (cmd) {
        case kCmdNop:
            break;
        case kCmdStcycl:
            r.cycle = imm;
            break;
        case kCmdStmod:
            r.mode = imm & 3u;
            break;
        case kCmdStmask:
            if (words >= 1) std::memcpy(&r.mask, payload, 4);
            break;
        case kCmdStrow:
            if (words >= 4) std::memcpy(r.row, payload, 16);
            break;
        case kCmdStcol:
            if (words >= 4) std::memcpy(r.col, payload, 16);
            break;
        case kCmdMpg:
            vif_.CtxFor(1).WriteMicroMem(imm * 8, payload, words * 4);
            mpg_++;
            mpg_bytes_ += words * 4;
            break;
        default:
            // MSCAL / MSCALF / MSCNT / FLUSH* / OFFSET / BASE / ITOP / MARK / MSKPATH3, the undefined
            // codes, and an UNPACK of undefined format (no payload: the engine ignores it).
            vif_.ExecuteCommand(1, cmd, imm, num, payload, words * 4, irq, VifUnpack::IsUnpackCmd(cmd) && (cmd & 0x10) != 0);
            codes_engine_++;
            if (irq) ibit_raised_++;
            return;
    }
    codes_native_++;
    if (irq) {
        ibit_raised_++;
        vif_.RaiseIBit(1);
    }
}

std::string VifNativeSpans::Row() const {
    return "spans=" + std::to_string(spans_native_) + "/" + std::to_string(spans_declined_) + ":declined=" +
           std::to_string(declined_tte_busy_) + "/" + std::to_string(declined_latch_) + ":deep_split=" + std::to_string(deep_split_) +
           ":tte_qw=" + std::to_string(tte_quadwords_) + ":unpack=" + std::to_string(unpack_typed_) + "/" +
           std::to_string(unpack_engine_) + ":codes=" + std::to_string(codes_native_) + "/" + std::to_string(codes_engine_) +
           ":mpg=" + std::to_string(mpg_) + "/" + std::to_string(mpg_bytes_) + ":direct=" + std::to_string(direct_) + "/" +
           std::to_string(direct_qw_) + ":parked=" + std::to_string(parked_) + ":resumed=" + std::to_string(resumed_) +
           ":ibit=" + std::to_string(ibit_raised_);
}
