// PadScript: the NATIVEEE_PAD_SCRIPT overlay
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
// a word; their numeric fields are separated by ':' and a target triple by ',':
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
//                                                             building walls of a rooftop hub. (A jump next to a
//                                                             lamp post or pipe GRABS it in this title: Sly climbs.)
//   jump_when_near:<from>:<x>,<y>,<z>:<radius>[:<timeout>]    goto, then press CROSS for 8 stamps once near
//   face:<from>:<yaw_deg>[:<tol>[:<timeout>]]                 push the stick along world heading yaw_deg
//                                                             until Sly's yaw is within tol (default 10) of
//                                                             it, at most `timeout` stamps (default 120)
//   wait_still:<from>[:<timeout>]                             neutral until Sly's position moved under
//                                                             still_eps for 10 consecutive stamps (default
//                                                             timeout 300)
//   nav_pause:<from>:<stamps>                                 hold the closed-loop queue for `stamps`
//                                                             (a timed gap between two goto's)
//   climb:<from>:<x>,<y>,<z>[:<timeout>]                      Sly's pipe / pole climb: walk to within 70 of
//                                                             (x,y), face it, leap (CROSS 8 stamps, stick at it)
//                                                             with CIRCLE held through the leap (the grab), then
//                                                             hold the stick UP (ly 0) to climb until pos_z
//                                                             reaches z - 15 (reached) or stops rising for 45
//                                                             stamps / `timeout` (default 600) passes (timeout)
//   rail:<from>:<x>,<y>,<z>:<ex>,<ey>,<ez>[:<timeout>]        a rope / rail walk: walk to within 50 of the
//                                                             rope's start (x,y), face the end, leap onto it with
//                                                             CIRCLE held, then steer toward (ex,ey) along the
//                                                             rope until within 40 (reached) or `timeout`
//                                                             (default 900)
//   until:<from>:<probe><op><value>[:<timeout>]               neutral pad until the probe expression holds:
//                                                             <probe> = a name of the probe table (pos_z,
//                                                             level_time, ...), <op> one of < > <= >= == !=
//                                                             (== / != within 0.5); default timeout 600; the
//                                                             timeout path reports `timeout`, the hold `reached`
//   interact:<from>[:<hold>[:<buttons_hex>]]                  press CIRCLE (or `buttons_hex`) for `hold` stamps
//                                                             (default 10) at the queue's turn -- the marker /
//                                                             switch / pickup press
//   warp:<from>:<x>,<y>,<z>[:<off_hex>]                       TOOLING, never part of a judged route: write the
//                                                             triple as three f32 at `off_hex` (default the
//                                                             player's authoritative position, kWarpOffset in
//                                                             pad_script.cpp) from the object the probe table's
//                                                             pos_x chain dereferences (NavProbe::PokeF32) --
//                                                             Sly appears there next frame and falls onto
//                                                             whatever is below: the terrain survey (landing z
//                                                             per (x,y)) and the transport shortcut past a
//                                                             traversal the navigator cannot do
//   path:<from>:<name>[:<radius>]                             expand the named waypoint list of the PATH TABLE
//                                                             (verify/nav/ep1_waypoints.json: {"paths": {"<name>":
//                                                             ["<closed-loop token>", ...]}}) in place: its steps
//                                                             queue in order (`from` applies to the first; a
//                                                             goto without a radius takes `radius`). An unknown
//                                                             name / table truncates the script (Warning()).
// Closed-loop steps form a QUEUE in script order: each starts at the later of its `from` stamp and the
// previous closed-loop step's end, owns the left stick while active (a timed step's sticks at the same
// stamp lose), and ends when its condition holds or its timeout passes. While the probe's sample is
// invalid (no player object) a step waits (its timeout still runs). Census() reports each step's outcome.
#pragma once

