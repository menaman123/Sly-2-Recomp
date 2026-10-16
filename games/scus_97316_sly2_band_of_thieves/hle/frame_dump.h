// games/scus_97316_sly2_band_of_thieves/hle/frame_dump.h -- FrameDump: the --dump-frames writer of
// the title composition. ConfigLoader parses `--dump-frames <dir>` into its HarnessRequest (the
// usage text promises "<dir>/f<key>.ppm") but nothing in the public engine consumes it; the leg
// writes, per present key, the display GRAB (f<key>.ppm: DrawPipeline::Frame::rgba, what the
// frames lane hashed; its rows run PS2 top-down) and the PRESENTED window image (p<key>.ppm:
// Backend::ReadPresented, what a player sees; GL bottom-up, flipped here) as binary P6 PPM.
#pragma once

#include <cstdint>
#include <string>

namespace ps2r::title::sly2 {

class FrameDump {
public:
    FrameDump() = default;
    // Creates `dir` (and its parents); false when it cannot be made. Empty dir = off.
    bool Open(const std::string& dir);
    bool Enabled() const { return !dir_.empty(); }
    const std::string& Dir() const { return dir_; }

    // <dir>/<prefix><key>.ppm from w*h RGBA8; `flip` = the rows are bottom-up (the presented
    // window image read back from GL: row 0 is the bottom), else row 0 is the picture's top
    // (the display grab: the target attachment stores PS2 row 0 first). False on an I/O failure.
    bool Write(const char* prefix, uint32_t key, int w, int h, const uint8_t* rgba, bool flip);
    uint64_t Written() const { return written_; }
    uint64_t Failed() const { return failed_; }

private:
    std::string dir_;
    uint64_t written_ = 0;
    uint64_t failed_ = 0;
};

}  // namespace ps2r::title::sly2
