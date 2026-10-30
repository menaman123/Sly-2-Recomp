#pragma once
// VifChainCensus: the read-only pre-walk of a VIF1 DMA chain that histograms what
// Sly 2 feeds the unit -- the DMAtag ids and sizes, every VIFcode (cmd, num, the
// STCYCL CL/WL in force, mask / mode / USN / TOPS for UNPACK) and the CHAIN
// TEMPLATE (the tag + code sequence up to and including the MSCAL that consumes
// it, or the chain end) by frequency. It consumes nothing: the generic walk (or
// the native executor) runs the chain afterwards. Behind the knob
// NATIVEEE_VIF_CENSUS=<file> (the table is written there at shutdown; counts only,
// never payload bytes).
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/dmac/dmac_channel.h"

class MainMemory;
class Scratchpad;
class Vif;

class VifChainCensus {
public:
    VifChainCensus(const MainMemory& ram, Scratchpad& spad, const Vif& vif);

    // One chain as the kick left it (before any tag is fed).
    void Observe(const DmacChannelRegs& regs);

    // The table (markdown): tags, codes, templates by frequency, the parse census.
    std::string Table() const;
    // The one-line run.meta row.
    std::string Row() const;

    uint64_t chains() const { return chains_; }

private:
    struct CodeKey {
        uint32_t cmd;      // 7-bit command
        uint32_t num;      // 0..255 as coded (0 = 256 for UNPACK/MPG)
        uint32_t cl, wl;   // the CYCLE in force
        uint32_t flags;    // UNPACK: bit0 masked, bit1 USN, bit2 TOPS, bits 4-5 MODE, bit 6 i-bit; others: bit 6 i-bit
        uint32_t imm;      // the immediate (STCYCL / MSCAL / BASE / OFFSET ...; UNPACK: the VU address, 10 bits)
        bool operator<(const CodeKey& o) const;
    };
    struct Count {
        uint64_t n = 0;
        uint64_t qw = 0;
    };
    struct TemplateCount {
        uint64_t n = 0;
        std::string example;  // the first exact instance (nums + addresses)
    };

    void ParseSpan(const uint8_t* data, size_t bytes, std::string& shape, std::string& exact, uint64_t& qw_unpacked);
    void EndTemplate(std::string& shape, std::string& exact);

    const MainMemory& ram_;
    Scratchpad& spad_;
    const Vif& vif_;

    // The parser's carried state (one unit, VIF1): the CYCLE in force and a payload crossing a span end.
    uint32_t cl_ = 1, wl_ = 1;
    uint32_t pending_bytes_ = 0;
    uint32_t pending_cmd_ = 0;
    bool in_direct_ = false;

    uint64_t chains_ = 0;
    uint64_t tags_ = 0;
    uint64_t chains_tte_ = 0;
    uint64_t chains_spr_ = 0;
    uint64_t chains_unreadable_ = 0;
    uint64_t chains_capped_ = 0;
    uint64_t payload_cross_tag_ = 0;   // a command's payload continued in the next tag's span
    uint64_t direct_qw_ = 0;
    uint64_t unknown_codes_ = 0;
    std::map<std::string, Count> tag_hist_;        // "REF" / "CNT qwc=12" ...
    std::map<CodeKey, Count> code_hist_;
    std::map<std::string, TemplateCount> templates_;  // shape -> count
    uint64_t template_instances_ = 0;
};
