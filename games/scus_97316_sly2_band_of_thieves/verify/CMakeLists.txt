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
