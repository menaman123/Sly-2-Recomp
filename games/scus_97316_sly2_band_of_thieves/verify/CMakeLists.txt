# games/scus_97316_sly2_band_of_thieves/verify -- the title's judges as ctest rows. The engine's
# tests/CMakeLists.txt adds this directory AFTER enable_testing() (a title's rows must be registered
# where testing is enabled; the hle/ directory is added before it, for the libraries).
# --- the TEXTURE gate (verify/judge_textures.py) --------------------------------------------------
# Every distinct texture the resolver decodes in a leg (NATIVEEE_TEX_DUMP=<from>,<to> -> <out>/textures/)
# judged against Oracle B (the engine's fast decode tier == the per-pixel PsmLayout reference, recorded
# per texture at dump time) and Oracle A (PCSX2 2.7.329 HW per-draw texture dumps of the same scene,
# matched by the TEX0 tuple, compared per texel with error-class attribution: swizzle / clut /
# format_tbw / source / value). The selftest runs everywhere. The Cairo-opening gate needs the two
# game-derived fixture directories (never in the repository): under $NATIVEEE_PRIVATE_FIXTURES,
#   games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/textures/   (the leg's dump:
#       nativeee --headless --frames 1003 --dump-frames <d> --harness-out <o> --iso <iso>
#       with NATIVEEE_TEX_DUMP=400,1000; copy <d>/textures)
#   games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/pcsx2/      (PCSX2 DumpGSData:
#       SaveTexture + SaveAlpha + SaveInfo, SaveFrameStart=768 SaveFrameCount=601 SaveFrameBy=60; the
#       engine's present key k == PCSX2 frame k + 368 on this leg)
# and SKIPs (77) without them -- the lanes that hold a disc run it on those fixture frames.
add_test(NAME Sly2_JudgeTexturesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_textures.py --selftest)
set_tests_properties(Sly2_JudgeTexturesSelftest PROPERTIES TIMEOUT 120)
ps2r_add_private_fixture_test(NAME Sly2_JudgeTexturesCairo
    FIXTURES games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/textures
             games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/pcsx2
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_textures.py
            --textures {PRIVATE_FIXTURES}/games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/textures
            --pcsx2 {PRIVATE_FIXTURES}/games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/pcsx2
            --artefacts ${CMAKE_CURRENT_BINARY_DIR}/judge_textures --summary)
set_tests_properties(Sly2_JudgeTexturesCairo PROPERTIES TIMEOUT 600)

# --- the SMOOTHNESS judge (verify/judge_smooth.py) -------------------------------------------------
# The presented-frame steadiness of a play run from <harness-out>/present_times.csv (the title's
# FrameTimeline: every EE boundary, consumer present and new-picture window draw on the wall clock):
# histograms, the field mixture, and the round-H gates (cap off: p99 -25 % and >50 ms gaps -50 % vs
# main; cap 30: p99 <= 36 ms and no gap > 50 ms). The selftest runs everywhere; the gates need runs.
add_test(NAME Sly2_JudgeSmoothSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_smooth.py --selftest)

# judge_pictures.py (round P): pictures per second (unique game-produced frames: the output lane) per
# wall window with the frozen gates (p50 >= 59, p99 inter-picture gap <= 20 ms, no three-field beat) and
# the clock invariant from the run.meta `clock=` row (logic_hz 60.0 +- 0.1, slow_motion 0). The selftest
# runs everywhere; the gates need a play run with --harness-out.
add_test(NAME Sly2_JudgePicturesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_pictures.py --selftest)
# judge_pacing.py (round 3): the presenter's pacing from the SHOWN lane with the frozen gates (p99 shown gap
# <= 20 ms, <= 1 % of gaps > 20 ms = doubled frames, none > 40 ms), the field histogram, the per-second drop
# table and the production floor (fields minus pictures received: the repeats no presenter can avoid) so the
# presenter's own cost is read apart from the game's rate. The selftest runs everywhere; the gates need a run.
add_test(NAME Sly2_JudgePacingSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_pacing.py --selftest)
# judge_render_throughput.py (round P phase B): the GS consumer ALONE over a captured consumer stream
# (engine/gs/hle/capture, `tools/gs_replay stream` -> frames.csv) with the frozen gates (fps p50 >= 160 at internal
# scale 1 / >= 120 at scale 2 as 1000 / p50 frame ms, no measured frame > 12 ms) and the frames-lane hash identity
# of two replays. The selftest runs everywhere; the gates need a capture (game-derived, outside the repository).
add_test(NAME Sly2_JudgeRenderThroughputSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_render_throughput.py --selftest)
set_tests_properties(Sly2_JudgeRenderThroughputSelftest PROPERTIES TIMEOUT 60)
set_tests_properties(Sly2_JudgeSmoothSelftest PROPERTIES TIMEOUT 60)

