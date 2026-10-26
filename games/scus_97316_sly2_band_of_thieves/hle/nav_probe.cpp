#include "games/scus_97316_sly2_band_of_thieves/hle/nav_probe.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#include "games/scus_97316_sly2_band_of_thieves/hle/nav_json.h"

namespace ps2r::title::sly2 {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

NavProbe::NavProbe(const char* spec_cstr, const std::string& title_dir, const std::string& out_dir, MainMemory& ram) : ram_(ram) {
    probe_path_ = title_dir + "/verify/nav/sly2_probe.json";
    if (!out_dir.empty()) trace_path_ = out_dir + "/nav_trace.csv";
    if (!spec_cstr) return;
    spec_given_ = true;
    const std::string spec(spec_cstr);
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t semi = spec.find(';', pos);
        if (semi == std::string::npos) semi = spec.size();
        const std::string item = spec.substr(pos, semi - pos);
        pos = semi + 1;
        if (item.empty()) continue;
        if (item.rfind("probe=", 0) == 0) { probe_path_ = item.substr(6); continue; }
        if (item == "trace") { trace_wanted_ = true; continue; }
        if (item.rfind("trace=", 0) == 0) { trace_wanted_ = true; trace_path_ = item.substr(6); continue; }
        if (item.rfind("every=", 0) == 0) { every_ = std::max<uint64_t>(1, std::strtoull(item.c_str() + 6, nullptr, 0)); continue; }
        if (item.rfind("ramdump=", 0) == 0) {
            const std::string rest = item.substr(8);
            const size_t at = rest.find('@');
            ramdump_dir_ = rest.substr(0, at);
            if (at != std::string::npos) {
                const char* p = rest.c_str() + at + 1;
                while (*p) {
                    char* e = nullptr;
                    const uint64_t st = std::strtoull(p, &e, 10);
                    if (e == p) break;
                    ramdump_stamps_.push_back(st);
                    p = (*e == '+') ? e + 1 : e;
                    if (e == p) break;
                }
            }
            continue;
        }
        std::fprintf(stderr, "nativeee: NATIVEEE_NAV: unknown item '%s' (ignored)\n", item.c_str());
    }
    EnsureArmed();
}

void NavProbe::EnsureArmed() {
    if (!table_tried_) {
        table_tried_ = true;
        if (!LoadTable(probe_path_))
            std::fprintf(stderr, "nativeee: NATIVEEE_NAV: probe table %s: %s (no probes; the closed-loop steps are blind)\n", probe_path_.c_str(),
                         table_error_.c_str());
        else
            std::fprintf(stderr, "nativeee: NATIVEEE_NAV: %zu probes from %s%s\n", probes_.size(), probe_path_.c_str(),
                         ramdump_stamps_.empty() ? "" : (" ramdump x" + std::to_string(ramdump_stamps_.size()) + " -> " + ramdump_dir_).c_str());
    }
    if (trace_wanted_ || !spec_given_) EnableTrace();
}

NavProbe::~NavProbe() {
    if (trace_) std::fclose(trace_);
}

