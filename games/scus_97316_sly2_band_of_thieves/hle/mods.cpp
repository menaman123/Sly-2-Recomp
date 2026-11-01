#include "games/scus_97316_sly2_band_of_thieves/hle/mods.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "app/host/session_runtime.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/frame_limit.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/nav_json.h"

namespace ps2r::title::sly2 {

Mods::Mods(const ps2r::Config& cfg, MainMemory& ram, const std::string& title_dir, const char* spec)
    : enabled_(cfg.mods.enabled), ram_(ram) {
    if (spec && *spec) spec_ = spec;
    if (!spec_.empty() && !enabled_) {
        std::fprintf(stderr, "nativeee: mods: NATIVEEE_MODS given but mods.enabled is off: the schedule is ignored\n");
        spec_.clear();
    }
    if (!enabled_) return;
    LoadNames(title_dir);
    // warp=<wid>@<frame> | skip@<frame>, comma-separated.
    for (std::string rest = spec_; !rest.empty();) {
        const size_t comma = rest.find(',');
        const std::string item = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const size_t at = item.find('@');
        if (at == std::string::npos) {
            std::fprintf(stderr, "nativeee: mods: NATIVEEE_MODS item '%s' lacks @<frame>; ignored\n", item.c_str());
            continue;
        }
        Step st{};
        st.frame = std::strtoull(item.substr(at + 1).c_str(), nullptr, 10);
        const std::string verb = item.substr(0, at);
        if (verb == "skip") st.wid = -1;
        else if (verb.compare(0, 5, "warp=") == 0) st.wid = std::atoi(verb.substr(5).c_str());
        else {
            std::fprintf(stderr, "nativeee: mods: NATIVEEE_MODS item '%s': not warp=<wid> or skip; ignored\n", item.c_str());
            continue;
        }
        schedule_.push_back(st);
    }
}

void Mods::LoadNames(const std::string& title_dir) {
    const std::string path = title_dir + "/verify/nav/sly2_probe.json";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "nativeee: mods: no probe table at %s (levels are listed by number)\n", path.c_str());
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    NavJson root;
    size_t at = 0;
    if (!NavJson::Parse(text, &root, &at)) {
        std::fprintf(stderr, "nativeee: mods: probe table parse failed at byte %zu\n", at);
        return;
    }
    const NavJson* save = root.Member("save");
    const NavJson* episodes = save ? save->Member("episodes") : nullptr;
    if (!episodes || episodes->kind != NavJson::Array) return;
    for (const NavJson& ep : episodes->items) {
        const NavJson* name = ep.Member("name");
        const NavJson* levels = ep.Member("levels");
        if (!levels || levels->kind != NavJson::Array) continue;
        for (const NavJson& lv : levels->items)
            names_.push_back((name && name->kind == NavJson::String ? name->str : std::string("?")) + "/" +
                             (lv.kind == NavJson::String ? lv.str : std::string("?")));
    }
}

uint32_t Mods::Read32(uint32_t addr) const {
    uint32_t v = 0;
    const uint32_t phys = MainMemory::Phys(addr);
    if (phys + 4 <= ram_.Size()) std::memcpy(&v, ram_.Data() + phys, 4);
    return v;
}

void Mods::Write32(uint32_t addr, uint32_t v) {
    const uint32_t phys = MainMemory::Phys(addr);
    if (phys + 4 <= ram_.Size()) std::memcpy(ram_.Data() + phys, &v, 4);
}

bool Mods::InRam(uint32_t addr, uint32_t bytes) const {
    const uint32_t phys = MainMemory::Phys(addr);
    return addr != 0 && phys + bytes <= ram_.Size();
}

