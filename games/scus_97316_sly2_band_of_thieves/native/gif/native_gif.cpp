#include "games/scus_97316_sly2_band_of_thieves/native/gif/native_gif.h"

#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
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

// Template ids of the dedicated row bodies (the descriptor word's low nibbles).
constexpr uint64_t kTplStRgbaqXyz2 = 0x512;    // regs[0]=ST(2) regs[1]=RGBAQ(1) regs[2]=XYZ2(5)
constexpr uint64_t kTplStRgbaqXyzf2 = 0x412;   // ... XYZF2(4)
constexpr uint64_t kTplRgbaqXyz2 = 0x51;
constexpr uint64_t kTplUvRgbaqXyz2 = 0x513;    // UV(3) RGBAQ XYZ2
constexpr uint64_t kTplRgbaqUvXyz2 = 0x531;    // RGBAQ UV XYZ2

}  // namespace

NativeGif::NativeGif(GifArbiter* arbiter, ps2r::gs::GsState& gs) : arbiter_(arbiter), gs_(gs) {
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
        bool all_known = true, all_ad = true;
        if (tag.flg == GifFormat::Packed) {
            for (unsigned i = 0; i < tag.nregs; ++i) {
                const uint8_t r = tag.regs[i];
                if (!KnownDescriptor(r)) all_known = false;
                if (r != kAd) all_ad = false;
            }
            if (!all_known && !all_ad) {
                ++c_.declined_mixed;
                return false;
            }
        }
        if (tag.nloop == 0) {
            // NLOOP 0 without EOP leaves the parser's tag in transfer: an A+D set
            // then eats the next quadword, an IMAGE tag touches the transfer engine
            // with an empty span. Only the all-known PACKED form consumes nothing.
            if (!tag.eop && !(tag.flg == GifFormat::Packed && all_known)) {
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
        if (tag.flg == GifFormat::Packed && all_ad) {
            // SIGNAL / FINISH / LABEL stay with the parser (its active path feeds the IRQ census).
            for (uint64_t i = 0; i < qw; ++i) {
                const uint8_t addr = data[o + i * kQw + 8];
                if (addr >= 0x60 && addr <= 0x62) {
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
    while (o < size) {
        GifTag tag;
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
        } else if (tag.regs[0] == kAd) {
            ApplyAd(tag, p);
        } else {
            ApplyPacked(tag, p);
        }
        o += PayloadQw(tag) * kQw;
        if (tag.eop) gs_.KickBoundary();
    }
}

// PACKED, every descriptor in the decoded set: the dedicated row body for the
// templates the title kicks most, the generic row for the rest of the class.
void NativeGif::ApplyPacked(const GifTag& tag, const uint8_t* p) {
    ps2r::gs::GsRegisters& regs = gs_.Regs();
    ps2r::gs::VertexKick& kick = gs_.Kick();
    const uint32_t n = tag.nloop;
    uint64_t tpl = 0;
    for (unsigned i = 0; i < tag.nregs && i < 4; ++i) tpl |= static_cast<uint64_t>(tag.regs[i]) << (4 * i);
    if (tag.nregs == 3) {
        switch (tpl) {
            case kTplStRgbaqXyz2:
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyz(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_strip_st_rgbaq_xyz2 += n;
                return;
            case kTplStRgbaqXyzf2:
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedSt(Lo(p), Hi(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyzf(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_strip_st_rgbaq_xyzf2 += n;
                return;
            case kTplUvRgbaqXyz2:
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedUv(Lo(p));
                    regs.PackedRgbaq(Lo(p + kQw), Hi(p + kQw));
                    kick.PackedXyz(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_uv_rgbaq_xyz2 += n;
                return;
            case kTplRgbaqUvXyz2:
                for (uint32_t i = 0; i < n; ++i, p += 3 * kQw) {
                    regs.PackedRgbaq(Lo(p), Hi(p));
                    regs.PackedUv(Lo(p + kQw));
                    kick.PackedXyz(true, Lo(p + 2 * kQw), Hi(p + 2 * kQw));
                }
                c_.rows_rgbaq_uv_xyz2 += n;
                return;
            default:
                break;
        }
    } else if (tag.nregs == 2 && tpl == kTplRgbaqXyz2) {
        for (uint32_t i = 0; i < n; ++i, p += 2 * kQw) {
            regs.PackedRgbaq(Lo(p), Hi(p));
            kick.PackedXyz(true, Lo(p + kQw), Hi(p + kQw));
        }
        c_.rows_rgbaq_xyz2 += n;
        return;
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
    for (uint64_t i = 0; i < items; ++i, p += kQw) gs_.WriteRegister(static_cast<uint8_t>(Hi(p) & 0xFF), Lo(p));
    c_.ad_items += items;
}

std::string NativeGif::Detail() const {
    char buf[320];
    std::snprintf(buf, sizeof buf,
                 "tags=%llu:strip_st_rgbaq_xyz2=%llu:strip_st_rgbaq_xyzf2=%llu:rgbaq_xyz2=%llu:uv_rgbaq_xyz2=%llu:rgbaq_uv_xyz2=%llu:"
                 "generic_rows=%llu:ad=%llu:img_qw=%llu:declined_reglist=%llu:disabled=%llu:partial=%llu:ad_signal=%llu:mixed=%llu",
                 static_cast<unsigned long long>(c_.tags), static_cast<unsigned long long>(c_.rows_strip_st_rgbaq_xyz2),
                 static_cast<unsigned long long>(c_.rows_strip_st_rgbaq_xyzf2), static_cast<unsigned long long>(c_.rows_rgbaq_xyz2),
                 static_cast<unsigned long long>(c_.rows_uv_rgbaq_xyz2), static_cast<unsigned long long>(c_.rows_rgbaq_uv_xyz2),
                 static_cast<unsigned long long>(c_.rows_generic), static_cast<unsigned long long>(c_.ad_items),
                 static_cast<unsigned long long>(c_.image_qw), static_cast<unsigned long long>(c_.declined_reglist),
                 static_cast<unsigned long long>(c_.declined_disabled), static_cast<unsigned long long>(c_.declined_partial),
                 static_cast<unsigned long long>(c_.declined_ad_signal), static_cast<unsigned long long>(c_.declined_mixed));
    return buf;
}

}  // namespace ps2r::title::sly2
