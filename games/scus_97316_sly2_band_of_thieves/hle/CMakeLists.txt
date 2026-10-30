# Sly 2's title module: its TitleProfile and its play composition. This file's presence marks the
# directory as a title.
#
# ps2r_title_hle   ActiveTitleProfile() (title_profile.cpp).
# ps2r_title_play  the play composition (boot_leg.cpp and the leg_*.cpp phases), defined only when the
#                  recompiled code exists. Without an ELF, nativeee reports that it has no play composition.
#
# This composition links the IOP directly (ps2r_title_iop) and never includes app/host/iop_arm.h, so
# PS2R_IOP_ORACLE stays 0.
add_library(ps2r_title_hle STATIC
    title_profile.cpp
)
target_include_directories(ps2r_title_hle PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_title_hle PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_title_hle PUBLIC ps2r_title_api)   # the TitleProfile contract

if(TARGET ps2r_title_recomp AND TARGET ps2r_title_iop)
    add_library(ps2r_title_play STATIC
        audio_dump.cpp
        boot_leg.cpp
        clock_tick.cpp
        frame_dump.cpp
        frame_limit.cpp
        gif_trace_sink.cpp
        leg_audio.cpp
        leg_census.cpp
        leg_core.cpp
        leg_devices.cpp
        leg_disc.cpp
        leg_dma_ipu.cpp
        leg_gs.cpp
        leg_image.cpp
        leg_interrupts.cpp
        leg_iop.cpp
        frame_timeline.cpp
        leg_knobs.cpp
        leg_mtvu.cpp
        leg_pad.cpp
        leg_park.cpp
        leg_park_sink.cpp
        leg_play.cpp
        leg_saves.cpp
        leg_services.cpp
        leg_text.cpp
        leg_verify_slot.cpp
        leg_vif_irq.cpp
        leg_watchdog.cpp
        motion_blur.cpp
        nav_json.cpp
        nav_probe.cpp
        pad_recorder.cpp
        pad_script.cpp
        shadow_counter_split.cpp
        store_watch.cpp
        text_watch.cpp
        vert_trace_sink.cpp
        video_stack.cpp
        vu1_probe.cpp
        warp_tool.cpp
        widescreen.cpp
    )
    target_include_directories(ps2r_title_play PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/.. ${PS2R_IOP_GEN_ROOT})
    target_compile_options(ps2r_title_play PRIVATE -Wall -Wextra -Werror)
    # The title id, the title directory (for hle/sdk_bindings.tsv), the build's extracted ELF, and the
    # embedded modules' expected SHA-256 (the IOP host refuses any other image).
    target_compile_definitions(ps2r_title_play PRIVATE
        PS2R_TITLE_ID="${PS2R_TITLE}"
        PS2R_TITLE_DIR="${PS2R_GAMES_DIR}/${PS2R_TITLE}"
        PS2R_TITLE_ELF_PATH="${PS2R_TITLE_ELF}"
        PS2R_SLY2_SHA_FILEIO="${PS2R_TITLE_EMBEDDED_FILEIO_service_SHA256}"
        PS2R_SLY2_SHA_LGAUD="${PS2R_TITLE_EMBEDDED_lgAud_SHA256}"
        PS2R_SLY2_SHA_SND989="${PS2R_TITLE_EMBEDDED_989snd_Library_SHA256}"
        PS2R_SLY2_SHA_SPIOP="${PS2R_TITLE_EMBEDDED_spiop_SHA256}")
    target_link_libraries(ps2r_title_play PUBLIC
        ps2r_app_host ps2r_title_hle ps2r_title_recomp ps2r_sif ps2r_gs_sync ps2r_title_iop
        ps2r_gs_state ps2r_gif ps2r_gif_hle ps2r_vu ps2r_vif ps2r_dmac ps2r_ipu_model ps2r_gs_hle_capture)
    # The generated dispatch table is only reached through DispatchTable, so force the whole archive in.
    add_dependencies(ps2r_title_play ps2r_title_recomp)

    # iop_host.cpp is the only composition TU that includes the generated iop_recompiled.h headers.
    add_library(ps2r_title_iop_host STATIC iop_host.cpp)
    target_include_directories(ps2r_title_iop_host PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/.. ${PS2R_IOP_GEN_ROOT})
    target_compile_options(ps2r_title_iop_host PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(ps2r_title_iop_host PUBLIC ps2r_app_host ps2r_title_hle ps2r_title_iop ps2r_sif)
    target_link_libraries(ps2r_title_play PUBLIC ps2r_title_iop_host)
    set_property(GLOBAL APPEND PROPERTY PS2R_ON_MACHINE_TARGETS ps2r_title_iop_host)
    # The game binary's name for the engine's bundle tooling.
    set_property(GLOBAL PROPERTY PS2R_TITLE_BINARY_NAME sly2)
elseif(TARGET ps2r_title_recomp)
    message(STATUS "games/scus_97316_sly2_band_of_thieves: recompiled EE code but no hosted IOP modules -> no play composition")
else()
    message(STATUS "games/scus_97316_sly2_band_of_thieves: no recompiled code -> no play composition (ps2r_title_play not defined)")
endif()
