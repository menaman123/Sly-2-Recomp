#include "games/scus_97316_sly2_band_of_thieves/native/gif/native_gif.h"

#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
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

enum class TagKind : uint8_t { PackedKnown, PackedAd, Image, Empty, Declined };

constexpr uint8_t kAd = 0x0E;

bool KnownDescriptor(uint8_t r) {
    switch (static_cast<GsReg>(r)) {
        case GsReg::Prim: case GsReg::Rgbaq: case GsReg::St: case GsReg::Uv: case GsReg::Xyzf2: case GsReg::Xyz2:
        case GsReg::Fog: case GsReg::Xyzf3: case GsReg::Xyz3: case GsReg::Nop:
            return true;
        default:
            return false;
    }
}

// The payload quadwords of a PACKED / IMAGE tag (REGLIST and FLG 3 are declined before this).
uint64_t PayloadQw(const GifTag& tag) {
    return tag.flg == GifFormat::Image ? tag.nloop : static_cast<uint64_t>(tag.nloop) * tag.nregs;
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

NativeGif::NativeGif(GifArbiter* arbiter, ps2r::gs::GsState& gs, GifRegs* regs) : arbiter_(arbiter), gs_(gs), regs_(regs) {
    if (arbiter_) arbiter_->SetPacketInterceptor(&NativeGif::Trampoline, this);
}

NativeGif::~NativeGif() {
    if (arbiter_) arbiter_->SetPacketInterceptor(nullptr, nullptr);
}

bool NativeGif::Trampoline(void* user, GifPath path, const uint8_t* data, size_t size) {
    return static_cast<NativeGif*>(user)->Intercept(path, data, size);
}

bool NativeGif::Intercept(GifPath /*path*/, const uint8_t* data, size_t size) {
    if (!Validate(data, size)) {
        ++c_.packets_declined;
        NativeCensus::Instance().Fallback(NativeUnit::Gif);
        return false;
    }
    Apply(data, size);
    ++c_.packets_served;
    NativeCensus::Instance().Served(NativeUnit::Gif);
    return true;
}

// Every tag of the packet must be one the builder reproduces exactly, and the
// packet must end on a tag boundary (the parser would otherwise buffer a tail or
// keep a transfer open across packets).
bool NativeGif::Validate(const uint8_t* data, size_t size) {
    size_t o = 0;
    while (o < size) {
        if (size - o < kQw) {
            ++c_.declined_partial;
            return false;
        }
        GifTag tag;
        tag.Parse(Lo(data + o), Hi(data + o));
        o += kQw;
        if (tag.flg == GifFormat::Reglist) {
            ++c_.declined_reglist;
            return false;
        }
        if (tag.flg == GifFormat::Disabled) {
            ++c_.declined_disabled;
            return false;
        }
        bool has_ad = false, all_known = true;
        if (tag.flg == GifFormat::Packed) {
            for (unsigned i = 0; i < tag.nregs; ++i) {
                const uint8_t r = tag.regs[i];
                if (r == kAd) has_ad = true;
                else if (!KnownDescriptor(r)) all_known = false;
            }
            if (!all_known) {   // a descriptor outside the decoded set (the register-as-data form)
                ++c_.declined_mixed;
                return false;
            }
        }
        if (tag.nloop == 0) {
            // NLOOP 0 without EOP leaves the parser's tag in transfer: an A+D set
            // then eats the next quadword, an IMAGE tag touches the transfer engine
            // with an empty span. Only the all-known PACKED form consumes nothing.
            if (!tag.eop && !(tag.flg == GifFormat::Packed && !has_ad)) {
                ++c_.declined_partial;
                return false;
            }
            continue;
        }
        const uint64_t qw = PayloadQw(tag);
        if (qw > (size - o) / kQw) {
            ++c_.declined_partial;
            return false;
        }
        if (has_ad) {
            // SIGNAL / FINISH / LABEL stay with the parser (its active path feeds the IRQ census).
            for (uint64_t i = 0; i < qw; ++i) {
                if (tag.regs[i % tag.nregs] != kAd) continue;
                if (SignalFinishLabel(data[o + i * kQw + 8])) {
                    ++c_.declined_ad_signal;
                    return false;
                }
            }
        }
        o += qw * kQw;
    }
    return true;
}

void NativeGif::Apply(const uint8_t* data, size_t size) {
    size_t o = 0;
    GifTag tag{};
    while (o < size) {
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
        if (tag.flg == GifFormat::Image) {
            gs_.WriteImage(p, static_cast<size_t>(tag.nloop) * kQw);
            c_.image_qw += tag.nloop;
        } else if (tag.nregs == 1 && tag.regs[0] == kAd) {
            ApplyAd(tag, p);
        } else {
            ApplyPacked(tag, p);
        }
        o += PayloadQw(tag) * kQw;
        if (tag.eop) gs_.KickBoundary();
    }
    // The register file's transfer view as the parser leaves it after a complete
    // packet: the last tag, its loops done, the descriptor cursor at 0, nothing in
    // transfer (GIF_TAG0-3 / GIF_CNT / GIF_STAT.OPH read back from it).
    if (regs_ && size >= kQw) {
        GifTransferState& t = regs_->Transfer();
        t.tag = tag;
        t.loop = tag.nloop;
        t.reg = 0;
        t.in_transfer = false;
    }
}

// PACKED, every descriptor in the decoded set or A+D: the dedicated row body
// for the templates the title kicks most, the generic row for the rest.
void NativeGif::ApplyPacked(const GifTag& tag, const uint8_t* p) {
    ps2r::gs::GsRegisters& regs = gs_.Regs();
    ps2r::gs::VertexKick& kick = gs_.Kick();
    const uint32_t n = tag.nloop;
    uint64_t tpl = 0;
    for (unsigned i = 0; i < tag.nregs && i < 4; ++i) tpl |= static_cast<uint64_t>(tag.regs[i]) << (4 * i);
    if (tag.nregs <= 4) {
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
    // The generic row: the same member calls the parser's dispatch ends in.
    for (uint32_t i = 0; i < n; ++i) {
        for (unsigned j = 0; j < tag.nregs; ++j, p += kQw) {
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
                default: break;   // NOP: consumed, ignored
            }
        }
    }
    c_.rows_generic += n;
}

// PACKED A+D: address in the high half's low byte, value in the low half. The
// façade's WriteRegister is the parser's DispatchGifRegister for every address
// but SIGNAL / FINISH / LABEL (declined at validation): PRIM masked, the 64-bit
// vertex forms kicked, the drop set (0x0B 0x0E 0x0F 0x1C, undefined) a no-op.
void NativeGif::ApplyAd(const GifTag& tag, const uint8_t* p) {
    const uint64_t items = static_cast<uint64_t>(tag.nloop) * tag.nregs;
    for (uint64_t i = 0; i < items; ++i, p += kQw) ApplyAdItem(p);
    c_.ad_items += items;
}

void NativeGif::ApplyAdItem(const uint8_t* p) { gs_.WriteRegister(static_cast<uint8_t>(Hi(p) & 0xFF), Lo(p)); }

std::string NativeGif::Detail() const {
    char buf[400];
    std::snprintf(buf, sizeof buf,
                 "tags=%llu:r4f12=%llu:r41f2=%llu:r5=%llu:r41=%llu:r44e=%llu:r4fff=%llu:r512=%llu:r412=%llu:generic=%llu:ad=%llu:"
                 "img_qw=%llu:declined_reglist=%llu:disabled=%llu:partial=%llu:ad_signal=%llu:other_desc=%llu",
                 static_cast<unsigned long long>(c_.tags), static_cast<unsigned long long>(c_.rows_4f12),
                 static_cast<unsigned long long>(c_.rows_41f2), static_cast<unsigned long long>(c_.rows_5),
                 static_cast<unsigned long long>(c_.rows_41), static_cast<unsigned long long>(c_.rows_44e),
                 static_cast<unsigned long long>(c_.rows_4fff), static_cast<unsigned long long>(c_.rows_512),
                 static_cast<unsigned long long>(c_.rows_412), static_cast<unsigned long long>(c_.rows_generic),
                 static_cast<unsigned long long>(c_.ad_items), static_cast<unsigned long long>(c_.image_qw),
                 static_cast<unsigned long long>(c_.declined_reglist), static_cast<unsigned long long>(c_.declined_disabled),
                 static_cast<unsigned long long>(c_.declined_partial), static_cast<unsigned long long>(c_.declined_ad_signal),
                 static_cast<unsigned long long>(c_.declined_mixed));
    return buf;
}

}  // namespace ps2r::title::sly2
