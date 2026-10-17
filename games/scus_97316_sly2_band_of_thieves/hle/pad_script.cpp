// games/scus_97316_sly2_band_of_thieves/hle/pad_script.cpp -- PadScript (see pad_script.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/pad_script.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ps2r::title::sly2 {

bool PadScript::Load(const std::string& spec) {
    steps_.clear();
    warning_.clear();
    source_ = "none";
    if (spec.empty()) return true;
    std::error_code ec;
    if (std::filesystem::is_regular_file(spec, ec)) {
        std::ifstream in(spec, std::ios::binary);
        if (!in.is_open()) return false;
        std::ostringstream text;
        text << in.rdbuf();
        if (!in && !in.eof()) return false;
        source_ = "file";
        ParseText(text.str());
        return true;
    }
    source_ = "inline";
    ParseText(spec);
    return true;
}

void PadScript::ParseText(const std::string& text) {
    steps_.clear();
    warning_.clear();
    // Tokens: runs of characters other than ',' and blanks; '#' drops the rest of its line.
    std::vector<std::string> tokens;
    std::string tok;
    bool comment = false;
    auto flush = [&] {
        if (!tok.empty()) tokens.push_back(tok);
        tok.clear();
    };
    for (const char c : text) {
        if (c == '\n' || c == '\r') {
            comment = false;
            flush();
        } else if (comment) {
            continue;
        } else if (c == '#') {
            comment = true;
            flush();
        } else if (c == ',' || c == ' ' || c == '\t') {
            flush();
        } else {
            tok += c;
        }
    }
    flush();
    unsigned stick_slot = 4;   // the next stick value to fill: 1 = ly, 2 = rx, 3 = ry, 4 = none
    for (const std::string& t : tokens) {
        if (t.find(':') != std::string::npos) {
            char* e = nullptr;
            Step st{};
            stick_slot = 4;
            st.frame = std::strtoull(t.c_str(), &e, 10);
            if (*e != ':') { warning_ = t; break; }
            st.buttons = static_cast<uint16_t>(std::strtoul(e + 1, &e, 16));
            if (*e != ':') { warning_ = t; break; }
            st.hold = std::strtoull(e + 1, &e, 10);
            if (*e == ':') {
                st.sticks = true;
                st.lx = static_cast<uint8_t>(std::strtoul(e + 1, &e, 10));
                stick_slot = 1;
            }
            steps_.push_back(st);
        } else if (!steps_.empty() && steps_.back().sticks && stick_slot < 4) {
            const uint8_t v = static_cast<uint8_t>(std::strtoul(t.c_str(), nullptr, 10));
            Step& st = steps_.back();
            if (stick_slot == 1) st.ly = v;
            else if (stick_slot == 2) st.rx = v;
            else st.ry = v;
            ++stick_slot;
        } else {
            warning_ = t;
            break;
        }
    }
}

void PadScript::Apply(uint64_t stamp, PadProtocol::Input* in) const {
    for (const Step& st : steps_)
        if (stamp >= st.frame && stamp < st.frame + st.hold) {
            in->buttons = static_cast<uint16_t>(in->buttons | st.buttons);
            if (st.sticks) {
                in->lx = st.lx;
                in->ly = st.ly;
                in->rx = st.rx;
                in->ry = st.ry;
            }
        }
}

uint64_t PadScript::End() const {
    uint64_t end = 0;
    for (const Step& st : steps_)
        if (st.frame + st.hold > end) end = st.frame + st.hold;
    return end;
}

std::string PadScript::Census() const {
    std::string s = "steps=" + std::to_string(steps_.size()) + ":end=" + std::to_string(End()) + ":source=" + source_;
    if (!warning_.empty()) s += ":truncated_at=" + warning_;
    return s;
}

}  // namespace ps2r::title::sly2
