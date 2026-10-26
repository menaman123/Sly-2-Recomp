#include "games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h"

#include "engine/dmac/dmac_channel.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/gif/gif_regs.h"
#include "engine/vif/vif.h"

namespace ps2r::title::sly2 {

LegDmaIpu::LegDmaIpu(const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu)
    : vif_gate(devices.vif),
      vif1_window(mtvu.mtvu ? mtvu.mtvu->Vif1Window(vif_gate) : static_cast<MmioHandler&>(vif_gate)),
      ipu_fifo(ipu),
      ipu_dump_close{ipu_dump},
      ipu_out_dump_close{ipu_out_dump} {
    Dmac& dmac = devices.dmac;
    // Under the drain thread every GIF register access fences the parser first: STAT and TAG are built
    // from transfer state the parser owns.
    core.mmio.Register(GifRegs::kBase, GifRegs::kSize,
                       mtvu.mtvu ? mtvu.mtvu->GifWindow(static_cast<GifRegs&>(devices.gif_regs)) : static_cast<MmioHandler&>(devices.gif_regs), "GIF");
    core.mmio.Register(Vif::kVif0Base, Vif::kRegsSize, vif_gate, "VIF0");
    core.mmio.Register(Vif::kVif1Base, Vif::kRegsSize, vif1_window, "VIF1");
    core.mmio.Register(Vif::kVif1FifoBase, Vif::kFifoSize, vif1_window, "VIF1_FIFO");
    core.map.RegisterHwWindow(0x11000000u, 0x1000u, devices.vu0.state().micro, "VU0_MICRO");
    core.map.RegisterHwWindow(0x11004000u, 0x1000u, devices.vu0.state().data, "VU0_DATA");
    // The VU1 windows map the live unit (the worker's under MTVU): the game's VIF1 interrupt handler reads
    // the lit vertex colours its VU1 lighting left at 0x1100c320+ while the worker is parked on the i-bit.
    core.map.RegisterHwWindow(0x11008000u, 0x4000u, mtvu.LiveVu1().state().micro, "VU1_MICRO");
    core.map.RegisterHwWindow(0x1100C000u, 0x4000u, mtvu.LiveVu1().state().data, "VU1_DATA");
    core.mmio.Register(0x10008000u, 0x6064u, dmac, "DMAC");
    core.mmio.Register(0x1000F520u, 0x10u, dmac, "D_ENABLER");
    core.mmio.Register(0x1000F590u, 0x10u, dmac, "D_ENABLEW");
    dmac.SetGuestCall(core.guest_call);
    dmac.SetInterruptRaise([this] { ++dmac_int1_raises; });
    // _EnableDmac / _DisableDmac are D_STAT.CIM on hardware: kernel word and register are one state.
    SyscallTable::Instance().SetDmacMaskSink([&dmac](int ch, bool enable) { dmac.SetChannelInterruptMask(ch, enable); });
    core.mmio.Register(0x10002000u, ps2r::ipu::IpuDevice::kWindowBytes, ipu, "IPU");
    // The FIFO ports: libipu pushes the quantiser matrices (sceIpuSetIQ/SetVQ) through the write port, not the DMAC.
    core.mmio.Register(0x10007000u, ps2r::ipu::IpuFifoPort::kWindowBytes, ipu_fifo, "IPU_FIFO");
    // NATIVEEE_IPU_DUMP=<file>: every byte the toIPU channel feeds.
    if (knobs.ipu_dump) ipu_dump = std::fopen(knobs.ipu_dump->c_str(), "wb");
    dmac.SetIpuFeedInput([this](uint8_t* host, size_t bytes) {
        if (ipu_dump) std::fwrite(host, 1, bytes, ipu_dump);
        ipu.FeedInputFifo(host, bytes);
    });
    // NATIVEEE_IPU_OUT_DUMP=<file>: every byte the fromIPU channel drains (BDEC RAW16 / CSC RGB32).
    if (knobs.ipu_out_dump) ipu_out_dump = std::fopen(knobs.ipu_out_dump->c_str(), "wb");
    dmac.SetIpuDrainOutput([this](uint8_t* host, size_t bytes) {
        const size_t got = ipu.DrainOutputFifo(host, bytes);
        if (ipu_out_dump && got) std::fwrite(host, 1, got, ipu_out_dump);
        return got;
    });
    ipu.SetOutputReady([&dmac] {
        if (dmac.channel(static_cast<int>(DmacChannelId::IpuFrom)).chcr & DmacChcr::kStr)
            dmac.StartChannel(static_cast<int>(DmacChannelId::IpuFrom));
    });
    // The input FIFO holds eight quadwords: toIPU moves what fits and resumes as the IPU consumes
    // (libmpeg reads D4_QWC and IPU_CTRL.IFC to find its place in the stream).
    dmac.SetIpuInputRoom([this] { return ipu.InputRoomBytes(); });
    ipu.SetInputRoomReady([&dmac] {
        auto& to = dmac.channel(static_cast<int>(DmacChannelId::IpuTo));
        if ((to.chcr & DmacChcr::kStr) && to.qwc != 0) dmac.StartChannel(static_cast<int>(DmacChannelId::IpuTo));
    });
    // COP2: VU0 macro ops and CMSAR1 reach the units.
    VuMacroBridge::Bind(&devices.vu0, &devices.vu1, &devices.vu0_registry);
    if (knobs.vu0_trace) {
        const size_t colon = knobs.vu0_trace->find(':');
        std::string err;
        if (colon == std::string::npos || !vu0_trace.Parse(knobs.vu0_trace->substr(colon + 1), &err)) {
            std::fprintf(stderr, "NATIVEEE_VU0_TRACE: bad spec (%s)\n", err.c_str());
        } else {
            vu0_trace_path = knobs.vu0_trace->substr(0, colon);
            VuMacroBridge::SetTrace(&vu0_trace);
        }
    }
}

void LegDmaIpu::WriteVu0Trace() {
    if (vu0_trace_path.empty()) return;
    VuMacroBridge::SetTrace(nullptr);
    vu0_trace.Flush(vu0_trace_path);
    vu0_trace_path.clear();
}

}  // namespace ps2r::title::sly2