# --- the SHADOW judge (verify/judge_shadow.py, round H item 2 / D8) --------------------------------
# The title's stencil shadows (the per-frame G-byte counter block: shuffle save, +-2 volumes under GEQUAL,
# bit moves into the alpha byte, the P_8H-of-FRAME darkening lerp) judged against PCSX2's per-draw SaveRT
# dump of the same scene: the pixels the darkening draw changed on each side (overlap >= 0.80, spurious
# <= 0.30, no extra darkening over the caster), fixture verify/shadow/shadow_fixture.json (numbers only;
# the dumps are game-derived and stay outside the repository). The selftest runs everywhere.
add_test(NAME Sly2_JudgeShadowSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_shadow.py --selftest)
set_tests_properties(Sly2_JudgeShadowSelftest PROPERTIES TIMEOUT 60)
# The counter-draw rewrite behind the shadow judge (hle/shadow_counter_split.{h,cpp}): class test, overlap
# test, the subtract / add pair vs the GS wrap formula on a synthetic closed volume, the sink's forwarding.
add_executable(sly2_test_shadow_counter_split
    ${CMAKE_CURRENT_SOURCE_DIR}/test_shadow_counter_split.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../hle/shadow_counter_split.cpp)
target_include_directories(sly2_test_shadow_counter_split PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(sly2_test_shadow_counter_split PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sly2_test_shadow_counter_split PRIVATE ps2r_gs_ir)
add_test(NAME Sly2_ShadowCounterSplit COMMAND sly2_test_shadow_counter_split)
set_tests_properties(Sly2_ShadowCounterSplit PROPERTIES TIMEOUT 60)

# --- the CEL-SHADING judge (verify/judge_cel.py, stage CEL) ------------------------------------------
# The title's ink (one GS LINE pair per visible silhouette edge, PRIM 0xc1 LINE ABE AA1 + TEST ATST NEVER
# AFAIL RGB_ONLY ZTST GREATER, colour 16/16/16/128, then the Z pass) and its two-palette shading judged
# against PCSX2's per-draw SaveInfo dump + frame PNG of the same scene: ink contrast / visibility /
# thickness over each side's own line list, the ring coverage on pose-matched cases, the per-material
# luminance modes and gradient leak inside the body mask, and the character's textures byte-identical
# (Oracle A). Fixture verify/cel/cel_fixture.json (numbers only; frames, dumps and twins are game-derived
# and stay outside the repository). The selftest runs everywhere.
add_test(NAME Sly2_JudgeCelSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_cel.py --selftest)
set_tests_properties(Sly2_JudgeCelSelftest PROPERTIES TIMEOUT 120)

# --- the SOLIDITY gate (verify/judge_solidity.py) ---------------------------------------------------
# One presented frame against its PCSX2 twin inside the fixture's masks (verify/solidity/fixtures.json:
# numbers only): lum_wall / lum_sly_w1 <= 6, see_through <= 0.1 (round H item 1: the D1 / D3 / D7
# see-through class, fixed by E330). The frames and twins are game-derived (never in the repository);
# the selftest runs everywhere on synthetic frames.
add_test(NAME Sly2_JudgeSoliditySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_solidity.py --selftest)
set_tests_properties(Sly2_JudgeSoliditySelftest PROPERTIES TIMEOUT 120)

# --- the CHARGED-CANE EFFECT gate (verify/judge_cane.py, round 6 track cane) -------------------------
# The particle systems' presence: median orange / bright pixel counts inside the cane-head box over the
# hold frames of the Paris SQUARE-hold dump (verify/cane/fixtures.json: numbers only) >= 100 / >= 100,
# the pre-fix dump below, the PCSX2 twin above (the VU0 macro-mode MAC-flag commit of engine fix/cane).
# Frames and twin are game-derived (never in the repository); the selftest runs everywhere.
add_test(NAME Sly2_JudgeCaneSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_cane.py --selftest)
set_tests_properties(Sly2_JudgeCaneSelftest PROPERTIES TIMEOUT 60)

# --- the METAL IDENTITY judge (verify/judge_metal_identity.py, round 3 track metal) ------------------
# The Metal arm's picture per pixel inside the BAND the GL arm's two blend routes span (fixed-function
# default vs `--force-shader-blend`, +-2/255): outside <= 0.3 % of the pixels and max excess <= 8 on every
# 50th frame of the Paris / Castle / Cairo consumer-stream captures (game-derived, never in the repository:
# under $NATIVEEE_PRIVATE_FIXTURES/games/scus_97316_sly2_band_of_thieves/verify/metal/<capture>.gsstream).
# The selftest runs everywhere; the gate needs a Metal device (macOS) and replays gs_replay three times per
# capture (one process at a time), SKIP (77) without the captures or a device.
add_test(NAME Sly2_JudgeMetalIdentitySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_metal_identity.py --selftest)
set_tests_properties(Sly2_JudgeMetalIdentitySelftest PROPERTIES TIMEOUT 60)
if(APPLE AND TARGET gs_replay)
    ps2r_add_private_fixture_test(NAME Sly2_JudgeMetalIdentity
        FIXTURES games/scus_97316_sly2_band_of_thieves/verify/metal/paris_600_1800.gsstream
                 games/scus_97316_sly2_band_of_thieves/verify/metal/castle_1000_1150.gsstream
                 games/scus_97316_sly2_band_of_thieves/verify/metal/cairo_450_600.gsstream
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_metal_identity.py run
                --gs-replay $<TARGET_FILE:gs_replay>
                --stream {PRIVATE_FIXTURES}/games/scus_97316_sly2_band_of_thieves/verify/metal/paris_600_1800.gsstream
                --stream {PRIVATE_FIXTURES}/games/scus_97316_sly2_band_of_thieves/verify/metal/castle_1000_1150.gsstream
                --stream {PRIVATE_FIXTURES}/games/scus_97316_sly2_band_of_thieves/verify/metal/cairo_450_600.gsstream
                --artefacts ${CMAKE_CURRENT_BINARY_DIR}/judge_metal_identity --frames 300 --step 50)
    set_tests_properties(Sly2_JudgeMetalIdentity PROPERTIES TIMEOUT 900)
endif()

# scene_matrix.py (round 4, track scenes): the SCENE MATRIX -- one play run per scene x video backend (hubs of
# every episode, the dense Paris spawn view, a ledge, a mission, the Cairo museum, a briefing cutscene, the cold
# boot through the FMV; Metal and GL) read into a RED / GREEN table: GREEN = pictures/s p50 >= 59 AND doubled frames
# <= 1 % (judge_pictures' output lane + judge_pacing's shown lane), DEFECT = the run parked / crashed / timed out;
# per cell also frame_hz, gs_draws per frame, VU1 worker / GIF drain / consumer ms per frame from run.meta. `plan`
# lists the scenes, `cell` reads one run, `table` writes the matrix, `soak` reads a long run per 10 s window. The
# runs are driven by a shell wrapper around the play harness (the work tree's scene_matrix.sh: one windowed
# process at a time); the selftest runs everywhere on synthetic runs.
add_test(NAME Sly2_SceneMatrixSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scene_matrix.py --selftest)
set_tests_properties(Sly2_SceneMatrixSelftest PROPERTIES TIMEOUT 60)
