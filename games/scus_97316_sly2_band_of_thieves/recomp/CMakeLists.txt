# games/scus_97316_sly2_band_of_thieves/recomp -- the recompiled EE code, GENERATED at build time.
#
# Nothing game-derived is committed here. cmake/TitleInputs.cmake resolves the user's ELF
# (NATIVEEE_DISC -> ps2r_disc_extract, SHA-256 gated against ../TITLE_INPUTS.sha256; or
# NATIVEEE_TITLE_ELF, hashed at configure) and names ${PS2R_RECOMP_GEN_DIR}; this file runs the
# in-tree recompiler over that ELF into it and defines ps2r_title_recomp over the output.
# Without an ELF the directory returns early (STATUS) and every consumer skips on
# if(TARGET ps2r_title_recomp).
#
# Shape adapted from the first title's recomp/CMakeLists.txt (which compiled frozen, committed
# output; here the generation step is added in front): shards as one OBJECT library at -O2, the
# common TU at -O2 as well since the eecode round (it holds the per-call dispatch path --
# recomp_raw_dispatch, EeCallFrameGuard, ee_call_stack_pop, find_containing_function -- which at
# the first title's -O0 showed as separate leaves worth ~0.5 ms/frame of the EE thread; ~2 min of
# compile on an M4 under load), the semantic flags -fno-strict-aliasing -fwrapv -ffp-contract=off
# (-w: the generated TUs are not our code).
if(NOT PS2R_TITLE_RECOMP_AVAILABLE)
    message(STATUS "games/scus_97316_sly2_band_of_thieves: no title ELF -- ps2r_title_recomp not defined "
                   "(give -DNATIVEEE_DISC=<iso> or -DNATIVEEE_TITLE_ELF=<SCUS_973.16>)")
    return()
endif()
if(NOT TARGET ps2r_recompiler_v2)
    message(FATAL_ERROR "ps2r_title_recomp: the in-tree recompiler (ps2r_recompiler_v2) is not built; set PS2R_BUILD_RECOMPILER=ON")
endif()

set(PS2R_SLY2_RECOMP_SHARDS 32 CACHE STRING "ps2r_recompiler_v2 --shards / --init-parts for this title (TitleProfile::recomp.shard_count)")
set(_gen "${PS2R_RECOMP_GEN_DIR}")
set(_cwd "${CMAKE_CURRENT_BINARY_DIR}/tool_cwd")   # the tool writes logs/ and looks for stub files in its cwd
file(MAKE_DIRECTORY "${_gen}" "${_cwd}")

# The output file list (the tool's naming: recompiled.h, recompiled.cpp, recompiled_shard_NNN.cpp;
# continuations.py adds continuations.h which recompiled.cpp includes).
set(_shards "")
math(EXPR _last "${PS2R_SLY2_RECOMP_SHARDS} - 1")
foreach(i RANGE 0 ${_last})
    string(REGEX REPLACE "^(.)$" "00\\1" _n "${i}")
    string(REGEX REPLACE "^(..)$" "0\\1" _n "${_n}")
    list(APPEND _shards "${_gen}/recompiled_shard_${_n}.cpp")
endforeach()
set(_generated "${_gen}/recompiled.h" "${_gen}/recompiled.cpp" "${_gen}/continuations.h" ${_shards})

# recomp/inputs/ holds the OPTIONAL stub lists the tool reads from its cwd (tiny_stubs.txt /
# tiny_returners.txt: addresses the discovery misses). Sly 2 has none yet; an absent file is
# "Could not find stub file" (0 auto-registered), not an error. They are copied into the cwd
# when present so a future list takes effect without editing this file.
set(_stub_inputs "")
foreach(_name tiny_stubs.txt tiny_returners.txt)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/inputs/${_name}")
        list(APPEND _stub_inputs "${CMAKE_CURRENT_SOURCE_DIR}/inputs/${_name}")
    endif()
endforeach()
set(_copy_stubs "")
if(_stub_inputs)
    set(_copy_stubs COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_stub_inputs} "${_cwd}")
endif()

