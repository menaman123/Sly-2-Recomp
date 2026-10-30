#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_census.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/ee/mem/scratchpad.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vif/vif_chain_tags.h"

namespace {

constexpr int kMaxTagsPerChain = 1 << 16;  // the census' own guard (the walker's is the RAM size)

uint32_t Word(const uint8_t* p, size_t w) {
    uint32_t x;
    std::memcpy(&x, p + w * 4, 4);
    return x;
}

std::string Hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

const char* CmdName(uint32_t cmd) {
    switch (cmd) {
        case 0x00: return "NOP";
        case 0x01: return "STCYCL";
        case 0x02: return "OFFSET";
        case 0x03: return "BASE";
        case 0x04: return "ITOP";
        case 0x05: return "STMOD";
        case 0x06: return "MSKPATH3";
        case 0x07: return "MARK";
        case 0x10: return "FLUSHE";
        case 0x11: return "FLUSH";
        case 0x13: return "FLUSHA";
        case 0x14: return "MSCAL";
        case 0x15: return "MSCALF";
        case 0x17: return "MSCNT";
        case 0x20: return "STMASK";
        case 0x30: return "STROW";
        case 0x31: return "STCOL";
        case 0x4A: return "MPG";
        case 0x50: return "DIRECT";
        case 0x51: return "DIRECTHL";
        default: return nullptr;
    }
}

const char* FormatName(uint32_t fmt) {
    static const char* const kNames[16] = {"S-32", "S-16", "S-8", "?3", "V2-32", "V2-16", "V2-8", "?7",
                                           "V3-32", "V3-16", "V3-8", "?b", "V4-32", "V4-16", "V4-8", "V4-5"};
    return kNames[fmt & 15];
}

}  // namespace

bool VifChainCensus::CodeKey::operator<(const CodeKey& o) const {
    if (cmd != o.cmd) return cmd < o.cmd;
    if (flags != o.flags) return flags < o.flags;
    if (cl != o.cl) return cl < o.cl;
    if (wl != o.wl) return wl < o.wl;
    if (imm != o.imm) return imm < o.imm;
    return num < o.num;
}

VifChainCensus::VifChainCensus(const MainMemory& ram, Scratchpad& spad, const Vif& vif) : ram_(ram), spad_(spad), vif_(vif) {}

void VifChainCensus::EndTemplate(std::string& shape, std::string& exact) {
    if (shape.empty()) return;
    TemplateCount& t = templates_[shape];
    if (t.n == 0) t.example = exact;
    t.n++;
    template_instances_++;
    shape.clear();
    exact.clear();
}

