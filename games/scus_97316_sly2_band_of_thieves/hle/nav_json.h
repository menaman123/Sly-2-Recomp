// games/scus_97316_sly2_band_of_thieves/hle/nav_json.h -- NavJson: the minimal JSON reader the navigator's
// DATA files use (verify/nav/sly2_probe.json for NavProbe, verify/nav/ep1_waypoints.json for PadScript's
// `path:` steps). Objects, arrays, strings, numbers, true/false/null; string escapes limited to \" \\ \/ \n \t.
// Enough for a hand-written data file; a malformed file fails the parse (the byte offset names the spot).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r::title::sly2 {

class NavJson {
public:
    enum Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<std::string> keys;    // Object: parallel to items
    std::vector<NavJson> items;       // Array / Object members

    // Parses `text` into *out. On failure *error_at receives the byte offset the parse stopped at.
    static bool Parse(const std::string& text, NavJson* out, size_t* error_at);

    // Object member by key (nullptr when absent or not an object).
    const NavJson* Member(const char* key) const;
    // A chain element / address: "0x..." hex or a decimal string, or a number.
    bool AsU32(uint32_t* out) const;

private:
    static bool ParseValue(const std::string& s, size_t* i, NavJson* out, int depth);
    static bool ParseString(const std::string& s, size_t* i, std::string* out);
};

}  // namespace ps2r::title::sly2
