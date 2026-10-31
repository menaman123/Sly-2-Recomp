#include "games/scus_97316_sly2_band_of_thieves/native/gif/native_gif.h"

#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gs/state/gs_state.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"

namespace ps2r::title::sly2 {

namespace {

constexpr size_t kQw = 16;

inline uint64_t Lo(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
inline uint64_t Hi(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p + 8, 8);
    return v;
}

constexpr uint8_t kAd = 0x0E;

// The payload items of a tag: PACKED one quadword per descriptor per loop, REGLIST one 64-bit
// value per descriptor per loop (two per quadword), IMAGE one quadword per loop.
uint64_t PayloadItems(const GifTag& tag) {
    return tag.flg == GifFormat::Image ? tag.nloop : static_cast<uint64_t>(tag.nloop) * tag.nregs;
}
// ... and the quadwords they occupy (FLG 3 is declined before this).
uint64_t PayloadQw(const GifTag& tag) {
    const uint64_t items = PayloadItems(tag);
    return tag.flg == GifFormat::Reglist ? (items + 1) / 2 : items;
}

// Template ids of the dedicated row bodies: the descriptor word's low NREG nibbles
// (regs[0] in the low nibble), the census's tag_hi column.
constexpr uint64_t kTpl4f12 = 0x4f12;   // ST RGBAQ NOP XYZF2
constexpr uint64_t kTpl41f2 = 0x41f2;   // ST NOP RGBAQ XYZF2
constexpr uint64_t kTpl5 = 0x5;         // XYZ2
constexpr uint64_t kTpl41 = 0x41;       // RGBAQ XYZF2
constexpr uint64_t kTpl44e = 0x44e;     // A+D XYZF2 XYZF2
constexpr uint64_t kTpl4fff = 0x4fff;   // NOP NOP NOP XYZF2
constexpr uint64_t kTpl512 = 0x512;     // ST RGBAQ XYZ2
constexpr uint64_t kTpl412 = 0x412;     // ST RGBAQ XYZF2

bool SignalFinishLabel(uint8_t addr) { return addr >= 0x60 && addr <= 0x62; }

}  // namespace

NativeGif::NativeGif(GifArbiter* arbiter, ps2r::gs::GsState& gs, GifRegs* regs, GifParser* parser)
    : arbiter_(arbiter), gs_(gs), regs_(regs), parser_(parser) {
    if (arbiter_) arbiter_->SetPacketInterceptor(&NativeGif::Trampoline, this);
}

NativeGif::~NativeGif() {
    if (arbiter_) arbiter_->SetPacketInterceptor(nullptr, nullptr);
}

bool NativeGif::Trampoline(void* user, GifPath path, const uint8_t* data, size_t size) {
    return static_cast<NativeGif*>(user)->Intercept(path, data, size);
}

bool NativeGif::Intercept(GifPath path, const uint8_t* data, size_t size) {
    // A tag in transfer in the register file: this builder's own (left open at the previous
    // packet's cut) is continued here; the parser's own is the parser's.
    bool cont = false;
    if (regs_ && regs_->Transfer().in_transfer) {
        if (!open_) {
            ++c_.declined_parser_tag;
            ++c_.packets_declined;
            NativeCensus::Instance().Fallback(NativeUnit::Gif);
            return false;
        }
        if (path == GifPath::Path3) {
            // The parser's rule (GifParser::ReceiveData): a PATH3 packet arriving while a tag
            // is in transfer abandons that tag (each PATH3 chain is a self-contained stream).
            GifTransferState& t = regs_->Transfer();
            t.in_transfer = false;
            t.loop = 0;
            t.reg = 0;
            open_ = false;
            ++c_.abandoned_tags;
        } else {
            cont = true;
        }
    }
    if (!Validate(data, size, cont)) {
        ++c_.packets_declined;
        NativeCensus::Instance().Fallback(NativeUnit::Gif);
        return false;
    }
    Apply(path, data, size, cont);
    ++c_.packets_served;
    NativeCensus::Instance().Served(NativeUnit::Gif);
    return true;
}

// Every tag of the packet must be one the builder reproduces exactly, and the packet
// must end on a quadword boundary (the parser would otherwise buffer a tail). A tag
// whose payload runs past the packet end is left open at the cut (Apply sets the
// transfer view; the next packet continues it here) -- needs the register file.
// `cont` = the packet opens with the continuation of the tag in transfer.
bool NativeGif::Validate(const uint8_t* data, size_t size, bool cont) {
    if ((size % kQw) != 0) {
        ++c_.declined_partial;
        return false;
    }
    size_t o = 0;
    if (cont) {
        const GifTransferState& t = regs_->Transfer();
        const uint64_t done = static_cast<uint64_t>(t.loop) * t.tag.nregs + t.reg;
        const uint64_t total = PayloadItems(t.tag);
        if (done >= total) return false;   // not a state this builder leaves
        const uint64_t need_qw = t.tag.flg == GifFormat::Reglist ? (total - done + 1) / 2 : total - done;
        if (need_qw > size / kQw) return true;  // still open after this packet
        o = static_cast<size_t>(need_qw) * kQw;
    }
    while (o < size) {
        GifTag tag;
        tag.Parse(Lo(data + o), Hi(data + o));
        o += kQw;
        if (tag.flg == GifFormat::Disabled) {
            ++c_.declined_disabled;
            return false;
        }
        bool has_ad = false;
        if (tag.flg == GifFormat::Packed) {
            for (unsigned i = 0; i < tag.nregs; ++i) {
                if (tag.regs[i] == kAd) has_ad = true;
            }
        }
        if (tag.nloop == 0) {
            // NLOOP 0 without EOP leaves the parser's tag in transfer: an A+D set
            // then eats the next quadword, an IMAGE tag touches the transfer engine
            // with an empty span. Only the all-known PACKED form consumes nothing.
            if (!tag.eop && !(tag.flg == GifFormat::Packed && !has_ad)) {
                ++c_.declined_nloop0;
                return false;
            }
            continue;
        }
        const uint64_t qw = PayloadQw(tag);
        if (qw > (size - o) / kQw) {
            // The payload continues in the next packet (needs the register file to say so).
            if (!regs_) {
                ++c_.declined_partial;
                return false;
            }
            return true;
        }
        o += qw * kQw;
    }
    return true;
}

// `items` of `tag`'s payload at `p`, starting at item index `first` (the descriptor cursor
// first % nregs; a quadword edge for REGLIST since cuts are whole quadwords).
void NativeGif::ApplyItems(const GifTag& tag, const uint8_t* p, uint64_t first, uint64_t items) {
    if (tag.flg == GifFormat::Image) {
        gs_.WriteImage(p, static_cast<size_t>(items) * kQw);
        c_.image_qw += items;
    } else if (tag.flg == GifFormat::Reglist) {
        ApplyReglist(tag, p, first, items);
    } else if (tag.nregs == 1 && tag.regs[0] == kAd) {
        ApplyAd(p, items);
    } else {
        ApplyPacked(tag, p, first, items);
    }
}

void NativeGif::Apply(GifPath path, const uint8_t* data, size_t size, bool cont) {
    if (parser_) parser_->SetActivePath(path);
    size_t o = 0;
    GifTag tag{};
    bool open = false;        // the last tag's payload continues in the next packet
    uint64_t open_items = 0;  // its items applied so far (this and earlier packets)
    if (cont) {
        // The tag in transfer (ours): its remaining payload, or as much as this packet holds.
        const GifTransferState& t = regs_->Transfer();
        tag = t.tag;
        const uint64_t done = static_cast<uint64_t>(t.loop) * tag.nregs + t.reg;
        const uint64_t total = PayloadItems(tag);
        const uint64_t avail_qw = size / kQw;
        const uint64_t need_qw = tag.flg == GifFormat::Reglist ? (total - done + 1) / 2 : total - done;
        uint64_t items = total - done;
        if (need_qw > avail_qw) {
            open = true;
            items = tag.flg == GifFormat::Reglist ? avail_qw * 2 : avail_qw;
            c_.partial_qw += avail_qw;
        } else {
            o = static_cast<size_t>(need_qw) * kQw;
            c_.partial_qw += need_qw;
        }
        ApplyItems(tag, data, done, items);
        open_items = done + items;
        ++c_.continued_packets;
        if (!open && tag.eop) gs_.KickBoundary();
    }
    while (!open && o < size) {
        tag.Parse(Lo(data + o), Hi(data + o));
        o += kQw;
        ++c_.tags;
        // BeginTag: Q = 1.0; PRE: PRIM (11 bits) as a register write.
        gs_.GifTagRead();
        if (tag.pre) gs_.WriteRegister(0x00, tag.prim);
        if (tag.nloop == 0) {
            if (tag.eop) gs_.KickBoundary();
            continue;
        }
        const uint8_t* p = data + o;
        const uint64_t avail_qw = (size - o) / kQw;
        const uint64_t qw = PayloadQw(tag);
        uint64_t items = PayloadItems(tag);
        if (qw > avail_qw) {
            // The cut: whole quadwords only (the packet is a multiple of 16 bytes), two
            // REGLIST values per quadword strictly before the final one.
            open = true;
            items = tag.flg == GifFormat::Reglist ? avail_qw * 2 : avail_qw;
            open_items = items;
            ++c_.partial_tags;
            c_.partial_qw += avail_qw;
        }
        ApplyItems(tag, p, 0, items);
        if (open) break;
        o += qw * kQw;
        if (tag.eop) gs_.KickBoundary();
    }
    // The register file's transfer view as the parser leaves it: after a complete packet
    // the last tag, its loops done, the descriptor cursor at 0, nothing in transfer
    // (GIF_TAG0-3 / GIF_CNT / GIF_STAT.OPH read back from it); after a cut the tag in
    // transfer with the cursor at the cut, re-classified for the parser's view.
    if (regs_ && (size >= kQw || cont)) {
        GifTransferState& t = regs_->Transfer();
        t.tag = tag;
        if (open) {
            t.loop = static_cast<uint32_t>(open_items / tag.nregs);
            t.reg = static_cast<uint32_t>(open_items % tag.nregs);
            t.in_transfer = true;
            if (parser_) parser_->AdoptTransfer();
        } else {
            t.loop = tag.nloop;
            t.reg = 0;
            t.in_transfer = false;
        }
    }
    open_ = open;
}

// PACKED, every descriptor in the decoded set or A+D: the dedicated row body
// for the templates the title kicks most, the generic row for the rest.
void NativeGif::ApplyPacked(const GifTag& tag, const uint8_t* p, uint64_t first, uint64_t items) {
    ps2r::gs::GsRegisters& regs = gs_.Regs();
    ps2r::gs::VertexKick& kick = gs_.Kick();
    // The dedicated row bodies take whole rows from a row start; a cut inside a row (a split
    // stream) falls to the generic walk, which applies exactly `items` descriptors from the cursor.
    const uint32_t n = static_cast<uint32_t>(items / tag.nregs);
    const bool whole_rows = first % tag.nregs == 0 && items % tag.nregs == 0;
    uint64_t tpl = 0;
    for (unsigned i = 0; i < tag.nregs && i < 4; ++i) tpl |= static_cast<uint64_t>(tag.regs[i]) << (4 * i);
    if (tag.nregs <= 4 && whole_rows) {
        switch (tpl) {
            case kTpl4f12:   // ST RGBAQ NOP XYZF2
                for (uint32_t i = 0; i < n; ++i, p += 4 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyzf(true, Lo(p + 3 * kQw), Hi(p + 3 * kQw));
                }
                c_.rows_4f12 += n;
                return;
            case kTpl41f2:   // ST NOP RGBAQ XYZF2
                for (uint32_t i = 0; i < n; ++i, p += 4 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                    kick.PackedXyzf(true, Lo(p + 3 * kQw), Hi(p + 3 * kQw));
                }
                c_.rows_41f2 += n;
                return;
            case kTpl5:      // XYZ2
                if (tag.nregs != 1) break;
                for (uint32_t i = 0; i < n; ++i, p += kQw) kick.PackedXyz(true, Lo(p), Hi(p));
                c_.rows_5 += n;
                return;
            case kTpl41:     // RGBAQ XYZF2
                if (tag.nregs != 2) break;
                for (uint32_t i = 0; i < n; ++i, p += 2 * kQw) {
                    regs.PackedRgbaq(Lo(p), Hi(p));
                    kick.PackedXyzf(true, Lo(p + kQw), Hi(p + kQw));
                }
                c_.rows_41 += n;
                return;
            case kTpl44e:    // A+D XYZF2 XYZF2
                if (tag.nregs != 3) break;
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    ApplyAdItem(p);
                    kick.PackedXyzf(true, Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyzf(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_44e += n;
                c_.ad_items += n;
                return;
            case kTpl4fff:   // NOP NOP NOP XYZF2
                for (uint32_t i = 0; i < n; ++i, p += 4 * kQw) kick.PackedXyzf(true, Lo(p + 3 * kQw), Hi(p + 3 * kQw));
                c_.rows_4fff += n;
                return;
            case kTpl512:    // ST RGBAQ XYZ2
                if (tag.nregs != 3) break;
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyz(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_512 += n;
                return;
            case kTpl412:    // ST RGBAQ XYZF2
                if (tag.nregs != 3) break;
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyzf(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_412 += n;
                return;
            default:
                break;
        }
    }
    // The generic walk: the same member calls the parser's dispatch ends in, one descriptor
    // per quadword, `items` of them (the cursor wraps to the next loop after the last descriptor).
    unsigned j = static_cast<unsigned>(first % tag.nregs);
    for (uint64_t i = 0; i < items; ++i, p += kQw) {
        {
            const uint64_t lo = Lo(p);
            switch (static_cast<GsReg>(tag.regs[j])) {
                case GsReg::Prim: gs_.WriteRegister(0x00, lo & 0x7FFu); break;
                case GsReg::Rgbaq: regs.PackedRgbaq(lo, Hi(p)); break;
                case GsReg::St: regs.PackedSt(lo, Hi(p)); break;
                case GsReg::Uv: regs.PackedUv(lo); break;
                case GsReg::Fog: regs.PackedFog(Hi(p)); break;
                case GsReg::Xyzf2: kick.PackedXyzf(true, lo, Hi(p)); break;
                case GsReg::Xyz2: kick.PackedXyz(true, lo, Hi(p)); break;
                case GsReg::Xyzf3: kick.PackedXyzf(false, lo, Hi(p)); break;
                case GsReg::Xyz3: kick.PackedXyz(false, lo, Hi(p)); break;
                case GsReg::Ad: ApplyAdItem(p); ++c_.ad_items; break;
                case GsReg::Nop: break;   // consumed, ignored
                default:
                    // The register-as-data form: the low 64 bits to the descriptor's register
                    // (DispatchPackedQuadword's default -> DispatchGifRegister; the GsState façade
                    // applies the same set and drops the same reserved addresses).
                    gs_.WriteRegister(tag.regs[j], lo);
                    break;
            }
        }
        if (++j >= tag.nregs) j = 0;
    }
    c_.rows_generic += n;
}

// REGLIST: two 64-bit register values per quadword, low half first; the high half of
// the final quadword is padding when the item count is odd (ConsumeReglist restated;
// DispatchGifRegister = GsState::WriteRegister for every descriptor, PRIM masked, the
// 64-bit vertex forms kicked, A+D-as-data / NOP / reserved dropped).
void NativeGif::ApplyReglist(const GifTag& tag, const uint8_t* p, uint64_t first, uint64_t items) {
    unsigned j = static_cast<unsigned>(first % tag.nregs);
    for (uint64_t i = 0; i < items; ++i) {
        const uint64_t v = (i & 1) ? Hi(p) : Lo(p);
        gs_.WriteRegister(tag.regs[j], v);
        if (++j >= tag.nregs) j = 0;
        if (i & 1) p += kQw;
    }
    c_.reglist_items += items;
}

// PACKED A+D: address in the high half's low byte, value in the low half. The
// façade's WriteRegister is the parser's DispatchGifRegister for every address
// but SIGNAL / FINISH / LABEL (declined at validation): PRIM masked, the 64-bit
// vertex forms kicked, the drop set (0x0B 0x0E 0x0F 0x1C, undefined) a no-op.
void NativeGif::ApplyAd(const uint8_t* p, uint64_t items) {
    for (uint64_t i = 0; i < items; ++i, p += kQw) ApplyAdItem(p);
    c_.ad_items += items;
}

void NativeGif::ApplyAdItem(const uint8_t* p) {
    const uint8_t addr = static_cast<uint8_t>(Hi(p) & 0xFF);
    if (SignalFinishLabel(addr)) ++c_.ad_signal;
    gs_.WriteRegister(addr, Lo(p));
}

std::string NativeGif::Detail() const {
    char buf[480];
    std::snprintf(buf, sizeof buf,
                 "tags=%llu:r4f12=%llu:r41f2=%llu:r5=%llu:r41=%llu:r44e=%llu:r4fff=%llu:r512=%llu:r412=%llu:generic=%llu:ad=%llu:"
                 "img_qw=%llu:reglist=%llu:ad_signal=%llu:partial=%llu/%llu:continued=%llu:abandoned=%llu:declined_disabled=%llu:partial_unit=%llu:"
                 "nloop0=%llu:parser_tag=%llu",
                 static_cast<unsigned long long>(c_.tags), static_cast<unsigned long long>(c_.rows_4f12),
                 static_cast<unsigned long long>(c_.rows_41f2), static_cast<unsigned long long>(c_.rows_5),
                 static_cast<unsigned long long>(c_.rows_41), static_cast<unsigned long long>(c_.rows_44e),
                 static_cast<unsigned long long>(c_.rows_4fff), static_cast<unsigned long long>(c_.rows_512),
                 static_cast<unsigned long long>(c_.rows_412), static_cast<unsigned long long>(c_.rows_generic),
                 static_cast<unsigned long long>(c_.ad_items), static_cast<unsigned long long>(c_.image_qw),
                 static_cast<unsigned long long>(c_.reglist_items), static_cast<unsigned long long>(c_.ad_signal),
                 static_cast<unsigned long long>(c_.partial_tags), static_cast<unsigned long long>(c_.partial_qw),
                 static_cast<unsigned long long>(c_.continued_packets), static_cast<unsigned long long>(c_.abandoned_tags),
                 static_cast<unsigned long long>(c_.declined_disabled), static_cast<unsigned long long>(c_.declined_partial),
                 static_cast<unsigned long long>(c_.declined_nloop0), static_cast<unsigned long long>(c_.declined_parser_tag));
    return buf;
}

}  // namespace ps2r::title::sly2
