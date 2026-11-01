// Mods: the testing aids behind mods.enabled (STAGE FIX-2, mods track) -- the level select and the skip
// cutscene, each an act on the GAME'S OWN machinery at a frame boundary (nothing is short-circuited):
//
//   LEVEL SELECT (Warp): the game's loader FUN_0020e890 runs once per frame from main and acts when the
//   pending flag [G+0x3bf0] (G = the game singleton 0x3dd490) is set, copying the 0x4c-byte request at
//   G+0x3bf4 -- {u32 flags, LevelDesc[0x40], u32 spawn oid, s32 -1} -- the request FUN_0020df80 writes for
//   every door, portal, Resume Game and job restart in the game. Warp writes the same request (flags 0,
//   the descriptor of the chosen level from the game's own table, spawn 0x189 = the loader's default
//   spawn as the hub / restart paths use) and sets the flag; the game tears the old level down, streams
//   the new one and spawns the player as it would for its own request. The level table is the game's:
//   [0x3e1c38] = the level count (44), [0x3e1c40] -> LevelDesc[n], [0x3e1c44] -> s32[n] episode,
//   [0x3e1c48] -> s32[n] kind (0 = the episode's exterior = its hub); a level whose descriptor is empty
//   (w1 == w15, the loader's own null test FUN_00210b98) is left out (wid 18, the castle interior: no file
//   on the disc). Names come from the title's probe table (verify/nav/sly2_probe.json "save".episodes[]
//   .levels[], the save schema's order = the wid order). [0x3e1110] = the current wid.
//
//   SKIP CUTSCENE: a MOVIE (the MPEG player FUN_001b3bd0, a synchronous loop on main that ends when
//   sceMpegIsEnd(ctx) = *(u32*)(*(u32*)(ctx + 0x40)) is non-zero) is ended by setting that word -- the
//   loop exits through its own end test and the game's own teardown (FUN_001b4140: the sound stop, the
//   IPU reset, the stream close) runs; the player's repeat flag (player+0x108) is cleared as the retail
//   skip (0x1b3dc8) clears it. The movie predicate is player+0x110 ([0x2f67c0], the stream buffer) != 0.
//   An IN-ENGINE cutscene (a scripted sequence holds the player: [0x2dfbc4] & 4) is FAST-FORWARDED: the
//   pacer runs kFastForwardK fields per wall field (TickSource Turbo through FrameLimit) with the game's
//   clock left on the emulated Count, so every frame is a normal 1/60 s step and the game's state is the
//   one it would reach at normal speed, only sooner; the device ring is not fed meanwhile (muted). The
//   fast-forward ends when the predicate clears (or on a second Skip). Paced play only: a headless lane
//   has no wall pace to speed up (the census says so).
//
//   NATIVEEE_MODS=warp=<wid>@<frame>[,skip@<frame>...]   the lane schedule: the same acts at boundaries.
//   run.meta mods=: off | on:levels=..:cur=..:warps=..:refused=..:skips=..:fmv_skips=..:ff=..:ff_frames=..
//                   :ff_wall_ms=..:cutscene_clear_at=..:cutscene_clear_wall_ms=..:schedule=<spec>
#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

#include "app/host/session_controls.h"
#include "engine/core/config/config.h"
#include "engine/ee/mem/main_memory.h"

namespace ps2r::app { class SessionRuntime; }

namespace ps2r::title::sly2 {

class FrameLimit;

class Mods {
public:
    // Retail SCUS_973.16 addresses.
    static constexpr uint32_t kLevelCount = 0x3e1c38, kLevelDescs = 0x3e1c40, kLevelEpisodes = 0x3e1c44, kLevelKinds = 0x3e1c48;
    static constexpr uint32_t kCurrentWid = 0x3e1110;
    static constexpr uint32_t kLoadPending = 0x3e1080, kLoadReq = 0x3e1084, kTransition = 0x443740;
    static constexpr uint32_t kMovieBuf = 0x2f67c0, kMovieRepeat = 0x2f67b8, kMpegCtx = 0x2f67c8;
    static constexpr uint32_t kPadFlags = 0x2dfbc4;
    static constexpr uint32_t kDefaultSpawn = 0x189;
    static constexpr uint32_t kDescBytes = 0x40, kReqBytes = 0x4c, kMaxLevels = 64;
    static constexpr uint32_t kFastForwardK = 8;
    static constexpr uint16_t kPadSelect = 0x0001, kPadStart = 0x0008;   // libpad bit order

    struct Level {
        int wid = 0, episode = 0, kind = 0;
        std::string name;
    };

    // `spec` = NATIVEEE_MODS (nullptr = none); `title_dir` resolves the probe table.
    Mods(const ps2r::Config& cfg, MainMemory& ram, const std::string& title_dir, const char* spec);
    Mods(const Mods&) = delete;
    Mods& operator=(const Mods&) = delete;

    bool Enabled() const { return enabled_; }
    // The play session (the menu's requests and the published rows); null headless.
    void BindSession(ps2r::app::SessionRuntime* session) { session_ = session; }
    // The frame-rate policy the fast-forward drives (null = no fast-forward possible).
    void BindFrameLimit(FrameLimit* limit) { frame_limit_ = limit; }
    // The boundary (LegVerifySlot::VerifyStep, before the frame-rate policy is applied): the table, the
    // schedule, the session's requests, the fast-forward state.
    void OnBoundary(uint64_t boundary);
    // The live pad sample before it is stamped (play mode): Select + Start = Skip, masked from the game.
    void OnPadSample(uint16_t* buttons);
    // The device ring is not fed while this is set.
    bool FastForwarding() const { return ff_active_; }
    // The acts (also the schedule's). False with *why set when refused.
    bool Warp(int wid, std::string* why);
    bool Skip(std::string* why);
    const std::vector<Level>& Levels() const { return levels_; }
    std::string Census() const;

private:
    struct Step { uint64_t frame; int wid; bool done = false; };   // wid < 0 = skip
    uint32_t Read32(uint32_t addr) const;
    void Write32(uint32_t addr, uint32_t v);
    bool InRam(uint32_t addr, uint32_t bytes) const;
    void RefreshTable();
    void LoadNames(const std::string& title_dir);
    bool MoviePlaying() const { return Read32(kMovieBuf) != 0; }
    bool CutscenePlaying() const { return (Read32(kPadFlags) & 4) != 0; }
    void SetFastForward(bool on, uint64_t boundary);

    const bool enabled_;
    MainMemory& ram_;
    ps2r::app::SessionRuntime* session_ = nullptr;
    FrameLimit* frame_limit_ = nullptr;
    std::vector<std::string> names_;   // by wid, from the probe table
    std::vector<Level> levels_;
    uint32_t table_count_ = 0, table_descs_ = 0;   // the words the table was built from
    std::vector<Step> schedule_;
    std::string spec_;
    bool combo_held_ = false;
    bool ff_active_ = false;
    std::chrono::steady_clock::time_point ff_start_;
    uint64_t ff_start_boundary_ = 0, boundary_ = 0;
    uint64_t warps_ = 0, refused_ = 0, skips_ = 0, fmv_skips_ = 0, ff_starts_ = 0, ff_frames_ = 0, ff_wall_ms_ = 0;
    uint64_t cutscene_clear_at_ = 0, cutscene_clear_wall_ms_ = 0;
    bool cutscene_seen_ = false;
    std::chrono::steady_clock::time_point first_boundary_wall_{};
    int last_warp_wid_ = -1;
    std::string last_refusal_;
};

}  // namespace ps2r::title::sly2
