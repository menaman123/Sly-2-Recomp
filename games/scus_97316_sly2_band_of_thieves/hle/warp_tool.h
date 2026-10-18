// games/scus_97316_sly2_band_of_thieves/hle/warp_tool.h -- WarpTool: the title-side episode warp. Coverage
// tooling in the pad-script family: before the cold boot it rewrites the save the game is about to read from
// the PRIVATE card directory (cfg.paths.mc_dir/mc0/BASCUS-97316YAOTWTD!/save1) so that the game's own "Resume
// Game" path (FUN_00165e38: episodeCurrent -> the per-episode hub-file table -> the level loader) lands in the
// chosen episode's hub. Nothing else changes: the game parses, validates (trailer) and loads the save exactly as
// it would a card written on a console; the field offsets and the episode / mission / task vocabulary are DATA
// in verify/nav/sly2_probe.json ("save": the schema read from a RAM dump, warp_notes.md).
//
//   NATIVEEE_WARP=<episode>[,<job>]
//     <episode>   0..8 or a name (intro nightclub palace temple prison castle train sawmill blimp)
//     <job>       optional: the number of the episode's jobs (sly2_probe.json "save".episodes[].jobs order) to
//                 mark complete (mission is_complete + the t<ep>_<job>_intro / _complete tasks state 3); giving
//                 ANY job count (0 included) also marks t<ep>_intro complete, which skips the episode's opening
//                 cutscene. Without <job> the episode starts fresh (its intro plays).
//
// What the rewrite sets (save offsets from the table): episodeCurrent = episodeBest = <episode>; widCurrent =
// the episode's exterior wid; oidPoCurrent = -1 (the loader's default spawn point); every earlier episode's
// percent_complete = 100 (the chosen one is left < 100 so Resume loads its hub, not the episode-complete
// level); dt (play time) is forced non-zero (a zero dt means "empty slot" to the slot screen). The rest of the
// save (settings, health, the Paris progress) is left as found: the Paris autosave card is the fixture.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "games/scus_97316_sly2_band_of_thieves/hle/nav_json.h"

namespace ps2r::title::sly2 {

class WarpTool {
public:
    // `spec` = the NATIVEEE_WARP value (nullptr = unset: nothing happens). `mc_dir` = cfg.paths.mc_dir (the
    // directory holding mc0/), `title_dir` resolves verify/nav/sly2_probe.json. The rewrite happens in the
    // constructor (before the boot); Applied()/Census() report it.
    WarpTool(const char* spec, const std::string& mc_dir, const std::string& title_dir);
    WarpTool(const WarpTool&) = delete;
    WarpTool& operator=(const WarpTool&) = delete;

    bool Requested() const { return requested_; }
    bool Applied() const { return applied_; }
    int Episode() const { return episode_; }
    // One run.meta row: off | warp=<ep>:<name>:wid=<w>:jobs=<n>:<applied|failed:<why>>
    std::string Census() const;

    // The rewrite on a save image in memory (the file's bytes), exposed for the self-test: returns false and
    // sets *error when the image or the table refuses. `jobs` < 0 = no job / intro change.
    static bool Rewrite(std::vector<uint8_t>& image, const NavJson& save_table, int episode, int jobs, std::string* error);

private:
    // A block table of the save ("missions" / "tasks"): base + stride * index, with the field offsets the
    // table names; Set writes one u32 field of the named block (false: unknown name / outside the image).
    struct Blocks {
        uint32_t base = 0, stride = 0;
        const NavJson* names = nullptr;
        bool Load(const NavJson* table);
        bool Set(std::vector<uint8_t>& image, const std::string& name, uint32_t field_off, uint32_t value) const;
    };

    bool LoadTable(const std::string& path, NavJson* out, std::string* error);
    static bool ParseSpec(const std::string& spec, const NavJson& save_table, int* episode, int* jobs, std::string* error);

    bool requested_ = false;
    bool applied_ = false;
    int episode_ = -1;
    int jobs_ = -1;
    uint32_t wid_ = 0;
    std::string name_;
    std::string error_;
};

}  // namespace ps2r::title::sly2
