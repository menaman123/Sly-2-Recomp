// games/scus_97316_sly2_band_of_thieves/hle/pad_script.h -- PadScript: the NATIVEEE_PAD_SCRIPT overlay
// of the title composition (boot triage / replay / coverage navigation). The value is either the script
// text itself or the path of a readable file holding it (what PadRecorder writes), in one grammar:
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
//
// CLOSED-LOOP steps (the navigator; they need NavProbe's eyes, nav_probe.h) are tokens whose first field is
// a word; their numeric fields are separated by ':' and the target by ',':
//   goto:<from>:<x>,<y>,<z>[:<radius>[:<timeout>]]            hold the left stick toward the target (camera-
//                                                             relative heading) until the horizontal distance
//                                                             is under `radius` (default: the table's) or
//                                                             `timeout` stamps (default 900) pass. When the
//                                                             distance has not improved for 30 stamps the step
//                                                             UNSTICKS: a CROSS jump while steering (20 stamps),
//                                                             then escalating detours (+-60 deg 40 stamps, +-90
//                                                             deg 90, +-135 deg 120; a bug-style wall walk),
//                                                             restarting from the jump once 100 units of
//                                                             progress were made -- the ledges, props and
//                                                             building walls of a rooftop hub
//   jump_when_near:<from>:<x>,<y>,<z>:<radius>[:<timeout>]    goto, then press CROSS for 8 stamps once near
//   face:<from>:<yaw_deg>[:<tol>[:<timeout>]]                 push the stick along world heading yaw_deg
//                                                             until Sly's yaw is within tol (default 10) of
//                                                             it, at most `timeout` stamps (default 120)
//   wait_still:<from>[:<timeout>]                             neutral until Sly's position moved under
//                                                             still_eps for 10 consecutive stamps (default
//                                                             timeout 300)
//   nav_pause:<from>:<hold>                                   hold the closed-loop queue for `hold` stamps
//                                                             (a timed gap between two goto's)
// Closed-loop steps form a QUEUE in script order: each starts at the later of its `from` stamp and the
// previous closed-loop step's end, owns the left stick while active (a timed step's sticks at the same
// stamp lose), and ends when its condition holds or its timeout passes. While the probe's sample is
// invalid (no player object) a step waits (its timeout still runs). Census() reports each step's outcome.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/pad/hle/pad_protocol.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/nav_probe.h"

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
    enum class NavKind { Goto, JumpWhenNear, Face, WaitStill, Pause };
    struct NavStep {
        NavKind kind = NavKind::Goto;
        uint64_t from = 0;        // the earliest start stamp
        double x = 0, y = 0, z = 0;
        double radius = 0;        // 0 = the table's default
        double yaw = 0, tol = 10;
        uint64_t timeout = 900;
        // outcome
        uint64_t started = 0, ended = 0;
        bool active = false, done = false, reached = false;
        uint64_t still_run = 0;   // wait_still: consecutive still stamps
        uint64_t press_until = 0;   // jump_when_near: the CROSS hold's end stamp
        double best_dist = 0;       // goto: the closest approach so far ...
        uint64_t best_stamp = 0;    // ... and when; no improvement for 30 stamps starts an unstick manoeuvre
        uint64_t unstick_until = 0; // the manoeuvre's end stamp (0 = none)
        unsigned unstick_phase = 0; // 0 jump, then the escalating detours (pad_script.cpp kDetours); cycles
        double unstick_base = 0;    // best_dist when the current escalation cycle began
        uint64_t unsticks = 0;
        double last_x = 0, last_y = 0, last_z = 0, last_dist = 0;
    };

    PadScript() = default;

    // `spec` = the knob's value: a readable regular file is read and parsed (Source() = "file"),
    // anything else is parsed as the script text (Source() = "inline"). Returns false only when
    // `spec` names a file that exists but cannot be read; an empty spec is an empty script.
    bool Load(const std::string& spec);
    // Parses `text` (replacing the current steps).
    void ParseText(const std::string& text);

    // The boundary duty: overlay every timed step live at `stamp` on `in`, then the active closed-loop
    // step steering from probe.Last() (read this stamp).
    void Apply(uint64_t stamp, const NavProbe& probe, PadProtocol::Input* in);

    const std::vector<Step>& Steps() const { return steps_; }
    const std::vector<NavStep>& NavSteps() const { return nav_; }
    bool Empty() const { return steps_.empty() && nav_.empty(); }
    bool ClosedLoop() const { return !nav_.empty(); }
    const std::string& Source() const { return source_; }
    // The stray token that truncated the script ("" = the whole text parsed).
    const std::string& Warning() const { return warning_; }
    // The first stamp past every timed step (0 = no steps).
    uint64_t End() const;
    // run.meta pad_script= body: steps=,end=,source=[,truncated_at=][,nav=<n>:<kind>@<start>-<end>=<reached|timeout|active|pending>[/u<unsticks>]...]
    std::string Census() const;

    // The left stick for a world heading (degrees) under the convention: lx,ly in 0..255.
    static void StickForHeading(double heading_deg, double cam_yaw_deg, const NavProbe::Convention& c, uint8_t* lx, uint8_t* ly);
    // The horizontal heading (degrees) and distance from (x,y,z) to the target under the convention's up axis.
    static void HeadingTo(const NavProbe::Convention& c, double x, double y, double z, double tx, double ty, double tz, double* heading,
                          double* dist);

private:
    bool ParseNavToken(const std::string& t);
    void Steer(uint64_t stamp, const NavProbe& probe, PadProtocol::Input* in);

    std::vector<Step> steps_;
    std::vector<NavStep> nav_;
    size_t nav_index_ = 0;      // the current closed-loop step
    uint64_t nav_prev_end_ = 0;
    std::string source_ = "none";
    std::string warning_;
};

}  // namespace ps2r::title::sly2
