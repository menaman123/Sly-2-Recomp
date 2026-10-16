// games/scus_97316_sly2_band_of_thieves/hle/iop_host.h -- IopHost: the title's IOP
// composition. Hosts the three recompiled embedded modules (lgAud, 989snd, spiop; iop/) on the
// engine's cooperative IOP kernel with the stock Sony modules served by the engine's HLE:
//
//   * the EE's sceSifSetDma pushes land in IOP RAM (SifDmaCopy); sceSifAllocIopHeap is the
//     sysmem heap (HeapAlloc / HeapFree);
//   * sceSifLoadModuleBuffer("buffer@<iop addr> <args>") reads the pushed image's .iopmod name:
//     a hosted module is SHA-256 checked against the build's input, relocated to its base
//     (IopLoader + iop_relocator -- the same relocation the recompiler applied), its entry run
//     with argc/argv, its threads dispatched to their first block, and registered (module id);
//     libsd / usbd / sio2man / padman / mcman / mcserv are rows (their libraries are the
//     engine's; usbd's image is loaded so a probe of its text finds it);
//   * every library import of a hosted module resolves through the IopLibraryTable (stock HLE
//     bodies + the RegisterLibraryEntries tables of the other hosted modules); an import no
//     library serves is a PARK (iop_import_unresolved), never a silent answer;
//   * every sceSifRegisterRpc a hosted module makes becomes an IopRpcServer on the EE's
//     RpcRouter, so the EE's sceSifBindRpc/sceSifCallRpc into it run the module's own handler.
//
// No reply is faked anywhere: what the modules do is what their recompiled code does.
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/iop/hle/cdvdman/cdvdman_module.h"
#include "engine/iop/hle/intrman/intrman_module.h"
#include "engine/iop/hle/ioman/ioman_module.h"
#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/iop/hle/loadcore/loadcore_module.h"
#include "engine/iop/hle/modload/modload_module.h"
#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/hle/sifman/sifman_module.h"
#include "engine/iop/hle/stdio/stdio_module.h"
#include "engine/iop/hle/sysclib/sysclib_module.h"
#include "engine/iop/hle/sysmem/sysmem_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thevent/thevent_module.h"
#include "engine/iop/hle/thmsgbx/thmsgbx_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/hle/timrman/timrman_module.h"
#include "engine/iop/hle/usbd/usbd_module.h"
#include "engine/iop/iop_library_table.h"
#include "engine/iop/iop_loader.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/iop_module_table.h"
#include "engine/sif/hle/iop_rpc_server.h"
#include "engine/sif/hle/main_memory_ee_ram_sink.h"
#include "engine/sif/hle/main_memory_ee_ram_source.h"

class MainMemory;
class RpcRouter;
namespace ps2r::sound {
class SpuCore;
}
namespace ps2r::cdvd {
class CdvdReader;
}
namespace ps2r::verify {
class RunMeta;
}

namespace ps2r::title::sly2 {

class IopHost {
public:
    // The expected SHA-256 (hex) of each hosted module's image, as the build's TITLE_INPUTS
    // row has it (compile definitions from hle/CMakeLists.txt); "" = not checked.
    struct ExpectedHashes {
        std::string fileio, lgaud, snd989, spiop;
    };

    IopHost(const Config& cfg, MainMemory& ram, RpcRouter& router, sound::SpuCore& spu, cdvd::CdvdReader* reader,
                ExpectedHashes hashes);
    ~IopHost();
    IopHost(const IopHost&) = delete;
    IopHost& operator=(const IopHost&) = delete;

    // The EE's sceSifSetDma (SyscallTable::SetSifDmaCopy): EE RAM -> IOP RAM.
    void SifDmaCopy(uint32_t ee_src, uint32_t iop_dst, uint32_t size);
    // The EE's sceSifAllocIopHeap / sceSifFreeIopHeap (SceSif::IopHeap).
    uint32_t HeapAlloc(uint32_t size);
    void HeapFree(uint32_t addr);
    // The EE's sceSifLoadModuleBuffer (SceSif::SetModuleLoader): the module id, or a negative error.
    int32_t LoadModuleBuffer(const std::string& path);
    // The IOP reboot (sceSifRebootIop(IOPRP300.IMG)): the reboot image's stock modules the engine
    // does not model natively are hosted from the image's own bytes -- FILEIO_service today. The
    // container is the disc file's bytes; false when the module is not found or refused.
    bool BootRebootImageModules(const uint8_t* image, size_t size);

    iop::IopModuleTable& Table() { return table_; }
    iop::IopMemory& Memory() { return mem_; }
    // The run.meta census rows (iop_hosted=, iop_libraries=, iop_rpc_servers=, ...).
    void WriteCensus(verify::RunMeta* meta);
    const std::string& LoadLog() const { return load_log_; }

private:
    struct Hosted {
        const char* iopmod_name;
        const char* id;
        uint32_t base, extent;
        const std::string* expected_sha;
        bool loaded = false;
        uint32_t status = 0;
        int rpc_tid = 0;
    };
    struct Push { uint32_t ee_src, size; };

    static const std::function<void(iop::IopContext&)>* ResolveGuest(uint32_t addr);
    static std::function<void(iop::IopContext&)> ResolveGuestFn(uint32_t addr);   // the by-value form the HLE modules take
    int32_t BootHosted(Hosted& h, const uint8_t* image, uint32_t size, const std::vector<std::string>& argv);
    int32_t LoadImageOnly(const std::string& name, uint32_t iop_addr, uint32_t size, uint16_t version);
    void OnServerRegistered(const iop::hle::SifcmdModule::ServerRecord& r);
    void OnServerRemoved(uint32_t sid);
    void RunCallback(uint32_t addr, iop::IopContext& ctx);

    MainMemory& ram_;
    RpcRouter& router_;
    ExpectedHashes hashes_;
    iop::IopModuleTable table_;
    iop::IopMemory mem_;
    iop::IopLibraryTable libs_;
    MainMemoryEeRamSink ee_sink_;
    MainMemoryEeRamSource ee_source_;
    iop::IopLoader loader_;
    iop::hle::SysmemModule sysmem_;
    iop::hle::IntrmanModule intrman_;
    iop::hle::StdioModule stdio_;
    iop::hle::SysclibModule sysclib_;
    iop::hle::IomanModule ioman_;
    iop::hle::LoadcoreModule loadcore_;
    iop::hle::SifcmdModule sifcmd_;
    iop::hle::SifmanModule sifman_;
    iop::hle::ThbaseModule thbase_;
    iop::hle::ThsemapModule thsemap_;
    iop::hle::TheventModule thevent_;
    iop::hle::ThmsgbxModule thmsgbx_;
    iop::hle::TimrmanModule timrman_;
    iop::hle::LibsdModule libsd_;
    iop::hle::CdvdmanModule cdvdman_;
    iop::hle::ModloadModule modload_;
    iop::hle::UsbdModule usbd_;
    std::vector<Hosted> hosted_;
    std::map<uint32_t, Push> pushes_;   // IOP destination -> the EE source/size of the last push there
    std::list<std::string> server_names_;
    std::vector<std::unique_ptr<IopRpcServer>> servers_;
    std::string rpc_servers_census_;
    std::string load_log_;
    uint64_t dma_bytes_ = 0;
    uint32_t module_loads_ = 0;
};

}  // namespace ps2r::title::sly2
