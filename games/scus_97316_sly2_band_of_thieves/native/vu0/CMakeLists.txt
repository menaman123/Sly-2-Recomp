# games/scus_97316_sly2_band_of_thieves/native/vu0 -- Sly 2's VU0 microprograms as host code (track gs-vu0).
#
# vu0_native_gen (built here, links the engine's ps2r_vu for the ONE decoder + the stall / flag
# pipelines it folds) reads the title ELF's VU0 program image -- the bytes the game uploads to VU0
# micro memory: vaddr 0x259e00 (.vutext + 0x10, after the VIF header), 0x2a0 bytes = the six
# VCALLMS entries 0x10 / 0x60 / 0xf0 / 0x120 / 0x150 / 0x1c0 -- and emits one body per straight-line
# program into ${PS2R_TITLE_GEN_ROOT}/games/<title>/native/vu0/vu0_programs_gen.cpp (game-derived:
# never committed; MANIFEST.sha256 here pins it the way recomp/ pins the shards). native_vu0.cpp is
# the installer + the VuTierDispatch::SetVu0Hook body behind title.native.vu0.
set(_gen "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/native/vu0")
file(MAKE_DIRECTORY "${_gen}")
set(PS2R_SLY2_VU0_IMAGE_VADDR 259e00 CACHE STRING "VU0 program image vaddr in the ELF (hex)")
set(PS2R_SLY2_VU0_IMAGE_SIZE 2a0 CACHE STRING "VU0 program image size (hex)")
set(PS2R_SLY2_VU0_ENTRIES "10,60,f0,120,150,1c0" CACHE STRING "VCALLMS entries (hex, comma-separated)")

add_executable(ps2r_sly2_vu0_native_gen vu0_native_gen.cpp)
target_include_directories(ps2r_sly2_vu0_native_gen PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(ps2r_sly2_vu0_native_gen PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sly2_vu0_native_gen PRIVATE ps2r_vu ps2r_gs_ir)

set(_generated "${_gen}/vu0_programs_gen.cpp")
add_custom_command(
    OUTPUT "${_generated}"
    COMMAND ps2r_sly2_vu0_native_gen --elf "${PS2R_TITLE_ELF}" --image-vaddr ${PS2R_SLY2_VU0_IMAGE_VADDR}
            --image-size ${PS2R_SLY2_VU0_IMAGE_SIZE} --entry "${PS2R_SLY2_VU0_ENTRIES}"
            --out "${_generated}" --report "${_gen}/report.txt"
    DEPENDS ps2r_sly2_vu0_native_gen "${PS2R_TITLE_ELF_READY}"
    COMMENT "Generating Sly 2's VU0 microprogram bodies (vu0_native_gen) -> ${_generated}"
    VERBATIM)
set_source_files_properties("${_generated}" PROPERTIES GENERATED TRUE)

# The manifest check (strict unless PS2R_RECOMP_CHECK_MANIFEST is off), as recomp/ does it.
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
        COMMAND ${CMAKE_COMMAND} "-DLABEL=ps2r_title_native_vu0" "-DMANIFEST=${_manifest}" "-DGEN_DIR=${_gen}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DSTRICT=${_strict}" "-DSTAMP=${_stamp}"
                -P "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        DEPENDS "${_generated}" "${_manifest}" "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        COMMENT "Verifying the VU0 native bodies against native/vu0/MANIFEST.sha256 (strict=${_strict})"
        VERBATIM)
    set(_verify_stamp "${_stamp}")
    message(STATUS "games/${PS2R_TITLE}/native/vu0: MANIFEST.sha256 present (check ${_strict})")
else()
    message(STATUS "games/${PS2R_TITLE}/native/vu0: no MANIFEST.sha256 yet -- first generation; record one from ${_gen}")
endif()

# Its own library (a custom command's OUTPUT is consumed by a target of the same directory), linked
# into ps2r_title_native.
add_library(ps2r_title_native_vu0 STATIC native_vu0.cpp "${_generated}")
if(_verify_stamp)
    target_sources(ps2r_title_native_vu0 PRIVATE "${_verify_stamp}")
    set_source_files_properties("${_verify_stamp}" PROPERTIES HEADER_FILE_ONLY TRUE GENERATED TRUE)
    set_source_files_properties("${_generated}" PROPERTIES OBJECT_DEPENDS "${_verify_stamp}")
endif()
target_include_directories(ps2r_title_native_vu0 PUBLIC ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(ps2r_title_native_vu0 PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(PS2R_SIMD)
    target_compile_definitions(ps2r_title_native_vu0 PRIVATE PS2R_SIMD=1)
endif()
target_link_libraries(ps2r_title_native_vu0 PUBLIC ps2r_title_native ps2r_vu ps2r_gs_ir)
target_link_libraries(ps2r_title_native PUBLIC ps2r_title_native_vu0)

# The oracle: the generated bodies vs the cached tier and the interpreter on random register files.
add_executable(test_sly2_vu0_native test_sly2_vu0_native.cpp)
target_include_directories(test_sly2_vu0_native PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(test_sly2_vu0_native PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
target_link_libraries(test_sly2_vu0_native PRIVATE ps2r_title_native_vu0 ps2r_vu ps2r_gs_ir)
add_test(NAME Sly2Vu0Native COMMAND test_sly2_vu0_native "${PS2R_TITLE_ELF}" 2000)
set_tests_properties(Sly2Vu0Native PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77 LABELS "native")