bool NavProbe::LoadTable(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) { table_error_ = "cannot open"; return false; }
    std::ostringstream text;
    text << in.rdbuf();
    const std::string s = text.str();
    size_t i = 0;
    NavJson root;
    if (!NavJson::Parse(s, &root, &i)) { table_error_ = "malformed JSON at byte " + std::to_string(i); return false; }
    const NavJson* probes = root.Member("probes");
    if (!probes || probes->kind != NavJson::Array) { table_error_ = "no \"probes\" array"; return false; }
    for (const NavJson& pn : probes->items) {
        Probe p;
        const NavJson* name = pn.Member("name");
        const NavJson* chain = pn.Member("chain");
        if (!name || name->kind != NavJson::String || !chain || chain->kind != NavJson::Array || chain->items.empty()) {
            table_error_ = "probe #" + std::to_string(probes_.size()) + ": needs \"name\" and a non-empty \"chain\"";
            probes_.clear();
            return false;
        }
        p.name = name->str;
        for (const NavJson& c : chain->items) {
            uint32_t v = 0;
            if (!c.AsU32(&v)) { table_error_ = "probe " + p.name + ": bad chain element"; probes_.clear(); return false; }
            p.chain.push_back(v);
        }
        if (const NavJson* t = pn.Member("type"); t && t->kind == NavJson::String) p.type = t->str;
        if (const NavJson* sc = pn.Member("scale"); sc && sc->kind == NavJson::Number) p.scale = sc->number;
        probes_.push_back(std::move(p));
    }
    if (const NavJson* nav = root.Member("nav"); nav && nav->kind == NavJson::Object) {
        if (const NavJson* u = nav->Member("up_axis"); u && u->kind == NavJson::String && !u->str.empty()) conv_.up_axis = u->str[0];
        if (const NavJson* f = nav->Member("forward_offset_deg"); f && f->kind == NavJson::Number) conv_.forward_offset_deg = f->number;
        if (const NavJson* r = nav->Member("right_sign"); r && r->kind == NavJson::Number) conv_.right_sign = r->number < 0 ? -1 : 1;
        if (const NavJson* r = nav->Member("default_radius"); r && r->kind == NavJson::Number) conv_.default_radius = r->number;
        if (const NavJson* e = nav->Member("still_eps"); e && e->kind == NavJson::Number) conv_.still_eps = e->number;
    }
    return true;
}

void NavProbe::EnableTrace() {
    trace_wanted_ = true;
    if (!trace_) OpenTrace();
}

void NavProbe::OpenTrace() {
    if (trace_ || trace_path_.empty() || probes_.empty()) return;
    trace_ = std::fopen(trace_path_.c_str(), "w");
    if (!trace_) { std::fprintf(stderr, "nativeee: NATIVEEE_NAV: cannot write %s\n", trace_path_.c_str()); return; }
    std::fprintf(trace_, "stamp,valid");
    for (const Probe& p : probes_) std::fprintf(trace_, ",%s", p.name.c_str());
    std::fprintf(trace_, "\n");
}

bool NavProbe::ReadWord(uint32_t addr, uint32_t* out) const {
    const uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys + 4 > ram_.Size() || (phys & 3)) return false;
    std::memcpy(out, ram_.Data() + phys, 4);
    return true;
}

bool NavProbe::ReadProbe(const Probe& p, double* out) const {
    uint32_t addr = p.chain[0];
    for (size_t k = 1; k < p.chain.size(); k++) {
        uint32_t ptr = 0;
        if (!ReadWord(addr, &ptr) || ptr == 0) return false;
        addr = ptr + p.chain[k];
    }
    const uint32_t phys = addr & 0x1FFFFFFFu;
    const size_t width = (p.type == "u8" || p.type == "i8") ? 1 : (p.type == "u16" || p.type == "i16") ? 2 : p.type == "atan2_deg" ? 8 : 4;
    if (phys + width > ram_.Size()) return false;
    const uint8_t* b = ram_.Data() + phys;
    double v = 0;
    if (p.type == "f32") { float f; std::memcpy(&f, b, 4); v = f; }
    else if (p.type == "atan2_deg") {   // two f32 (x, y) -> the heading in degrees
        float fx, fy;
        std::memcpy(&fx, b, 4);
        std::memcpy(&fy, b + 4, 4);
        v = std::atan2(fy, fx) * 180.0 / 3.14159265358979323846;
    }
    else if (p.type == "i32") { int32_t x; std::memcpy(&x, b, 4); v = x; }
    else if (p.type == "u32") { uint32_t x; std::memcpy(&x, b, 4); v = x; }
    else if (p.type == "i16") { int16_t x; std::memcpy(&x, b, 2); v = x; }
    else if (p.type == "u16") { uint16_t x; std::memcpy(&x, b, 2); v = x; }
    else if (p.type == "i8") v = static_cast<int8_t>(*b);
    else v = *b;
    *out = v * p.scale;
    return true;
}

