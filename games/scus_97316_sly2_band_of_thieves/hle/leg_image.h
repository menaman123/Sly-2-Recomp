// LegImage: loads the ELF (entry checked against the TitleProfile), sets up the boot environment and the
// generated dispatch table, and binds the SDK: builtins, sceGsSync, sceSif (over the IOP host), libcdvd
// (over the disc), libmc (over the host card store) and the title's hle/sdk_bindings.tsv rows. Installs the
// frame boundary at TitleProfile::Gs::vblank_wait_entry, since Sly 2 links no sceGsSyncV.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/verify/verify_host.h"
#include "engine/title/title_profile.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_disc.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_iop.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_services.h"

namespace ps2r::title::sly2 {

class LegImage {
public:
    LegImage(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& elf_path,
             const std::string& title_dir, const LegKnobs& knobs, LegCore& core, LegDisc& disc, LegServices& services, LegIop& iop);
    LegImage(const LegImage&) = delete;
    LegImage& operator=(const LegImage&) = delete;

    // 0, or the exit code after a failed install (already reported).
    int Error() const { return error_; }

    std::string iop_modules;          // run.meta iop_modules=: module loads, in order
    uint64_t iop_module_loads = 0;
    uint64_t boundary_site_hits = 0;  // run.meta boundary_site_hits=: WaitForVblank mirror entries

    // NATIVEEE_FN_CENSUS: per-function call counts.
    void WriteFnCensus();

private:
    bool LoadElf(ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& elf_path, LegCore& core);
    bool InstallDispatch(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& title_dir,
                         const LegKnobs& knobs, LegCore& core, LegDisc& disc, LegServices& services, LegIop& iop);
    void InstallFnTrace(const std::string& list);
    void InstallFnCensus();

    std::string fn_census_path_;
    std::vector<std::shared_ptr<std::atomic<uint64_t>>> fn_census_counts_;   // by dense id

    int error_ = 0;
};

}  // namespace ps2r::title::sly2
