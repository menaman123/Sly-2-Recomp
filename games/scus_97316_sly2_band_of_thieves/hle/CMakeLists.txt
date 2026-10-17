# games/scus_97316_sly2_band_of_thieves/hle -- the title's TitleProfile and its play composition.
#
# The presence of THIS file marks the directory as a title (root CMakeLists: games/*/hle/CMakeLists.txt).
#
# ps2r_title_hle   (required by the contract) -- defines ActiveTitleProfile() (title_profile.cpp).
#                  Per-title HLE (native replacements for EE functions, services, workarounds) is
#                  what grows this target later; at the boot stage there is none.
# ps2r_title_play  (optional; app/host links it when present, engine/title/title_play.h) -- the
#                  composition: boot_leg.cpp's TitlePlayMain and RunLeg -- the cold-boot leg over the
#                  engine's fallbacks (headless harness leg, headless --dump-frames, or the play arm: the
#                  SessionRuntime window + live pad) as a chain of phase classes, one per leg_*.{h,cpp}
#                  pair in the first title's construction order (leg_knobs = the NATIVEEE_* knobs, the
#                  title's one getenv site; leg_core, leg_gs, leg_devices, leg_mtvu, leg_dma_ipu,
#                  leg_interrupts, leg_disc, leg_services, leg_audio, leg_play, leg_iop, leg_pad,
#                  leg_image, leg_park, leg_census, leg_park_sink, leg_verify_slot, leg_watchdog; the
#                  trace sinks vert_trace_sink / gif_trace_sink and audio_dump are their own classes),
#                  plus iop_host.cpp: the title's three embedded
#                  IOP modules (lgAud, 989snd, spiop; ../iop) hosted on the engine's IOP kernel, the
#                  stock Sony modules served by the engine's HLE; video_stack.cpp: the generic GS
#                  renderer over GL (DrawPipeline with no class renderers, an empty VramLayout) into
#                  a hidden window whose presented image feeds the session window / frame_dump.cpp
#                  (the --dump-frames P6 writer); pad_script.cpp / pad_recorder.cpp: the
#                  NATIVEEE_PAD_SCRIPT overlay and the NATIVEEE_PAD_RECORD writer (record a session,
#                  replay it headless); store_watch.cpp: the NATIVEEE_WATCH store observer (boot
#                  triage). Defined only when the recompiled
#                  code exists (ps2r_title_recomp, ps2r_title_iop): with no ELF given there is
#                  nothing to run and nativeee keeps its "supplies no play composition" report.
#
# PS2R_IOP_ORACLE stays 0 on ps2r_app_host: this composition links ps2r_iop / ps2r_sif_iop
# directly (through ps2r_title_iop) and never includes app/host/iop_arm.h. For this title the
# IOP arm IS the play shape -- its audio/streaming owners are its own recompiled modules.
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
        frame_dump.cpp
        gif_trace_sink.cpp
        iop_host.cpp
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
        leg_knobs.cpp
        leg_mtvu.cpp
        leg_pad.cpp
        leg_park.cpp
        leg_park_sink.cpp
        leg_play.cpp
        leg_services.cpp
        leg_text.cpp
        leg_verify_slot.cpp
        leg_watchdog.cpp
        pad_recorder.cpp
        pad_script.cpp
        store_watch.cpp
        vert_trace_sink.cpp
        video_stack.cpp
        vu1_probe.cpp
    )
    target_include_directories(ps2r_title_play PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/.. ${PS2R_IOP_GEN_ROOT})
    target_compile_options(ps2r_title_play PRIVATE -Wall -Wextra -Werror)
    # The identity app/host compiles into nativeee, repeated for the composition TU: the title id
    # (run.meta / padlog label), the title directory (hle/sdk_bindings.tsv at run time) and the
    # build's SHA-gated extracted ELF (a development build's input; --game-files overrides).
    # The embedded modules' expected SHA-256 (TITLE_INPUTS rows, via TitleInputs.cmake): the host
    # refuses a pushed image that is not the one the build recompiled.
    target_compile_definitions(ps2r_title_play PRIVATE
        PS2R_TITLE_ID="${PS2R_TITLE}"
        PS2R_TITLE_DIR="${PS2R_GAMES_DIR}/${PS2R_TITLE}"
        PS2R_TITLE_ELF_PATH="${PS2R_TITLE_ELF}"
        PS2R_SLY2_SHA_FILEIO="${PS2R_TITLE_EMBEDDED_FILEIO_service_SHA256}"
        PS2R_SLY2_SHA_LGAUD="${PS2R_TITLE_EMBEDDED_lgAud_SHA256}"
        PS2R_SLY2_SHA_SND989="${PS2R_TITLE_EMBEDDED_989snd_Library_SHA256}"
        PS2R_SLY2_SHA_SPIOP="${PS2R_TITLE_EMBEDDED_spiop_SHA256}")
    # ps2r_app_host: LegCloser, UserData, build_id and (PUBLIC) the engine libraries the leg arms;
    # ps2r_title_recomp: the generated dispatch table; ps2r_sif / ps2r_gs_sync: the sceSif and
    # sceGsSync families the manifest binds; ps2r_title_iop: the three recompiled IOP modules +
    # ps2r_iop + ps2r_sif_iop (the kernel-hosted IOP arm).
    # The hardware fallbacks the sched stage armed: ps2r_gs_state (GsState, the privileged page),
    # ps2r_gif + ps2r_gif_hle (GIF parser / arbiter / GS sink), ps2r_vu (the units + the COP2
    # bridge), ps2r_vif, ps2r_dmac, ps2r_ipu_model (the IPU register window + FIFOs the title's
    # libmpeg drives; this composition is its own IOP-hosting play shape, see above).
    target_link_libraries(ps2r_title_play PUBLIC
        ps2r_app_host ps2r_title_hle ps2r_title_recomp ps2r_sif ps2r_gs_sync ps2r_title_iop
        ps2r_gs_state ps2r_gif ps2r_gif_hle ps2r_vu ps2r_vif ps2r_dmac ps2r_ipu_model)
    # The generated dispatch table is referenced only through DispatchTable (ps2r_ee) -> the
    # generated TU's symbols: force the whole recomp archive in regardless of link order.
    add_dependencies(ps2r_title_play ps2r_title_recomp)
elseif(TARGET ps2r_title_recomp)
    message(STATUS "games/scus_97316_sly2_band_of_thieves: recompiled EE code but no hosted IOP modules -> no play composition")
else()
    message(STATUS "games/scus_97316_sly2_band_of_thieves: no recompiled code -> no play composition (ps2r_title_play not defined)")
endif()
