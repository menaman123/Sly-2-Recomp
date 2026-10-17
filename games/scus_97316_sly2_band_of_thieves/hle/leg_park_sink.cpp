// games/scus_97316_sly2_band_of_thieves/hle/leg_park_sink.cpp -- LegParkSink (leg_park_sink.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_park_sink.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "engine/core/verify/run_meta.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/thread_manager.h"
#include "games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h"

namespace ps2r::title::sly2 {

LegParkSink::LegParkSink(ps2r::verify::VerifyHost& host, LegCensus& census, LegPark& park, LegPlay& play, LegDisc& disc, LegIop& iop) {
    using ps2r::triage::TriageBundle;
    LegCloser& closer = park.closer;
    TriageBundle::InstallTerminateHook([&census, &closer](const char* what) {
        census.Write();
        closer.Close("exception", 0, ThreadManager::Instance().CurrentTid(), what);
    });
    GuestPark::SetSink([&census, &closer, &play](const ParkRecord& r) {
        TriageBundle::NoteThrowSite(r.detail ? r.detail : r.reason);
        census.Write();
        closer.Close(r.reason, r.pc, ThreadManager::Instance().CurrentTid(), r.detail);
        // Play: the session closes cleanly around the park (the window goes, the workers are
        // joined); a headless leg's park is its result -- leave at once, census written.
        if (play.session) {
            play.session->RequestClose();
            throw ThreadExitUnwind{};
        }
        std::_Exit(0);
    });

    // The IOP reboot image (booted here, after the park sink exists, so an unresolved import of a
    // hosted module parks visibly).
    {
        IsoFs::Entry rp;
        std::vector<uint8_t> rp_bytes;
        bool booted = false;
        if (disc.iso_open && disc.iso_fs.Find(IopModules::kRebootImage, rp) && !rp.directory && rp.size) {
            rp_bytes.resize(rp.size);
            const size_t got = disc.iso_fs.ReadBytes(static_cast<uint64_t>(rp.lba) * IsoFs::kSectorSize, rp.size, rp_bytes.data());
            if (got == rp.size) booted = iop.iop_host.BootRebootImageModules(rp_bytes.data(), rp_bytes.size());
        }
        if (!booted) std::fprintf(stderr, "nativeee: the reboot image's FILEIO module was not hosted (%s)\n",
                                  disc.iso_open ? "not found or refused" : "no disc image");
        if (auto* meta = host.Meta()) meta->Set("iop_reboot_image_hosted", booted ? "1" : "0");
    }
}

}  // namespace ps2r::title::sly2
