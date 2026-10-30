#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_unpack_native.h"

#include <cstring>

#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"

namespace {

constexpr uint32_t kQwMask = 0x3FFu;
constexpr uint32_t kFmtV4_32 = 0xC;
constexpr uint32_t kFmtV4_5 = 0xF;

// Source elements per vector and bits per element, by the format nibble (0 = undefined).
constexpr uint8_t kElems[16] = {1, 1, 1, 0, 2, 2, 2, 0, 3, 3, 3, 0, 4, 4, 4, 1};
constexpr uint8_t kBits[16] = {32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 0, 32, 16, 8, 16};

inline uint32_t SourceBytesPerVector(uint32_t format) {
    return (kElems[format & 15] * kBits[format & 15]) / 8;
}

// One source element of `kBits` bits at `p`, sign- or zero-extended (USN).
template <uint32_t kBitsT>
inline uint32_t Element(const uint8_t* p, bool usn) {
    if constexpr (kBitsT == 8) {
        const uint8_t v = *p;
        return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v)));
    } else if constexpr (kBitsT == 16) {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return usn ? v : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
    } else {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
}

// The decoders restate engine/vif/vif_unpack.cpp's per-vector rules: S replicates;
// V2 writes {x, y, x, y} (W zero for V2-32 when the payload started on an odd word);
// V3 takes W from the element that FOLLOWS (the next vector's X, or the lookahead
// word; nothing past the buffer end), zero for V3-32 when the vector's parity
// differs from the payload's start parity; V4-5 expands RGB5A1.
template <uint32_t kElemsT, uint32_t kBitsT>
struct Decode {
    static void Read(const uint8_t* p, const uint8_t* end, bool usn, uint32_t written, uint8_t phase, uint32_t* v) {
        constexpr uint32_t kStep = kBitsT / 8;
        if constexpr (kElemsT == 1) {
            v[0] = v[1] = v[2] = v[3] = Element<kBitsT>(p, usn);
        } else if constexpr (kElemsT == 2) {
            v[0] = Element<kBitsT>(p, usn);
            v[1] = Element<kBitsT>(p + kStep, usn);
            v[2] = v[0];
            v[3] = v[1];
            if constexpr (kBitsT == 32) {
                if (phase & 1) v[3] = 0;
            }
            (void)written;
        } else if constexpr (kElemsT == 3) {
            v[0] = Element<kBitsT>(p, usn);
            v[1] = Element<kBitsT>(p + kStep, usn);
            v[2] = Element<kBitsT>(p + 2 * kStep, usn);
            v[3] = 0;
            const uint8_t* peek = p + 3 * kStep;
            if (peek < end) v[3] = Element<kBitsT>(peek, usn);
            if constexpr (kBitsT == 32) {
                if ((written & 1u) != (phase & 1u)) v[3] = 0;
            }
        } else {
            v[0] = Element<kBitsT>(p, usn);
            v[1] = Element<kBitsT>(p + kStep, usn);
            v[2] = Element<kBitsT>(p + 2 * kStep, usn);
            v[3] = Element<kBitsT>(p + 3 * kStep, usn);
            (void)written;
        }
        (void)end;
    }
};
struct DecodeV4_5 {
    static void Read(const uint8_t* p, const uint8_t*, bool, uint32_t, uint8_t, uint32_t* v) {
        uint16_t raw;
        std::memcpy(&raw, p, 2);
        v[0] = ((raw >> 0) & 0x1Fu) << 3;
        v[1] = ((raw >> 5) & 0x1Fu) << 3;
        v[2] = ((raw >> 10) & 0x1Fu) << 3;
        v[3] = (raw & 0x8000u) >> 8;
    }
};

