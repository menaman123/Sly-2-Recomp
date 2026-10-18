// games/scus_97316_sly2_band_of_thieves/hle/pad_script.cpp -- PadScript (see pad_script.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/pad_script.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "games/scus_97316_sly2_band_of_thieves/hle/nav_json.h"

namespace ps2r::title::sly2 {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint16_t kCross = 0x4000;
constexpr uint16_t kCircle = 0x2000;
constexpr uint64_t kJumpHold = 8;
constexpr uint64_t kStillStamps = 10;
constexpr uint64_t kStuckStamps = 30;     // goto: no closer approach for this long = stuck
constexpr uint64_t kUnstickJump = 20;     // the jump phase's length
// The detour phases after the jump: (heading offset in degrees, length in stamps), escalating -- a prop
// needs a short sidestep, a building a long walk along its wall; the cycle restarts from the jump.
struct Detour { double deg; uint64_t stamps; };
constexpr Detour kDetours[] = {{60, 40}, {-60, 40}, {90, 90}, {-90, 90}, {135, 120}, {-135, 120}};
constexpr unsigned kPhases = 1 + sizeof(kDetours) / sizeof(kDetours[0]);
// climb / rail: the approach radius, the facing tolerance, the leap (CROSS from stamp 0, CIRCLE = the grab
// from stamp 3 to the leap's end), the climb's stall window and the goal tolerance.
constexpr double kClimbNear = 70, kRailNear = 50, kFaceTol = 12;
constexpr uint64_t kFaceMax = 90, kLeapStamps = 40, kGrabFrom = 3, kClimbStall = 45, kApproachMax = 600;
constexpr double kClimbGoalSlack = 15, kRailEndRadius = 40;
// warp: the player's authoritative position = the local position (+0x30) of the position-only transform block
// (0xfb36e0 in Paris = level object + 0x2160; nav_notes.md: root bone 0xfb5ae0 -> 0xfb37e0 facing -> 0xfb36e0
// position -> 0xfb35e0 identity), the probe table's pos_x object being [0x2de1a0]. Chosen by the poke survey
// (firstjob_notes.md).
constexpr uint32_t kWarpOffset = 0x2190;

// Splits `s` on `sep`.
std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t next = s.find(sep, pos);
        if (next == std::string::npos) next = s.size();
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

bool Num(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* e = nullptr;
    *out = std::strtod(s.c_str(), &e);
    return e && *e == '\0';
}

bool UNum(const std::string& s, uint64_t* out) {
    if (s.empty()) return false;
    char* e = nullptr;
    *out = std::strtoull(s.c_str(), &e, 10);
    return e && *e == '\0';
}

double WrapDeg(double d) {
    while (d > 180) d -= 360;
    while (d <= -180) d += 360;
    return d;
}

const char* KindName(PadScript::NavKind k) {
    switch (k) {
        case PadScript::NavKind::Goto: return "goto";
        case PadScript::NavKind::JumpWhenNear: return "jump_when_near";
        case PadScript::NavKind::Face: return "face";
        case PadScript::NavKind::WaitStill: return "wait_still";
        case PadScript::NavKind::Pause: return "nav_pause";
        case PadScript::NavKind::Climb: return "climb";
        case PadScript::NavKind::Rail: return "rail";
        case PadScript::NavKind::Until: return "until";
        case PadScript::NavKind::Interact: return "interact";
        case PadScript::NavKind::Warp: return "warp";
    }
    return "?";
}

bool IsNumberStart(const std::string& t) {
    return !t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' || t[0] == '.');
}

}  // namespace

bool PadScript::LoadPaths(const std::string& json_path) {
    paths_.clear();
    paths_warning_.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(json_path, ec)) return true;   // no table: no paths
    std::ifstream in(json_path, std::ios::binary);
    if (!in.is_open()) { paths_warning_ = json_path + ": cannot open"; return false; }
    std::ostringstream text;
    text << in.rdbuf();
    NavJson root;
    size_t at = 0;
    if (!NavJson::Parse(text.str(), &root, &at)) { paths_warning_ = json_path + ": malformed JSON at byte " + std::to_string(at); return false; }
    const NavJson* paths = root.Member("paths");
    if (!paths || paths->kind != NavJson::Object) { paths_warning_ = json_path + ": no \"paths\" object"; return false; }
    for (size_t k = 0; k < paths->keys.size(); k++) {
        const NavJson& list = paths->items[k];
        if (list.kind != NavJson::Array) { paths_warning_ = json_path + ": path " + paths->keys[k] + " is not an array"; return false; }
        std::vector<std::string> tokens;
        for (const NavJson& t : list.items) {
            if (t.kind != NavJson::String) { paths_warning_ = json_path + ": path " + paths->keys[k] + " holds a non-string step"; return false; }
            tokens.push_back(t.str);
        }
        paths_[paths->keys[k]] = std::move(tokens);
    }
    return true;
}

