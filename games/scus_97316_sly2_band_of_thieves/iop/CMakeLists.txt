# games/scus_97316_sly2_band_of_thieves/iop -- the title's IOP modules, RECOMPILED at build time.
#
# Sly 2 embeds its IOP modules in the ELF (TITLE_INPUTS.sha256 `SCUS_973.16!<name>` rows;
# cmake/TitleInputs.cmake extracts them SHA-256 gated into ${PS2R_TITLE_INPUTS_DIR}). Three have
# no native engine owner and are hosted on the engine's IOP kernel as native code:
#
#   id       .iopmod name     base      exports  what it is
#   fileio   FILEIO_service   0x20000   (none)   the IOPRP300 reboot image's stock fileio (RPC 0x80000001 the EE's libfileio calls)
#   lgaud    lgAud            0x28000   lgaud    Logitech USB headset audio driver (idle USB bus: no device)
#   snd989   989snd_Library   0x33000   snd989   SCEA's 989snd sound driver -- the RPC servers 0x123456/0x123457 the EE calls
#   spiop    spiop            0x5a000   (none)   Sucker Punch's streaming module (hangs into 989snd's/lgaud's exports)
#
# Each is recompiled by the in-tree ps2r_iop_recompiler (--base <B> --namespace iop_<id>
# --import-call: relocated to its load address, wrapped in its own namespace, every library
# import routed through the engine's IopLibraryTable) into ${PS2R_IOP_GEN_ROOT}/<id>/ and
# compiled as ps2r_title_iop_<id> over the engine's generic ABI (engine/iop/abi). The bases
# are title layout data (iop_modules.h): images end below the sysmem heap floor (0x60000).
# MANIFEST.sha256 per module pins the generated output (hashes only, no game bytes).
#
# Nothing game-derived is committed here; the generated TUs live in the build directory.
if(NOT PS2R_TITLE_EMBEDDED_AVAILABLE)
    message(STATUS "games/scus_97316_sly2_band_of_thieves/iop: no extracted embedded modules -- ps2r_title_iop_* not defined")
    return()
endif()
if(NOT TARGET ps2r_iop_recompiler)
    message(FATAL_ERROR "ps2r_title_iop: the in-tree IOP recompiler (ps2r_iop_recompiler) is not built; set PS2R_BUILD_RECOMPILER=ON")
endif()

# ps2r_sly2_iop_module(<id> <embedded module name> <base>)
#   defines ps2r_title_iop_<id> over the recompiler's output for the extracted image.
function(ps2r_sly2_iop_module id module base)
    set(_irx "${PS2R_TITLE_EMBEDDED_${module}}")
    set(_ready "${PS2R_TITLE_EMBEDDED_${module}_READY}")
    if(_irx STREQUAL "")
        message(FATAL_ERROR "ps2r_title_iop_${id}: TITLE_INPUTS.sha256 has no row for SCUS_973.16!${module}")
    endif()
    set(_gen "${PS2R_IOP_GEN_ROOT}/${id}")
    file(MAKE_DIRECTORY "${_gen}")
    set(_generated "${_gen}/iop_recompiled.h" "${_gen}/iop_recompiled.cpp")
    add_custom_command(
        OUTPUT ${_generated}
        COMMAND $<TARGET_FILE:ps2r_iop_recompiler>
                --input "${_irx}" --base ${base} --namespace iop_${id} --import-call --quiet
                --output-header "${_gen}/iop_recompiled.h" --output-cpp "${_gen}/iop_recompiled.cpp"
        DEPENDS ps2r_iop_recompiler "${_ready}"
        COMMENT "Recompiling embedded IOP module ${module} -> ${_gen} (ps2r_iop_recompiler --base ${base} --namespace iop_${id})"
        VERBATIM)
    set_source_files_properties(${_generated} PROPERTIES GENERATED TRUE)
    add_custom_target(ps2r_title_iop_${id}_generate DEPENDS ${_generated})

    # MANIFEST.sha256 (hashes of the GENERATED files): the regeneration must reproduce it
    # byte for byte when present and PS2R_RECOMP_CHECK_MANIFEST is ON. Record from the first
    # successful generation:  (cd ${_gen} && shasum -a 256 iop_recompiled.h iop_recompiled.cpp) > iop/<id>/MANIFEST.sha256
    set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.sha256")   # the function runs from iop/<id>/
    set(_verify_stamp "")
    if(EXISTS "${_manifest}")
        set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${id}_manifest.verified")
        if(PS2R_RECOMP_CHECK_MANIFEST)
            set(_strict ON)
        else()
            set(_strict OFF)
        endif()
        add_custom_command(
            OUTPUT "${_stamp}"
            COMMAND ${CMAKE_COMMAND} "-DLABEL=ps2r_title_iop_${id}" "-DMANIFEST=${_manifest}" "-DGEN_DIR=${_gen}"
                    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DSTRICT=${_strict}" "-DSTAMP=${_stamp}"
                    -P "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
            DEPENDS ${_generated} "${_manifest}" "${CMAKE_SOURCE_DIR}/cmake/ps2r_verify_manifest.cmake"
            COMMENT "Verifying the recompiled ${module} against iop/${id}/MANIFEST.sha256 (strict=${_strict})"
            VERBATIM)
        set(_verify_stamp "${_stamp}")
    else()
        message(STATUS "games/scus_97316_sly2_band_of_thieves/iop/${id}: no MANIFEST.sha256 yet -- first generation; record one from ${_gen}")
    endif()

    add_library(ps2r_title_iop_${id} STATIC "${_gen}/iop_recompiled.cpp")
    if(_verify_stamp)
        target_sources(ps2r_title_iop_${id} PRIVATE "${_verify_stamp}")
        set_source_files_properties("${_verify_stamp}" PROPERTIES HEADER_FILE_ONLY TRUE GENERATED TRUE)
        set_source_files_properties("${_gen}/iop_recompiled.cpp" PROPERTIES OBJECT_DEPENDS "${_verify_stamp}")
    endif()
    target_include_directories(ps2r_title_iop_${id} PUBLIC
        ${CMAKE_SOURCE_DIR}/engine/iop/abi/anchor   # "../host_app/x.h" -> engine/iop/abi/host_app/x.h
        ${CMAKE_SOURCE_DIR}
        ${_gen})
    # The generated TU compiles like the first title's: -O0, no warnings, semantic flags only.
    set_source_files_properties("${_gen}/iop_recompiled.cpp" PROPERTIES COMPILE_OPTIONS "-w;-fno-strict-aliasing;-fwrapv;-O0")
    target_link_libraries(ps2r_title_iop_${id} PUBLIC ps2r_iop)
    add_dependencies(ps2r_title_iop_${id} ps2r_title_iop_${id}_generate)
    message(STATUS "games/scus_97316_sly2_band_of_thieves/iop/${id}: ${module} at base ${base} -> ps2r_title_iop_${id}")
endfunction()

add_subdirectory(fileio)
add_subdirectory(lgaud)
add_subdirectory(snd989)
add_subdirectory(spiop)

# The aggregate the play composition links: every hosted module + the IOP kernel + the SIF models.
add_library(ps2r_title_iop INTERFACE)
target_link_libraries(ps2r_title_iop INTERFACE ps2r_title_iop_fileio ps2r_title_iop_lgaud ps2r_title_iop_snd989
                      ps2r_title_iop_spiop ps2r_iop ps2r_sif_iop)
