// --dump-frames DIR: per presented frame, the display grab (f<key>.ppm, the image the frames lane
// hashes) and the window image as shown (p<key>.ppm), as binary P6 PPM.
#pragma once

#include <cstdint>
#include <string>

namespace ps2r::title::sly2 {

class FrameDump {
public:
    FrameDump() = default;
    // Creates `dir`; false if it cannot. An empty dir turns dumping off.
    bool Open(const std::string& dir);
    bool Enabled() const { return !dir_.empty(); }
    const std::string& Dir() const { return dir_; }

    // Writes <dir>/<prefix><key>.ppm from w*h RGBA8. `flip` for bottom-up rows (a GL read-back).
    bool Write(const char* prefix, uint32_t key, int w, int h, const uint8_t* rgba, bool flip);
    uint64_t Written() const { return written_; }
    uint64_t Failed() const { return failed_; }

private:
    std::string dir_;
    uint64_t written_ = 0;
    uint64_t failed_ = 0;
};

}  // namespace ps2r::title::sly2
