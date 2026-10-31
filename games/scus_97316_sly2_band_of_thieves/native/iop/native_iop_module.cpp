// native_iop_module.cpp -- NativeIopModule + NativeIopFilter.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/native_iop_module.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps2r::title::sly2 {

// -- NativeIopFilter ---------------------------------------------------------------------------------

NativeIopFilter::NativeIopFilter(const char* spec) {
    if (!spec || !*spec) return;
    spec_ = spec;
    std::string s = spec_;
    if (s == "all") return;
    all_ = false;
    if (s == "none") {
        none_ = true;
        return;
    }
    if (s[0] == '-') {
        exclude_ = true;
        s.erase(0, 1);
    }
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t semi = s.find(';', pos);
        if (semi == std::string::npos) semi = s.size();
        std::string item = s.substr(pos, semi - pos);
        pos = semi + 1;
        if (item.empty()) continue;
        Rule r;
        const size_t colon = item.find(':');
        r.module = item.substr(0, colon);
        if (colon != std::string::npos) {
            size_t p = colon + 1;
            while (p <= item.size()) {
                size_t comma = item.find(',', p);
                if (comma == std::string::npos) comma = item.size();
                const std::string a = item.substr(p, comma - p);
                p = comma + 1;
                if (!a.empty()) r.addrs.push_back(static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)));
            }
        }
        rules_.push_back(std::move(r));
    }
}

NativeIopFilter NativeIopFilter::FromEnvironment() { return NativeIopFilter(std::getenv("NATIVEEE_IOP_NATIVE")); }

bool NativeIopFilter::Admits(const char* module_id, uint32_t addr) const {
    if (all_) return true;
    if (none_) return false;
    bool listed = false;
    for (const Rule& r : rules_) {
        if (r.module != module_id) continue;
        if (r.addrs.empty() || std::find(r.addrs.begin(), r.addrs.end(), addr) != r.addrs.end()) {
            listed = true;
            break;
        }
    }
    return exclude_ ? !listed : listed;
}

// -- NativeIopModule ---------------------------------------------------------------------------------

NativeIopModule::NativeIopModule(const char* id, uint32_t base, uint32_t extent) : id_(id), base_(base), extent_(extent) {}

NativeIopModule::~NativeIopModule() { Uninstall(); }

void NativeIopModule::Add(uint32_t addr, iop_seam::NativeFn fn, const char* name, void* self) {
    for (Candidate& c : candidates_) {
        if (c.addr != addr) continue;
        c.fn = fn;
        c.self = self ? self : this;
        c.name = name;
        return;
    }
    candidates_.push_back(Candidate{addr, fn, self ? self : this, name ? name : "", false});
}

int NativeIopModule::Install(const NativeIopFilter& filter) {
    Uninstall();
    if (!table_.slots) return 0;
    for (Candidate& c : candidates_) {
        if (!filter.Admits(id_, c.addr)) continue;
        iop_seam::IopFnSlot* s = iop_seam::Find(table_.slots, table_.count, c.addr);
        if (!s) {
            std::fprintf(stderr, "native/iop: %s: 0x%08x (%s) is not a generated function start -- not installed\n", id_, c.addr,
                         c.name);
            continue;
        }
        s->native = c.fn;
        s->self = c.self;
        c.installed = true;
        ++installed_;
    }
    return installed_;
}

void NativeIopModule::Uninstall() {
    if (table_.slots) {
        for (Candidate& c : candidates_) {
            if (!c.installed) continue;
            if (iop_seam::IopFnSlot* s = iop_seam::Find(table_.slots, table_.count, c.addr)) {
                s->native = nullptr;
                s->self = nullptr;
            }
            c.installed = false;
        }
    }
    installed_ = 0;
}

uint32_t NativeIopModule::NativeFunctions() const {
    uint32_t n = 0;
    for (uint32_t i = 0; i < table_.count; ++i)
        if (table_.slots[i].native) ++n;
    return n;
}

uint64_t NativeIopModule::NativeCalls() const {
    uint64_t n = 0;
    for (uint32_t i = 0; i < table_.count; ++i) n += table_.slots[i].native_calls;
    return n;
}

uint64_t NativeIopModule::RecompiledCalls() const {
    uint64_t n = 0;
    for (uint32_t i = 0; i < table_.count; ++i) n += table_.slots[i].recompiled_calls;
    return n;
}

std::string NativeIopModule::Census() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s:%u/%u:%llu/%llu", id_, NativeFunctions(), table_.count,
                  static_cast<unsigned long long>(NativeCalls()), static_cast<unsigned long long>(RecompiledCalls()));
    return buf;
}

std::vector<std::pair<uint32_t, uint64_t>> NativeIopModule::RemainingCalls() const {
    std::vector<std::pair<uint32_t, uint64_t>> out;
    for (uint32_t i = 0; i < table_.count; ++i)
        if (table_.slots[i].recompiled_calls) out.emplace_back(table_.slots[i].addr, table_.slots[i].recompiled_calls);
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

}  // namespace ps2r::title::sly2
