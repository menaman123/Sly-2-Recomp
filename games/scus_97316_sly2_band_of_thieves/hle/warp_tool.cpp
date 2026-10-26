#include "games/scus_97316_sly2_band_of_thieves/hle/warp_tool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ps2r::title::sly2 {

namespace {

bool Hex(const NavJson* node, uint32_t* out) { return node && node->AsU32(out); }

uint32_t Read32(const std::vector<uint8_t>& image, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, image.data() + off, 4);
    return v;
}

void Write32(std::vector<uint8_t>& image, size_t off, uint32_t v) { std::memcpy(image.data() + off, &v, 4); }

// Index of `name` in a JSON string array (-1 = absent).
int IndexOf(const NavJson* names, const std::string& name) {
    if (!names || names->kind != NavJson::Array) return -1;
    for (size_t i = 0; i < names->items.size(); i++)
        if (names->items[i].kind == NavJson::String && names->items[i].str == name) return static_cast<int>(i);
    return -1;
}

}  // namespace

bool WarpTool::Blocks::Load(const NavJson* table) {
    if (!table || table->kind != NavJson::Object) return false;
    names = table->Member("names");
    return Hex(table->Member("base"), &base) && Hex(table->Member("stride"), &stride) && names && stride != 0;
}

bool WarpTool::Blocks::Set(std::vector<uint8_t>& image, const std::string& name, uint32_t field_off, uint32_t value) const {
    const int idx = IndexOf(names, name);
    if (idx < 0) return false;
    const size_t off = static_cast<size_t>(base) + static_cast<size_t>(stride) * static_cast<size_t>(idx) + field_off;
    if (off + 4 > image.size()) return false;
    Write32(image, off, value);
    return true;
}

WarpTool::WarpTool(const char* spec, const std::string& mc_dir, const std::string& title_dir) {
    if (!spec || !*spec) return;
    requested_ = true;
    NavJson root;
    if (!LoadTable(title_dir + "/verify/nav/sly2_probe.json", &root, &error_)) {
        std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
        return;
    }
    const NavJson* save = root.Member("save");
    if (!save) {
        error_ = "probe table has no \"save\" object";
        std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
        return;
    }
    if (!ParseSpec(spec, *save, &episode_, &jobs_, &error_)) {
        std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
        return;
    }
    const NavJson* episodes = save->Member("episodes");
    const NavJson& ep = episodes->items[static_cast<size_t>(episode_)];
    if (const NavJson* n = ep.Member("name")) name_ = n->str;
    Hex(ep.Member("exterior_wid"), &wid_);
    const NavJson* file = save->Member("file");
    const std::string path = mc_dir + "/" + (file && file->kind == NavJson::String ? file->str : std::string("mc0/BASCUS-97316YAOTWTD!/save1"));
    std::vector<uint8_t> image;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error_ = "cannot read the save " + path;
            std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
            return;
        }
        image.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!Rewrite(image, *save, episode_, jobs_, &error_)) {
        std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()))) {
        error_ = "cannot write the save " + path;
        std::fprintf(stderr, "nativeee: warp: %s\n", error_.c_str());
        return;
    }
    applied_ = true;
    std::fprintf(stderr, "nativeee: warp: save1 rewritten -> episode %d (%s) wid %u jobs %d: %s\n", episode_, name_.c_str(), wid_, jobs_,
                 path.c_str());
}

std::string WarpTool::Census() const {
    if (!requested_) return "off";
    std::string s = "warp=" + std::to_string(episode_) + ":" + (name_.empty() ? "?" : name_) + ":wid=" + std::to_string(wid_) +
                    ":jobs=" + std::to_string(jobs_) + ":";
    if (applied_) return s + "applied";
    std::string why = error_;
    for (char& c : why)
        if (c == ' ' || c == '=' || c == '\n') c = '_';
    return s + "failed:" + why;
}

bool WarpTool::LoadTable(const std::string& path, NavJson* out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "no probe table at " + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t at = 0;
    if (!NavJson::Parse(text, out, &at)) {
        *error = "probe table parse failed at byte " + std::to_string(at);
        return false;
    }
    return true;
}

bool WarpTool::ParseSpec(const std::string& spec, const NavJson& save_table, int* episode, int* jobs, std::string* error) {
    const NavJson* episodes = save_table.Member("episodes");
    if (!episodes || episodes->kind != NavJson::Array || episodes->items.empty()) {
        *error = "probe table \"save\" has no episodes";
        return false;
    }
    std::string ep = spec, job;
    const size_t comma = spec.find(',');
    if (comma != std::string::npos) {
        ep = spec.substr(0, comma);
        job = spec.substr(comma + 1);
    }
    *episode = -1;
    *jobs = -1;
    if (!ep.empty() && ep.find_first_not_of("0123456789") == std::string::npos) {
        *episode = std::atoi(ep.c_str());
    } else {
        for (size_t i = 0; i < episodes->items.size(); i++) {
            const NavJson* n = episodes->items[i].Member("name");
            if (n && n->kind == NavJson::String && n->str == ep) *episode = static_cast<int>(i);
        }
    }
    if (*episode < 0 || *episode >= static_cast<int>(episodes->items.size())) {
        *error = "NATIVEEE_WARP: unknown episode '" + ep + "' (0..8 or a name)";
        return false;
    }
    if (!job.empty()) {
        if (job.find_first_not_of("0123456789") != std::string::npos) {
            *error = "NATIVEEE_WARP: the job count must be a number, got '" + job + "'";
            return false;
        }
        *jobs = std::atoi(job.c_str());
    }
    return true;
}

