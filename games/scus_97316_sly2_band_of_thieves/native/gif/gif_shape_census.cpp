#include "games/scus_97316_sly2_band_of_thieves/native/gif/gif_shape_census.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "engine/gif/draw_attribution.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_mtvu_marker.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_unit.h"

namespace ps2r::title::sly2 {

namespace {

constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

uint64_t FnvBytes(const uint8_t* p, size_t n, uint64_t h = kFnvOffset) {
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kFnvPrime;
    return h;
}

uint64_t FnvU64(uint64_t v, uint64_t h) {
    for (int i = 0; i < 8; ++i) h = (h ^ ((v >> (8 * i)) & 0xFF)) * kFnvPrime;
    return h;
}

// The payload quadwords a tag announces (GS User's Manual 7.2): PACKED one per
// descriptor per loop; REGLIST two registers per quadword (the odd tail padded);
// IMAGE one per loop; DISABLED none.
uint64_t PayloadQw(const GifTag& tag) {
    const uint64_t n = static_cast<uint64_t>(tag.nloop) * tag.nregs;
    switch (tag.flg) {
        case GifFormat::Packed: return n;
        case GifFormat::Reglist: return (n + 1) / 2;
        case GifFormat::Image: return tag.nloop;
        case GifFormat::Disabled: break;
    }
    return 0;
}

bool HasAd(const GifTag& tag) {
    if (tag.flg != GifFormat::Packed) return false;
    for (unsigned i = 0; i < tag.nregs; ++i)
        if (tag.regs[i] == static_cast<uint8_t>(GsReg::Ad)) return true;
    return false;
}

}  // namespace

size_t GifShapeCensus::KeyHash::operator()(const Key& k) const {
    uint64_t h = FnvU64(k.tag_lo, kFnvOffset);
    h = FnvU64(k.tag_hi, h);
    h = FnvU64(k.ad_seq, h);
    h = FnvU64(k.fn, h);
    h = FnvU64(k.vu1_hash, h);
    h = FnvU64(k.path, h);
    return static_cast<size_t>(h);
}

GifShapeCensus::GifShapeCensus(std::string path, GifArbiter* arbiter, VuUnit* vu1)
    : path_(std::move(path)), arbiter_(arbiter), vu1_(vu1) {
    if (arbiter_ && vu1_) {
        arbiter_->SetSubmitObserver(&GifShapeCensus::OnArbiterSubmit, this);
        arbiter_->SetDrainObserver(&GifShapeCensus::OnArbiterDeliver, this);
    }
}

GifShapeCensus::~GifShapeCensus() {
    CloseOpen();
    Write();
    if (cap_file_) std::fclose(static_cast<std::FILE*>(cap_file_));
    if (arbiter_ && vu1_) {
        arbiter_->SetSubmitObserver(nullptr, nullptr);
        arbiter_->SetDrainObserver(nullptr, nullptr);
    }
}

// Submit tap: one FIFO entry per PATH1 packet = the microprogram running at the
// XGKICK (the cached tier's current image; 0 when another tier runs it).
void GifShapeCensus::OnArbiterSubmit(void* user, GifPath path, const uint8_t*, size_t) {
    auto* self = static_cast<GifShapeCensus*>(user);
    if (path != GifPath::Path1) return;
    const VuCachedProgram* prog = self->vu1_->dispatch().cached().current();
    self->vu1_fifo_.push_back(prog ? prog->hash : 0);
}

bool GifShapeCensus::StartPacketCapture(const std::string& file, uint64_t max_bytes) {
    if (!arbiter_ || cap_file_) return false;
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (!f) return false;
    std::fwrite("GIFPKT01", 1, 8, f);
    cap_file_ = f;
    cap_max_bytes_ = max_bytes;
    return true;
}

// Drain tap (before the parser sees the packet): PATH1 pops its entry; the
// packet capture takes every packet with data, in delivery order.
void GifShapeCensus::OnArbiterDeliver(void* user, const GifArbiterPacket& pkt) {
    auto* self = static_cast<GifShapeCensus*>(user);
    if (pkt.data.empty()) return;
    if (self->cap_file_ && self->cap_bytes_ < self->cap_max_bytes_) {
        auto* f = static_cast<std::FILE*>(self->cap_file_);
        const uint8_t head[4] = {static_cast<uint8_t>(pkt.path), 0, 0, 0};
        const uint32_t size = static_cast<uint32_t>(pkt.data.size());
        std::fwrite(head, 1, 4, f);
        std::fwrite(&size, 4, 1, f);
        std::fwrite(pkt.data.data(), 1, pkt.data.size(), f);
        self->cap_bytes_ += pkt.data.size();
        ++self->cap_packets_;
    }
    if (pkt.path != GifPath::Path1) {
        self->vu1_current_ = 0;
        return;
    }
    if (self->vu1_fifo_.empty()) {
        ++self->fifo_underflows_;
        self->vu1_current_ = 0;
        return;
    }
    self->vu1_current_ = self->vu1_fifo_.front();
    self->vu1_fifo_.pop_front();
}

void GifShapeCensus::CloseOpen() {
    if (!open_.valid) return;
    Key k = open_.key;
    if (open_.has_ad) k.ad_seq = FnvBytes(open_.ad_regs.data(), open_.ad_regs.size()) ^ (open_.ad_regs.size() << 56);
    Row& r = rows_[k];
    if (r.tags == 0 && open_.has_ad) {
        char buf[8];
        for (size_t i = 0; i < open_.ad_regs.size() && i < 48; ++i) {
            std::snprintf(buf, sizeof buf, "%s%02x", i ? "." : "", open_.ad_regs[i]);
            r.ad_regs += buf;
        }
        if (open_.ad_regs.size() > 48) r.ad_regs += "...";
    }
    ++r.tags;
    r.nloop_sum += open_.nloop;
    r.nloop_min = std::min(r.nloop_min, open_.nloop);
    r.nloop_max = std::max(r.nloop_max, open_.nloop);
    r.qw += open_.qw;
    r.regs += open_.items.regs;
    r.packed += open_.items.packed;
    r.kicks += open_.items.kicks;
    r.draw_kicks += open_.items.draw_kicks;
    r.image_qw += open_.items.image_qw;
    r.eops += open_.items.eops;
    open_ = Open{};
}

void GifShapeCensus::BeginTag(const GifTag& tag, GifPath path) {
    CloseOpen();
    open_.valid = true;
    open_.key.path = static_cast<uint8_t>(path);
    // Re-encode the tag's template word from the parsed fields (NLOOP left out).
    uint64_t lo = 0;
    lo |= static_cast<uint64_t>(tag.eop) << 15;
    lo |= static_cast<uint64_t>(tag.pre) << 46;
    lo |= static_cast<uint64_t>(tag.prim & 0x7FF) << 47;
    lo |= static_cast<uint64_t>(static_cast<uint8_t>(tag.flg) & 3) << 58;
    lo |= static_cast<uint64_t>(tag.nregs & 0xF) << 60;
    uint64_t hi = 0;
    for (unsigned i = 0; i < 16; ++i) hi |= static_cast<uint64_t>(tag.regs[i] & 0xF) << (4 * i);
    open_.key.tag_lo = lo;
    open_.key.tag_hi = hi;
    open_.key.fn = DrawAttribution::CurrentFn();
    open_.key.vu1_hash = path == GifPath::Path1 ? vu1_current_ : 0;
    open_.has_ad = HasAd(tag);
    open_.nloop = tag.nloop;
    open_.qw = PayloadQw(tag);
}

void GifShapeCensus::WriteRegister(uint8_t reg, uint64_t) {
    if (!open_.valid) return;
    ++open_.items.regs;
    if (open_.has_ad && open_.ad_regs.size() < 256) open_.ad_regs.push_back(reg);
}

void GifShapeCensus::PackedRgbaq(uint8_t, uint8_t, uint8_t, uint8_t) {
    if (open_.valid) ++open_.items.packed;
}
void GifShapeCensus::PackedSt(float, float, float) {
    if (open_.valid) ++open_.items.packed;
}
void GifShapeCensus::PackedUv(uint16_t, uint16_t) {
    if (open_.valid) ++open_.items.packed;
}
void GifShapeCensus::PackedFog(uint8_t) {
    if (open_.valid) ++open_.items.packed;
}

void GifShapeCensus::KickVertex(float, float, float, uint8_t, bool draw) {
    if (!open_.valid) return;
    ++open_.items.kicks;
    if (draw) ++open_.items.draw_kicks;
}

void GifShapeCensus::ImageData(uint64_t, uint64_t) {
    if (open_.valid) ++open_.items.image_qw;
}

void GifShapeCensus::ImageSpan(const uint8_t*, size_t qwords) {
    if (open_.valid) open_.items.image_qw += qwords;
}

void GifShapeCensus::EndOfPacket() {
    if (open_.valid) ++open_.items.eops;
}

void GifShapeCensus::FinishPath3() {
    ++finish_path3_;
    CloseOpen();
}

bool GifShapeCensus::Write() const {
    if (path_.empty()) return false;
    std::FILE* f = std::fopen(path_.c_str(), "wb");
    if (!f) return false;
    std::vector<std::pair<const Key*, const Row*>> sorted;
    sorted.reserve(rows_.size());
    for (const auto& kv : rows_) sorted.emplace_back(&kv.first, &kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second->qw > b.second->qw; });
    std::fprintf(f, "# gif shape census: rows=%zu finish_path3=%llu vu1_fifo_underflows=%llu\n", rows_.size(),
                 static_cast<unsigned long long>(finish_path3_), static_cast<unsigned long long>(fifo_underflows_));
    std::fprintf(f,
                 "path\ttag_lo_tpl\ttag_hi\tflg\tnregs\teop\tpre\tprim\tad_seq\tfn\tvu1_hash\ttags\tnloop_sum\tnloop_min\tnloop_max\tqw\tregs\tpacked\tkicks\tdraw_kicks\timage_qw\teops\tad_regs\n");
    for (const auto& [k, r] : sorted) {
        std::fprintf(f,
                     "%u\t%016llx\t%016llx\t%u\t%u\t%u\t%u\t%03x\t%016llx\t%08x\t%016llx\t%llu\t%llu\t%u\t%u\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%s\n",
                     k->path, static_cast<unsigned long long>(k->tag_lo), static_cast<unsigned long long>(k->tag_hi),
                     static_cast<unsigned>((k->tag_lo >> 58) & 3), static_cast<unsigned>((k->tag_lo >> 60) & 0xF),
                     static_cast<unsigned>((k->tag_lo >> 15) & 1), static_cast<unsigned>((k->tag_lo >> 46) & 1),
                     static_cast<unsigned>((k->tag_lo >> 47) & 0x7FF), static_cast<unsigned long long>(k->ad_seq), k->fn,
                     static_cast<unsigned long long>(k->vu1_hash), static_cast<unsigned long long>(r->tags),
                     static_cast<unsigned long long>(r->nloop_sum), r->nloop_min, r->nloop_max,
                     static_cast<unsigned long long>(r->qw), static_cast<unsigned long long>(r->regs),
                     static_cast<unsigned long long>(r->packed), static_cast<unsigned long long>(r->kicks),
                     static_cast<unsigned long long>(r->draw_kicks), static_cast<unsigned long long>(r->image_qw),
                     static_cast<unsigned long long>(r->eops), r->ad_regs.empty() ? "-" : r->ad_regs.c_str());
    }
    std::fclose(f);
    return true;
}

}  // namespace ps2r::title::sly2
