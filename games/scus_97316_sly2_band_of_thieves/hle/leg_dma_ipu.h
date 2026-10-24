// games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h -- LegDmaIpu: the register windows of the GIF /
// VIF / VU memory / DMAC and the IPU of the title's play composition (after the MTVU stack exists: the
// VIF1 window is the stack's syncing wrapper under MTVU, the GIF window its drain-fencing wrapper, the
// plain handlers otherwise). The VU memory windows map
// the INLINE units (the first title's composition did the same under MTVU): an EE-side access to VU1
// memory while the worker owns the unit is not a path this title takes (its microprograms and data
// arrive through VIF1 MPG / UNPACK). The DMAC's guest call, INT1 raise counter and the kernel's
// _EnableDmac / _DisableDmac mask sink (D_STAT.CIM: the kernel word and the register are one state).
// The IPU register window + its two DMAC channels (ch4 RAM -> input FIFO, ch3 output FIFO -> RAM; a
// short drain retries when output appears): the hardware the title's libmpeg drives; the FIFO ports
// (IPU_out_FIFO 0x10007000 read, IPU_in_FIFO 0x10007010 write) libipu pushes the quantiser matrices
// through; NATIVEEE_IPU_DUMP / NATIVEEE_IPU_OUT_DUMP (boot triage) tap the two streams. COP2: the VU0
// macro ops and CMSAR1 reach the units through VuMacroBridge (unbound they park vu_micro_absent); the
// guard member unbinds when this object goes.
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/ee/mem/mmio_handler.h"
#include "engine/ipu/ipu_device.h"
#include "engine/ipu/ipu_fifo_port.h"
#include "engine/vif/vif_mmio_gate.h"
#include "engine/vu/macro/vu_macro_bridge.h"
#include "engine/vu/vu0/vu0_trace.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_core.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_devices.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_knobs.h"
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_mtvu.h"

namespace ps2r::title::sly2 {

class LegDmaIpu {
public:
    LegDmaIpu(const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu);
    LegDmaIpu(const LegDmaIpu&) = delete;
    LegDmaIpu& operator=(const LegDmaIpu&) = delete;

    VifMmioGate vif_gate;
    MmioHandler& vif1_window;   // the MTVU stack's syncing window over the gate, or the gate itself
    uint64_t dmac_int1_raises = 0;   // census dmac_int1_raises=
    ps2r::ipu::IpuDevice ipu;
    ps2r::ipu::IpuFifoPort ipu_fifo;
    // The triage taps, closed when this object goes (the FILE* members above each guard).
    struct FileClose {
        std::FILE*& f;
        ~FileClose() { if (f) std::fclose(f); }
    };
    std::FILE* ipu_dump = nullptr;
    FileClose ipu_dump_close;
    std::FILE* ipu_out_dump = nullptr;
    FileClose ipu_out_dump_close;
    struct VuMacroUnbind {
        ~VuMacroUnbind() { VuMacroBridge::Unbind(); }
    } vu_macro_unbind;
    // NATIVEEE_VU0_TRACE=<file>:<pc>[,<pc>...] (leg_knobs.h): the engine's VCALLMS operand census.
    Vu0Trace vu0_trace;
    std::string vu0_trace_path;
    void WriteVu0Trace();   // every exit path (leg_census)
};

}  // namespace ps2r::title::sly2
