# games/scus_97316_sly2_band_of_thieves/native/vu1 -- Sly 2's VU1 microprograms as NATIVE host code,
# generated at BUILD time (STAGE NATIVE, title.native.vu1). native_vu1.h states the design.
#
# Inputs
#   NATIVEEE_VU1_CORPUS   ;-separated directories of Vu1Case records (the engine's kick capture format:
#                         each carries the 16 KB micro memory = the microprogram image = GAME DATA, kept
#                         outside the repository, e.g. ~/recomp-agent-data/scus_97316/corpora/vu1/*).
#                         Cache variable; defaults to the environment variable of the same name. Without
#                         it: the table is empty (vu1_native_images_empty.cpp), the knob is inert, STATUS.
#   images.txt            the AUTHORITATIVE image list (hashes + facts, no bytes): an image of the corpus
#                         not listed is skipped; a listed image with no case fails the generation.
#   MANIFEST.sha256       pins the generated TUs (the recomp shards' rule; PS2R_RECOMP_CHECK_MANIFEST strict).
#                         Recorded for the default policy (chain=1); another policy skips the check.
# Tool    vu1_native_gen (this dir; links ps2r_vu) -> ${PS2R_TITLE_GEN_ROOT}/games/<title>/native/vu1/
#         {vu1_<hash>.cpp, vu1_native_images.cpp, vu1_native_images.txt}
# Target  ps2r_title_native_vu1 (STATIC): native_vu1.cpp (the registry fill) + the generated TUs, compiled
#         with the engine's VU semantic flags (engine/vu/CMakeLists.txt: -O2 -fno-strict-aliasing -fwrapv,
#         GNU adds -ffp-contract=off -frounding-math; the prelude's rule) and -w (generated code).
# Test    Vu1NativeCorpus: test_vu1_native_corpus over the corpus with --caps (every case + the cap sweep).
#
# Portability: every hand-written TU here is C++17 with no POSIX / _WIN32 / __APPLE__; the generated TUs
# are the engine's own (the prelude's VU1_AOT_MUSTTAIL needs clang or gcc >= 15 when chaining: an MSVC
# front end generates with --chain 0).
set(_vu1_corpus_default "$ENV{NATIVEEE_VU1_CORPUS}")
set(NATIVEEE_VU1_CORPUS "${_vu1_corpus_default}" CACHE STRING
    "VU1 kick corpus directories (Vu1Case records; game data outside the repository) the native VU1 code is generated from")
set(PS2R_SLY2_VU1_NATIVE_CHAIN 1 CACHE STRING "vu1_native_gen --chain (block chaining with guaranteed tail calls; 0 for a compiler without musttail)")
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(PS2R_SLY2_VU1_NATIVE_CHAIN 0)
endif()

set(_gen "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/native/vu1")
file(MAKE_DIRECTORY "${_gen}")

