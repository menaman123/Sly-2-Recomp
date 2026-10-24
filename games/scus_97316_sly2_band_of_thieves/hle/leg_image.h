// games/scus_97316_sly2_band_of_thieves/hle/leg_image.h -- LegImage: the ELF image + boot environment and
// the generated dispatch + the SDK binding manifest of the title's play composition. The ELF is loaded
// into main memory and its entry checked against the TitleProfile; BootEnv is configured and pre-seeded;
// the generated dispatch table is initialised; the manifest's builtins, the sceGsSync family, the sceSif
// rows (the IOP heap = the host's sysmem heap; sceSifLoadModuleBuffer answered by the IOP host, the load
// order kept in run.meta iop_modules=), the libcdvd rows over the disc and the libmc rows over the host
// store are bound; the title's hle/sdk_bindings.tsv rows are resolved and installed (wait bodies with
// their continuation proofs); the frame boundary site (TitleProfile::Gs::vblank_wait_entry -- the
// engine's WaitForVblank mirror where the title links no sceGsSyncV); NATIVEEE_FN_TRACE wraps generated
// functions with entry / return prints (boot triage only). The run.meta rows elf_*, sif_rows,
// cdvd_rows, libmc_rows, title, functions, sdk_rows* are written here.
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

    // 0, or the leg's exit code when the image / manifest could not be installed (the message printed).
    int Error() const { return error_; }

    std::string iop_modules;          // run.meta iop_modules=: every module load answered, in order
    uint64_t iop_module_loads = 0;
    uint64_t boundary_site_hits = 0;  // run.meta boundary_site_hits=: WaitForVblank mirror entries

    // NATIVEEE_FN_CENSUS: writes the per-function call counts (every exit path, from leg_census).
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
