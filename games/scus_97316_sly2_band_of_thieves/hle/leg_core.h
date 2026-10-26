// LegCore: the first phase. EE memory and the MMIO router (unmodelled pages read as idle), the time
// domain, INTC and the EE timers, the SIF registers, the tick source and frame rendezvous, the frame
// boundary, and the kernel hooks. Members are public on purpose: the leg modules are one composition
// split into phases, and later phases reach this shared state through a LegCore&.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
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
#include "games/scus_97316_sly2_band_of_thieves/hle/text_watch.h"

namespace ps2r::title::sly2 {

class LegCore {
public:
    LegCore(const ps2r::Config& cfg, const LegKnobs& knobs);
    LegCore(const LegCore&) = delete;
    LegCore& operator=(const LegCore&) = delete;

    // False if the game boundary did not attach; RunLeg then exits with 2.
    bool Attached() const { return attached_; }

    // --- memory + MMIO ---------------------------------------------------------------------------
    MainMemory ram;
    Scratchpad spad;
    AddressMap map;
    MmioSink sink;
    MmioRouter mmio;
    FastMem fast_mem;
    StoreWatch store_watch;   // the FastMem observer is process-wide, so it lives for the whole leg

    // --- time domain, boundary owner, INTC + timers, SIF registers ---------------------------------
    Intc& intc;
    EmuDomain domain;
    DerivedClocks clocks;
    EeTimers timers;
    SifState sif_state;
    TickSource tick_source;
    FrameRendezvous rendezvous;
    std::atomic<bool> running{true};
    // Play mode: a window and a wall-paced boundary (neither --headless nor time.tick_sync).
    const bool play_mode;
    const std::string dump_frames_dir;
    const bool want_video;

    // --- the frame boundary ---------------------------------------------------------------------
    BoundarySequencer sequencer;
    GameBoundary game_boundary;
    BoundaryProtocol protocol;

    // --- kernel seams ------------------------------------------------------------------------------
    // The guest-call hook the kernel, INTC and DMAC dispatch through (DispatchTable::Invoke).
    SyscallTable::GuestCall guest_call;
    std::string first_syscalls;   // run.meta first_syscalls=: the first 40 syscalls, name(num)
    uint64_t syscall_count = 0;
    std::deque<std::string> last_syscalls;   // run.meta last_syscalls=: the last 24 syscalls (park / exit triage)
    // run.meta guest_call_unknown=: guest calls (redirected syscalls, INTC/DMAC handlers, alarms) whose
    // target is not a generated function. DispatchTable::Invoke silently runs nothing for those.
    std::atomic<uint64_t> guest_call_unknown{0};
    std::mutex guest_call_unknown_mutex;
    std::string guest_call_unknown_list;   // up to 16 entries addr@caller_pc

private:
    bool attached_ = false;
};

}  // namespace ps2r::title::sly2