// The game builds its table once at boot (FUN_0020f318); it is read whenever the count / pointer words
// change (a cold boot's first frames, a restored image).
void Mods::RefreshTable() {
    const uint32_t count = Read32(kLevelCount), descs = Read32(kLevelDescs);
    if (count == table_count_ && descs == table_descs_) return;
    table_count_ = count;
    table_descs_ = descs;
    levels_.clear();
    const uint32_t episodes = Read32(kLevelEpisodes), kinds = Read32(kLevelKinds);
    if (count == 0 || count > kMaxLevels || !InRam(descs, count * kDescBytes) || !InRam(episodes, count * 4) || !InRam(kinds, count * 4)) {
        if (session_) session_->SetModsLevels({});
        return;
    }
    for (uint32_t wid = 0; wid < count; wid++) {
        const uint32_t d = descs + wid * kDescBytes;
        if (Read32(d + 4) == Read32(d + 15 * 4)) continue;   // an empty descriptor: no level file
        Level l;
        l.wid = static_cast<int>(wid);
        l.episode = static_cast<int32_t>(Read32(episodes + wid * 4));
        l.kind = static_cast<int32_t>(Read32(kinds + wid * 4));
        l.name = wid < names_.size() ? names_[wid] : "level " + std::to_string(wid);
        levels_.push_back(l);
    }
    if (session_) {
        std::vector<ps2r::app::SessionControls::ModsLevel> rows;
        for (const Level& l : levels_) rows.push_back({l.wid, l.episode, l.name, l.kind == 0 ? "hub" : "level"});
        session_->SetModsLevels(std::move(rows));
    }
    std::fprintf(stderr, "nativeee: mods: level table read: %zu of %u levels listed\n", levels_.size(), count);
}

bool Mods::Warp(int wid, std::string* why) {
    RefreshTable();
    const Level* level = nullptr;
    for (const Level& l : levels_)
        if (l.wid == wid) level = &l;
    if (!level) { *why = "level " + std::to_string(wid) + " is not in the game's table"; return false; }
    if (MoviePlaying()) { *why = "a movie is playing"; return false; }
    if (Read32(kLoadPending) != 0 || Read32(kTransition) == 1) { *why = "a level load is already pending"; return false; }
    // LoadReq{flags 0, the descriptor, the default spawn, -1}, then the flag: the loader acts next frame.
    const uint32_t desc = table_descs_ + static_cast<uint32_t>(wid) * kDescBytes;
    Write32(kLoadReq + 0x00, 0);
    for (uint32_t off = 0; off < kDescBytes; off += 4) Write32(kLoadReq + 4 + off, Read32(desc + off));
    Write32(kLoadReq + 0x44, kDefaultSpawn);
    Write32(kLoadReq + 0x48, 0xffffffffu);
    Write32(kLoadPending, 1);
    ++warps_;
    last_warp_wid_ = wid;
    std::fprintf(stderr, "nativeee: mods: warp -> wid %d (%s): the game's loader request written\n", wid, level->name.c_str());
    return true;
}

bool Mods::Skip(std::string* why) {
    if (MoviePlaying()) {
        const uint32_t inner = Read32(kMpegCtx + 0x40);
        if (!InRam(inner, 4)) { *why = "the movie's end word is not readable"; return false; }
        Write32(inner, 1);              // sceMpegIsEnd -> non-zero: the player's loop exits
        Write32(kMovieRepeat, 0);       // as the retail skip: the movie is not repeated
        ++skips_;
        ++fmv_skips_;
        std::fprintf(stderr, "nativeee: mods: skip: movie ended through its end word (the game's teardown runs)\n");
        return true;
    }
    if (CutscenePlaying()) {
        if (!frame_limit_) { *why = "no frame-rate policy to fast-forward with"; return false; }
        ++skips_;
        if (ff_active_) { SetFastForward(false, boundary_); return true; }
        SetFastForward(true, boundary_);
        return true;
    }
    *why = "no movie or cutscene is playing";
    return false;
}

