// games/scus_97316_sly2_band_of_thieves/hle/iop_host.cpp -- IopHost.
#include "games/scus_97316_sly2_band_of_thieves/hle/iop_host.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "engine/core/hash/sha256.h"
#include "engine/core/log/log.h"
#include "engine/core/verify/run_meta.h"
#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/iop/abi/iop_abi_surface.h"
#include "engine/iop/hle/idle/idle_module.h"
#include "engine/iop/hle/stock_library_binder.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_relocator.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h"

// The three recompiled modules (generated into ${PS2R_IOP_GEN_ROOT}/<id>/, each in its namespace).
#include "fileio/iop_recompiled.h"
#include "lgaud/iop_recompiled.h"
#include "snd989/iop_recompiled.h"
#include "spiop/iop_recompiled.h"

namespace ps2r::title::sly2 {

namespace {

using GuestFunction = std::function<void(iop::IopContext&)>;

using InitializeFn = void (*)();
struct Namespace {
    const char* id;
    uint32_t base, extent;
    std::map<uint32_t, GuestFunction>* functions;
    InitializeFn initialize;
    const iop_lgaud::ImportStub* stubs;   // the same POD layout in every namespace
    uint32_t stub_count;
};

// The generated TUs' tables, one per hosted module. ImportStub has the same layout in
// every namespace; the cast keeps one table type here.
const Namespace kNamespaces[] = {
    {"fileio", iop_fileio::kBase, iop_fileio::kBase + iop_fileio::kLoadExtent, &iop_fileio::iop_recompiled_functions,
     &iop_fileio::initialize_iop_recompiled_functions,
     reinterpret_cast<const iop_lgaud::ImportStub*>(iop_fileio::kImportStubs), iop_fileio::kImportStubCount},
    {"lgaud", iop_lgaud::kBase, iop_lgaud::kBase + iop_lgaud::kLoadExtent, &iop_lgaud::iop_recompiled_functions,
     &iop_lgaud::initialize_iop_recompiled_functions, iop_lgaud::kImportStubs, iop_lgaud::kImportStubCount},
    {"snd989", iop_snd989::kBase, iop_snd989::kBase + iop_snd989::kLoadExtent, &iop_snd989::iop_recompiled_functions,
     &iop_snd989::initialize_iop_recompiled_functions,
     reinterpret_cast<const iop_lgaud::ImportStub*>(iop_snd989::kImportStubs), iop_snd989::kImportStubCount},
    {"spiop", iop_spiop::kBase, iop_spiop::kBase + iop_spiop::kLoadExtent, &iop_spiop::iop_recompiled_functions,
     &iop_spiop::initialize_iop_recompiled_functions,
     reinterpret_cast<const iop_lgaud::ImportStub*>(iop_spiop::kImportStubs), iop_spiop::kImportStubCount},
};

void InitializeNamespaces() {
    static bool done = false;
    if (done) return;
    done = true;
    for (const Namespace& n : kNamespaces) n.initialize();
}

std::string Hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

// The static detail buffer a park reports (GuestPark keeps the pointer).
char g_park_detail[256];

}  // namespace

const GuestFunction* IopHost::ResolveGuest(uint32_t addr) {
    InitializeNamespaces();
    for (const Namespace& n : kNamespaces) {
        if (addr < n.base || addr >= n.extent) continue;
        auto it = n.functions->find(addr);
        return it == n.functions->end() ? nullptr : &it->second;
    }
    return nullptr;
}

IopHost::IopHost(const Config& cfg, MainMemory& ram, RpcRouter& router, sound::SpuCore& spu,
                         cdvd::CdvdReader* reader, ExpectedHashes hashes)
    : ram_(ram),
      router_(router),
      hashes_(std::move(hashes)),
      table_(cfg),
      ee_sink_(ram),
      ee_source_(ram),
      loader_(mem_),
      sysmem_(mem_, IopModules::Heap()),
      intrman_(mem_),
      stdio_(mem_),
      sysclib_(mem_),
      ioman_(mem_, reader),
      loadcore_(mem_, libs_),
      sifcmd_(table_, mem_),
      sifman_(mem_, &ee_sink_),
      thbase_(table_, mem_, &IopHost::ResolveGuestFn, iop::hle::ThbaseModule::ThreadLayout{0, 0, 0}),
      thsemap_(table_, mem_),
      thevent_(table_, mem_),
      thmsgbx_(table_, mem_),
      timrman_(table_, &IopHost::ResolveGuestFn, iop::hle::TimrmanModule::TimerLayout{IopModules::kHandlerScratchSp, 0}),
      libsd_(mem_, spu),
      cdvdman_(mem_, reader),
      modload_(table_, mem_),
      usbd_(mem_) {
    InitializeNamespaces();
    hosted_.push_back(Hosted{IopModules::kFileio.iopmod_name, IopModules::kFileio.id, IopModules::kFileio.base,
                             IopModules::kFileio.extent, &hashes_.fileio});
    hosted_.push_back(Hosted{IopModules::kLgaud.iopmod_name, IopModules::kLgaud.id, IopModules::kLgaud.base,
                             IopModules::kLgaud.extent, &hashes_.lgaud});
    hosted_.push_back(Hosted{IopModules::kSnd989.iopmod_name, IopModules::kSnd989.id, IopModules::kSnd989.base,
                             IopModules::kSnd989.extent, &hashes_.snd989});
    hosted_.push_back(Hosted{IopModules::kSpiop.iopmod_name, IopModules::kSpiop.id, IopModules::kSpiop.base,
                             IopModules::kSpiop.extent, &hashes_.spiop});

    // The generated-code ABI: memory, the library registry, the resolver over the three
    // namespaces + every module's import stubs (a stub address taken as a pointer).
    iop::IopAbiSurface::BindMemory(&mem_);
    iop::IopAbiSurface::BindLibraries(&libs_);
    iop::IopAbiSurface::BindResolver(&IopHost::ResolveGuest);
    for (const Namespace& n : kNamespaces)
        for (uint32_t i = 0; i < n.stub_count; ++i)
            iop::IopAbiSurface::RegisterImportStub(n.stubs[i].addr, n.stubs[i].lib, n.stubs[i].index);
    iop::IopAbiSurface::SetUnresolvedImport([](std::string_view lib, uint32_t index, iop::IopContext& ctx) {
        std::snprintf(g_park_detail, sizeof(g_park_detail), "%.*s:%u pc=0x%08x ra=0x%08x", static_cast<int>(lib.size()),
                      lib.data(), index, ctx.pc, ctx.gpr[31]);
        std::fprintf(stderr, "nativeee: iop_import_unresolved %s\n", g_park_detail);
        GuestPark::Forever("iop_import_unresolved", ctx.pc, g_park_detail);
    });

    // The stock libraries' bodies under their export ordinals.
    iop::hle::StockLibraryBinder::Modules m;
    m.sysmem = &sysmem_;
    m.intrman = &intrman_;
    m.ioman = &ioman_;
    m.loadcore = &loadcore_;
    m.stdio = &stdio_;
    m.sysclib = &sysclib_;
    m.thbase = &thbase_;
    m.thsemap = &thsemap_;
    m.thevent = &thevent_;
    m.thmsgbx = &thmsgbx_;
    m.timrman = &timrman_;
    m.sifman = &sifman_;
    m.sifcmd = &sifcmd_;
    m.libsd = &libsd_;
    m.cdvdman = &cdvdman_;
    m.modload = &modload_;
    m.usbd = &usbd_;
    iop::hle::StockLibraryBinder::Bind(libs_, m);

    // Thread stacks from the heap (the console's CreateThread), the module's own $gp per thread.
    thbase_.SetStackAllocator([this](uint32_t size) { return sysmem_.Heap().Alloc(size); },
                              [this](uint32_t addr) { sysmem_.Heap().Free(addr); });
    // Host-run callbacks (hard-timer beats, DMA-complete, CD completion, SPU transfer completion).
    auto run_cb = [this](uint32_t addr, iop::IopContext& ctx) { RunCallback(addr, ctx); };
    cdvdman_.SetCompletion(&table_.Kernel(), run_cb);
    libsd_.SetCompletion(&table_.Kernel(), run_cb);
    sifman_.SetCompletion(&table_.Kernel(), run_cb);
    sifcmd_.SetEeRamSource(&ee_source_);
    sifcmd_.SetEeRamSink(&ee_sink_);
    // A SIF command a hosted module sends (fileio's completions) goes to the EE's command buffer
    // and its sceSifAddCmdHandler handler (SceSif delivers it after the EE's RPC round trip).
    sifcmd_.SetCommandSink([](uint32_t cid, const uint8_t* packet, uint32_t size) {
        if (!SceSif::QueueCommand(cid, packet, size))
            std::fprintf(stderr, "iop: SIF command 0x%08x (%u bytes) dropped: no EE command buffer bound\n", cid, size);
    });
    sifcmd_.SetRegistrar([this](const iop::hle::SifcmdModule::ServerRecord& r) { OnServerRegistered(r); },
                         [this](uint32_t sid) { OnServerRemoved(sid); });

    // The post-reboot IOPRP300 set as module rows (what sceSifLoadModuleBuffer's callers see
    // through modload): names + versions of the stock kernel modules the engine serves.
    struct Row { const char* name; uint32_t version; };
    static const Row kIoprp[] = {
        {"excepman", 0x101}, {"intrman", 0x102}, {"ssbusc", 0x101},  {"dmacman", 0x102}, {"timrman", 0x103},
        {"sysclib", 0x104},  {"stdio", 0x103},   {"heaplib", 0x101}, {"thbase", 0x102},  {"thevent", 0x101},
        {"thsemap", 0x101},  {"thmsgbx", 0x101}, {"thfpool", 0x101}, {"thvpool", 0x101}, {"vblank", 0x101},
        {"ioman", 0x104},    {"modload", 0x107}, {"romdrv", 0x201},  {"sifman", 0x101},  {"sifcmd", 0x101},
        {"cdvdman", 0x101},  {"cdvdfsv", 0x101},
    };
    for (const Row& r : kIoprp) table_.RegisterModule(r.name, r.version, 0);

    // The IOPH section over this host's kernel-facing modules (every row past the stock rows above is
    // the image's; the hosted-module flags are re-derived from the restored rows).
    iop::IopHostSection::Deps deps;
    deps.ram = &mem_;
    deps.table = &table_;
    deps.heap = &sysmem_.Heap();
    deps.libs = &libs_;
    deps.thbase = &thbase_;
    deps.thsemap = &thsemap_;
    deps.thevent = &thevent_;
    deps.thmsgbx = &thmsgbx_;
    deps.timrman = &timrman_;
    deps.sifcmd = &sifcmd_;
    deps.sifman = &sifman_;
    deps.cdvdman = &cdvdman_;
    deps.libsd = &libsd_;
    deps.usbd = &usbd_;
    deps.boot_rows = table_.ModuleCount();
    deps.after_restore = [this] { AfterSectionRestore(); };
    section_ = std::make_unique<iop::IopHostSection>(std::move(deps));
}

void IopHost::AfterSectionRestore() {
    for (Hosted& h : hosted_) {
        const iop::IopModuleTable::ModuleEntry* row = table_.FindModule(h.iopmod_name);
        h.loaded = row != nullptr;
        if (row) h.rpc_tid = row->rpc_tid;
    }
    const iop::IopHostSection::RestoreCensus& c = section_->LastRestore();
    load_log_ += std::string(load_log_.empty() ? "" : ",") + "restored:rows=" + std::to_string(c.rows) + ":threads=" +
                 std::to_string(c.threads_armed) + "/" + std::to_string(c.threads_recorded) + ":servers=" + std::to_string(c.servers) +
                 ":timers=" + std::to_string(c.timers_armed) + ":libs=" + std::to_string(c.libraries);
    PS2R_LOG(HOST, Info, "iop: IOPH restored: %u rows, %u/%u threads re-armed, %u servers, %u timers, %u libraries, frame %llu",
             c.rows, c.threads_armed, c.threads_recorded, c.servers, c.timers_armed, c.libraries,
             static_cast<unsigned long long>(c.frames));
}

IopHost::~IopHost() {
    table_.Shutdown();
    iop::IopAbiSurface::Unbind();
}

GuestFunction IopHost::ResolveGuestFn(uint32_t addr) {
    const GuestFunction* fn = ResolveGuest(addr);
    return fn ? *fn : GuestFunction{};
}

void IopHost::RunCallback(uint32_t addr, iop::IopContext& ctx) {
    const GuestFunction* fn = ResolveGuest(addr);
    if (!fn) {
        std::fprintf(stderr, "iop: callback at 0x%08x is not a recompiled function\n", addr);
        return;
    }
    // The callback runs on the scratch stack with the owning module's $gp.
    if (ctx.gpr[29] == 0) ctx.gpr[29] = IopModules::kHandlerScratchSp;
    if (ctx.gpr[28] == 0) {
        for (const auto& mod : table_.Modules())
            if (mod.text_addr && addr >= mod.text_addr && addr < mod.text_addr + mod.text_size) ctx.gpr[28] = mod.gp;
    }
    ctx.pc = addr;
    (*fn)(ctx);
}

void IopHost::SifDmaCopy(uint32_t ee_src, uint32_t iop_dst, uint32_t size) {
    const uint8_t* src = ram_.Pointer(MainMemory::Phys(ee_src), size);
    uint8_t* dst = mem_.Pointer(iop_dst, size);
    if (src && dst && size) {
        std::memcpy(dst, src, size);
        dma_bytes_ += size;
    }
    pushes_[iop_dst] = Push{ee_src, size};
}

uint32_t IopHost::HeapAlloc(uint32_t size) { return sysmem_.Heap().Alloc(size); }
void IopHost::HeapFree(uint32_t addr) { sysmem_.Heap().Free(addr); }

int32_t IopHost::LoadModuleBuffer(const std::string& path) {
    // "buffer@<iop addr>[ <args>]": the image the EE pushed, by its IOP address.
    if (path.rfind("buffer@", 0) != 0) {
        load_log_ += (load_log_.empty() ? "" : ",") + path + "=unsupported";
        return -1;
    }
    const uint32_t iop_addr = static_cast<uint32_t>(std::strtoul(path.c_str() + 7, nullptr, 16));
    std::vector<std::string> args;
    const size_t space = path.find(' ');
    if (space != std::string::npos) {
        // The args are NUL-separated on the console; SceSif hands them over with the NULs as spaces.
        std::string cur;
        for (size_t i = space + 1; i <= path.size(); ++i) {
            const char c = i < path.size() ? path[i] : ' ';
            if (c == ' ') {
                if (!cur.empty()) args.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
    }
    const auto push = pushes_.find(iop_addr);
    const uint32_t pushed_size = push != pushes_.end() ? push->second.size : 0;
    const uint8_t* image = mem_.Pointer(iop_addr, pushed_size ? pushed_size : 0x34);
    const iop::IopRelocator::IopMod mod = image ? iop::IopRelocator::ReadIopMod(image, pushed_size ? pushed_size : IopModules::kHeapCeiling - iop_addr) : iop::IopRelocator::IopMod{};
    if (!mod.ok) {
        load_log_ += (load_log_.empty() ? "" : ",") + path + "=not_an_irx";
        PS2R_LOG(HOST, Warn, "sceSifLoadModuleBuffer(%s): no .iopmod at the pushed image", path.c_str());
        return -1;
    }
    for (Hosted& h : hosted_) {
        if (mod.name != h.iopmod_name) continue;
        // argv[0] is the module's name (loadcore's start_module convention), then its args.
        std::vector<std::string> argv;
        argv.push_back(mod.name);
        argv.insert(argv.end(), args.begin(), args.end());
        return BootHosted(h, image, pushed_size, argv);
    }
    return LoadImageOnly(mod.name, iop_addr, pushed_size, mod.version);
}

bool IopHost::BootRebootImageModules(const uint8_t* image, size_t size) {
    // The reboot image is a ROM file: a directory of modules, each an ELF. The one hosted is the
    // module whose .iopmod names kFileio; found by its ELF magic + .iopmod name, as the extractor does.
    if (!image || size < 0x34) return false;
    for (size_t o = 0; o + 0x34 <= size; o += 4) {
        if (image[o] != 0x7F || image[o + 1] != 'E' || image[o + 2] != 'L' || image[o + 3] != 'F') continue;
        const iop::IopRelocator::IopMod mod = iop::IopRelocator::ReadIopMod(image + o, size - o);
        if (!mod.ok) continue;
        for (Hosted& h : hosted_) {
            if (mod.name != h.iopmod_name || h.loaded) continue;
            std::vector<std::string> argv;
            argv.push_back(mod.name);
            const int32_t id = BootHosted(h, image + o, static_cast<uint32_t>(size - o), argv);
            PS2R_LOG(HOST, Info, "iop: reboot image module %s -> id %d", mod.name.c_str(), id);
            return id > 0;
        }
    }
    load_log_ += std::string(load_log_.empty() ? "" : ",") + "reboot_image=no_hosted_module";
    return false;
}

int32_t IopHost::BootHosted(Hosted& h, const uint8_t* image, uint32_t size, const std::vector<std::string>& argv) {
    // The ELF extent of the image bytes (what TITLE_INPUTS hashed): section headers / last section end.
    std::vector<iop::IopRelocator::Section> secs;
    if (!image || !iop::IopRelocator::ParseSections(image, size, secs, nullptr)) {
        load_log_ += std::string(load_log_.empty() ? "" : ",") + h.iopmod_name + "=bad_image";
        return -1;
    }
    uint32_t extent = 0;
    {
        auto rd32 = [image](uint32_t o) {
            return static_cast<uint32_t>(image[o]) | (static_cast<uint32_t>(image[o + 1]) << 8) |
                   (static_cast<uint32_t>(image[o + 2]) << 16) | (static_cast<uint32_t>(image[o + 3]) << 24);
        };
        auto rd16 = [image](uint32_t o) { return static_cast<uint32_t>(image[o]) | (static_cast<uint32_t>(image[o + 1]) << 8); };
        const uint32_t shoff = rd32(0x20);
        const uint32_t shentsize = rd16(0x2E);
        const uint32_t shnum = rd16(0x30);
        extent = shoff + shentsize * shnum;
        for (const auto& s : secs)
            if (s.type != 8 && s.offset + s.size > extent) extent = s.offset + s.size;
        if (extent > size) extent = size;
    }
    const std::string digest = hash::Sha256::HexOf(image, extent);
    if (!h.expected_sha->empty() && digest != *h.expected_sha) {
        std::fprintf(stderr, "nativeee: pushed IOP module %s is not the image this build recompiled (sha256 %s, expected %s)\n",
                     h.iopmod_name, digest.c_str(), h.expected_sha->c_str());
        load_log_ += std::string(load_log_.empty() ? "" : ",") + h.iopmod_name + "=sha_mismatch";
        return -1;
    }
    const iop::IopLoader::SectionReport sec = loader_.LoadSections(image, extent, h.base);
    if (!sec.ok) {
        std::fprintf(stderr, "nativeee: IOP module %s: %s\n", h.iopmod_name, sec.error.c_str());
        load_log_ += std::string(load_log_.empty() ? "" : ",") + h.iopmod_name + "=load_failed";
        return -1;
    }
    const iop::IopRelocator::IopMod mod = iop::IopRelocator::ReadIopMod(image, extent);
    iop::IopLoader::ModuleLayout layout;
    layout.gp = mod.gp + h.base;
    layout.entry = mod.entry + h.base;
    // The entry's stack: a heap block (the console's loadcore runs the entry on a kernel stack).
    const uint32_t entry_stack = sysmem_.Heap().Alloc(0x1000);
    layout.stack_top = entry_stack ? entry_stack + 0x1000 : IopModules::kHandlerScratchSp;
    layout.stack_stride = 0;
    layout.rpc_tid_addr = 0;
    iop::IopLoader::EntryArgs args;
    if (!argv.empty()) {
        args.argv = argv;
        args.block = sysmem_.Heap().Alloc(IopModules::kModuleArgsBytes);
        args.block_size = IopModules::kModuleArgsBytes;
    }
    // The module row BEFORE the entry runs: its own loadcore/modload probes may look it up.
    iop::IopModuleTable::ModuleEntry row;
    row.name = mod.name;
    row.version = mod.version;
    row.entry = layout.entry;
    row.gp = layout.gp;
    row.text_addr = sec.text_addr;
    row.text_size = sec.text_bytes;
    row.data_size = sec.data_bytes + sec.rodata_bytes;
    row.bss_size = sec.bss_bytes;
    const int row_index = table_.RegisterModule(row);
    PS2R_LOG(HOST, Info, "iop: booting %s at 0x%08x (entry 0x%08x gp 0x%08x, %u relocations, %zu args)", h.iopmod_name,
             h.base, layout.entry, layout.gp, sec.relocations, argv.size());
    const iop::IopLoader::EntryReport rep = loader_.RunEntry(layout, &IopHost::ResolveGuestFn, table_, args);
    if (!rep.ran) {
        std::fprintf(stderr, "nativeee: IOP module %s: no recompiled function at its entry 0x%08x\n", h.iopmod_name,
                     layout.entry);
        load_log_ += std::string(load_log_.empty() ? "" : ",") + h.iopmod_name + "=no_entry";
        return -1;
    }
    h.loaded = true;
    h.status = rep.status;
    h.rpc_tid = rep.rpc_tid;
    if (entry_stack) sysmem_.Heap().Free(entry_stack);
    module_loads_++;
    load_log_ += std::string(load_log_.empty() ? "" : ",") + h.iopmod_name + "=hosted@" + Hex(h.base) +
                 ":status=" + std::to_string(rep.status);
    PS2R_LOG(HOST, Info, "iop: %s entry returned %u (0 = resident); libraries now: %s", h.iopmod_name, rep.status,
             libs_.Census().c_str());
    // The console's modload treats 1 (NO_RESIDENT_END) as "loaded, nothing stays"; 2 (REMOVABLE) likewise.
    return row_index + 1;
}

int32_t IopHost::LoadImageOnly(const std::string& name, uint32_t iop_addr, uint32_t size, uint16_t version) {
    // A module the engine serves natively (its library is an HLE body, or an EE-side server):
    // a row, with its image placed in RAM (heap) so a hosted module's probe of the module's
    // text (modload ReferModuleStatus -> a scan for a version string) finds real bytes.
    iop::IopModuleTable::ModuleEntry row;
    row.name = name;
    row.version = version;
    const uint8_t* image = mem_.Pointer(iop_addr, size);
    if (image && size) {
        const uint32_t memsz = iop::IopRelocator::LoadExtent(image, size);
        const uint32_t base = memsz ? sysmem_.Heap().Alloc((memsz + 0xFFu) & ~0xFFu) : 0;
        if (base) {
            const iop::IopLoader::SectionReport sec = loader_.LoadSections(image, size, base);
            if (sec.ok) {
                const iop::IopRelocator::IopMod mod = iop::IopRelocator::ReadIopMod(image, size);
                row.entry = mod.entry + base;
                row.gp = mod.gp + base;
                row.text_addr = sec.text_addr;
                row.text_size = sec.text_bytes;
                row.data_size = sec.data_bytes + sec.rodata_bytes;
                row.bss_size = sec.bss_bytes;
            } else {
                sysmem_.Heap().Free(base);
            }
        }
    }
    const int row_index = table_.RegisterModule(row);
    module_loads_++;
    load_log_ += std::string(load_log_.empty() ? "" : ",") + name + "=row" + (row.text_addr ? "+image@" + Hex(row.text_addr) : "");
    PS2R_LOG(HOST, Info, "iop: %s (v%x.%02x) served natively; row %d%s", name.c_str(), version >> 8, version & 0xFF,
             row_index + 1, row.text_addr ? " (image placed)" : "");
    return row_index + 1;
}

void IopHost::OnServerRegistered(const iop::hle::SifcmdModule::ServerRecord& r) {
    auto server = std::make_unique<IopRpcServer>(r, mem_, ram_, table_.Rpc(), sifcmd_, &IopHost::ResolveGuest);
    // A re-registration (a restored record, a module re-running its init): the server for that id is
    // REPLACED and the router's row re-pointed -- one EE-facing server per id.
    bool replaced = false;
    for (auto& existing : servers_) {
        if (existing->Record().sid != r.sid) continue;
        existing = std::move(server);
        router_.RegisterServer(r.sid, *existing, router_.ServerName(r.sid));
        replaced = true;
        break;
    }
    if (!replaced) {
        server_names_.push_back("IOP:" + Hex(r.sid));
        router_.RegisterServer(r.sid, *server, server_names_.back().c_str());
        servers_.push_back(std::move(server));
    }
    rpc_servers_census_ += (rpc_servers_census_.empty() ? "" : ",") + Hex(r.sid) + "->" + Hex(r.func) + "@tid" +
                           std::to_string(r.tid);
    PS2R_LOG(HOST, Info, "iop: RPC server 0x%08x registered (func 0x%08x buff 0x%08x queue tid %d)", r.sid, r.func,
             r.buff, r.tid);
}

void IopHost::OnServerRemoved(uint32_t sid) {
    rpc_servers_census_ += (rpc_servers_census_.empty() ? "" : ",") + Hex(sid) + "->removed";
}

void IopHost::WriteCensus(verify::RunMeta* meta) {
    if (!meta) return;
    std::string hosted;
    for (const Hosted& h : hosted_) {
        if (!hosted.empty()) hosted += ',';
        hosted += std::string(h.iopmod_name) + "@" + Hex(h.base) + (h.loaded ? ":loaded:status=" + std::to_string(h.status) : ":not_loaded");
    }
    meta->Set("iop_hosted", hosted);
    meta->Set("iop_load_log", load_log_);
    meta->Set("iop_libraries", libs_.Census());
    meta->Set("iop_imports_unresolved", libs_.UnresolvedCensus());
    meta->Set("iop_import_calls", std::to_string(iop::IopAbiSurface::ImportCalls()));
    meta->Set("iop_rpc_servers", rpc_servers_census_);
    meta->Set("iop_heap_ptr", Hex(sysmem_.Heap().HeapPtr()));
    meta->Set("iop_dma_bytes_in", std::to_string(dma_bytes_));
    meta->Set("iop_module_rows", table_.ModulesCensus());
    meta->Set("iop_rpc_submitted", std::to_string(table_.Rpc().Submitted()));
    meta->Set("iop_rpc_completed", std::to_string(table_.Rpc().Completed()));
    meta->Set("iop_rpc_stalls", std::to_string(table_.Rpc().Stalls()));
    meta->Set("iop_printf_calls", std::to_string(stdio_.Calls()));
    meta->Set("iop_timer_beats", std::to_string(table_.Kernel().TimerBeats()));
    // The libsd services the hosted 989snd reached (ordinal:count) and the voice-transfer bytes.
    meta->Set("iop_libsd", libsd_.CallsCensus() + ":trans_bytes=" + std::to_string(libsd_.VoiceTransBytes()));
    // The modules' own disc traffic (spiop streams through cdvdman's sceCdRead).
    meta->Set("iop_cd", "reads=" + std::to_string(cdvdman_.ReadsIssued()) + ":sectors=" +
                            std::to_string(cdvdman_.SectorsRead()) + ":callbacks=" +
                            std::to_string(cdvdman_.CallbacksFired()) + ":search_hits=" +
                            std::to_string(cdvdman_.SearchHits()));
    std::string rpc_calls;
    for (const auto& s : servers_) {
        if (!rpc_calls.empty()) rpc_calls += ',';
        rpc_calls += Hex(s->Record().sid) + ":calls=" + std::to_string(s->Calls()) + ":send=" + std::to_string(s->SendBytes()) +
                     ":recv=" + std::to_string(s->RecvBytes()) + ":last_reply=" + Hex(s->LastReply());
    }
    meta->Set("iop_rpc_calls", rpc_calls);
}

}  // namespace ps2r::title::sly2
