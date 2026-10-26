#include "games/scus_97316_sly2_band_of_thieves/hle/frame_dump.h"

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace ps2r::title::sly2 {

bool FrameDump::Open(const std::string& dir) {
    dir_.clear();
    if (dir.empty()) return true;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec && !std::filesystem::is_directory(dir)) return false;
    dir_ = dir;
    return true;
}

bool FrameDump::Write(const char* prefix, uint32_t key, int w, int h, const uint8_t* rgba, bool flip) {
    if (dir_.empty() || w <= 0 || h <= 0 || !rgba) return false;
    const std::string path = dir_ + "/" + prefix + std::to_string(key) + ".ppm";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        ++failed_;
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int i = 0; i < h; ++i) {
        const int y = flip ? h - 1 - i : i;
        const uint8_t* src = rgba + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            row[static_cast<size_t>(x) * 3 + 0] = src[x * 4 + 0];
            row[static_cast<size_t>(x) * 3 + 1] = src[x * 4 + 1];
            row[static_cast<size_t>(x) * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    ++written_;
    return true;
}

}  // namespace ps2r::title::sly2