void Mods::SetFastForward(bool on, uint64_t boundary) {
    if (on == ff_active_) return;
    ff_active_ = on;
    if (frame_limit_) frame_limit_->SetFastForward(on ? kFastForwardK : 0);
    if (on) {
        ++ff_starts_;
        ff_start_ = std::chrono::steady_clock::now();
        ff_start_boundary_ = boundary;
        std::fprintf(stderr, "nativeee: mods: skip: cutscene fast-forward x%u from boundary %llu\n", kFastForwardK,
                     static_cast<unsigned long long>(boundary));
    } else {
        ff_wall_ms_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - ff_start_).count());
        std::fprintf(stderr, "nativeee: mods: fast-forward ended at boundary %llu after %llu frames\n",
                     static_cast<unsigned long long>(boundary), static_cast<unsigned long long>(boundary - ff_start_boundary_));
    }
}

void Mods::OnBoundary(uint64_t boundary) {
    if (!enabled_) return;
    boundary_ = boundary;
    RefreshTable();
    std::string why;
    for (Step& st : schedule_) {
        if (st.done || boundary < st.frame) continue;
        st.done = true;
        const bool ok = st.wid < 0 ? Skip(&why) : Warp(st.wid, &why);
        if (!ok) {
            ++refused_;
            last_refusal_ = why;
            std::fprintf(stderr, "nativeee: mods: schedule %s at boundary %llu refused: %s\n", st.wid < 0 ? "skip" : "warp",
                         static_cast<unsigned long long>(boundary), why.c_str());
        }
    }
    if (session_) {
        const int wid = session_->TakeModsWarpRequest();
        if (wid >= 0 && !Warp(wid, &why)) {
            ++refused_;
            last_refusal_ = why;
            std::fprintf(stderr, "nativeee: mods: warp %d refused: %s\n", wid, why.c_str());
        }
        if (session_->TakeModsSkipRequest() && !Skip(&why)) {
            ++refused_;
            last_refusal_ = why;
            std::fprintf(stderr, "nativeee: mods: skip refused: %s\n", why.c_str());
        }
    }
    // The fast-forward runs while the cutscene holds the player; its end is the census's clear.
    if (ff_active_) {
        ++ff_frames_;
        if (!CutscenePlaying() || MoviePlaying()) {
            cutscene_clear_at_ = boundary;
            SetFastForward(false, boundary);
        }
    }
    if (session_) session_->SetModsState(static_cast<int>(Read32(kCurrentWid)), MoviePlaying() || CutscenePlaying(), ff_active_);
}

void Mods::OnPadSample(uint16_t* buttons) {
    if (!enabled_) return;
    const bool combo = (*buttons & (kPadSelect | kPadStart)) == (kPadSelect | kPadStart);
    if (combo) {
        *buttons = static_cast<uint16_t>(*buttons & ~(kPadSelect | kPadStart));   // the game never sees the pair
        if (!combo_held_ && session_) session_->Controls().Submit(ps2r::app::SessionControls::Action::ModsSkipCutscene);
    }
    combo_held_ = combo;
}

std::string Mods::Census() const {
    if (!enabled_) return "off";
    std::string s = "on:levels=" + std::to_string(levels_.size()) + ":cur=" + std::to_string(Read32(kCurrentWid)) +
                    ":warps=" + std::to_string(warps_) + ":refused=" + std::to_string(refused_) + ":skips=" + std::to_string(skips_) +
                    ":fmv_skips=" + std::to_string(fmv_skips_) + ":ff=" + std::to_string(ff_starts_) + ":ff_frames=" +
                    std::to_string(ff_frames_) + ":ff_wall_ms=" + std::to_string(ff_wall_ms_) + ":cutscene_clear_at=" +
                    std::to_string(cutscene_clear_at_) + ":last_wid=" + std::to_string(last_warp_wid_) +
                    ":schedule=" + (spec_.empty() ? std::string("none") : spec_);
    if (!last_refusal_.empty()) {
        std::string why = last_refusal_;
        for (char& c : why)
            if (c == ' ' || c == '=' || c == ':' || c == '\n') c = '_';
        s += ":last_refusal=" + why;
    }
    return s;
}

}  // namespace ps2r::title::sly2
