// games/scus_97316_sly2_band_of_thieves/hle/pad_recorder.h -- PadRecorder: the NATIVEEE_PAD_RECORD writer
// of the title composition. Every boundary's SERVED pad sample (what PadProtocol::Stamp got: the live
// keyboard / gamepad with any NATIVEEE_PAD_SCRIPT overlay applied) is run-length encoded into a file in
// PadScript's grammar, so NATIVEEE_PAD_SCRIPT=<that file> replays the session stamp for stamp:
//
//   # nativeee pad record build=<build id> frames=<stamps> steps=<lines>
//   # <format line>
//   <from>:<buttons_hex>:<hold>:<lx>,<ly>,<rx>,<ry>      one line per run of an identical non-neutral state
//
// Neutral stamps (no button, every stick at 128) are implicit -- the parser's default. A run's line is
// appended and flushed the moment the state changes, so a killed session loses only the run that was
// open; Close() writes that run and patches the header's counts in place. The file holds nothing but
// stamp indices, button bits and stick values: no game data, safe to attach to a bug report.
#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "engine/pad/hle/pad_protocol.h"

namespace ps2r::title::sly2 {

class PadRecorder {
public:
    PadRecorder() = default;
    ~PadRecorder();
    PadRecorder(const PadRecorder&) = delete;
    PadRecorder& operator=(const PadRecorder&) = delete;

    // Creates / truncates `path` and writes the header; false when it cannot be opened.
    bool Open(const std::string& path, const std::string& build_id);
    bool Enabled() const { return file_ != nullptr; }
    const std::string& Path() const { return path_; }

    // The boundary duty (any thread; serialised): the sample served at `stamp`. Stamps are
    // expected in order, one per boundary; a gap closes the open run at the gap.
    void Record(uint64_t stamp, const PadProtocol::Input& in);
    // Writes the open run, patches the header, closes the file. Idempotent.
    void Close();

    uint64_t Frames() const { return frames_; }   // stamps recorded
    uint64_t Steps() const { return steps_; }     // lines written
    // run.meta pad_record= body: <path>:frames=,steps=  (or "off")
    std::string Census() const;

private:
    void EmitRun(uint64_t until);
    void WriteHeader();

    std::FILE* file_ = nullptr;
    std::string path_, build_id_;
    mutable std::mutex mutex_;
    PadProtocol::Input run_{};
    uint64_t run_from_ = 0;    // the open run's first stamp
    uint64_t next_stamp_ = 0;  // the stamp the next Record is expected to carry
    uint64_t frames_ = 0, steps_ = 0;
    bool open_run_ = false;
};

}  // namespace ps2r::title::sly2
