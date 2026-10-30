# Sly 2's NATIVE module (STAGE NATIVE): host code replacing the engine's per-frame MODELS of the
# PS2 units for THIS title's specific use of them (README.md states the contract). Added by
# hle/CMakeLists.txt (add_subdirectory ../native) and linked into ps2r_title_play; it exists only
# when the play composition exists (the recompiled code is present).
#
# ps2r_title_native   native_knobs (the title.native.* switches read from Config), native_census
#                     (the per-unit served/fallen-back counters -> run.meta `native=`), and the
#                     per-unit native paths the tracks add here (native_vu1.cpp, native_gif.cpp, ...).
#
# Portability (the Windows rule): every TU in this directory is portable C++17 -- no POSIX, no
# _WIN32 / __APPLE__; host services only through engine/platform; SIMD only through
# native_simd.h (NEON / SSE2 behind a portable type with a scalar fallback). The engine's
# tools/ci/win_cross_smoke.sh compiles the TUs listed below for x86_64-pc-windows-msvc.
add_library(ps2r_title_native STATIC
    native_census.cpp
    native_knobs.cpp
    native_simd.cpp
    # vif + dmac (track vif-dmac): the VIF1 chain executor on ChainWalker::SetVifChainExecutor
    vif/native_vif.cpp
    vif/vif_census.cpp
    vif/vif_chain_tags.cpp
    vif/vif_native_chains.cpp
    vif/vif_unpack_native.cpp
)
target_include_directories(ps2r_title_native PUBLIC ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
# -ffp-contract=off: the float contract spelled for every compiler (the engine's rule for GNU; Clang's
# default fuses only within one expression -- the native paths' arithmetic must round like the oracle's).
target_compile_options(ps2r_title_native PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(PS2R_SIMD)
    target_compile_definitions(ps2r_title_native PRIVATE PS2R_SIMD=1)
endif()
# ps2r_core: Config (title.native.*); ps2r_host: engine/platform (the only host surface allowed here).
target_link_libraries(ps2r_title_native PUBLIC ps2r_core ps2r_host)
# The seams the native paths plug into: the DMAC chain walker, the VIF unit, the VU units.
target_link_libraries(ps2r_title_native PUBLIC ps2r_dmac ps2r_vif ps2r_vu ps2r_ee)

# The vif-dmac equivalence test: synthesized chains (the title's grammar) through the engine's walk and
# through the native executor -> identical VU1 data memory, VIF registers, channel registers, i-bit stalls.
add_executable(sly2_test_native_vif vif/test_native_vif.cpp)
target_include_directories(sly2_test_native_vif PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(sly2_test_native_vif PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sly2_test_native_vif PRIVATE ps2r_title_native ps2r_gs_ir)
add_test(NAME Sly2_NativeVifChains COMMAND sly2_test_native_vif)
set_tests_properties(Sly2_NativeVifChains PROPERTIES TIMEOUT 120)