#include <cstdint>
#include <map>
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
    enum class NavKind { Goto, JumpWhenNear, Face, WaitStill, Pause, Climb, Rail, Until, Interact, Warp };
    struct NavStep {
        NavKind kind = NavKind::Goto;
        uint64_t from = 0;        // the earliest start stamp
        double x = 0, y = 0, z = 0;
        double ex = 0, ey = 0, ez = 0;   // rail: the rope's end
        double radius = 0;        // 0 = the table's default
        double yaw = 0, tol = 10;
        uint64_t timeout = 900;
        std::string probe, op;    // until: the expression
        double value = 0;
        uint16_t buttons = 0;     // interact
        uint32_t offset = 0;      // warp: the f32 triple's offset from the pos_x object
        // outcome
        uint64_t started = 0, ended = 0;
        bool active = false, done = false, reached = false;
        unsigned phase = 0;         // climb / rail: 0 approach, 1 face, 2 leap, 3 climb / walk
        uint64_t phase_start = 0;
        double phase_best = 0;      // climb: the highest z seen in phase 3 ...
        uint64_t phase_best_stamp = 0;   // ... and when
        uint64_t still_run = 0;   // wait_still: consecutive still stamps
        uint64_t press_until = 0;   // jump_when_near / interact: the press's end stamp
        double best_dist = 0;       // goto: the closest approach so far ...
        uint64_t best_stamp = 0;    // ... and when; no improvement for 30 stamps starts an unstick manoeuvre
        uint64_t unstick_until = 0; // the manoeuvre's end stamp (0 = none)
        unsigned unstick_phase = 0; // 0 jump, then the escalating detours (pad_script.cpp kDetours); cycles
        double unstick_base = 0;    // best_dist when the current escalation cycle began
        uint64_t unsticks = 0;
        double last_x = 0, last_y = 0, last_z = 0, last_dist = 0;
    };

    PadScript() = default;

    // The path table for `path:` steps (verify/nav/ep1_waypoints.json). A missing file is not an error
    // (no paths); a malformed one is reported and ignored. Call before Load().
    bool LoadPaths(const std::string& json_path);
    // `spec` = the knob's value: a readable regular file is read and parsed (Source() = "file"),
    // anything else is parsed as the script text (Source() = "inline"). Returns false only when
    // `spec` names a file that exists but cannot be read; an empty spec is an empty script.
    bool Load(const std::string& spec);
    // Parses `text` (replacing the current steps).
    void ParseText(const std::string& text);

    // The boundary duty: overlay every timed step live at `stamp` on `in`, then the active closed-loop
    // step steering from probe.Last() (read this stamp).
    void Apply(uint64_t stamp, NavProbe& probe, PadProtocol::Input* in);

    const std::vector<Step>& Steps() const { return steps_; }
    bool Empty() const { return steps_.empty() && nav_.empty(); }
    bool ClosedLoop() const { return !nav_.empty(); }
    const std::string& Source() const { return source_; }
    // The stray token that truncated the script ("" = the whole text parsed).
    const std::string& Warning() const { return warning_; }
    // The path table's load report ("" = none / fine).
    const std::string& PathsWarning() const { return paths_warning_; }
    // The first stamp past every timed step (0 = no steps).
    uint64_t End() const;
    // run.meta pad_script= body: steps=,end=,source=[,truncated_at=][,nav=<n>:<kind>@<start>-<end>=<reached|timeout|active|pending>[/u<unsticks>]...]
    std::string Census() const;

    // The left stick for a world heading (degrees) under the convention: lx,ly in 0..255.
    static void StickForHeading(double heading_deg, double cam_yaw_deg, const NavProbe::Convention& c, uint8_t* lx, uint8_t* ly);
    // The horizontal heading (degrees) and distance from (x,y,z) to the target under the convention's up axis.
    static void HeadingTo(const NavProbe::Convention& c, double x, double y, double z, double tx, double ty, double tz, double* heading,
                          double* dist);
    // The `until:` expression over a sample value.
    static bool Compare(double value, const std::string& op, double rhs);

private:
    bool ParseNavToken(const std::string& t);
    bool ParseTriple(const std::string& f, double* x, double* y, double* z);
    bool ExpandPath(const std::vector<std::string>& f);
    void Steer(uint64_t stamp, NavProbe& probe, PadProtocol::Input* in);
    void Finish(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, bool reached, const char* why);
    void SteerGoto(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in);
    void SteerClimb(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in);
    void SteerRail(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, PadProtocol::Input* in);
    // climb / rail phases 0..2 shared: approach (x,y) within `near`, face `heading_to`, leap with the grab.
    // Returns true while a phase is still running (the stick / buttons were set).
    bool ApproachAndLeap(NavStep& st, uint64_t stamp, const NavProbe::Sample& s, const NavProbe::Convention& c, double near,
                         double face_x, double face_y, PadProtocol::Input* in);

    std::vector<Step> steps_;
    std::vector<NavStep> nav_;
    std::map<std::string, std::vector<std::string>> paths_;
    size_t nav_index_ = 0;      // the current closed-loop step
    uint64_t nav_prev_end_ = 0;
    std::string source_ = "none";
    std::string warning_, paths_warning_;
};

}  // namespace ps2r::title::sly2