// One span of the chain through the code parser; appends the codes to the template strings.
void VifChainCensus::ParseSpan(const uint8_t* data, size_t bytes, std::string& shape, std::string& exact, uint64_t& qw_unpacked) {
    const size_t nwords = bytes / 4;
    size_t w = 0;
    while (w < nwords) {
        if (in_direct_ || pending_bytes_ > 0) {
            // A payload (DIRECT quadwords, or a code's data) continuing from the previous span.
            const size_t avail = (nwords - w) * 4;
            const size_t take = std::min<size_t>(avail, pending_bytes_);
            pending_bytes_ -= static_cast<uint32_t>(take);
            w += (take + 3) / 4;
            if (pending_bytes_ == 0) in_direct_ = false;
            continue;
        }
        const uint32_t code = Word(data, w);
        const bool irq = (code >> 31) != 0;
        const uint32_t cmd = (code >> 24) & 0x7F;
        const uint32_t num = (code >> 16) & 0xFF;
        const uint32_t imm = code & 0xFFFF;
        w++;

        CodeKey k{cmd, num, cl_, wl_, irq ? 0x40u : 0u, imm};
        uint32_t payload_bytes = 0;
        std::string s, e;
        if (VifUnpack::IsUnpackCmd(cmd)) {
            const uint32_t fmt = cmd & 0xF;
            const bool masked = (cmd & 0x10) != 0;
            const bool usn = (imm & 0x4000) != 0;
            const bool tops = (imm & 0x8000) != 0;
            const uint32_t mode = vif_.regs(1).mode;  // the MODE in force (STMOD inside the chain is rare; recorded as its own code)
            k.flags |= (masked ? 1u : 0u) | (usn ? 2u : 0u) | (tops ? 4u : 0u) | ((mode & 3u) << 4);
            k.imm = imm & 0x3FFu;
            k.num = 0;  // the histogram buckets UNPACKs by shape, not by count
            payload_bytes = VifUnpack::GetUnpackSize(cmd, num, cl_, wl_);
            const uint32_t n = num == 0 ? 256 : num;
            qw_unpacked += n;
            s = std::string("U") + FormatName(fmt) + (masked ? "m" : "") + (usn ? "u" : "") + (tops ? "+T" : "") + "[" +
                std::to_string(cl_) + "/" + std::to_string(wl_) + "]" + (irq ? "(i)" : "");
            e = s + "@" + Hex(imm & 0x3FFu) + "x" + std::to_string(n);
        } else {
            switch (cmd) {
                case 0x01: cl_ = imm & 0xFF; wl_ = (imm >> 8) & 0xFF; if (cl_ == 0) cl_ = 1; if (wl_ == 0) wl_ = 1; break;
                case 0x20: payload_bytes = 4; break;
                case 0x30: case 0x31: payload_bytes = 16; break;
                case 0x4A: payload_bytes = (num == 0 ? 256 : num) * 8; break;
                case 0x50: case 0x51: {
                    const uint32_t qwc = imm == 0 ? 0x10000u : imm;
                    direct_qw_ += qwc;
                    // The payload starts on the next quadword boundary of the stream.
                    w = (w + 3) & ~static_cast<size_t>(3);
                    payload_bytes = qwc * 16;
                    in_direct_ = true;
                    break;
                }
                default: break;
            }
            const char* name = CmdName(cmd);
            if (!name) {
                unknown_codes_++;
                s = "?" + Hex(cmd);
            } else {
                s = name;
            }
            if (irq) s += "(i)";
            if (cmd == 0x01 || cmd == 0x14 || cmd == 0x15 || cmd == 0x03 || cmd == 0x02 || cmd == 0x05 || cmd == 0x06) {
                s += "#" + Hex(imm);  // the immediate is part of the template's identity
                e = s;
            } else if (cmd == 0x20 && w < nwords) {
                s += "#" + Hex(Word(data, w));  // the mask value too
                e = s;
            } else {
                e = s;
                if (cmd == 0x04 || cmd == 0x07) e += "#" + Hex(imm);
            }
            if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) k.imm = imm;
            else if (cmd != 0x01 && cmd != 0x03 && cmd != 0x02 && cmd != 0x05 && cmd != 0x06) k.imm = 0;
            if (cmd != 0x4A) k.num = 0;
        }
        Count& c = code_hist_[k];
        c.n++;
        c.qw += VifUnpack::IsUnpackCmd(cmd) ? (num == 0 ? 256 : num) : 0;
        shape += s + " ";
        exact += e + " ";

        if (payload_bytes > 0) {
            const size_t avail = (nwords - w) * 4;
            if (payload_bytes > avail) {
                pending_bytes_ = payload_bytes - static_cast<uint32_t>(avail);
                pending_cmd_ = cmd;
                if (!in_direct_) payload_cross_tag_++;
                w = nwords;
            } else {
                w += (payload_bytes + 3) / 4;
                in_direct_ = false;
            }
        }
    }
}

