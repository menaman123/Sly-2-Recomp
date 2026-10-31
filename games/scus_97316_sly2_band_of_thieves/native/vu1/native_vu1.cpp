// NativeVu1 -- the registry fill and the miss census. Rules in the header.
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1.h"

#include <cstdio>

#include "engine/vu/tiers/vu_block_tier.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_emitter.h"
#include "engine/vu/vu1_aot/vu1_aot_exports.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"
#include "games/scus_97316_sly2_band_of_thieves/native/native_census.h"
#include "games/scus_97316_sly2_band_of_thieves/native/vu1/native_vu1_table.h"

namespace ps2r::title::sly2 {

void NativeVu1::Select(VuSettings& settings) { settings.vu1_mode = VuTierMode::Aot; }

size_t NativeVu1::ImageCount() { return kNativeVu1ImageCount; }

bool NativeVu1::Install(VuUnit& unit, std::string* note) {
    if (kNativeVu1ImageCount == 0) {
        if (note) *note = "native vu1: no generated image (the build had no VU1 corpus); every kick runs the engine's model";
        return false;
    }
    Vu1AotTier& tier = unit.dispatch().aot();
    size_t ok = 0;
    std::string bad;
    for (size_t i = 0; i < kNativeVu1ImageCount; ++i) {
        const NativeVu1Image& img = kNativeVu1Images[i];
        const Vu1AotImageExports* ex = img.exports ? img.exports() : nullptr;
        // The header check the tier repeats at the kick (InstallNative): refused here = never registered,
        // so the image runs the modelled path and the census names it as a miss.
        const bool header_ok = ex != nullptr && ex->abi_version == kVu1AotAbiVersion && ex->slot_size == sizeof(VuDecodedSlot) &&
                               ex->image_hash == img.hash && (ex->flags_lazy != 0) == unit.flags().lazy() && ex->block_count == img.blocks;
        if (!header_ok) {
            bad += (bad.empty() ? "" : ",") + Vu1AotEmitter::Hex16(img.hash);
            continue;
        }
        // A whole-image row (legacy mode) is registered under its own hash; a program row is reached
        // through the resolver (its whole-memory hashes are only known at the kick).
        if (img.size >= 16384) {
            tier.SetStatus(img.hash, Vu1AotTier::Status::Verified);
            tier.SetNativeExports(img.hash, ex);
        }
        ++ok;
    }
    tier.set_assume_verified(false);
    tier.SetKickObserver(&NativeVu1::OnMiss, this);
    tier.SetUnknownImageResolver(&NativeVu1::Resolve, this);
    installed_ = ok;
    if (note) {
        *note = "native vu1: " + std::to_string(ok) + " of " + std::to_string(kNativeVu1ImageCount) + " generated image(s) registered on unit " +
                std::to_string(unit.id()) + (bad.empty() ? "" : " (header mismatch, left to the model: " + bad + ")");
    }
    return ok > 0;
}

const NativeVu1Image* NativeVu1::Identify(const uint8_t* micro) {
    // The rows come longest first from the generator (the 2-bundle stub is a prefix of the main program).
    const NativeVu1Image* best = nullptr;
    for (size_t i = 0; i < kNativeVu1ImageCount; ++i) {
        const NativeVu1Image& img = kNativeVu1Images[i];
        if (img.size == 0 || img.size > 16384 || img.exports == nullptr) continue;
        if (best != nullptr && img.size <= best->size) continue;
        if (VuImageHash::Fast(micro, img.size) == img.hash) best = &img;
    }
    return best;
}

bool NativeVu1::Resolve(void* user, VuUnit& unit, uint64_t image_hash) {
    NativeVu1* self = static_cast<NativeVu1*>(user);
    std::lock_guard<std::mutex> lock(self->mu_);  // two units (inline + MTVU worker) share this object
    if (self->unknown_.count(image_hash)) return false;
    const NativeVu1Image* img = Identify(unit.state().micro);
    if (img == nullptr || img->size >= 16384) {
        self->unknown_.insert(image_hash);
        return false;
    }
    const Vu1AotImageExports* ex = img->exports();
    if (ex == nullptr || ex->image_hash != img->hash || (ex->flags_lazy != 0) != unit.flags().lazy()) {
        self->unknown_.insert(image_hash);
        return false;
    }
    Vu1AotTier& tier = unit.dispatch().aot();
    tier.SetStatus(image_hash, Vu1AotTier::Status::Verified);
    tier.SetNativeExportsAlias(image_hash, ex);
    self->alias_of_[image_hash] = img->hash;
    return true;
}

size_t NativeVu1::aliases() const {
    std::lock_guard<std::mutex> lock(mu_);
    return alias_of_.size();
}

void NativeVu1::OnMiss(void* user, VuUnit& /*unit*/, uint32_t /*start_pc*/, uint64_t image_hash, Vu1AotTier::Status /*status*/) {
    NativeVu1* self = static_cast<NativeVu1*>(user);
    std::lock_guard<std::mutex> lock(self->mu_);
    ++self->miss_by_image_[image_hash];
    ++self->misses_;
}

uint64_t NativeVu1::misses() const {
    std::lock_guard<std::mutex> lock(mu_);
    return misses_;
}

void NativeVu1::Publish(VuUnit& live) const {
    const Vu1AotTier& tier = live.dispatch().aot();
    const Vu1AotTier::Stats& s = tier.stats();
    NativeCensus& census = NativeCensus::Instance();
    census.Served(NativeUnit::Vu1, s.native_kicks);
    census.Fallback(NativeUnit::Vu1, s.fallback_kicks);
    std::string detail = "images=" + std::to_string(installed_) + ".aliases=" + std::to_string(aliases()) + ".native_bundles=" + std::to_string(s.native_bundles) +
                         ".native_blocks=" + std::to_string(s.native_blocks) + ".installs=" + std::to_string(s.native_installs) +
                         ".refused=" + std::to_string(tier.native_refused());
    // The per-image census from the block table (every image the unit decoded: kicks, bundles, native or modelled).
    struct Acc {
        std::string text;
        bool first = true;
    } acc;
    live.dispatch().block().ForEachProgram(
        +[](void* user, const VuBlockProgram& prog) {
            Acc* a = static_cast<Acc*>(user);
            a->text += (a->first ? "" : "|") + Vu1AotEmitter::Hex16(prog.hash) + "x" + std::to_string(prog.exec_count) + "/" +
                       std::to_string(prog.bundles) + (prog.native != nullptr ? "n" : "m");
            a->first = false;
        },
        &acc);
    if (!acc.text.empty()) detail += ".progs=" + acc.text;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!miss_by_image_.empty()) {
            detail += ".misses=";
            bool first = true;
            for (const auto& kv : miss_by_image_) {
                detail += (first ? "" : "|") + Vu1AotEmitter::Hex16(kv.first) + "x" + std::to_string(kv.second);
                first = false;
            }
        }
    }
    census.SetDetail(NativeUnit::Vu1, detail);
}

}  // namespace ps2r::title::sly2
