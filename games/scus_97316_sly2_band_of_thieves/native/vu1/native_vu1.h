// native_vu1.h -- Sly 2's VU1 microprograms as NATIVE host code (STAGE NATIVE, title.native.vu1).
//
// The engine models VU1 per kick: a tier (cached / block / interpreter) walks the microprogram's
// bundles. This title uses a FIXED set of microprogram images (native/vu1/images.txt), so their native
// C++ is GENERATED ONCE AT BUILD TIME by the engine's own VU1 AOT generator (Vu1AotEmitter: one region
// function per block, every slot a compile-time constant, the engine's lane kernels and pipelines as the
// primitives) from the recorded kick corpus -- the image bytes are game data and never enter the
// repository; the generated TUs land in <build>/generated/... like the recompiled EE shards and are
// pinned by MANIFEST.sha256 -- and compiled INTO the title (ps2r_title_native_vu1). No interpreter tier
// walks a listed image at run time, no JIT, no first-run compile, no host compiler, no dlopen.
//
// The SEAM is the engine's existing one (engine/vu/vu1_aot/vu1_aot_tier.h): the AOT tier consults its
// registry (image hash -> status + exports) at every kick BEFORE running anything; a Verified image with
// exports runs its generated functions (the block table stays the dispatcher: JR / JALR targets, the E
// bit, the cycle cap and the NOP sled are the loop's). NativeVu1 FILLS that registry at startup from
// the generated table (native_vu1_table.h) instead of the run-time cache / builder, and selects the AOT
// tier on the dispatch (Select) before the MTVU worker copies the settings. A kick of an image NOT in
// the table (a miss) falls back to the engine's model (the block tier, proven == the interpreter) and is
// counted (NativeCensus::Fallback + the per-image miss list in the row's detail).
//
// Identity: the generated bodies are proven against the interpreter on the corpus at build-test time
// (Vu1NativeCorpus: every case + a cycle-cap sweep) and in the game by the lane's frames hash (every
// vertex the native program emits equals the modelled program's). The run-time InstallNative check
// (ABI version, slot size, MAC-ring arm, block structure) refuses a table that does not match the
// running engine -- that kick runs the specialised table and `refused` counts it.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "engine/vu/vu1_aot/vu1_aot_tier.h"

struct VuSettings;
class VuUnit;

namespace ps2r::title::sly2 {

class NativeVu1 {
public:
    NativeVu1() = default;
    NativeVu1(const NativeVu1&) = delete;
    NativeVu1& operator=(const NativeVu1&) = delete;

    // The tier the native path needs on `settings` (the AOT tier; a miss runs its fallback). Call on
    // the dispatch settings BEFORE the MTVU worker copies them (LegDevices).
    static void Select(VuSettings& settings);
    // How many images the build generated (0 = no corpus at build time: the knob is inert).
    static size_t ImageCount();

    // Fill `unit`'s AOT registry: every generated image Verified with its exports; the miss observer.
    // Call before the unit's first kick (the MTVU worker's unit before Start). False = nothing to
    // install (no generated image) or a table whose header disagrees with this engine (`note` says).
    bool Install(VuUnit& unit, std::string* note);

    // Publish the counters into NativeCensus (served = native kicks, fallback = misses) with the detail
    // "images=N:native_blocks=N:refused=N:misses=<hash>:<count>,...". Call once at the census write.
    void Publish(const VuUnit& live) const;

    uint64_t misses() const;

private:
    static void OnMiss(void* user, VuUnit& unit, uint32_t start_pc, uint64_t image_hash, Vu1AotTier::Status status);

    mutable std::mutex mu_;
    std::map<uint64_t, uint64_t> miss_by_image_;  // image hash -> kicks that fell back (VU thread writes)
    uint64_t misses_ = 0;
    size_t installed_ = 0;
};

}  // namespace ps2r::title::sly2
