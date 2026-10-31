#pragma once
// VifChainTags: the DMAtag decode and the guest-memory reads a VIF1 chain walk
// needs, restated for the title's native path with the engine walker's exact
// address rules (engine/dmac/chain_walker.cpp: a tag is read at `tadr & 0x0FFFFFFF`
// masked to the 25-bit RAM window, or at the scratchpad offset when the address
// carries the 0x70000000 prefix / the tag's SPR bit; a span pointer is null when
// it does not fit entirely inside the memory it names). Static only.
#include <cstddef>
#include <cstdint>

class MainMemory;
class Scratchpad;

struct VifChainTag {
    uint64_t lo = 0;   // the tag's low 64 bits (QWC, PCE, ID, IRQ | ADDR, SPR)
    uint64_t hi = 0;   // the upper two words (the TTE quadword's payload)
    uint32_t qwc = 0;
    uint32_t id = 0;
    uint32_t addr = 0;  // ADDR, 16-byte aligned, SPR bit stripped
    bool irq = false;
    bool spr = false;
    bool terminator = false;  // both halves all-ones: the VIF walk's end-of-list
    bool readable = false;    // the 16 bytes were inside the memory named (else every field is 0)
};

class VifChainTags {
public:
    static constexpr uint32_t kRamMask = 0x01FFFFFFu;
    static constexpr uint32_t kTadrMask = 0x0FFFFFFFu;

    // Reads and decodes the tag at `tadr` (already masked with kTadrMask).
    static VifChainTag Read(const MainMemory& ram, Scratchpad& spad, uint32_t tadr, bool from_spr);
    // `bytes` of span data at `addr` in RAM or the scratchpad; null when out of range.
    static const uint8_t* Span(const MainMemory& ram, Scratchpad& spad, uint32_t addr, bool from_spr, size_t bytes);
    // The tag's name for a census key.
    static const char* IdName(uint32_t id);
};
