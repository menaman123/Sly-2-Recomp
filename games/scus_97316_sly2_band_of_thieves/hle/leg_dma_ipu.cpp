// games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.cpp -- LegDmaIpu (leg_dma_ipu.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/leg_dma_ipu.h"

#include "engine/dmac/dmac_channel.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/vif/vif.h"

namespace ps2r::title::sly2 {

LegDmaIpu::LegDmaIpu(const LegKnobs& knobs, LegCore& core, LegDevices& devices, LegMtvu& mtvu)
    : vif_gate(devices.vif),
      vif1_window(mtvu.mtvu ? mtvu.mtvu->Vif1Window(vif_gate) : static_cast<MmioHandler&>(vif_gate)),
      ipu_fifo(ipu),
      ipu_dump_close{ipu_dump},
      ipu_out_dump_close{ipu_out_dump} {
    Dmac& dmac = devices.dmac;
    core.mmio.Register(Vif::kVif0Base, Vif::kRegsSize, vif_gate, "VIF0");
    core.mmio.Register(Vif::kVif1Base, Vif::kRegsSize, vif1_window, "VIF1");
    core.mmio.Register(Vif::kVif1FifoBase, Vif::kFifoSize, vif1_window, "VIF1_FIFO");
    core.map.RegisterHwWindow(0x11000000u, 0x1000u, devices.vu0.state().micro, "VU0_MICRO");
    core.map.RegisterHwWindow(0x11004000u, 0x1000u, devices.vu0.state().data, "VU0_DATA");
    core.map.RegisterHwWindow(0x11008000u, 0x4000u, devices.vu1.state().micro, "VU1_MICRO");
    core.map.RegisterHwWindow(0x1100C000u, 0x4000u, devices.vu1.state().data, "VU1_DATA");
    core.mmio.Register(0x10008000u, 0x6064u, dmac, "DMAC");
    core.mmio.Register(0x1000F520u, 0x10u, dmac, "D_ENABLER");
    core.mmio.Register(0x1000F590u, 0x10u, dmac, "D_ENABLEW");
    dmac.SetGuestCall(core.guest_call);
    dmac.SetInterruptRaise([this] { ++dmac_int1_raises; });
    // _EnableDmac / _DisableDmac are D_STAT.CIM on the hardware: the kernel word and the
    // register are one state (libmpeg enables channel 3 for its fromIPU completion handler).
    SyscallTable::Instance().SetDmacMaskSink([&dmac](int ch, bool enable) { dmac.SetChannelInterruptMask(ch, enable); });
    // The IPU register window + its two DMAC channels.
    core.mmio.Register(0x10002000u, ps2r::ipu::IpuDevice::kWindowBytes, ipu, "IPU");
    // The FIFO ports: libipu pushes the quantiser matrices (sceIpuSetIQ/SetVQ) through the write port, not the DMAC.
    core.mmio.Register(0x10007000u, ps2r::ipu::IpuFifoPort::kWindowBytes, ipu_fifo, "IPU_FIFO");
    // NATIVEEE_IPU_DUMP=<file>: every byte the toIPU channel feeds, appended (boot triage only).
    if (knobs.ipu_dump) ipu_dump = std::fopen(knobs.ipu_dump->c_str(), "wb");
    dmac.SetIpuFeedInput([this](uint8_t* host, size_t bytes) {
        if (ipu_dump) std::fwrite(host, 1, bytes, ipu_dump);
        ipu.FeedInputFifo(host, bytes);
    });
    // NATIVEEE_IPU_OUT_DUMP=<file>: every byte the fromIPU channel drains (BDEC RAW16 / CSC RGB32), appended.
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
    // The eight-quadword input FIFO: the toIPU channel moves what fits and resumes as the
    // IPU consumes (libmpeg reads D4_QWC and IPU_CTRL.IFC to find where the stream is).
    dmac.SetIpuInputRoom([this] { return ipu.InputRoomBytes(); });
    ipu.SetInputRoomReady([&dmac] {
        auto& to = dmac.channel(static_cast<int>(DmacChannelId::IpuTo));
        if ((to.chcr & DmacChcr::kStr) && to.qwc != 0) dmac.StartChannel(static_cast<int>(DmacChannelId::IpuTo));
    });
    // COP2: the VU0 macro ops and CMSAR1 reach the units (unbound they park vu_micro_absent).
    VuMacroBridge::Bind(&devices.vu0, &devices.vu1, &devices.vu0_registry);
}

}  // namespace ps2r::title::sly2
