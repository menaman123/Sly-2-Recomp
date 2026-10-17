// games/scus_97316_sly2_band_of_thieves/hle/pad_script.h -- PadScript: the NATIVEEE_PAD_SCRIPT overlay
// of the title composition (boot triage / replay). The value is either the script text itself or the
// path of a readable file holding it (what PadRecorder writes), in one grammar:
//
//   <frame>:<buttons_hex>:<hold>[:<lx>,<ly>[,<rx>,<ry>]]  [, <frame>:...]
//
// press `buttons` (libpad active-high order: SELECT 0x0001 L3 0x0002 R3 0x0004 START 0x0008 UP 0x0010
// RIGHT 0x0020 DOWN 0x0040 LEFT 0x0080 L2 0x0100 R2 0x0200 L1 0x0400 R1 0x0800 TRIANGLE 0x1000 CIRCLE
// 0x2000 CROSS 0x4000 SQUARE 0x8000) and, when given, hold the sticks at lx,ly[,rx,ry] (0..255, 128 =
// centre; 0,128 = left stick full left) for `hold` pad stamps (one per boundary) starting at the
// `frame`-th stamp, over the live / neutral pad. Steps overlap by OR (buttons) / last-wins (sticks).
// Tokens are separated by commas, blanks or newlines; `#` starts a comment to the end of the line; a
// token with a ':' starts a step, a bare number fills the previous step's next stick slot (ly, rx, ry).
// A token that fits neither truncates the script there (Warning() names it).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/pad/hle/pad_protocol.h"

namespace ps2r::title::sly2 {

class PadScript {
public:
    struct Step {
        uint64_t frame = 0;
        uint16_t buttons = 0;
        uint64_t hold = 0;
        bool sticks = false;
        uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    };

    PadScript() = default;

    // `spec` = the knob's value: a readable regular file is read and parsed (Source() = "file"),
    // anything else is parsed as the script text (Source() = "inline"). Returns false only when
    // `spec` names a file that exists but cannot be read; an empty spec is an empty script.
    bool Load(const std::string& spec);
    // Parses `text` (replacing the current steps).
    void ParseText(const std::string& text);

    // The boundary duty: overlay every step live at `stamp` on `in`.
    void Apply(uint64_t stamp, PadProtocol::Input* in) const;

    const std::vector<Step>& Steps() const { return steps_; }
    bool Empty() const { return steps_.empty(); }
    const std::string& Source() const { return source_; }
    // The stray token that truncated the script ("" = the whole text parsed).
    const std::string& Warning() const { return warning_; }
    // The first stamp past every step (0 = no steps).
    uint64_t End() const;
    // run.meta pad_script= body: steps=,end=,source=[,truncated_at=]
    std::string Census() const;

private:
    std::vector<Step> steps_;
    std::string source_ = "none";
    std::string warning_;
};

}  // namespace ps2r::title::sly2
