// iop_services.cpp -- IopServices / IopRam / IopStackFrame over the engine's IopAbiSurface binding.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/iop_services.h"

#include <cstdio>

#include "engine/iop/abi/iop_abi_surface.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::title::sly2 {

uint32_t IopServices::Import(IopContext& ctx, const char* lib, uint32_t index) {
    iop::IopAbiSurface::ImportCall(lib, index, ctx);
    return ctx.gpr[2];
}

uint32_t IopServices::Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0) {
    ctx.gpr[4] = a0;
    iop::IopAbiSurface::ImportCall(lib, index, ctx);
    return ctx.gpr[2];
}

uint32_t IopServices::Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1) {
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    iop::IopAbiSurface::ImportCall(lib, index, ctx);
    return ctx.gpr[2];
}

uint32_t IopServices::Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2) {
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    ctx.gpr[6] = a2;
    iop::IopAbiSurface::ImportCall(lib, index, ctx);
    return ctx.gpr[2];
}

uint32_t IopServices::Import(IopContext& ctx, const char* lib, uint32_t index, uint32_t a0, uint32_t a1, uint32_t a2,
                             uint32_t a3) {
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    ctx.gpr[6] = a2;
    ctx.gpr[7] = a3;
    iop::IopAbiSurface::ImportCall(lib, index, ctx);
    return ctx.gpr[2];
}

bool IopServices::CallGuest(IopContext& ctx, uint32_t addr, uint32_t ra) {
    const iop::IopAbiSurface::GuestFunction* fn = iop::IopAbiSurface::Resolve(addr);
    if (!fn) {
        std::fprintf(stderr, "native/iop: CallGuest(0x%08x): no guest function there\n", addr);
        return false;
    }
    if (ra) ctx.gpr[31] = ra;
    (*fn)(ctx);
    ctx.FixZero();
    return true;
}

uint32_t IopRam::R32(uint32_t addr) { return iop::IopAbiSurface::Memory().Read<uint32_t>(addr); }
uint16_t IopRam::R16(uint32_t addr) { return iop::IopAbiSurface::Memory().Read<uint16_t>(addr); }
uint8_t IopRam::R8(uint32_t addr) { return iop::IopAbiSurface::Memory().Read<uint8_t>(addr); }
void IopRam::W32(uint32_t addr, uint32_t v) { iop::IopAbiSurface::Memory().Write<uint32_t>(addr, v); }
void IopRam::W16(uint32_t addr, uint16_t v) { iop::IopAbiSurface::Memory().Write<uint16_t>(addr, v); }
void IopRam::W8(uint32_t addr, uint8_t v) { iop::IopAbiSurface::Memory().Write<uint8_t>(addr, v); }
uint8_t* IopRam::Ptr(uint32_t addr, uint32_t size) { return iop::IopAbiSurface::Memory().Pointer(addr, size); }
const uint8_t* IopRam::CPtr(uint32_t addr, uint32_t size) { return iop::IopAbiSurface::Memory().Pointer(addr, size); }

IopStackFrame::IopStackFrame(IopContext& ctx, uint32_t bytes) : ctx_(ctx), bytes_(bytes) {
    ctx_.gpr[29] = static_cast<uint32_t>(static_cast<int32_t>(ctx_.gpr[29]) - static_cast<int32_t>(bytes_));
}

IopStackFrame::~IopStackFrame() {
    ctx_.gpr[29] = static_cast<uint32_t>(static_cast<int32_t>(ctx_.gpr[29]) + static_cast<int32_t>(bytes_));
}

void IopStackFrame::Word(uint32_t offset, uint32_t value) { IopRam::W32(ctx_.gpr[29] + offset, value); }
uint32_t IopStackFrame::Word(uint32_t offset) const { return IopRam::R32(ctx_.gpr[29] + offset); }

}  // namespace ps2r::title::sly2