# The generator tool (host): the engine's AOT emitter over the corpus.
add_executable(vu1_native_gen vu1_native_gen.cpp)
target_include_directories(vu1_native_gen PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(vu1_native_gen PRIVATE -Wall -Wextra -Werror)
target_link_libraries(vu1_native_gen PRIVATE ps2r_vu)

# The image list -> the generated file list (one TU per listed hash; the list is the recipe).
set(_images_txt "${CMAKE_CURRENT_SOURCE_DIR}/images.txt")
set(_hashes "")
if(EXISTS "${_images_txt}")
    file(STRINGS "${_images_txt}" _rows REGEX "^[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]")
    foreach(_row IN LISTS _rows)
        string(SUBSTRING "${_row}" 0 16 _h)
        list(APPEND _hashes "${_h}")
    endforeach()
endif()
list(LENGTH _hashes _n_images)

set(_corpus_args "")
foreach(_d IN LISTS NATIVEEE_VU1_CORPUS)
    if(IS_DIRECTORY "${_d}")
        list(APPEND _corpus_args --corpus "${_d}")
    else()
        message(WARNING "games/${PS2R_TITLE}/native/vu1: NATIVEEE_VU1_CORPUS entry is not a directory: ${_d}")
    endif()
endforeach()

if(NOT _corpus_args OR _n_images EQUAL 0)
    message(STATUS "games/${PS2R_TITLE}/native/vu1: no VU1 corpus (NATIVEEE_VU1_CORPUS) or empty images.txt -- "
                   "ps2r_title_native_vu1 carries an EMPTY table; title.native.vu1 falls back to the engine's model")
    add_library(ps2r_title_native_vu1 STATIC native_vu1.cpp vu1_native_images_empty.cpp)
    target_include_directories(ps2r_title_native_vu1 PUBLIC ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
    target_compile_options(ps2r_title_native_vu1 PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(ps2r_title_native_vu1 PUBLIC ps2r_vu ps2r_title_native)
    return()
endif()

set(_generated_tus "")
foreach(_h IN LISTS _hashes)
    list(APPEND _generated_tus "${_gen}/vu1_${_h}.cpp")
endforeach()
set(_generated ${_generated_tus} "${_gen}/vu1_native_images.cpp" "${_gen}/vu1_native_images.txt")
add_custom_command(
    OUTPUT ${_generated}
    COMMAND vu1_native_gen ${_corpus_args} --out "${_gen}" --images "${_images_txt}" --chain ${PS2R_SLY2_VU1_NATIVE_CHAIN}
    DEPENDS vu1_native_gen "${_images_txt}"
    COMMENT "Generating Sly 2's native VU1 microprograms -> ${_gen} (vu1_native_gen, ${_n_images} image(s), chain=${PS2R_SLY2_VU1_NATIVE_CHAIN})"
    VERBATIM)
set_source_files_properties(${_generated} PROPERTIES GENERATED TRUE)
add_custom_target(ps2r_title_native_vu1_generate DEPENDS ${_generated})

# The manifest (cmake/ps2r_verify_manifest.cmake, the recomp rule). To record it:
#   (cd ${_gen} && shasum -a 256 vu1_*.cpp vu1_native_images.cpp vu1_native_images.txt) > native/vu1/MANIFEST.sha256
set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.sha256")
set(_verify_stamp "")
if(EXISTS "${_manifest}" AND PS2R_SLY2_VU1_NATIVE_CHAIN EQUAL 1)
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/manifest.verified")
    if(PS2R_RECOMP_CHECK_MANIFEST)
        set(_strict ON)
    else()
        set(_strict OFF)
    endif()
    add_custom_command(
        OUTPUT "${_stamp}"
        COMMAND ${CMAKE_COMMAND} "-DLABEL=ps2r_title_native_vu1" "-DMANIFEST=${_manifest}" "-DGEN_DIR=${_gen}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DSTRICT=${_strict}" "-DSTAMP=${_stamp}"
                -P "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        DEPENDS ${_generated} "${_manifest}" "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
        COMMENT "Verifying the native VU1 output against native/vu1/MANIFEST.sha256 (strict=${_strict})"
        VERBATIM)
    set(_verify_stamp "${_stamp}")
    message(STATUS "games/${PS2R_TITLE}/native/vu1: MANIFEST.sha256 present (check ${_strict}); ${_n_images} image(s) from ${NATIVEEE_VU1_CORPUS}")
else()
    message(STATUS "games/${PS2R_TITLE}/native/vu1: no MANIFEST.sha256 check (absent or chain=${PS2R_SLY2_VU1_NATIVE_CHAIN}); ${_n_images} image(s) from ${NATIVEEE_VU1_CORPUS}")
endif()

# The engine's VU semantic flags for the generated bodies (engine/vu/CMakeLists.txt + the run-time
# builder's command line: -O2 -fno-strict-aliasing -fwrapv; GNU adds -ffp-contract=off -frounding-math).
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_vu_flags /w /O2 /clang:-fno-strict-aliasing /clang:-fwrapv /bigobj)
else()
    set(_vu_flags -w -O2 -fno-strict-aliasing -fwrapv)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND _vu_flags -ffp-contract=off -frounding-math)
    endif()
endif()

add_library(ps2r_title_native_vu1 STATIC native_vu1.cpp ${_generated_tus} "${_gen}/vu1_native_images.cpp")
if(_verify_stamp)
    target_sources(ps2r_title_native_vu1 PRIVATE "${_verify_stamp}")
    set_source_files_properties("${_verify_stamp}" PROPERTIES HEADER_FILE_ONLY TRUE GENERATED TRUE)
    set_source_files_properties(${_generated_tus} "${_gen}/vu1_native_images.cpp" PROPERTIES OBJECT_DEPENDS "${_verify_stamp}")
endif()
target_include_directories(ps2r_title_native_vu1 PUBLIC ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
set_source_files_properties(native_vu1.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
set_source_files_properties(${_generated_tus} PROPERTIES COMPILE_OPTIONS "${_vu_flags}")
if(PS2R_SIMD)
    target_compile_definitions(ps2r_title_native_vu1 PRIVATE PS2R_SIMD=1)
endif()
target_link_libraries(ps2r_title_native_vu1 PUBLIC ps2r_vu ps2r_title_native)
add_dependencies(ps2r_title_native_vu1 ps2r_title_native_vu1_generate)

# The corpus gate on the LINKED code.
add_executable(test_vu1_native_corpus test_vu1_native_corpus.cpp)
target_include_directories(test_vu1_native_corpus PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(test_vu1_native_corpus PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_vu1_native_corpus PRIVATE ps2r_title_native_vu1 ps2r_vu)
set(_corpus_dirs "")
foreach(_d IN LISTS NATIVEEE_VU1_CORPUS)
    if(IS_DIRECTORY "${_d}")
        list(APPEND _corpus_dirs "${_d}")
    endif()
endforeach()
add_test(NAME Vu1NativeCorpus COMMAND test_vu1_native_corpus ${_corpus_dirs} --caps)
set_tests_properties(Vu1NativeCorpus PROPERTIES TIMEOUT 1800 LABELS "vu;native" SKIP_RETURN_CODE 77)
