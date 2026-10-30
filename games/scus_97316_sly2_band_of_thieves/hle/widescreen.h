// Widescreen: Sly 2 rendered for the session's display aspect (video.aspect: 16:9, 16:10, 21:9, 32:9, or
// the window's shape under auto). The presenter already shows the frame at that ratio; this widens what the
// game draws into it, so the picture is not stretched.
//
//   * Camera init (FUN_0014d6d8) stores the camera's horizontal scale, 0x3fa63bda for 4:3, at camera + 0x3c.
//     The camera recalc (FUN_0014db70, run by init and the field-of-view setters) derives the projection and
//     the culling frustum from it (camera + 0x40 = tan(fov / 2) * scale). The scale is multiplied by
//     ratio / (4/3) after init, and when the ratio changes the boundary step rewrites it and reruns the
//     recalc, so a change on the Display page shows at the next frame.
//   * The point projection (FUN_0014f308, screen positions of world points) divides by camera + 0x40; that
//     divisor is scaled by (4/3) / ratio for the call, the same correction as the community 16:9 patch.
// At 4:3 neither is touched.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/gs/hle/present/aspect_selection.h"

namespace ps2r::title::sly2 {

class Widescreen {
public:
    static constexpr uint32_t kCameraInit = 0x0014d6d8;
    static constexpr uint32_t kRecalc = 0x0014db70;
    static constexpr uint32_t kProjectPoint = 0x0014f308;
    static constexpr uint32_t kHorizontalScale = 0x3c;   // camera field written by kCameraInit
    static constexpr uint32_t kProjectDivisor = 0x40;    // camera field read by kProjectPoint

    Widescreen(MainMemory& ram, std::shared_ptr<gs::AspectSelection> aspect) : ram_(ram), aspect_(std::move(aspect)) {}

    // Wraps the two generated bodies; false when either is not a generated function start.
    bool Install();
    // Game thread, at a boundary (`ctx` the game's registers, kept): the camera's horizontal scale for the
    // ratio in force, recalculated when it changed.
    void OnBoundary(CpuContext& ctx);
    // The widening in force (1 = 4:3).
    float Factor() const;
    // run.meta widescreen=: the camera, the game's own horizontal scale, the value in place, the factor.
    std::string Census() const;

private:
    float LoadF(uint32_t addr) const;
    void StoreF(uint32_t addr, float v);
    // Writes the widened scale; reruns the recalc on the game's stack, registers saved around it.
    void Apply(CpuContext& ctx);

    MainMemory& ram_;
    std::shared_ptr<gs::AspectSelection> aspect_;
    uint32_t camera_ = 0;
    float base_ = 0.0f;       // the game's own value of camera + 0x3c
    float written_ = 0.0f;    // the value this class last stored there
};

}  // namespace ps2r::title::sly2