// The skipping-write loop over the destination: `n` vectors from `src`, WL
// written then CL-WL skipped per cycle, MODE and MASK applied per field, the
// destination wrapping at 1024 quadwords. The write cycle counts the written
// vectors modulo WL (the reference's `write_cycle`).
template <class D, bool kMasked, bool kMode>
void Loop(const VifUnpackNative::Command& c, const VifRegs& regs, uint8_t* dmem) {
    const uint32_t step = SourceBytesPerVector(c.format);
    const uint8_t* src = c.src;
    const uint8_t* end = c.src + c.src_bytes;
    uint32_t addr = c.addr_qw & kQwMask;
    uint32_t written = 0;
    uint32_t write_cycle = 0;
    uint32_t cycle_pos = 0;
    while (written < c.n) {
        if (cycle_pos >= c.wl) {
            // The skip cycles of this CL period: advance the destination, consume nothing.
            addr = (addr + 1) & kQwMask;
            if (++cycle_pos >= c.cl) cycle_pos = 0;
            continue;
        }
        uint32_t vec[4];
        D::Read(src, end, c.usn, written, c.phase, vec);
        src += step;
        if (kMode) {
            const uint32_t col = write_cycle & 3u;
            for (int f = 0; f < 4; f++) {
                uint32_t add = 0;
                if (c.mode == 1 || c.mode == 3) add += regs.row[f];
                if (c.mode == 2 || c.mode == 3) add += regs.col[col];
                vec[f] += add;
            }
        }
        uint8_t* dst = dmem + static_cast<size_t>(addr) * 16;
        if (kMasked) {
            const uint32_t row = write_cycle & 3u;
            uint32_t out[4];
            std::memcpy(out, dst, 16);  // the existing quadword (kept where the mask says 3)
            for (int f = 0; f < 4; f++) {
                switch (VifUnpack::MaskActionFor(regs.mask, f, row)) {
                    case MaskAction::Write: out[f] = vec[f]; break;
                    case MaskAction::NoWrite: break;
                    case MaskAction::Row: out[f] = regs.row[f]; break;
                    case MaskAction::Col: out[f] = regs.col[row]; break;
                }
            }
            std::memcpy(dst, out, 16);
        } else {
            std::memcpy(dst, vec, 16);
        }
        written++;
        if (++write_cycle >= c.wl) write_cycle = 0;
        addr = (addr + 1) & kQwMask;
        if (++cycle_pos >= c.cl) cycle_pos = 0;
    }
}

template <class D>
void Dispatch(const VifUnpackNative::Command& c, const VifRegs& regs, uint8_t* dmem) {
    const bool mode = c.mode != 0;
    if (c.masked) {
        if (mode) Loop<D, true, true>(c, regs, dmem);
        else Loop<D, true, false>(c, regs, dmem);
    } else {
        if (mode) Loop<D, false, true>(c, regs, dmem);
        else Loop<D, false, false>(c, regs, dmem);
    }
}

}  // namespace

bool VifUnpackNative::NeedsLookahead(uint32_t format) { return kElems[format & 15] == 3; }

bool VifUnpackNative::Supports(const Command& c) {
    const uint32_t step = SourceBytesPerVector(c.format);
    if (step == 0) return false;
    if (c.cl < c.wl) return false;  // the filling write: the engine's loop
    if (c.n == 0 || c.n > 256) return false;
    if (c.cl == 0 || c.wl == 0) return false;
    if (c.src == nullptr || c.src_bytes < static_cast<size_t>(step) * c.n) return false;
    return true;
}

void VifUnpackNative::Run(const Command& c, const VifRegs& regs, uint8_t* dmem) {
    if (!c.masked && c.mode == 0 && c.cl == c.wl && c.format == kFmtV4_32) {
        // The plain V4-32 stream: a straight copy, split only at the 1024-quadword wrap.
        const uint32_t start = c.addr_qw & kQwMask;
        const uint32_t first = (start + c.n <= 1024u) ? c.n : (1024u - start);
        std::memcpy(dmem + static_cast<size_t>(start) * 16, c.src, static_cast<size_t>(first) * 16);
        if (first < c.n) std::memcpy(dmem, c.src + static_cast<size_t>(first) * 16, static_cast<size_t>(c.n - first) * 16);
        return;
    }
    switch (c.format) {
        case 0x0: Dispatch<Decode<1, 32>>(c, regs, dmem); break;
        case 0x1: Dispatch<Decode<1, 16>>(c, regs, dmem); break;
        case 0x2: Dispatch<Decode<1, 8>>(c, regs, dmem); break;
        case 0x4: Dispatch<Decode<2, 32>>(c, regs, dmem); break;
        case 0x5: Dispatch<Decode<2, 16>>(c, regs, dmem); break;
        case 0x6: Dispatch<Decode<2, 8>>(c, regs, dmem); break;
        case 0x8: Dispatch<Decode<3, 32>>(c, regs, dmem); break;
        case 0x9: Dispatch<Decode<3, 16>>(c, regs, dmem); break;
        case 0xA: Dispatch<Decode<3, 8>>(c, regs, dmem); break;
        case 0xC: Dispatch<Decode<4, 32>>(c, regs, dmem); break;
        case 0xD: Dispatch<Decode<4, 16>>(c, regs, dmem); break;
        case 0xE: Dispatch<Decode<4, 8>>(c, regs, dmem); break;
        case kFmtV4_5: Dispatch<DecodeV4_5>(c, regs, dmem); break;
        default: break;
    }
}
