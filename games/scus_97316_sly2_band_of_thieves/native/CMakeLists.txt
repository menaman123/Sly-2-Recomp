# Sly 2's NATIVE module (STAGE NATIVE): host code replacing the engine's per-frame MODELS of the
# PS2 units for THIS title's specific use of them (README.md states the contract). Added by
# hle/CMakeLists.txt (add_subdirectory ../native) unconditionally -- the portable core below builds
# in a no-disc tree too (CI, the Windows lane) -- and linked into ps2r_title_play when that exists.
# A per-unit TU that needs the recompiled code or build-time generated data (a per-image VU1 body
# table, a packet template table) goes in the if(TARGET ps2r_title_recomp) block at the end.
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

# The per-unit native paths that need the title's recompiled code / build-time generated data
# (never committed: generated like the recomp shards, MANIFEST-pinned). Tracks add theirs here.
if(TARGET ps2r_title_recomp)
    # vu1/ -> ps2r_title_native_vu1: the title's VU1 microprograms generated at build time from the kick
    # corpus (native/vu1/CMakeLists.txt; linked into ps2r_title_play by hle/CMakeLists.txt).
    add_subdirectory(vu1)
endif()