void VifChainCensus::Observe(const DmacChannelRegs& regs) {
    using namespace DmacChcr;
    chains_++;
    // The CYCLE in force at the chain head is the unit's register (it persists across chains).
    cl_ = VifRegsCl(vif_.regs(1));
    wl_ = VifRegsWl(vif_.regs(1));
    if (cl_ == 0) cl_ = 1;
    if (wl_ == 0) wl_ = 1;
    pending_bytes_ = 0;
    in_direct_ = false;

    const bool tte = (regs.chcr & kTte) != 0;
    const bool tie = (regs.chcr & kTie) != 0;
    if (tte) chains_tte_++;
    uint32_t tadr = regs.tadr & VifChainTags::kTadrMask;
    bool from_spr = Scratchpad::Contains(regs.tadr);
    if (from_spr) chains_spr_++;
    uint32_t asp = Asp(regs.chcr);
    uint32_t asr0 = regs.asr0, asr1 = regs.asr1;

    std::string shape, exact;
    bool kicked = false;
    int tags = 0;
    bool end = false;
    while (!end && tags < kMaxTagsPerChain) {
        const VifChainTag t = VifChainTags::Read(ram_, spad_, tadr, from_spr);
        if (!t.readable) {
            chains_unreadable_++;
            break;
        }
        if (t.terminator) {
            tag_hist_["TERM"].n++;
            break;
        }
        tags++;
        tags_++;
        uint32_t src = 0;
        bool src_spr = false;
        bool transfer = false;
        uint32_t next = tadr;
        bool next_spr = from_spr;
        const uint32_t after = tadr + 16 + t.qwc * 16;
        switch (t.id) {
            case DmacTagId::kRefe: src = t.addr; src_spr = t.spr; next = tadr + 16; end = true; transfer = true; break;
            case DmacTagId::kCnt: src = tadr + 16; src_spr = from_spr; next = after; transfer = true; break;
            case DmacTagId::kNext: src = tadr + 16; src_spr = from_spr; next = t.addr; next_spr = t.spr; transfer = t.qwc > 0; break;
            case DmacTagId::kRef:
            case DmacTagId::kRefs: src = t.addr; src_spr = t.spr; next = tadr + 16; transfer = true; break;
            case DmacTagId::kCall:
                src = tadr + 16; src_spr = from_spr;
                if (asp == 0) { asr0 = after; asp = 1; } else if (asp == 1) { asr1 = after; asp = 2; }
                next = t.addr; next_spr = t.spr; transfer = true;
                break;
            case DmacTagId::kRet:
                src = tadr + 16; src_spr = from_spr;
                if (asp == 2) { next = asr1; asp = 1; } else if (asp == 1) { next = asr0; asp = 0; } else { end = true; }
                transfer = true;
                break;
            case DmacTagId::kEnd: src = tadr + 16; src_spr = from_spr; next = after; end = true; transfer = t.qwc > 0; break;
            default: end = true; break;
        }
        {
            std::string tk = VifChainTags::IdName(t.id);
            if (t.irq) tk += "(irq)";
            if (t.spr || src_spr) tk += "(spr)";
            Count& c = tag_hist_[tk];
            c.n++;
            c.qw += t.qwc;
        }
        shape += VifChainTags::IdName(t.id) + std::string(t.irq ? "i" : "") + ":";
        exact += VifChainTags::IdName(t.id) + std::string(t.irq ? "i" : "") + std::to_string(t.qwc) + ":";
        if (tte) {
            // The TTE quadword: the tag's upper two words, zero-padded; its pad words are not payload.
            uint8_t qw[16];
            std::memset(qw, 0, sizeof(qw));
            std::memcpy(qw, &t.hi, 8);
            uint64_t dummy = 0;
            ParseSpan(qw, 8, shape, exact, dummy);  // the two code words only (the pad is skipped by the unit)
        }
        if (transfer && t.qwc > 0) {
            const size_t bytes = static_cast<size_t>(t.qwc) * 16;
            const uint8_t* p = VifChainTags::Span(ram_, spad_, src, src_spr, bytes);
            if (p) {
                uint64_t qw_unpacked = 0;
                const size_t before = shape.size();
                ParseSpan(p, bytes, shape, exact, qw_unpacked);
                // A span whose codes include a VU kick closes the template at its end.
                if (shape.find("MSCAL", before) != std::string::npos || shape.find("MSCNT", before) != std::string::npos) kicked = true;
            } else {
                shape += "(unreadable) ";
                exact += "(unreadable) ";
            }
        }
        shape += "| ";
        exact += "| ";
        if (kicked) {
            EndTemplate(shape, exact);
            kicked = false;
        }
        if (t.irq && tie) end = true;
        tadr = next;
        from_spr = next_spr;
    }
    if (tags >= kMaxTagsPerChain) chains_capped_++;
    if (!shape.empty()) {
        shape += "END";
        exact += "END";
        EndTemplate(shape, exact);
    }
}