# --no-legacy-title-stubs: the tool's hardcoded first-title stub blocks are another game's
# addresses (one collides with a real function start of this ELF); the pure discovery set is
# emitted. Invocation otherwise the first title's: pure discovery (no --ghidra-input /
# --overrides / --manual-json), --direct-calls.
# --yield-backedges: every loop back-edge (2,729 sites) runs the engine's yield check
# `if (--g_ee_backedge_budget == 0) ps2r_ee_yield_point(ctx);` -- the composition binds
# EeYieldPoint so a guest loop that reaches no kernel call still sees time pass, interrupts land
# and a higher-priority thread run (thbase). Off (the first title's regeneration) the output is
# byte-identical to the pre-flag tool.
add_custom_command(
    OUTPUT ${_generated}
    ${_copy_stubs}
    # The target NAME (not $<TARGET_FILE:>): a cross build (engine/platform/windows/
    # toolchain_cross_macos.cmake) then runs the host twin through CMAKE_CROSSCOMPILING_EMULATOR;
    # WORKING_DIRECTORY below is the cwd the tool wants.
    COMMAND ps2r_recompiler_v2
            --input "${PS2R_TITLE_ELF}"
            --output-header "${_gen}/recompiled.h" --output-cpp "${_gen}/recompiled.cpp"
            --shards ${PS2R_SLY2_RECOMP_SHARDS} --init-parts ${PS2R_SLY2_RECOMP_SHARDS}
            --direct-calls --no-legacy-title-stubs --yield-backedges
    # The continuation post-pass: the C++ tool (the port of continuations.py the prepared-game
    # pipeline ships; the engine bundle has no Python). Same output byte for byte -- this manifest
    # pins it -- and the ContinuationsToolMatchesPython test below diffs the two directly.
    COMMAND ps2r_continuations "${_gen}" --audits "${CMAKE_SOURCE_DIR}/tools/recompiler_v2/continuation_audits.json"
    DEPENDS ps2r_recompiler_v2 ps2r_continuations "${PS2R_TITLE_ELF_READY}" ${_stub_inputs}
            "${CMAKE_SOURCE_DIR}/tools/recompiler_v2/continuation_audits.json"
    WORKING_DIRECTORY "${_cwd}"
    COMMENT "Recompiling ${PS2R_TITLE_ELF} -> ${_gen} (ps2r_recompiler_v2, ${PS2R_SLY2_RECOMP_SHARDS} shards)"
    VERBATIM)
set_source_files_properties(${_generated} PROPERTIES GENERATED TRUE)
add_custom_target(ps2r_title_recomp_generate DEPENDS ${_generated})
# The Python reference, run with --check over the tool's output: the two post-passes agree on
# this title's generation (the manifest gate says the same; this names the culprit directly).
add_test(NAME ContinuationsToolMatchesPython
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/recompiler_v2/continuations.py" "${_gen}" --check)
set_tests_properties(ContinuationsToolMatchesPython PROPERTIES TIMEOUT 120 LABELS "recomp")

# MANIFEST.sha256 (hashes of the GENERATED files, not game data): when present and
# PS2R_RECOMP_CHECK_MANIFEST is ON, the regeneration must reproduce it byte for byte
# (cmake/ps2r_verify_manifest.cmake; STRICT removes a differing output). A title records it
# from its first successful generation:  (cd ${_gen} && shasum -a 256 recompiled.h recompiled.cpp
# continuations.h recompiled_shard_*.cpp) > recomp/MANIFEST.sha256
set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.sha256")
set(_verify_stamp "")
if(EXISTS "${_manifest}")
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/manifest.verified")
    if(PS2R_RECOMP_CHECK_MANIFEST)
        set(_strict ON)
    else()
        set(_strict OFF)
    endif()
    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND ${CMAKE_COMMAND} "-DLABEL=ps2r_title_recomp" "-DMANIFEST=${_manifest}" "-DGEN_DIR=${_gen}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DSTRICT=${_strict}" "-DSTAMP=${_stamp}"
                -P "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        DEPENDS ${_generated} "${_manifest}" "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        COMMENT "Verifying the recompiled output against recomp/MANIFEST.sha256 (strict=${_strict})"
        VERBATIM)
    set(_verify_stamp "${_stamp}")
    message(STATUS "games/scus_97316_sly2_band_of_thieves: recomp/MANIFEST.sha256 present (check ${_strict})")
else()
    message(STATUS "games/scus_97316_sly2_band_of_thieves: no recomp/MANIFEST.sha256 yet -- first generation; record one from ${_gen}")
endif()

