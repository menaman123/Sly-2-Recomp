// games/scus_97316_sly2_band_of_thieves/hle/leg_core.h -- LegCore: the first phase of the title's play
// composition (boot_leg.cpp RunLeg). Owns, in the first title's construction order, the EE memory + the
// MMIO sink/router (the sink answers every unmodelled page idle; INTC / timers / SIF displace it), the
// NATIVEEE_WATCH store observer, the emulated time domain and derived clocks, INTC + the EE timers, the
// SIF register block, the tick source, the frame rendezvous and the `running` flag, the boundary
// (sequencer + the game-thread boundary owner + the Phase-3 steps + the protocol), and the kernel seams
// (kernel clocks, GsSync, the guest-call hook, the syscall trace, the INTC mask word, the thread
// manager's event applier and guest body). Later phases hold a LegCore& and reach these members.
// Members are the composition's shared state (public by design: the modules are one leg split along its
// construction order, not an API); everything stays on RunLeg's frame through this object.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "engine/core/config/config.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/intc/ee_timers.h"
#include "engine/ee/intc/intc.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/timing/boundary_protocol.h"
#include "engine/ee/timing/boundary_sequencer.h"
#include "engine/ee/timing/derived_clocks.h"
#include "engine/ee/timing/emu_domain.h"
#include "engine/ee/timing/frame_rendezvous.h"
#include "engine/ee/timing/game_boundary.h"
#include "engine/ee/timing/tick_source.h"
#include "engine/sif/sif_state.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/store_watch.h"

namespace ps2r::title::sly2 {

class LegCore {
public:
    LegCore(const ps2r::Config& cfg, const LegKnobs& knobs);
    LegCore(const LegCore&) = delete;
    LegCore& operator=(const LegCore&) = delete;

    // False when the game boundary did not attach: the leg ends with exit 2 (nothing after the
    // boundary owner was armed).
    bool Attached() const { return attached_; }

    // --- memory + MMIO ---------------------------------------------------------------------------
    MainMemory ram;
    Scratchpad spad;
    AddressMap map;
    MmioSink sink;
    MmioRouter mmio;
    FastMem fast_mem;
    StoreWatch store_watch;   // the FastMem observer is process-wide and never cleared: lives here

    // --- time domain, boundary owner, INTC + timers, SIF registers ---------------------------------
    Intc& intc;
    EmuDomain domain;
    DerivedClocks clocks;
    EeTimers timers;
    SifState sif_state;
    TickSource tick_source;
    FrameRendezvous rendezvous;
    std::atomic<bool> running{true};
    // Play mode = a window exists and the boundary is wall-paced (neither --headless nor time.tick_sync).
    // Headless with --dump-frames <dir>: the GS pipeline into a hidden GL window. Headless without a dump
    // dir: a counting sink, no GL.
    const bool play_mode;
    const std::string dump_frames_dir;
    const bool want_video;

    // --- the boundary: sequencer + the game-thread boundary owner + the Phase-3 steps -------------
    BoundarySequencer sequencer;
    GameBoundary game_boundary;
    BoundaryProtocol protocol;

    // --- kernel seams ------------------------------------------------------------------------------
    // The guest-call hook the kernel, INTC and DMAC dispatch through (DispatchTable::Invoke).
    SyscallTable::GuestCall guest_call;
    std::string first_syscalls;   // run.meta first_syscalls=: the first 40 syscalls, name(num)
    uint64_t syscall_count = 0;

private:
    bool attached_ = false;
};

}  // namespace ps2r::title::sly2
