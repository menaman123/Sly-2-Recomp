#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_chain_tags.h"

#include <cstring>

#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"

namespace {

uint64_t Load64(const uint8_t* p) {
    uint64_t v = 0;
    if (p) std::memcpy(&v, p, 8);
    return v;
}

}  // namespace

VifChainTag VifChainTags::Read(const MainMemory& ram, Scratchpad& spad, uint32_t tadr, bool from_spr) {
    VifChainTag t;
    const uint8_t* p = from_spr ? spad.Pointer(Scratchpad::Offset(tadr), 16)
                                : ram.Pointer(tadr & kRamMask, 16);
    if (p) {
        t.readable = true;
        t.lo = Load64(p);
        t.hi = Load64(p + 8);
    }
    t.terminator = (t.lo == ~0ull && t.hi == ~0ull);
    t.qwc = static_cast<uint32_t>(t.lo & 0xFFFFu);
    t.id = static_cast<uint32_t>((t.lo >> 28) & 7u);
    t.irq = ((t.lo >> 31) & 1u) != 0;
    t.addr = static_cast<uint32_t>((t.lo >> 32) & 0x7FFFFFFFu) & ~0xFu;
    t.spr = (t.lo >> 63) != 0;
    return t;
}

const uint8_t* VifChainTags::Span(const MainMemory& ram, Scratchpad& spad, uint32_t addr, bool from_spr, size_t bytes) {
    if (from_spr) return spad.Pointer(Scratchpad::Offset(addr), bytes);
    return ram.Pointer(addr & kRamMask, bytes);
}

const char* VifChainTags::IdName(uint32_t id) {
    static const char* const kNames[8] = {"REFE", "CNT", "NEXT", "REF", "REFS", "CALL", "RET", "END"};
    return kNames[id & 7u];
}