bool PadScript::Load(const std::string& spec) {
    steps_.clear();
    nav_.clear();
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
    nav_.clear();
    nav_index_ = 0;
    nav_prev_end_ = 0;
    warning_.clear();
    // Tokens: runs of characters other than ',' and blanks; '#' drops the rest of its line. A closed-loop
    // token keeps its ',' (the target triple): it is re-joined from the pieces that follow its head.
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
            if (c == ',') tokens.push_back(",");
        } else {
            tok += c;
        }
    }
    flush();
    unsigned stick_slot = 4;   // the next stick value to fill: 1 = ly, 2 = rx, 3 = ry, 4 = none
    for (size_t i = 0; i < tokens.size(); i++) {
        const std::string& t = tokens[i];
        if (t == ",") continue;
        const bool word_head = !t.empty() && std::isalpha(static_cast<unsigned char>(t[0])) && t.find(':') != std::string::npos;
        if (word_head) {
            // Re-join: head[,piece]* where a piece is a bare number (the target's y, z) possibly carrying the
            // ':' tail (radius / timeout / the next triple) -- "goto:100:1.5,2,3:0.5" tokenises as
            // goto:100:1.5 , 2 , 3:0.5.
            std::string whole = t;
            size_t j = i + 1;
            while (j + 1 < tokens.size() && tokens[j] == "," && IsNumberStart(tokens[j + 1])) {
                whole += "," + tokens[j + 1];
                j += 2;
            }
            if (!ParseNavToken(whole)) { warning_ = whole; break; }
            i = j - 1;
            stick_slot = 4;
            continue;
        }
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

bool PadScript::ParseTriple(const std::string& f, double* x, double* y, double* z) {
    const std::vector<std::string> xyz = Split(f, ',');
    return xyz.size() == 3 && Num(xyz[0], x) && Num(xyz[1], y) && Num(xyz[2], z);
}

// path:<from>:<name>[:<radius>] -- the named list's tokens parse in place; `from` applies to the first,
// `radius` to every goto / jump_when_near of the list that names none.
bool PadScript::ExpandPath(const std::vector<std::string>& f) {
    if (f.size() < 3 || f.size() > 4) return false;
    uint64_t from = 0;
    double radius = 0;
    if (!UNum(f[1], &from)) return false;
    if (f.size() == 4 && !Num(f[3], &radius)) return false;
    const auto it = paths_.find(f[2]);
    if (it == paths_.end()) return false;
    const size_t first = nav_.size();
    for (const std::string& tok : it->second) {
        if (tok.rfind("path:", 0) == 0) return false;   // no nesting
        if (!ParseNavToken(tok)) return false;
    }
    if (nav_.size() == first) return false;   // an empty path is a mistake
    nav_[first].from = std::max(nav_[first].from, from);
    if (radius > 0)
        for (size_t k = first; k < nav_.size(); k++)
            if ((nav_[k].kind == NavKind::Goto || nav_[k].kind == NavKind::JumpWhenNear) && nav_[k].radius <= 0) nav_[k].radius = radius;
    return true;
}

// kind:from:<fields...> -- see the header's grammar.
bool PadScript::ParseNavToken(const std::string& t) {
    const std::vector<std::string> f = Split(t, ':');
    if (f.size() < 2) return false;
    if (f[0] == "path") return ExpandPath(f);
    NavStep st{};
    if (f[0] == "goto") st.kind = NavKind::Goto;
    else if (f[0] == "jump_when_near") st.kind = NavKind::JumpWhenNear;
    else if (f[0] == "face") st.kind = NavKind::Face;
    else if (f[0] == "wait_still") st.kind = NavKind::WaitStill;
    else if (f[0] == "nav_pause") st.kind = NavKind::Pause;
    else if (f[0] == "climb") st.kind = NavKind::Climb;
    else if (f[0] == "rail") st.kind = NavKind::Rail;
    else if (f[0] == "until") st.kind = NavKind::Until;
    else if (f[0] == "interact") st.kind = NavKind::Interact;
    else if (f[0] == "warp") st.kind = NavKind::Warp;
    else return false;
    if (!UNum(f[1], &st.from)) return false;
    switch (st.kind) {
        case NavKind::Goto:
        case NavKind::JumpWhenNear: {
            if (f.size() < 3 || !ParseTriple(f[2], &st.x, &st.y, &st.z)) return false;
            if (f.size() > 3 && !Num(f[3], &st.radius)) return false;
            if (st.kind == NavKind::JumpWhenNear && st.radius <= 0) return false;
            if (f.size() > 4 && !UNum(f[4], &st.timeout)) return false;
            if (f.size() > 5) return false;
            break;
        }
        case NavKind::Face: {
            if (f.size() < 3 || !Num(f[2], &st.yaw)) return false;
            st.timeout = 120;
            if (f.size() > 3 && !Num(f[3], &st.tol)) return false;
            if (f.size() > 4 && !UNum(f[4], &st.timeout)) return false;
            if (f.size() > 5) return false;
            break;
        }
        case NavKind::WaitStill: {
            st.timeout = 300;
            if (f.size() > 2 && !UNum(f[2], &st.timeout)) return false;
            if (f.size() > 3) return false;
            break;
        }
        case NavKind::Pause: {
            if (f.size() != 3 || !UNum(f[2], &st.timeout)) return false;
            break;
        }
        case NavKind::Climb: {
            if (f.size() < 3 || !ParseTriple(f[2], &st.x, &st.y, &st.z)) return false;
            st.timeout = 600;
            if (f.size() > 3 && !UNum(f[3], &st.timeout)) return false;
            if (f.size() > 4) return false;
            break;
        }
        case NavKind::Rail: {
            if (f.size() < 4 || !ParseTriple(f[2], &st.x, &st.y, &st.z) || !ParseTriple(f[3], &st.ex, &st.ey, &st.ez)) return false;
            if (f.size() > 4 && !UNum(f[4], &st.timeout)) return false;
            if (f.size() > 5) return false;
            break;
        }
        case NavKind::Until: {
            // <probe><op><value>: the op is the first run of < > = ! characters.
            if (f.size() < 3) return false;
            const std::string& e = f[2];
            const size_t op0 = e.find_first_of("<>=!");
            if (op0 == std::string::npos || op0 == 0) return false;
            size_t op1 = op0;
            while (op1 < e.size() && (e[op1] == '<' || e[op1] == '>' || e[op1] == '=' || e[op1] == '!')) ++op1;
            st.probe = e.substr(0, op0);
            st.op = e.substr(op0, op1 - op0);
            if (st.op != "<" && st.op != ">" && st.op != "<=" && st.op != ">=" && st.op != "==" && st.op != "!=") return false;
            if (!Num(e.substr(op1), &st.value)) return false;
            st.timeout = 600;
            if (f.size() > 3 && !UNum(f[3], &st.timeout)) return false;
            if (f.size() > 4) return false;
            break;
        }
        case NavKind::Interact: {
            st.timeout = 10;
            st.buttons = kCircle;
            if (f.size() > 2 && !UNum(f[2], &st.timeout)) return false;
            if (f.size() > 3) {
                char* e = nullptr;
                st.buttons = static_cast<uint16_t>(std::strtoul(f[3].c_str(), &e, 16));
                if (!e || *e || f[3].empty()) return false;
            }
            if (f.size() > 4 || st.timeout == 0) return false;
            break;
        }
        case NavKind::Warp: {
            if (f.size() < 3 || !ParseTriple(f[2], &st.x, &st.y, &st.z)) return false;
            st.offset = kWarpOffset;
            if (f.size() > 3) {
                char* e = nullptr;
                st.offset = static_cast<uint32_t>(std::strtoul(f[3].c_str(), &e, 16));
                if (!e || *e || f[3].empty()) return false;
            }
            if (f.size() > 4) return false;
            st.timeout = 1;
            break;
        }
    }
    nav_.push_back(st);
    return true;
}

void PadScript::HeadingTo(const NavProbe::Convention& c, double x, double y, double z, double tx, double ty, double tz, double* heading,
                          double* dist) {
    // The two horizontal axes in x,y,z order with the up axis removed: (a, b).
    double a, b;
    if (c.up_axis == 'y') { a = tx - x; b = tz - z; }
    else if (c.up_axis == 'x') { a = ty - y; b = tz - z; }
    else { a = tx - x; b = ty - y; }
    *dist = std::sqrt(a * a + b * b);
    *heading = std::atan2(b, a) * 180.0 / kPi;
}

void PadScript::StickForHeading(double heading_deg, double cam_yaw_deg, const NavProbe::Convention& c, uint8_t* lx, uint8_t* ly) {
    // Stick angle phi (0 = up, +90 = right under right_sign +1) that moves Sly along `heading`: pushing up
    // walks heading cam_yaw + forward_offset; pushing right adds right_sign * 90.
    const double phi = WrapDeg(heading_deg - cam_yaw_deg - c.forward_offset_deg) * c.right_sign;
    const double r = phi * kPi / 180.0;
    const double sx = std::sin(r), sy = -std::cos(r);   // up = ly 0
    *lx = static_cast<uint8_t>(std::lround(128 + 127 * sx));
    *ly = static_cast<uint8_t>(std::lround(128 + 127 * sy));
}

bool PadScript::Compare(double value, const std::string& op, double rhs) {
    if (op == "<") return value < rhs;
    if (op == ">") return value > rhs;
    if (op == "<=") return value <= rhs;
    if (op == ">=") return value >= rhs;
    if (op == "==") return std::fabs(value - rhs) <= 0.5;
    if (op == "!=") return std::fabs(value - rhs) > 0.5;
    return false;
}

void PadScript::Apply(uint64_t stamp, NavProbe& probe, PadProtocol::Input* in) {
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
    if (nav_index_ < nav_.size()) Steer(stamp, probe, in);
}

void PadScript::Finish(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, bool reached, const char* why) {
    st.done = true;
    st.active = false;
    st.reached = reached;
    st.ended = stamp;
    nav_prev_end_ = stamp;
    ++nav_index_;
    std::fprintf(stderr, "nativeee: nav: %s %s at stamp %llu after %llu (at %.2f,%.2f,%.2f dist %.2f)\n", KindName(st.kind), why,
                 static_cast<unsigned long long>(stamp), static_cast<unsigned long long>(stamp - st.started), s.x, s.y, s.z, st.last_dist);
}

void PadScript::Steer(uint64_t stamp, NavProbe& probe, PadProtocol::Input* in) {
    NavStep& st = nav_[nav_index_];
    if (!st.active) {
        if (stamp < st.from || stamp < nav_prev_end_) return;
        st.active = true;
        st.started = stamp;
        st.phase_start = stamp;
        std::fprintf(stderr, "nativeee: nav: %s starts at stamp %llu\n", KindName(st.kind), static_cast<unsigned long long>(stamp));
    }
    const NavProbe::Sample& s = probe.Last();
    const NavProbe::Convention& c = probe.Conv();
    if (st.kind == NavKind::Pause) {
        if (stamp - st.started >= st.timeout) Finish(st, stamp, s, true, "elapsed");
        return;
    }
    if (st.kind == NavKind::Interact) {   // a press of `timeout` stamps; no eyes needed
        in->buttons = static_cast<uint16_t>(in->buttons | st.buttons);
        if (stamp + 1 >= st.started + st.timeout) Finish(st, stamp, s, true, "pressed");
        return;
    }
    if (st.kind == NavKind::Warp) {   // the one write; done this stamp either way
        const float xyz[3] = {static_cast<float>(st.x), static_cast<float>(st.y), static_cast<float>(st.z)};
        const bool ok = probe.PokeF32("pos_x", st.offset, xyz, 3);
        Finish(st, stamp, s, ok, ok ? "warped" : "poke failed");
        return;
    }
    if (stamp - st.started >= st.timeout) { Finish(st, stamp, s, false, "timed out"); return; }
    if (!s.valid) return;   // no player yet: wait (the timeout runs)
    switch (st.kind) {
        case NavKind::Goto:
        case NavKind::JumpWhenNear: SteerGoto(st, stamp, s, c, in); break;
        case NavKind::Climb: SteerClimb(st, stamp, s, c, in); break;
        case NavKind::Rail: SteerRail(st, stamp, s, c, in); break;
        case NavKind::Face: {
            const double err = WrapDeg(st.yaw - s.yaw);
            if (std::fabs(err) <= st.tol) { Finish(st, stamp, s, true, "facing"); return; }
            StickForHeading(st.yaw, s.cam_yaw, c, &in->lx, &in->ly);
            break;
        }
        case NavKind::WaitStill: {
            const double dx = s.x - st.last_x, dy = s.y - st.last_y, dz = s.z - st.last_z;
            const bool still = stamp > st.started && std::sqrt(dx * dx + dy * dy + dz * dz) < c.still_eps;
            st.still_run = still ? st.still_run + 1 : 0;
            st.last_x = s.x;
            st.last_y = s.y;
            st.last_z = s.z;
            if (st.still_run >= kStillStamps) Finish(st, stamp, s, true, "still");
            break;
        }
        case NavKind::Until: {
            double v = 0;
            bool have = probe.Value(st.probe, &v);
            if (!have) {   // the synthesised names when the table spells them differently
                if (st.probe == "x") { v = s.x; have = true; }
                else if (st.probe == "y") { v = s.y; have = true; }
                else if (st.probe == "z") { v = s.z; have = true; }
            }
            if (have && Compare(v, st.op, st.value)) {
                st.last_dist = v;
                Finish(st, stamp, s, true, "holds");
            }
            break;
        }
        case NavKind::Pause:
        case NavKind::Interact:
        case NavKind::Warp: break;
    }
}

void PadScript::SteerGoto(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in) {
    double heading = 0, dist = 0;
    HeadingTo(c, s.x, s.y, s.z, st.x, st.y, st.z, &heading, &dist);
    st.last_dist = dist;
    const double radius = st.radius > 0 ? st.radius : c.default_radius;
    if (st.press_until) {   // jump_when_near: the press in progress
        in->buttons = static_cast<uint16_t>(in->buttons | kCross);
        if (stamp + 1 >= st.press_until) Finish(st, stamp, s, true, "jumped");
        return;
    }
    if (dist <= radius) {
        if (st.kind == NavKind::Goto) { Finish(st, stamp, s, true, "reached"); return; }
        st.press_until = stamp + kJumpHold;
        in->buttons = static_cast<uint16_t>(in->buttons | kCross);
        return;
    }
    // Progress watch: the closest approach so far; stuck = none for kStuckStamps.
    if (st.best_stamp == 0 || dist < st.best_dist - 2.0) {
        st.best_dist = dist;
        st.best_stamp = stamp;
    }
    if (st.unstick_until && stamp >= st.unstick_until) {
        st.unstick_until = 0;
        // Real progress during the manoeuvre (100 units closer than when the cycle began) restarts the
        // escalation from the jump; otherwise the next, longer phase.
        st.unstick_phase = st.best_dist < st.unstick_base - 100.0 ? 0 : (st.unstick_phase + 1) % kPhases;
        st.best_stamp = stamp;   // give the new course its own window
    }
    if (!st.unstick_until && stamp - st.best_stamp >= kStuckStamps) {
        if (st.unstick_phase == 0) st.unstick_base = st.best_dist;
        st.unstick_until = stamp + (st.unstick_phase == 0 ? kUnstickJump : kDetours[st.unstick_phase - 1].stamps);
        ++st.unsticks;
        std::fprintf(stderr, "nativeee: nav: goto stuck at stamp %llu (dist %.1f, best %.1f): unstick %s%.0f\n",
                     static_cast<unsigned long long>(stamp), dist, st.best_dist, st.unstick_phase == 0 ? "jump " : "detour ",
                     st.unstick_phase == 0 ? 0.0 : kDetours[st.unstick_phase - 1].deg);
    }
    if (st.unstick_until) {
        if (st.unstick_phase == 0) {
            if (stamp < st.unstick_until - kUnstickJump + kJumpHold) in->buttons = static_cast<uint16_t>(in->buttons | kCross);
        } else {
            heading += kDetours[st.unstick_phase - 1].deg;
        }
    }
    StickForHeading(heading, s.cam_yaw, c, &in->lx, &in->ly);
}

bool PadScript::ApproachAndLeap(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, double near,
                                double face_x, double face_y, PadProtocol::Input* in) {
    double heading = 0, dist = 0;
    HeadingTo(c, s.x, s.y, s.z, st.x, st.y, st.z, &heading, &dist);
    double face_heading = 0, face_dist = 0;
    HeadingTo(c, s.x, s.y, s.z, face_x, face_y, s.z, &face_heading, &face_dist);
    st.last_dist = dist;
    auto next = [&](unsigned phase, const char* name) {
        st.phase = phase;
        st.phase_start = stamp;
        std::fprintf(stderr, "nativeee: nav: %s phase %s at stamp %llu (at %.1f,%.1f,%.1f dist %.1f)\n", KindName(st.kind), name,
                     static_cast<unsigned long long>(stamp), s.x, s.y, s.z, dist);
    };
    if (st.phase == 0) {   // approach: plain steering (no unstick: a jump here would grab the wrong thing)
        if (dist <= near) { next(1, "face"); }
        else if (stamp - st.phase_start >= kApproachMax) { Finish(st, stamp, s, false, "approach stalled"); return true; }
        else { StickForHeading(heading, s.cam_yaw, c, &in->lx, &in->ly); return true; }
    }
    if (st.phase == 1) {   // face the thing to grab
        if (std::fabs(WrapDeg(face_heading - s.yaw)) <= kFaceTol || stamp - st.phase_start >= kFaceMax) next(2, "leap");
        else { StickForHeading(face_heading, s.cam_yaw, c, &in->lx, &in->ly); return true; }
    }
    if (st.phase == 2) {   // the leap with the grab: CROSS first, CIRCLE held through the arc, stick at it
        const uint64_t t = stamp - st.phase_start;
        if (t < kJumpHold) in->buttons = static_cast<uint16_t>(in->buttons | kCross);
        if (t >= kGrabFrom) in->buttons = static_cast<uint16_t>(in->buttons | kCircle);
        StickForHeading(face_heading, s.cam_yaw, c, &in->lx, &in->ly);
        if (t + 1 >= kLeapStamps) {
            next(3, st.kind == NavKind::Climb ? "climb" : "walk");
            st.phase_best = s.z;
            st.phase_best_stamp = stamp;
        }
        return true;
    }
    return false;
}

void PadScript::SteerClimb(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in) {
    if (ApproachAndLeap(st, stamp, s, c, kClimbNear, st.x, st.y, in)) return;
    // Phase 3: stick straight up climbs the pipe / pole; done at the goal height, stalled when z stops rising.
    if (s.z >= st.z - kClimbGoalSlack) { Finish(st, stamp, s, true, "climbed"); return; }
    if (s.z > st.phase_best + 2.0) {
        st.phase_best = s.z;
        st.phase_best_stamp = stamp;
    }
    if (stamp - st.phase_best_stamp >= kClimbStall) { Finish(st, stamp, s, false, "climb stalled"); return; }
    in->lx = 0x80;
    in->ly = 0;
}

void PadScript::SteerRail(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in) {
    if (ApproachAndLeap(st, stamp, s, c, kRailNear, st.ex, st.ey, in)) return;
    // Phase 3: walk the rope toward its end.
    double heading = 0, dist = 0;
    HeadingTo(c, s.x, s.y, s.z, st.ex, st.ey, st.ez, &heading, &dist);
    st.last_dist = dist;
    if (dist <= kRailEndRadius) { Finish(st, stamp, s, true, "rail end"); return; }
    StickForHeading(heading, s.cam_yaw, c, &in->lx, &in->ly);
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
    if (!paths_.empty()) s += ":paths=" + std::to_string(paths_.size());
    if (!nav_.empty()) {
        s += ":nav=" + std::to_string(nav_.size());
        for (const NavStep& st : nav_) {
            s += std::string(":") + KindName(st.kind) + "@" + std::to_string(st.started) + "-" + std::to_string(st.ended) + "=" +
                 (st.done ? (st.reached ? "reached" : "timeout") : st.active ? "active" : "pending");
            if (st.unsticks) s += "/u" + std::to_string(st.unsticks);
        }
    }
    return s;
}

}  // namespace ps2r::title::sly2
