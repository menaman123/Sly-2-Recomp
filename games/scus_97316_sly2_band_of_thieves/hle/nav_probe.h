// games/scus_97316_sly2_band_of_thieves/hle/nav_probe.h -- NavProbe: the title-side reader of Sly's world
// position / facing and the camera's yaw out of guest RAM at every pad stamp (the navigator's eyes), plus the
// RAM-dump hunt that found them. Boot triage / coverage tooling in the pad-script family (leg_pad.h slot 4):
// nothing here changes what the game sees; it only READS RAM at the boundary and feeds (a) the per-stamp
// CSV `nav_trace.csv` under the harness out dir and (b) PadScript's closed-loop steps (goto / face /
// wait_still / jump_when_near), which synthesise the left stick from the camera-relative heading to a target.
//
//   NATIVEEE_NAV=<item>[;<item>...]
//     probe=<file>                   the probe table (default <title dir>/verify/nav/sly2_probe.json)
//     trace[=<csv>]                  write every stamp's sample (default <harness out>/nav_trace.csv); the trace
//                                    is also on whenever the pad script holds a closed-loop step
//     every=<n>                      trace every n-th stamp (default 1)
//     ramdump=<dir>@<s>[+<s>...]     the hunt: write the 32 MiB guest RAM as <dir>/ram_<stamp>.bin at those stamps
//
// The probe table is DATA about the title (reverse-engineered facts, no game bytes), one object per value:
//   {"probes": [{"name": "pos_x", "chain": ["0x<static addr>", "0x<off>", ...], "type": "f32", "scale": 1.0}, ...]}
// chain[0] is a static EE address; every further element is an offset applied AFTER dereferencing the previous
// word (addr = read32(addr) + off): a one-element chain is a plain static variable. Types: f32 i32 u32 i16 u16
// u8 i8 atan2_deg; `scale` multiplies the read (an angle stored as a fixed-point short becomes degrees, say). A chain
// whose intermediate pointer is 0 or outside RAM makes the sample invalid for that stamp (the game has no
// player yet: loading, the resume screen). Type atan2_deg reads two f32 (x, y) at the address and yields
// atan2(y, x) in degrees -- a facing from a matrix row. Well-known names fill NavProbe::Sample's fields -- pos_x pos_y
// pos_z yaw cam_yaw cam_pitch on_ground -- every probe lands in the CSV in table order:
//   stamp,valid,<name>,<name>,...   (the header line names them)
// An optional "nav" object states the title's conventions the closed-loop steps steer by (pad_script.h):
//   {"up_axis": "y"|"z", "forward_offset_deg": <d>, "right_sign": 1|-1, "default_radius": <r>, "still_eps": <e>}
// forward_offset_deg: the world heading (degrees, atan2 over the two horizontal axes in x,y,z order) Sly walks
// when the left stick is pushed straight up with the camera at yaw 0; right_sign: +1 when pushing right turns
// that heading by +90 degrees. Both are calibrated from a trace (verify/nav/nav_calibrate.py).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/ee/mem/main_memory.h"

namespace ps2r::title::sly2 {

class NavProbe {
public:
    struct Probe {
        std::string name;
        std::vector<uint32_t> chain;   // chain[0] static address, then offsets
        std::string type = "f32";
        double scale = 1.0;
    };
    struct Convention {
        char up_axis = 'y';
        double forward_offset_deg = 0;
        double right_sign = 1;
        double default_radius = 1.0;
        double still_eps = 0.01;
    };
    struct Sample {
        bool valid = false;
        uint64_t stamp = 0;
        float x = 0, y = 0, z = 0;       // Sly's world position (the title's units)
        float yaw = 0;                   // Sly's facing, degrees (after `scale`)
        float cam_yaw = 0, cam_pitch = 0;   // the camera, degrees (after `scale`)
        int on_ground = -1;              // -1 = no such probe
        std::vector<double> values;      // every probe in table order (NaN = unreadable)
    };

    // `spec` = the NATIVEEE_NAV value (nullptr = unset: nothing armed). `title_dir` resolves the default probe
    // table, `out_dir` the default trace path (empty = no trace unless `trace=<csv>` names one).
    NavProbe(const char* spec, const std::string& title_dir, const std::string& out_dir, MainMemory& ram);
    ~NavProbe();
    NavProbe(const NavProbe&) = delete;
    NavProbe& operator=(const NavProbe&) = delete;

    // True when a probe table loaded (the closed-loop steps have eyes).
    bool Armed() const { return !probes_.empty(); }
    // Without NATIVEEE_NAV the probe is idle; a pad script with closed-loop steps calls this to load the default
    // table and open the trace (idempotent).
    void EnsureArmed();
    // Turn the trace on; idempotent.
    void EnableTrace();
    const Convention& Conv() const { return conv_; }

    // The boundary duty (slot 4, before the pad is composed): read every probe, write the trace line, the
    // RAM dump when this stamp is listed. Returns the sample (also kept as Last()).
    const Sample& Read(uint64_t stamp);
    const Sample& Last() const { return last_; }
    // The last sample's value of the probe called `name` (false = no such probe or unreadable this stamp).
    bool Value(const std::string& name, double* out) const;

    const std::vector<Probe>& Probes() const { return probes_; }
    const std::string& ProbePath() const { return probe_path_; }
    // run.meta nav_probe= body: probes=,table=,trace=,samples=,valid=,ramdumps= (or "off")
    std::string Census() const;

private:
    bool LoadTable(const std::string& path);
    bool ReadProbe(const Probe& p, double* out) const;
    bool ReadWord(uint32_t addr, uint32_t* out) const;
    void OpenTrace();
    void DumpRam(uint64_t stamp);

    MainMemory& ram_;
    std::string probe_path_, trace_path_, ramdump_dir_, table_error_;
    std::vector<uint64_t> ramdump_stamps_;
    std::vector<Probe> probes_;
    Convention conv_;
    bool spec_given_ = false, table_tried_ = false;
    bool trace_wanted_ = false;
    uint64_t every_ = 1;
    std::FILE* trace_ = nullptr;
    Sample last_;
    uint64_t samples_ = 0, valid_ = 0, ramdumps_ = 0;
};

}  // namespace ps2r::title::sly2
