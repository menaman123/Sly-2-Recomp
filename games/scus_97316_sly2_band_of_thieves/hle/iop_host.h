// IopHost: Sly 2's IOP. Runs the three recompiled embedded modules (lgAud, 989snd, spiop) on the
// engine's cooperative IOP kernel; the stock Sony modules are the engine's HLE.
//
//   * sceSifSetDma lands in IOP RAM (SifDmaCopy); sceSifAllocIopHeap is the sysmem heap.
//   * sceSifLoadModuleBuffer reads the pushed image's .iopmod name. A hosted module is SHA-256 checked,
//     relocated to its base, its entry run and its threads dispatched; libsd, usbd, sio2man, padman, mcman
//     and mcserv are table rows backed by engine libraries.
//   * Every library import resolves through the IopLibraryTable; an unresolved import parks the run
//     (iop_import_unresolved) rather than answering silently.
//   * Every sceSifRegisterRpc becomes an IopRpcServer on the EE's RpcRouter, so EE calls run the module's
//     own handler. No reply is faked.
//
// Section() is the IOPH save-state section; LegSaves registers it before the SPU sections so a restored
// SPU image wins.
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
#include "engine/iop/savestate/iop_host_section.h"
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
    // Expected SHA-256 (hex) of each hosted module's image, from TITLE_INPUTS.sha256; "" = not checked.
    struct ExpectedHashes {
        std::string fileio, lgaud, snd989, spiop;
    };

    IopHost(const Config& cfg, MainMemory& ram, RpcRouter& router, sound::SpuCore& spu, cdvd::CdvdReader* reader,
                ExpectedHashes hashes);
    ~IopHost();
    MainMemoryEeRamSink& EeSink() { return ee_sink_; }
    IopHost(const IopHost&) = delete;
    IopHost& operator=(const IopHost&) = delete;

    // sceSifSetDma: EE RAM -> IOP RAM.
    void SifDmaCopy(uint32_t ee_src, uint32_t iop_dst, uint32_t size);
    // sceSifAllocIopHeap / sceSifFreeIopHeap.
    uint32_t HeapAlloc(uint32_t size);
    void HeapFree(uint32_t addr);
    // sceSifLoadModuleBuffer: the module id, or a negative error.
    int32_t LoadModuleBuffer(const std::string& path);
    // sceSifRebootIop(IOPRP300.IMG): hosts the reboot image's stock modules the engine has no native
    // model for (FILEIO_service) from the image's own bytes. False if a module is missing or refused.
    bool BootRebootImageModules(const uint8_t* image, size_t size);

    iop::IopModuleTable& Table() { return table_; }
    iop::IopMemory& Memory() { return mem_; }
    // The IOPH save-state section: IOP RAM, kernel tables, hosted module rows, libraries, RPC servers
    // and timers. Restoring re-arms each hosted thread and re-registers each RPC server.
    iop::IopHostSection& Section() { return *section_; }
    // run.meta rows: iop_hosted=, iop_libraries=, iop_rpc_servers=, ...
    void WriteCensus(verify::RunMeta* meta);

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
    static std::function<void(iop::IopContext&)> ResolveGuestFn(uint32_t addr);   // by value, for the HLE modules
    int32_t BootHosted(Hosted& h, const uint8_t* image, uint32_t size, const std::vector<std::string>& argv);
    int32_t LoadImageOnly(const std::string& name, uint32_t iop_addr, uint32_t size, uint16_t version);
    void OnServerRegistered(const iop::hle::SifcmdModule::ServerRecord& r);
    void OnServerRemoved(uint32_t sid);
    void AfterSectionRestore();
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
    std::unique_ptr<iop::IopHostSection> section_;
    std::string rpc_servers_census_;
    std::string load_log_;
    uint64_t dma_bytes_ = 0;
    uint32_t module_loads_ = 0;
};

}  // namespace ps2r::title::sly2
