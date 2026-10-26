# Sly 2's judges as ctest rows. Added by the engine's tests/CMakeLists.txt after enable_testing().
# Fixtures derived from the game are never in the repository; tests that need them look under
# $NATIVEEE_PRIVATE_FIXTURES and SKIP (77) without them. Every judge's selftest runs everywhere.
# --- the TEXTURE gate (verify/judge_textures.py) --------------------------------------------------
# Every texture decoded in a run (NATIVEEE_TEX_DUMP) checked against the engine's per-pixel reference
# decode, and against PCSX2 2.7.329 per-draw texture dumps of the same scene (matched by TEX0, compared
# per texel, errors classed as swizzle / clut / format_tbw / source / value). The Cairo-opening gate needs,
# under $NATIVEEE_PRIVATE_FIXTURES,
#   games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/textures/   (the leg's dump:
#       nativeee --headless --frames 1003 --dump-frames <d> --harness-out <o> --iso <iso>
#       with NATIVEEE_TEX_DUMP=400,1000; copy <d>/textures)
#   games/scus_97316_sly2_band_of_thieves/verify/textures/cairo_opening/pcsx2/      (PCSX2 DumpGSData:
#       SaveTexture + SaveAlpha + SaveInfo, SaveFrameStart=768 SaveFrameCount=601 SaveFrameBy=60; the
#       engine's present key k == PCSX2 frame k + 368 on this leg)
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
# Frame steadiness of a play run from <harness-out>/present_times.csv: histograms, the field mixture, and
# gates (cap 30: p99 <= 36 ms and no gap > 50 ms). The gates need recorded runs.
add_test(NAME Sly2_JudgeSmoothSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_smooth.py --selftest)

# judge_pictures.py: unique game frames per second per wall window (p50 >= 59, p99 gap <= 20 ms, no
# three-field beat) and the clock= row (logic_hz 60.0 +- 0.1, slow_motion 0). Needs a play run.
add_test(NAME Sly2_JudgePicturesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_pictures.py --selftest)
# judge_pacing.py: presenter pacing from the shown lane (p99 gap <= 20 ms, <= 1 % doubled frames, none
# > 40 ms), separating the presenter's cost from the game's frame rate. Needs a play run.
add_test(NAME Sly2_JudgePacingSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_pacing.py --selftest)
# judge_render_throughput.py: the GS consumer alone, replaying a captured stream (tools/gs_replay):
# fps p50 >= 160 at scale 1 / >= 120 at scale 2, no frame > 12 ms, and identical hashes across two replays.
# Needs a capture.
add_test(NAME Sly2_JudgeRenderThroughputSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_render_throughput.py --selftest)
set_tests_properties(Sly2_JudgeRenderThroughputSelftest PROPERTIES TIMEOUT 60)
set_tests_properties(Sly2_JudgeSmoothSelftest PROPERTIES TIMEOUT 60)

# --- the SHADOW judge (verify/judge_shadow.py) -----------------------------------------------------
# Stencil shadows (the G-byte counter volumes and the darkening pass) against PCSX2's per-draw dump of the
# same scene: overlap >= 0.80, spurious <= 0.30, no extra darkening over the caster.
# Fixture: verify/shadow/shadow_fixture.json.
add_test(NAME Sly2_JudgeShadowSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_shadow.py --selftest)
set_tests_properties(Sly2_JudgeShadowSelftest PROPERTIES TIMEOUT 60)
# ShadowCounterSplit unit test: class and overlap detection, and the subtract / add pair against the GS
# wrap formula on a synthetic closed volume.
add_executable(sly2_test_shadow_counter_split
    ${CMAKE_CURRENT_SOURCE_DIR}/test_shadow_counter_split.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../hle/shadow_counter_split.cpp)
target_include_directories(sly2_test_shadow_counter_split PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(sly2_test_shadow_counter_split PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sly2_test_shadow_counter_split PRIVATE ps2r_gs_ir)
add_test(NAME Sly2_ShadowCounterSplit COMMAND sly2_test_shadow_counter_split)
set_tests_properties(Sly2_ShadowCounterSplit PROPERTIES TIMEOUT 60)

# --- the CEL-SHADING judge (verify/judge_cel.py) ---------------------------------------------------
# The outline ink (one GS LINE pair per visible silhouette edge, then a Z pass) and two-palette shading,
# against PCSX2's per-draw dump and frame of the same scene: ink contrast, visibility and thickness,
# per-material luminance, and byte-identical character textures. Fixture: verify/cel/cel_fixture.json.
add_test(NAME Sly2_JudgeCelSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_cel.py --selftest)
set_tests_properties(Sly2_JudgeCelSelftest PROPERTIES TIMEOUT 120)

# --- the SOLIDITY gate (verify/judge_solidity.py) ---------------------------------------------------
# One presented frame against its PCSX2 twin inside the fixture's masks (verify/solidity/fixtures.json):
# lum_wall / lum_sly_w1 <= 6, see_through <= 0.1 (no see-through geometry).
add_test(NAME Sly2_JudgeSoliditySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_solidity.py --selftest)
set_tests_properties(Sly2_JudgeSoliditySelftest PROPERTIES TIMEOUT 120)

# --- the CHARGED-CANE EFFECT gate (verify/judge_cane.py) --------------------------------------------
# The cane's particle effect is present: median orange / bright pixel counts inside the cane-head box over
# the Paris SQUARE-hold frames are >= 100 each (verify/cane/fixtures.json).
add_test(NAME Sly2_JudgeCaneSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/judge_cane.py --selftest)
set_tests_properties(Sly2_JudgeCaneSelftest PROPERTIES TIMEOUT 60)

# --- the METAL IDENTITY judge (verify/judge_metal_identity.py) -------------------------------------
# Metal's picture must lie within the band GL's two blend paths span (fixed-function vs shader, +-2/255):
# <= 0.3 % of pixels outside and max excess <= 8, on every 50th frame of the Paris / Castle / Cairo captures
# (verify/metal/<capture>.gsstream under $NATIVEEE_PRIVATE_FIXTURES). Needs a Metal device (macOS).
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

# scene_matrix.py: one play run per scene x video backend (every episode's hub, a ledge, a mission, a
# cutscene, the cold boot) read into a table: GREEN = pictures/s p50 >= 59 and <= 1 % doubled frames, DEFECT =
# parked / crashed / timed out. `plan` lists scenes, `cell` reads one run, `table` writes the matrix, `soak`
# reads a long run in 10 s windows.
add_test(NAME Sly2_SceneMatrixSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scene_matrix.py --selftest)
set_tests_properties(Sly2_SceneMatrixSelftest PROPERTIES TIMEOUT 60)
