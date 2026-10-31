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
// CONTENT IDENTITY (STAGE NATIVE-2): the engine keys a program by the hash of the WHOLE 16 KB micro memory,
// which carries the residue of earlier uploads, so one disc program had up to three identities and an unseen
// residue combination was a miss. The table (native_vu1_table.h) now keys each program by the hash of its own
// bytes (the `size` the disc's ELF uploads at VU address 0: native/vu1/extract_images.py, disc_programs.txt);
// `Identify` hashes a kicked image's first `size` bytes per row (longest first) and the engine's
// unknown-image resolver (Vu1AotTier::SetUnknownImageResolver) registers the whole-memory hash as an ALIAS
// of the program's exports (SetNativeExportsAlias) the first time it is kicked -- before the fallback would
// run, so no kick is lost. Sound because the generated TU covers only blocks inside the program's extent
// (vu1_native_gen bounds the closure) and the per-block structural check still runs at install.
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

#include <set>

#include "engine/vu/vu1_aot/vu1_aot_tier.h"

struct VuSettings;
class VuUnit;

namespace ps2r::title::sly2 {

struct NativeVu1Image;

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
    // "images=N.native_bundles=N.native_blocks=N.installs=N.refused=N.progs=<hash16>x<kicks>/<bundles><n|m>|..."
    // (every image the live unit's block table decoded: n = its generated code is installed, m = modelled)
    // and ".misses=<hash16>x<count>|...". Call once at the census write, after the unit stopped.
    void Publish(VuUnit& live) const;

    uint64_t misses() const;

    // The table row whose program bytes are the prefix of `micro` (16 KB; longest program first), or null.
    // Pure (the corpus test and the resolver share it).
    static const NativeVu1Image* Identify(const uint8_t* micro);
    // How many whole-memory images were aliased to a program so far (one per residue variant seen).
    size_t aliases() const;

private:
    static void OnMiss(void* user, VuUnit& unit, uint32_t start_pc, uint64_t image_hash, Vu1AotTier::Status status);
    static bool Resolve(void* user, VuUnit& unit, uint64_t image_hash);

    std::set<uint64_t> unknown_;                  // whole-memory hashes no program matched (VU thread)
    std::map<uint64_t, uint64_t> alias_of_;       // whole-memory hash -> content hash (VU thread)

    mutable std::mutex mu_;
    std::map<uint64_t, uint64_t> miss_by_image_;  // image hash -> kicks that fell back (VU thread writes)
    uint64_t misses_ = 0;
    size_t installed_ = 0;
};

}  // namespace ps2r::title::sly2