bool WarpTool::Rewrite(std::vector<uint8_t>& image, const NavJson& save, int episode, int jobs, std::string* error) {
    uint32_t file_bytes = 0, size_off = 0, size_val = 0, sum_off = 0, sum_val = 0;
    if (!Hex(save.Member("file_bytes"), &file_bytes) || !Hex(save.Member("trailer_size_off"), &size_off) ||
        !Hex(save.Member("trailer_size"), &size_val) || !Hex(save.Member("trailer_checksum_off"), &sum_off) ||
        !Hex(save.Member("trailer_checksum"), &sum_val)) {
        *error = "probe table \"save\" lacks the file / trailer facts";
        return false;
    }
    if (image.size() != file_bytes) {
        *error = "save1 is " + std::to_string(image.size()) + " bytes, expected " + std::to_string(file_bytes);
        return false;
    }
    if (Read32(image, size_off) != size_val || Read32(image, sum_off) != sum_val) {
        *error = "save1 trailer is not this ELF's layout (size/checksum words differ)";
        return false;
    }
    const NavJson* fields = save.Member("fields");
    uint32_t off_ep = 0, off_best = 0, off_wid = 0, off_oid = 0, off_dt = 0, oid_default = 0xffffffffu;
    if (!fields || !Hex(fields->Member("episodeCurrent"), &off_ep) || !Hex(fields->Member("episodeBest"), &off_best) ||
        !Hex(fields->Member("widCurrent"), &off_wid) || !Hex(fields->Member("oidPoCurrent"), &off_oid) || !Hex(fields->Member("dt"), &off_dt)) {
        *error = "probe table \"save\".fields lacks the header offsets";
        return false;
    }
    Hex(save.Member("oid_default"), &oid_default);
    const NavJson* episodes = save.Member("episodes");
    if (!episodes || episode < 0 || episode >= static_cast<int>(episodes->items.size())) {
        *error = "episode out of the table";
        return false;
    }
    const NavJson& ep = episodes->items[static_cast<size_t>(episode)];
    uint32_t wid = 0;
    if (!Hex(ep.Member("exterior_wid"), &wid)) {
        *error = "episode has no exterior_wid";
        return false;
    }
    Write32(image, off_ep, static_cast<uint32_t>(episode));
    Write32(image, off_best, static_cast<uint32_t>(episode));
    Write32(image, off_wid, wid);
    Write32(image, off_oid, oid_default);
    if (Read32(image, off_dt) == 0) {
        const float one = 1.0f;
        uint32_t bits = 0;
        std::memcpy(&bits, &one, 4);
        Write32(image, off_dt, bits);
    }
    // Earlier episodes complete, the chosen one open.
    if (const NavJson* pct = save.Member("episode_dt_pct")) {
        if (const NavJson* offs = pct->Member("offsets")) {
            for (size_t i = 0; i < offs->items.size() && i < episodes->items.size(); i++) {
                uint32_t o = 0;
                if (!offs->items[i].AsU32(&o) || o + 8 > image.size()) continue;
                if (static_cast<int>(i) < episode) Write32(image, o + 4, 100);
                else if (static_cast<int>(i) == episode && Read32(image, o + 4) >= 100) Write32(image, o + 4, 0);
            }
        }
    }
    if (jobs < 0) return true;
    Blocks missions, tasks;
    if (!missions.Load(save.Member("missions")) || !tasks.Load(save.Member("tasks"))) {
        *error = "probe table \"save\" lacks the missions / tasks blocks (needed for a job count)";
        return false;
    }
    uint32_t m_done = 0, t_done = 0, t_state = 0;
    Hex(save.Member("missions")->Member("is_complete_off"), &m_done);
    Hex(save.Member("tasks")->Member("is_complete_off"), &t_done);
    Hex(save.Member("tasks")->Member("state_off"), &t_state);
    const std::string prefix = "t" + std::to_string(episode) + "_";
    auto complete_task = [&](const std::string& name) {
        tasks.Set(image, name, t_done, 1);
        tasks.Set(image, name, t_state, 3);
    };
    complete_task(prefix + "intro");
    const NavJson* order = ep.Member("jobs");
    if (!order || order->kind != NavJson::Array) {
        *error = "episode lists no jobs";
        return false;
    }
    if (jobs > static_cast<int>(order->items.size())) {
        *error = "job count " + std::to_string(jobs) + " exceeds the episode's " + std::to_string(order->items.size()) + " jobs";
        return false;
    }
    for (int j = 0; j < jobs; j++) {
        const std::string& job = order->items[static_cast<size_t>(j)].str;
        if (!missions.Set(image, "m" + std::to_string(episode) + "_" + job, m_done, 1)) {
            *error = "unknown mission m" + std::to_string(episode) + "_" + job;
            return false;
        }
        complete_task(prefix + job + "_intro");
        complete_task(prefix + job + "_complete");
    }
    return true;
}

}  // namespace ps2r::title::sly2
