// LegDmaIpu: the GIF, VIF, VU memory, DMAC and IPU register windows, registered after the MTVU stack
// exists (under MTVU the VIF1 window syncs the worker and the GIF window fences the drain thread).
// Also the DMAC guest call and the kernel's _EnableDmac/_DisableDmac mask (D_STAT.CIM), the IPU with its
// two DMAC channels (ch4 RAM -> input FIFO, ch3 output FIFO -> RAM) and FIFO ports, and the COP2 bridge
// for VU0 macro ops (unbound, they park vu_micro_absent).
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
    MmioHandler& vif1_window;   // MTVU's syncing window over the gate, or the gate itself
    uint64_t dmac_int1_raises = 0;   // census dmac_int1_raises=
    ps2r::ipu::IpuDevice ipu;
    ps2r::ipu::IpuFifoPort ipu_fifo;
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
    // NATIVEEE_VU0_TRACE=<file>:<pc>[,<pc>...]: VCALLMS operand census.
    Vu0Trace vu0_trace;
    std::string vu0_trace_path;
    void WriteVu0Trace();
};

}  // namespace ps2r::title::sly2
