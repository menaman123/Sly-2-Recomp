// iop_call_trace.cpp -- IopCallTrace over the seam's two hooks.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_call_trace.h"

#include <cstdlib>
#include <cstring>

#include "engine/iop/abi/iop_abi_surface.h"

namespace ps2r::title::sly2 {

std::FILE* IopCallTrace::file_ = nullptr;
IopCallTrace::Deps IopCallTrace::deps_;
std::vector<std::string> IopCallTrace::only_modules_;
bool IopCallTrace::imports_ = true;
int IopCallTrace::max_depth_ = -1;
int IopCallTrace::depth_ = 0;
uint64_t IopCallTrace::seq_ = 0;
uint64_t IopCallTrace::rows_ = 0;

bool IopCallTrace::Open(const char* path, Deps deps) {
    Close();
    if (!path || !*path) return false;
    file_ = std::fopen(path, "w");
    if (!file_) {
        std::fprintf(stderr, "native/iop: NATIVEEE_IOP_TRACE: cannot open %s\n", path);
        return false;
    }
    deps_ = std::move(deps);
    only_modules_.clear();
    if (const char* m = std::getenv("NATIVEEE_IOP_TRACE_MODULES")) {
        std::string s = m;
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t comma = s.find(',', pos);
            if (comma == std::string::npos) comma = s.size();
            if (comma > pos) only_modules_.push_back(s.substr(pos, comma - pos));
            pos = comma + 1;
        }
    }
    imports_ = true;
    if (const char* i = std::getenv("NATIVEEE_IOP_TRACE_IMPORTS")) imports_ = !(i[0] == '0' && i[1] == 0);
    max_depth_ = -1;
    if (const char* d = std::getenv("NATIVEEE_IOP_TRACE_MAX_DEPTH")) max_depth_ = std::atoi(d);
    depth_ = 0;
    seq_ = 0;
    rows_ = 0;
    std::fprintf(file_, "# iop_call_trace v1");
    for (const Module& m : deps_.modules) std::fprintf(file_, " %s@%x-%x", m.id, m.base, m.extent);
    std::fprintf(file_, "\n");
    iop::IopAbiSurface::BindSeamTrace(&IopCallTrace::OnCall, imports_ ? &IopCallTrace::OnImport : nullptr);
    return true;
}

void IopCallTrace::Close() {
    if (!file_) return;
    iop::IopAbiSurface::BindSeamTrace(nullptr, nullptr);
    std::fclose(file_);
    file_ = nullptr;
}

const char* IopCallTrace::ModuleOf(uint32_t addr) {
    for (const Module& m : deps_.modules)
        if (addr >= m.base && addr < m.extent) return m.id;
    return "?";
}

bool IopCallTrace::ModuleTraced(const char* id) {
    if (only_modules_.empty()) return true;
    for (const std::string& m : only_modules_)
        if (m == id) return true;
    return false;
}

void IopCallTrace::OnCall(const iop_seam::IopFnSlot& slot, bool native, bool entry, iop::IopContext& ctx) {
    if (!file_) return;
    const char* mod = ModuleOf(slot.addr);
    // The depth counts every guest call (so the pairing stays right under a filter); rows are filtered.
    if (!entry) --depth_;
    const bool write = ModuleTraced(mod) && (max_depth_ < 0 || depth_ <= max_depth_);
    if (write) {
        const uint64_t t = deps_.now_us ? deps_.now_us() : 0;
        const char body = native ? 'n' : 'r';
        if (entry) {
            std::fprintf(file_, "E %llu %s %x %c %x %x %x %x %x %x %llu %d %d\n", static_cast<unsigned long long>(seq_), mod,
                         slot.addr, body, ctx.gpr[4], ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[29], ctx.gpr[31],
                         static_cast<unsigned long long>(t), deps_.current_tid ? deps_.current_tid() : 0, depth_);
        } else {
            std::fprintf(file_, "X %llu %s %x %c %x %x %llu\n", static_cast<unsigned long long>(seq_), mod, slot.addr, body,
                         ctx.gpr[2], ctx.gpr[3], static_cast<unsigned long long>(t));
        }
        ++rows_;
    }
    ++seq_;
    if (entry) ++depth_;
}

void IopCallTrace::StepEntry(uint32_t addr, const iop::IopContext& initial) {
    if (!file_) return;
    const char* mod = ModuleOf(addr);
    if (ModuleTraced(mod) && (max_depth_ < 0 || depth_ <= max_depth_)) {
        const uint64_t t = deps_.now_us ? deps_.now_us() : 0;
        std::fprintf(file_, "E %llu %s %x n %x %x %x %x %x %x %llu %d %d\n", static_cast<unsigned long long>(seq_), mod, addr,
                     initial.gpr[4], initial.gpr[5], initial.gpr[6], initial.gpr[7], initial.gpr[29], initial.gpr[31],
                     static_cast<unsigned long long>(t), deps_.current_tid ? deps_.current_tid() : 0, depth_);
        ++rows_;
    }
    ++seq_;
    ++depth_;   // the thread never exits its entry on the lanes; an Exit park does not pop it (nor does the coroutine form's)
}

void IopCallTrace::StepImport(const char* lib, uint32_t index, const iop::IopContext& ctx) {
    if (!file_ || !imports_) return;
    if (max_depth_ >= 0 && depth_ > max_depth_) {
        ++seq_;
        return;
    }
    const uint64_t t = deps_.now_us ? deps_.now_us() : 0;
    std::fprintf(file_, "I %llu %s:%u %x %x %x %x %llu %d\n", static_cast<unsigned long long>(seq_), lib, index, ctx.gpr[4],
                 ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], static_cast<unsigned long long>(t), depth_);
    ++rows_;
    ++seq_;
}

void IopCallTrace::StepImportExit(const char* lib, uint32_t index, uint32_t v0) {
    if (!file_ || !imports_) return;
    if (max_depth_ >= 0 && depth_ > max_depth_) {
        ++seq_;
        return;
    }
    const uint64_t t = deps_.now_us ? deps_.now_us() : 0;
    std::fprintf(file_, "J %llu %s:%u %x %llu\n", static_cast<unsigned long long>(seq_), lib, index, v0,
                 static_cast<unsigned long long>(t));
    ++rows_;
    ++seq_;
}

void IopCallTrace::OnImport(const char* lib, uint32_t index, bool entry, iop::IopContext& ctx) {
    if (!file_) return;
    if (max_depth_ >= 0 && depth_ > max_depth_) {
        ++seq_;
        return;
    }
    // An import from a filtered-out module: the caller's module is unknown here (the import stub carries no
    // address); the row is written when any module is traced -- the compare tool aligns on the E/X rows.
    const uint64_t t = deps_.now_us ? deps_.now_us() : 0;
    if (entry)
        std::fprintf(file_, "I %llu %s:%u %x %x %x %x %llu %d\n", static_cast<unsigned long long>(seq_), lib, index, ctx.gpr[4],
                     ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], static_cast<unsigned long long>(t), depth_);
    else
        std::fprintf(file_, "J %llu %s:%u %x %llu\n", static_cast<unsigned long long>(seq_), lib, index, ctx.gpr[2],
                     static_cast<unsigned long long>(t));
    ++rows_;
    ++seq_;
}

}  // namespace ps2r::title::sly2
