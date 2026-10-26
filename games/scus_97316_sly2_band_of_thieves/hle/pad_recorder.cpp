#include "games/scus_97316_sly2_band_of_thieves/hle/pad_recorder.h"

namespace ps2r::title::sly2 {
namespace {

bool SameSample(const PadProtocol::Input& a, const PadProtocol::Input& b) {
    return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
}

bool Neutral(const PadProtocol::Input& in) {
    return in.buttons == 0 && in.lx == 0x80 && in.ly == 0x80 && in.rx == 0x80 && in.ry == 0x80;
}

}  // namespace

PadRecorder::~PadRecorder() { Close(); }

bool PadRecorder::Open(const std::string& path, const std::string& build_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    file_ = f;
    path_ = path;
    build_id_ = build_id;
    frames_ = steps_ = next_stamp_ = run_from_ = 0;
    open_run_ = false;
    WriteHeader();
    std::fprintf(file_, "# <from_stamp>:<buttons_hex>:<hold>:<lx>,<ly>,<rx>,<ry> per run of one pad state; "
                        "neutral stamps are implicit. Replay: NATIVEEE_PAD_SCRIPT=<this file>\n");
    std::fflush(file_);
    return true;
}

void PadRecorder::WriteHeader() {
    // Fixed width (the counts are space-padded) so Close() can rewrite it in place.
    std::fprintf(file_, "# nativeee pad record build=%s frames=%-12llu steps=%-12llu\n", build_id_.c_str(),
                 static_cast<unsigned long long>(frames_), static_cast<unsigned long long>(steps_));
}

void PadRecorder::Record(uint64_t stamp, const PadProtocol::Input& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return;
    if (!open_run_ || stamp != next_stamp_ || !SameSample(in, run_)) {
        if (open_run_) EmitRun(next_stamp_);
        run_ = in;
        run_from_ = stamp;
        open_run_ = true;
    }
    next_stamp_ = stamp + 1;
    frames_ = next_stamp_;
}

void PadRecorder::EmitRun(uint64_t until) {
    if (Neutral(run_) || until <= run_from_) return;
    std::fprintf(file_, "%llu:%x:%llu:%u,%u,%u,%u\n", static_cast<unsigned long long>(run_from_), static_cast<unsigned>(run_.buttons),
                 static_cast<unsigned long long>(until - run_from_), run_.lx, run_.ly, run_.rx, run_.ry);
    std::fflush(file_);
    ++steps_;
}

void PadRecorder::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return;
    if (open_run_) EmitRun(next_stamp_);
    open_run_ = false;
    std::fseek(file_, 0, SEEK_SET);
    WriteHeader();
    std::fclose(file_);
    file_ = nullptr;
}

std::string PadRecorder::Census() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) return "off";
    return path_ + ":frames=" + std::to_string(frames_) + ":steps=" + std::to_string(steps_);
}

}  // namespace ps2r::title::sly2
