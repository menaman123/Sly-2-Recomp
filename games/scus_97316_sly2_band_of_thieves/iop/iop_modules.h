// games/scus_97316_sly2_band_of_thieves/iop/iop_modules.h -- the title data of Sly 2's hosted
// IOP modules: which embedded images are recompiled, under which namespace, at which load base,
// and how IOP RAM is laid out around them. Every number here is a fact of this title's images
// (irx_inventory: .iopmod sizes) or this composition's layout choice, never engine policy.
//
//   IOP RAM (2 MiB):
//     [0x00000, 0x20000)   the IOPRP300 kernel + stock modules (served natively; nothing hosted here)
//     [0x20000, 0x24280)   FILEIO_service (memsz 0x4280; the reboot image's fileio, hosted at the reboot)
//     [0x28000, 0x323c0)   lgAud          (memsz 0xa3c0)
//     [0x33000, 0x59e88)   989snd_Library (memsz 0x26e88)
//     [0x5a000, 0x5f700)   spiop          (memsz 0x5700)
//     [0x60000, 0x1fe000)  the sysmem heap (the EE's sceSifAllocIopHeap buffers -- the nine pushed
//                          images, 473,408 bytes -- 989snd's bank/stream buffers, the module threads'
//                          stacks, the module-args blocks)
//     [0x1fe000, 0x1ff000) the (unused) stack band the heap skips; thread stacks come from the heap
//     [0x1ff800, 0x200000) the scratch stacks of host-run callbacks (hard-timer beat, DMA-complete)
#pragma once

#include <cstdint>

#include "engine/iop/sysmem_heap.h"

namespace ps2r::title::sly2 {

struct IopModules {
    struct Module {
        const char* iopmod_name;   // the .iopmod name the pushed image carries
        const char* id;            // the recompiler namespace suffix / target id
        uint32_t base;             // the load address the image was recompiled for
        uint32_t extent;           // base + memsz
    };
    static constexpr Module kFileio = {"FILEIO_service", "fileio", 0x20000, 0x24280};
    static constexpr Module kLgaud = {"lgAud", "lgaud", 0x28000, 0x323c0};
    static constexpr Module kSnd989 = {"989snd_Library", "snd989", 0x33000, 0x59e88};
    static constexpr Module kSpiop = {"spiop", "spiop", 0x5a000, 0x5f700};

    static constexpr uint32_t kHeapFloor = 0x60000;
    static constexpr uint32_t kStackBandLo = 0x1fe000;
    static constexpr uint32_t kStackBandHi = 0x1ff000;
    static constexpr uint32_t kHeapCeiling = iop::SysmemHeap::kCeiling;   // 0x1ff800
    static constexpr uint32_t kHandlerScratchSp = 0x1ffc00;              // host-run callback stack (grows down)
    static constexpr uint32_t kModuleArgsBytes = 0x100;                  // per-module argv block from the heap
    // libkernl's sceSifInitCmd static command buffer (SCUS_973.16 .bss 0x44ed80, the tsv's
    // disassembly note): where a SIF command a hosted module sends lands for the EE's handler.
    static constexpr uint32_t kEeSifCmdBuffer = 0x44ed80;
    static constexpr uint32_t kEeSifCmdSlots = 32;
    static constexpr uint32_t kEeSifCmdSlotBytes = 128;
    static constexpr const char* kRebootImage = "IOPRP300.IMG";

    static constexpr iop::SysmemHeap::Layout Heap() {
        return iop::SysmemHeap::Layout{kHeapFloor, kStackBandLo, kStackBandHi, kHeapCeiling};
    }
};

}  // namespace ps2r::title::sly2
