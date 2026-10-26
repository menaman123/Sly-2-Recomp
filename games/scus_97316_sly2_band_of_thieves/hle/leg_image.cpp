#include "games/scus_97316_sly2_band_of_thieves/hle/leg_image.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <utility>
#include <vector>

#include "engine/cdvd/hle/libcdvd_bindings.h"
#include "engine/core/log/log.h"
#include "engine/core/verify/run_meta.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/dispatch_table.h"
#include "engine/ee/cpu/ps2_float.h"
#include "engine/ee/cpu/shadow_call_stack.h"
#include "engine/ee/hle/boot_env.h"
#include "engine/ee/hle/kernel_codes.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/elf_loader.h"
#include "engine/gs/hle/sce/gs_sync.h"
#include "engine/mc/hle/libmc_bindings.h"
#include "engine/sif/hle/rpc_router.h"
#include "engine/sif/hle/sce_sif.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_text.h"

namespace ps2r::title::sly2 {

LegImage::LegImage(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& elf_path,
                   const std::string& title_dir, const LegKnobs& knobs, LegCore& core, LegDisc& disc, LegServices& services, LegIop& iop) {
    if (!LoadElf(host, title, elf_path, core)) {
        error_ = 2;
        return;
    }
    BootEnv::Instance().Configure(title.boot);
    BootEnv::Instance().PreSeed();
    Ps2Float::SetGuestThreadRounding();
    DispatchTable::SetFastDispatch(cfg.engine.ee_fast_dispatch);   // must precede Initialize, which latches it
    DispatchTable::Initialize();
    if (!InstallDispatch(cfg, host, title, title_dir, knobs, core, disc, services, iop)) error_ = 2;
}

bool LegImage::LoadElf(ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& elf_path, LegCore& core) {
    std::vector<uint8_t> file;
    ElfLoader::Image image;
    std::string err;
    if (!ElfLoader::ReadFile(elf_path, file, &err) || !ElfLoader::Load(file, core.ram, image, &err)) {
        std::fprintf(stderr, "nativeee: ELF %s: %s\n", elf_path.c_str(), err.c_str());
        return false;
    }
    if (image.entry != title.entry) {
        std::fprintf(stderr, "nativeee: ELF entry 0x%08x != profile entry 0x%08x\n", image.entry, title.entry);
        return false;
    }
    PS2R_LOG(HOST, Info, "ELF loaded: %s entry=0x%08x segments=%zu", elf_path.c_str(), image.entry,
             image.segments.size());
    if (auto* meta = host.Meta()) {
        meta->Set("elf_path", elf_path);
        meta->Set("elf_entry", Hex(image.entry));
        meta->Set("elf_segments", std::to_string(image.segments.size()));
    }
    return true;
}

bool LegImage::InstallDispatch(const ps2r::Config& cfg, ps2r::verify::VerifyHost& host, const TitleProfile& title, const std::string& title_dir,
                               const LegKnobs& knobs, LegCore& core, LegDisc& disc, LegServices& services, LegIop& iop) {
    SdkBindingManifest& manifest = SdkBindingManifest::Instance();
    manifest.RegisterBuiltins();
    GsSync::Register(manifest);
    // The IOP heap is the host's sysmem heap. sceSifLoadModuleBuffer goes to the IOP host, which boots a
    // hosted module or adds a row for a natively served one.
    IopHost& iop_host = iop.iop_host;
    SceSif::IopHeap iop_heap;
    iop_heap.alloc = [&iop_host](uint32_t size) { return iop_host.HeapAlloc(size); };
    iop_heap.free = [&iop_host](uint32_t addr) { iop_host.HeapFree(addr); };
    SceSif::SetModuleLoader([this, &iop_host](const std::string& path) {
        const int32_t id = iop_host.LoadModuleBuffer(path);
        iop_module_loads++;
        iop_modules += (iop_modules.empty() ? "" : ",") + path.substr(0, path.find(' ')) + "=" + std::to_string(id);
        PS2R_LOG(HOST, Info, "sceSifLoadModuleBuffer(%s) -> %d", path.c_str(), id);
        return id;
    });
    // A client bound before this router existed (a restored state: sceSifBindRpc's bindings live in EE RAM,
    // the router's table does not): resolve the handle word (0xDEAD0000 | sid & 0xFFFF) against the
    // registered server ids. An unknown low half answers 0.
    RpcRouter* router = &services.rpc_router;
    const int sif_rows = SceSif::Bind(manifest, services.rpc_router, core.sif_state, std::move(iop_heap), [router](uint32_t handle) {
        if ((handle & 0xFFFF0000u) != 0xDEAD0000u) return 0u;
        const uint32_t low = handle & 0xFFFFu;
        if (!low) return 0u;
        for (uint32_t sid : router->ServerIds())
            if ((sid & 0xFFFFu) == low) return sid;
        return 0x80000000u | low;   // not registered yet: SDK system servers keep their low half
    });
    if (!manifest.MarkWaitBody("libkernl.a", "sceSifRpcLoop", TSW_SLEEP)) return false;
    const int cdvd_rows = LibcdvdBindings::Bind(manifest, disc.Reader(), &core.clocks, &disc.stream_reader);
    // Memory card: cfg.paths.mc_dir/mc0 is a formatted 8 MB card in slot 1. This libmc (3.0.0) passes
    // sceMcGetInfo's fifth argument in t0.
    const int libmc_rows = LibmcBindings::Bind(manifest, &services.mc_model, &services.mc_store);
    LibmcBindings::SetFormatInT0(cfg.hle.libmc_format_in_t0);

    if (auto* meta = host.Meta()) {
        meta->Set("sif_rows", std::to_string(sif_rows));
        meta->Set("cdvd_rows", std::to_string(cdvd_rows));
        meta->Set("libmc_rows", std::to_string(libmc_rows));
    }

    std::vector<SdkBindingManifest::TitleRow> rows;
    std::string err;
    const std::string tsv = title_dir + "/" + title.sdk_bindings_file;
    if (!SdkBindingManifest::LoadTitleManifest(tsv, rows, &err)) {
        std::fprintf(stderr, "nativeee: manifest %s: %s\n", tsv.c_str(), err.c_str());
        return false;
    }
    int traps = 0, unresolved = 0;
    const std::vector<SdkBindingManifest::Resolved> resolved = manifest.Resolve(rows);
    static std::deque<uint32_t> wait_entries;   // stable storage for the wait bodies' continuation proofs
    const int installed = manifest.Install(
        resolved, [](uint32_t a, SdkBindingManifest::Body b) { return DispatchTable::Install(a, b); },
        [](uint32_t a) { return DispatchTable::Capture(a); }, &traps, &unresolved,
        [](uint32_t addr, SdkBindingManifest::Body body, int wait_type) {
            wait_entries.push_back(addr);
            DispatchTable::Continuations proof;
            proof.entries = &wait_entries.back();
            proof.count = 1;
            proof.restart_wait = wait_type;
            return DispatchTable::GeneratedBody(addr, [addr, body = std::move(body)](CpuContext& ctx, uint32_t a) {
                const bool pushed = ShadowCallStack::Push(addr, ctx.cpuRegs.GPR.r[31].UL[0]);
                body(ctx, a);
                if (pushed) ShadowCallStack::Pop();
            }, proof);
        });
    // The frame boundary: GsSync::WaitForVblank replaces the game's flag + WaitSema vsync wait. The game's
    // VBLANK handler still runs at the boundary and does the display flip.
    if (title.gs.vblank_wait_entry) {
        const bool ok = DispatchTable::Install(title.gs.vblank_wait_entry, [this](CpuContext& ctx, uint32_t) {
            ++boundary_site_hits;
            GsSync::WaitForVblank(ctx);
        });
        if (!ok) {
            std::fprintf(stderr, "nativeee: vblank_wait_entry 0x%08x is not a generated function start\n",
                         title.gs.vblank_wait_entry);
            return false;
        }
    }
    // NATIVEEE_FN_TRACE=addr[,addr...]: print a0..a3 on entry and v0 on return of these functions.
    if (knobs.fn_trace) InstallFnTrace(*knobs.fn_trace);
    if (knobs.fn_census) {
        fn_census_path_ = *knobs.fn_census;
        InstallFnCensus();
    }
    std::string not_starts;
    for (const auto& r : resolved)
        if (DispatchTable::IdOf(r.title.address) >= DispatchTable::FunctionCount())
            not_starts += (not_starts.empty() ? "" : ",") + r.title.name + "@" + Hex(r.title.address);
    PS2R_LOG(HOST, Info, "sdk manifest %s: %zu rows, %d installed (%d trap rows, %d not function starts: %s)",
             tsv.c_str(), rows.size(), installed, traps, unresolved, not_starts.c_str());
    if (auto* meta = host.Meta()) {
        meta->Set("title", title.serial);
        meta->Set("functions", std::to_string(DispatchTable::FunctionCount()));
        meta->Set("sdk_rows", std::to_string(rows.size()));
        meta->Set("sdk_rows_installed", std::to_string(installed));
        meta->Set("sdk_rows_trap", std::to_string(traps));
        meta->Set("sdk_rows_not_fn_start", std::to_string(unresolved));
        meta->Set("sdk_rows_not_fn_start_list", not_starts);
    }
    return true;
}

void LegImage::InstallFnTrace(const std::string& list) {
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        if (comma == std::string::npos) comma = list.size();
        const uint32_t fn = static_cast<uint32_t>(std::strtoul(list.substr(pos, comma - pos).c_str(), nullptr, 16));
        pos = comma + 1;
        DispatchTable::Body original = DispatchTable::Capture(fn);
        if (!original) { std::fprintf(stderr, "fn-trace: 0x%08x is not a function start\n", fn); continue; }
        DispatchTable::Install(fn, [fn, original](CpuContext& ctx, uint32_t entry) {
            std::fprintf(stderr, "fn-trace: 0x%08x(a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x t0=0x%08x t1=0x%08x) ra=0x%08x\n", fn,
                         ctx.cpuRegs.GPR.r[4].UL[0], ctx.cpuRegs.GPR.r[5].UL[0], ctx.cpuRegs.GPR.r[6].UL[0],
                         ctx.cpuRegs.GPR.r[7].UL[0], ctx.cpuRegs.GPR.r[8].UL[0], ctx.cpuRegs.GPR.r[9].UL[0],
                         ctx.cpuRegs.GPR.r[31].UL[0]);
            original(ctx, entry);
            std::fprintf(stderr, "fn-trace: 0x%08x -> v0=0x%08x\n", fn, ctx.cpuRegs.GPR.r[2].UL[0]);
        });
    }
}