# --- compiler flags (the first title's mapping, GNU front ends only on this host) ----------------
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(PS2R_RECOMP_SEMANTIC_FLAGS /w /clang:-fno-strict-aliasing /clang:-fwrapv /clang:-ffp-contract=off /bigobj)
    set(PS2R_RECOMP_SHARD_OPT /O2)
    set(PS2R_RECOMP_COMMON_OPT /O2)
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(WARNING "ps2r_title_recomp: MSVC has no -fwrapv equivalent; use the clang-cl presets for a release build")
    set(PS2R_RECOMP_SEMANTIC_FLAGS /w /fp:precise /bigobj)
    set(PS2R_RECOMP_SHARD_OPT /O2)
    set(PS2R_RECOMP_COMMON_OPT /O2)
else()
    set(PS2R_RECOMP_SEMANTIC_FLAGS -w -fno-strict-aliasing -fwrapv -ffp-contract=off)
    set(PS2R_RECOMP_SHARD_OPT -O2)
    set(PS2R_RECOMP_COMMON_OPT -O2)
endif()

# The body shards: one OBJECT library (shard-only options target-wide). The generated header is
# precompiled with the ABI prologue (engine/ee/recomp_abi/anchor/recomp_pch.h) unless PS2R_RECOMP_PCH=OFF.
option(PS2R_RECOMP_PCH "Precompile the generated shard TUs' include prologue" ON)
add_library(ps2r_title_recomp_shards OBJECT ${_shards})
# The manifest stamp (when any) gates compilation of every TU, so an unverified regeneration is
# never compiled: a header-only generated source every object depends on.
if(_verify_stamp)
    target_sources(ps2r_title_recomp_shards PRIVATE "${_verify_stamp}")
    set_source_files_properties("${_verify_stamp}" PROPERTIES HEADER_FILE_ONLY TRUE GENERATED TRUE)
    set_source_files_properties(${_shards} PROPERTIES OBJECT_DEPENDS "${_verify_stamp}")
    set_source_files_properties("${_gen}/recompiled.cpp" PROPERTIES OBJECT_DEPENDS "${_verify_stamp}")
endif()
target_include_directories(ps2r_title_recomp_shards PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/ee/recomp_abi/anchor   # "../host_app/x.h" -> engine/ee/recomp_abi/host_app/x.h
    ${CMAKE_SOURCE_DIR}
    ${_gen})
target_compile_options(ps2r_title_recomp_shards PRIVATE ${PS2R_RECOMP_SEMANTIC_FLAGS} ${PS2R_RECOMP_SHARD_OPT})
target_link_libraries(ps2r_title_recomp_shards PUBLIC ps2r_ee)
add_dependencies(ps2r_title_recomp_shards ps2r_title_recomp_generate)
if(PS2R_RECOMP_PCH)
    target_precompile_headers(ps2r_title_recomp_shards PRIVATE
        "${_gen}/recompiled.h"
        ${CMAKE_SOURCE_DIR}/engine/ee/recomp_abi/anchor/recomp_pch.h)
endif()

# The common TU (the dispatch table, init parts, continuations) at -O2, plus the shard objects.
# No glue TUs: the recon stage verified that none of the first title's address-keyed emitters
# fire for this ELF (no hle_*/HLE_* reference in the generated code).
add_library(ps2r_title_recomp STATIC
    "${_gen}/recompiled.cpp"
    $<TARGET_OBJECTS:ps2r_title_recomp_shards>)
target_include_directories(ps2r_title_recomp PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/ee/recomp_abi/anchor
    ${CMAKE_SOURCE_DIR}
    ${_gen})
target_include_directories(ps2r_title_recomp INTERFACE ${_gen})   # consumers may include recompiled.h
target_compile_options(ps2r_title_recomp PRIVATE ${PS2R_RECOMP_SEMANTIC_FLAGS})
set_source_files_properties("${_gen}/recompiled.cpp" PROPERTIES COMPILE_OPTIONS "${PS2R_RECOMP_COMMON_OPT}")
# The generated TUs' externs: DispatchTable / seams / ShadowCallStack (ps2r_ee), g_vsync_frame and the
# sceGsSyncV bodies (ps2r_gs_sync), the VU0 macro bridge objects (ps2r_vu).
target_link_libraries(ps2r_title_recomp PUBLIC ps2r_ee ps2r_gs_sync ps2r_vu)
add_dependencies(ps2r_title_recomp ps2r_title_recomp_generate)