std::string VifChainCensus::Row() const {
    return "chains=" + std::to_string(chains_) + ":tags=" + std::to_string(tags_) + ":templates=" + std::to_string(templates_.size()) +
           ":instances=" + std::to_string(template_instances_) + ":tte=" + std::to_string(chains_tte_) + ":spr=" +
           std::to_string(chains_spr_) + ":cross_tag=" + std::to_string(payload_cross_tag_) + ":direct_qw=" + std::to_string(direct_qw_) +
           ":unknown=" + std::to_string(unknown_codes_);
}

std::string VifChainCensus::Table() const {
    std::string out;
    out += "# VIF1 chain census\n\n";
    out += Row() + "\n\n";
    out += "## DMA tags\n\n| tag | count | quadwords |\n|---|---|---|\n";
    for (const auto& [k, c] : tag_hist_) out += "| " + k + " | " + std::to_string(c.n) + " | " + std::to_string(c.qw) + " |\n";

    out += "\n## VIF codes (UNPACK bucketed by format/mask/usn/tops/mode, CL/WL in force and VU address; nums summed as quadwords)\n\n";
    out += "| cmd | cl/wl | flags | imm | count | quadwords |\n|---|---|---|---|---|---|\n";
    std::vector<std::pair<CodeKey, Count>> codes(code_hist_.begin(), code_hist_.end());
    std::sort(codes.begin(), codes.end(), [](const auto& a, const auto& b) { return a.second.n > b.second.n; });
    for (const auto& [k, c] : codes) {
        std::string name;
        if (VifUnpack::IsUnpackCmd(k.cmd)) {
            name = std::string("UNPACK ") + FormatName(k.cmd & 0xF) + ((k.flags & 1) ? " masked" : "");
        } else {
            const char* n = CmdName(k.cmd);
            name = n ? n : ("?0x" + Hex(k.cmd));
        }
        std::string flags;
        if (k.flags & 2) flags += "usn ";
        if (k.flags & 4) flags += "tops ";
        if ((k.flags >> 4) & 3) flags += "mode" + std::to_string((k.flags >> 4) & 3) + " ";
        if (k.flags & 0x40) flags += "i-bit ";
        out += "| " + name + " | " + std::to_string(k.cl) + "/" + std::to_string(k.wl) + " | " + flags + " | 0x" + Hex(k.imm) + " | " +
               std::to_string(c.n) + " | " + std::to_string(c.qw) + " |\n";
    }

    out += "\n## Chain templates (tag shape + code sequence up to the span holding the VU kick; nums/addresses elided; by frequency)\n\n";
    std::vector<std::pair<std::string, TemplateCount>> tpl(templates_.begin(), templates_.end());
    std::sort(tpl.begin(), tpl.end(), [](const auto& a, const auto& b) { return a.second.n > b.second.n; });
    uint64_t cum = 0;
    int rank = 0;
    out += "| # | count | cum % | template | first exact instance |\n|---|---|---|---|---|\n";
    for (const auto& [k, t] : tpl) {
        cum += t.n;
        rank++;
        const double pct = template_instances_ ? 100.0 * static_cast<double>(cum) / static_cast<double>(template_instances_) : 0.0;
        char pb[16];
        std::snprintf(pb, sizeof(pb), "%.1f", pct);
        std::string key = k, ex = t.example;
        if (key.size() > 400) key = key.substr(0, 400) + "...";
        if (ex.size() > 400) ex = ex.substr(0, 400) + "...";
        out += "| " + std::to_string(rank) + " | " + std::to_string(t.n) + " | " + pb + " | `" + key + "` | `" + ex + "` |\n";
        if (rank >= 60) break;
    }
    out += "\n(" + std::to_string(tpl.size()) + " distinct templates)\n";
    return out;
}