// NATIVEEE_FN_CENSUS: call counts of every generated function. Diff two runs to find what runs only
// while something happens.
void LegImage::InstallFnCensus() {
    const uint32_t n = DispatchTable::FunctionCount();
    fn_census_counts_.resize(n);
    uint32_t installed = 0;
    for (uint32_t id = 0; id < n; id++) {
        const uint32_t fn = DispatchTable::AddressOfId(id);
        DispatchTable::Body original = DispatchTable::Capture(fn);
        if (!original) continue;
        fn_census_counts_[id] = std::make_shared<std::atomic<uint64_t>>(0);
        if (DispatchTable::Install(fn, DispatchTable::CountCalls(fn, std::move(original), fn_census_counts_[id]))) ++installed;
    }
    PS2R_LOG(HOST, Info, "fn census: %u of %u functions counted -> %s", installed, n, fn_census_path_.c_str());
}

void LegImage::WriteFnCensus() {
    if (fn_census_path_.empty()) return;
    FILE* f = std::fopen(fn_census_path_.c_str(), "w");
    if (!f) return;
    for (uint32_t id = 0; id < fn_census_counts_.size(); id++) {
        if (!fn_census_counts_[id]) continue;
        const uint64_t c = fn_census_counts_[id]->load(std::memory_order_relaxed);
        if (c) std::fprintf(f, "%08x %llu\n", DispatchTable::AddressOfId(id), static_cast<unsigned long long>(c));
    }
    std::fclose(f);
    fn_census_path_.clear();
}

}  // namespace ps2r::title::sly2
