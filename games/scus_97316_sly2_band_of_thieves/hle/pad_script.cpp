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

namespace ps2r::title::sly2 {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint16_t kCross = 0x4000;
constexpr uint64_t kJumpHold = 8;
constexpr uint64_t kStillStamps = 10;
constexpr uint64_t kStuckStamps = 30;     // goto: no closer approach for this long = stuck
constexpr uint64_t kUnstickJump = 20;     // the jump phase's length
// The detour phases after the jump: (heading offset in degrees, length in stamps), escalating -- a prop
// needs a short sidestep, a building a long walk along its wall; the cycle restarts from the jump.
struct Detour { double deg; uint64_t stamps; };
constexpr Detour kDetours[] = {{60, 40}, {-60, 40}, {90, 90}, {-90, 90}, {135, 120}, {-135, 120}};
constexpr unsigned kPhases = 1 + sizeof(kDetours) / sizeof(kDetours[0]);

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
    }
    return "?";
}

}  // namespace

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
            // ':' tail (radius / timeout) -- "goto:100:1.5,2,3:0.5" tokenises as goto:100:1.5 , 2 , 3:0.5.
            std::string whole = t;
            size_t j = i + 1;
            while (j + 1 < tokens.size() && tokens[j] == "," && !tokens[j + 1].empty() &&
                   (std::isdigit(static_cast<unsigned char>(tokens[j + 1][0])) || tokens[j + 1][0] == '-' || tokens[j + 1][0] == '.')) {
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

// kind:from:<fields...> -- see the header's grammar.
bool PadScript::ParseNavToken(const std::string& t) {
    const std::vector<std::string> f = Split(t, ':');
    if (f.size() < 2) return false;
    NavStep st{};
    if (f[0] == "goto") st.kind = NavKind::Goto;
    else if (f[0] == "jump_when_near") st.kind = NavKind::JumpWhenNear;
    else if (f[0] == "face") st.kind = NavKind::Face;
    else if (f[0] == "wait_still") st.kind = NavKind::WaitStill;
    else if (f[0] == "nav_pause") st.kind = NavKind::Pause;
    else return false;
    if (!UNum(f[1], &st.from)) return false;
    switch (st.kind) {
        case NavKind::Goto:
        case NavKind::JumpWhenNear: {
            if (f.size() < 3) return false;
            const std::vector<std::string> xyz = Split(f[2], ',');
            if (xyz.size() != 3 || !Num(xyz[0], &st.x) || !Num(xyz[1], &st.y) || !Num(xyz[2], &st.z)) return false;
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

void PadScript::Apply(uint64_t stamp, const NavProbe& probe, PadProtocol::Input* in) {
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

void PadScript::Steer(uint64_t stamp, const NavProbe& probe, PadProtocol::Input* in) {
    NavStep& st = nav_[nav_index_];
    if (!st.active) {
        if (stamp < st.from || stamp < nav_prev_end_) return;
        st.active = true;
        st.started = stamp;
        std::fprintf(stderr, "nativeee: nav: %s starts at stamp %llu\n", KindName(st.kind), static_cast<unsigned long long>(stamp));
    }
    const NavProbe::Sample& s = probe.Last();
    const NavProbe::Convention& c = probe.Conv();
    auto finish = [&](bool reached, const char* why) {
        st.done = true;
        st.active = false;
        st.reached = reached;
        st.ended = stamp;
        nav_prev_end_ = stamp;
        ++nav_index_;
        std::fprintf(stderr, "nativeee: nav: %s %s at stamp %llu after %llu (at %.2f,%.2f,%.2f dist %.2f)\n", KindName(st.kind), why,
                     static_cast<unsigned long long>(stamp), static_cast<unsigned long long>(stamp - st.started), s.x, s.y, s.z, st.last_dist);
    };
    if (st.kind == NavKind::Pause) {
        if (stamp - st.started >= st.timeout) finish(true, "elapsed");
        return;
    }
    if (stamp - st.started >= st.timeout) { finish(false, "timed out"); return; }
    if (!s.valid) return;   // no player yet: wait (the timeout runs)
    switch (st.kind) {
        case NavKind::Goto:
        case NavKind::JumpWhenNear: {
            double heading = 0, dist = 0;
            HeadingTo(c, s.x, s.y, s.z, st.x, st.y, st.z, &heading, &dist);
            st.last_dist = dist;
            const double radius = st.radius > 0 ? st.radius : c.default_radius;
            if (st.press_until) {   // jump_when_near: the press in progress
                in->buttons = static_cast<uint16_t>(in->buttons | kCross);
                if (stamp + 1 >= st.press_until) finish(true, "jumped");
                return;
            }
            if (dist <= radius) {
                if (st.kind == NavKind::Goto) { finish(true, "reached"); return; }
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
            break;
        }
        case NavKind::Face: {
            const double err = WrapDeg(st.yaw - s.yaw);
            if (std::fabs(err) <= st.tol) { finish(true, "facing"); return; }
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
            if (st.still_run >= kStillStamps) finish(true, "still");
            break;
        }
        case NavKind::Pause: break;
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