const NavProbe::Sample& NavProbe::Read(uint64_t stamp) {
    last_ = Sample{};
    last_.stamp = stamp;
    if (!ramdump_stamps_.empty())
        for (const uint64_t st : ramdump_stamps_)
            if (st == stamp) DumpRam(stamp);
    if (probes_.empty()) return last_;
    ++samples_;
    last_.values.assign(probes_.size(), kNaN);
    bool core_ok = true;   // every pos_* / yaw / cam_yaw probe present in the table read fine
    for (size_t k = 0; k < probes_.size(); k++) {
        const Probe& p = probes_[k];
        double v = 0;
        const bool ok = ReadProbe(p, &v);
        if (ok) last_.values[k] = v;
        const float f = static_cast<float>(v);
        if (p.name == "pos_x") { last_.x = f; core_ok &= ok; }
        else if (p.name == "pos_y") { last_.y = f; core_ok &= ok; }
        else if (p.name == "pos_z") { last_.z = f; core_ok &= ok; }
        else if (p.name == "yaw") { last_.yaw = f; core_ok &= ok; }
        else if (p.name == "cam_yaw") { last_.cam_yaw = f; core_ok &= ok; }
        else if (p.name == "cam_pitch") last_.cam_pitch = f;
        else if (p.name == "on_ground") last_.on_ground = ok ? static_cast<int>(v) : -1;
    }
    last_.valid = core_ok && !(std::isnan(last_.x) || std::isnan(last_.y) || std::isnan(last_.z));
    if (last_.valid) ++valid_;
    if (trace_ && stamp % every_ == 0) {
        std::fprintf(trace_, "%llu,%d", static_cast<unsigned long long>(stamp), last_.valid ? 1 : 0);
        for (const double v : last_.values) {
            if (std::isnan(v)) std::fprintf(trace_, ",");
            else std::fprintf(trace_, ",%.6g", v);
        }
        std::fprintf(trace_, "\n");
        if (stamp % 60 == 0) std::fflush(trace_);
    }
    return last_;
}

void NavProbe::DumpRam(uint64_t stamp) {
    std::error_code ec;
    std::filesystem::create_directories(ramdump_dir_, ec);
    const std::string path = ramdump_dir_ + "/ram_" + std::to_string(stamp) + ".bin";
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(ram_.Data(), 1, ram_.Size(), f);
        std::fclose(f);
        ++ramdumps_;
        std::fprintf(stderr, "nativeee: NATIVEEE_NAV: RAM dumped at stamp %llu -> %s\n", static_cast<unsigned long long>(stamp), path.c_str());
    } else {
        std::fprintf(stderr, "nativeee: NATIVEEE_NAV: cannot write %s\n", path.c_str());
    }
}

bool NavProbe::Value(const std::string& name, double* out) const {
    for (size_t k = 0; k < probes_.size(); k++)
        if (probes_[k].name == name && k < last_.values.size() && !std::isnan(last_.values[k])) {
            *out = last_.values[k];
            return true;
        }
    return false;
}

bool NavProbe::PokeF32(const std::string& base, uint32_t offset, const float* values, size_t count) {
    const Probe* p = nullptr;
    for (const Probe& q : probes_)
        if (q.name == base) { p = &q; break; }
    if (!p || p->chain.empty()) return false;
    // The object the chain's last dereference yields (a one-element chain: the static address itself).
    uint32_t addr = p->chain[0];
    for (size_t k = 1; k < p->chain.size(); k++) {
        uint32_t ptr = 0;
        if (!ReadWord(addr, &ptr) || ptr == 0) return false;
        addr = ptr;
    }
    const uint32_t phys = (addr + offset) & 0x1FFFFFFFu;
    if (phys + 4 * count > ram_.Size() || (phys & 3)) return false;
    std::memcpy(ram_.Data() + phys, values, 4 * count);
    ++pokes_;
    return true;
}

std::string NavProbe::Census() const {
    if (!table_tried_) return "off";
    std::string s = "probes=" + std::to_string(probes_.size()) + ":table=" + probe_path_ + ":trace=" + (trace_ ? trace_path_ : std::string("off")) +
                    ":samples=" + std::to_string(samples_) + ":valid=" + std::to_string(valid_) + ":ramdumps=" + std::to_string(ramdumps_) +
                    ":pokes=" + std::to_string(pokes_);
    if (last_.valid) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), ":last=%llu@%.2f,%.2f,%.2f:yaw=%.1f:cam=%.1f", static_cast<unsigned long long>(last_.stamp), last_.x,
                      last_.y, last_.z, last_.yaw, last_.cam_yaw);
        s += buf;
    }
    return s;
}

}  // namespace ps2r::title::sly2
