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
    # track gif (gif/README.md): the packet-shape census and the native builder on the arbiter's interceptor seam
    gif/gif_shape_census.cpp
    gif/native_gif.cpp
    # vif + dmac (track vif-dmac): the VIF1 chain executor on ChainWalker::SetVifChainExecutor
    vif/native_vif.cpp
    vif/vif_census.cpp
    vif/vif_chain_tags.cpp
    vif/vif_native_chains.cpp
    vif/vif_native_spans.cpp
    vif/vif_unpack_native.cpp
    # iop + spu (track iop-spu, then snd): the native 989snd RPC server on the RpcRouter seam (snd/README.md)
    native_iop.cpp
    snd/native_snd_server.cpp
    # iop (track iopsys, STAGE NATIVE-3): the per-function native module frame over the recompiled TUs' seam
    # (iop/README.md): the module objects, the services, the call trace, the registered bodies
    iop/native_iop_module.cpp
    iop/native_iop_frame.cpp
    iop/native_iop_bodies.cpp
    iop/iop_services.cpp
    iop/iop_call_trace.cpp
    iop/snd989_lock.cpp
    iop/spiop_tray_thread.cpp
    iop/iop_step.cpp
    # spiop track (iop/spiop/README.md): the streaming module
    iop/spiop/spiop_native.cpp
    iop/spiop/spiop_threads.cpp
    # lgaud track (iop/native_lgaud.h): lgAud + FILEIO_service bodies
    iop/native_lgaud.cpp
    iop/native_fileio.cpp
)
target_include_directories(ps2r_title_native PUBLIC ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
# -ffp-contract=off: the float contract spelled for every compiler (the engine's rule for GNU; Clang's
# default fuses only within one expression -- the native paths' arithmetic must round like the oracle's).
target_compile_options(ps2r_title_native PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(PS2R_SIMD)
    target_compile_definitions(ps2r_title_native PRIVATE PS2R_SIMD=1)
endif()
# ps2r_core: Config (title.native.*); ps2r_host: engine/platform (the only host surface allowed here).
target_link_libraries(ps2r_title_native PUBLIC ps2r_core ps2r_host ps2r_gif ps2r_vu ps2r_gs_state ps2r_dmac ps2r_vif ps2r_ee ps2r_sif ps2r_iop ps2r_sound)
# The seams the tracks plug into: ps2r_gif (GifArbiter, GsSink, DrawAttribution), ps2r_vu (the VU1
# program identity for the census), ps2r_gs_state (GsState, the builders' target).

# The per-unit native paths that need the title's recompiled code / build-time generated data
# (never committed: generated like the recomp shards, MANIFEST-pinned). Tracks add theirs here.
if(TARGET ps2r_title_recomp)
    # vu1/ -> ps2r_title_native_vu1: the title's VU1 microprograms generated at build time from the kick
    # corpus (native/vu1/CMakeLists.txt; linked into ps2r_title_play by hle/CMakeLists.txt).
    add_subdirectory(vu1)
    # target_sources(ps2r_title_native PRIVATE native_vu1.cpp ...)
    # target_link_libraries(ps2r_title_native PUBLIC ps2r_vu ps2r_gif ...)
    add_subdirectory(vu0)
    # The CLOSURE check (STAGE NATIVE-2, native/COVERAGE.md): closure_check.py re-derives from the disc (or the ELF)
    # the inputs the game can hand to VU1 / VU0 / IOP -- the .vutext programs, the VCALLMS and MSCAL entry sets, the
    # IRX modules, the level streams -- and FAILS when one has no native path (images.txt row + generated TU + block).
    # With the disc (NATIVEEE_DISC) it also decompresses the ELF-tabled level streams and proves they carry no
    # microcode; --full (not run by ctest: minutes) covers every stream and the raw region.
    set(_closure_args --generated "${PS2R_TITLE_GEN_ROOT}/games/${PS2R_TITLE}/native/vu1")
    if(NATIVEEE_DISC AND EXISTS "${NATIVEEE_DISC}")
        list(APPEND _closure_args --iso "${NATIVEEE_DISC}" --quick)
    else()
        list(APPEND _closure_args --elf "${PS2R_TITLE_ELF}")
    endif()
    add_test(NAME Sly2_NativeClosure
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/closure_check.py" ${_closure_args})
    set_tests_properties(Sly2_NativeClosure PROPERTIES TIMEOUT 600 LABELS "native;closure" DEPENDS Vu1NativeCorpus)
endif()
# The seams the native paths plug into: the DMAC chain walker, the VIF unit, the VU units.

# The vif-dmac equivalence test: synthesized chains (the title's grammar) through the engine's walk and
# through the native executor -> identical VU1 data memory, VIF registers, channel registers, i-bit stalls.
add_executable(sly2_test_native_vif vif/test_native_vif.cpp)
target_include_directories(sly2_test_native_vif PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(sly2_test_native_vif PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sly2_test_native_vif PRIVATE ps2r_title_native ps2r_gs_ir)
add_test(NAME Sly2_NativeVifChains COMMAND sly2_test_native_vif)
set_tests_properties(Sly2_NativeVifChains PROPERTIES TIMEOUT 120)
# The IOP / SPU2 native path's seams: RpcRouter (ps2r_sif), LibsdModule (ps2r_iop), SpuCore (ps2r_sound).
# The 989snd RPC-replay oracle test (snd/test_snd_replay.cpp): without arguments a synthetic self-check of the
# reply-buffer model (what ctest runs: no game data); with a NATIVEEE_RPC_TRACE file [+ --iop-ram <dump>.iop] the
# recorded hosted lane replayed through Snd989Server (replies + libsd op sequences compared; snd/README.md).
add_executable(sly2_test_snd_replay snd/test_snd_replay.cpp)
target_include_directories(sly2_test_snd_replay PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GAMES_DIR}/..)
target_compile_options(sly2_test_snd_replay PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sly2_test_snd_replay PRIVATE ps2r_title_native)
add_test(NAME Sly2_SndReplaySelfCheck COMMAND sly2_test_snd_replay)
set_tests_properties(Sly2_SndReplaySelfCheck PROPERTIES TIMEOUT 60)
