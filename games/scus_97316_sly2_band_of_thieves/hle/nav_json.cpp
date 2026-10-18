// games/scus_97316_sly2_band_of_thieves/hle/nav_json.cpp -- NavJson (nav_json.h).
#include "games/scus_97316_sly2_band_of_thieves/hle/nav_json.h"

#include <cstdlib>

namespace ps2r::title::sly2 {

namespace {

void SkipBlanks(const std::string& s, size_t* i) {
    while (*i < s.size() && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) ++*i;
}

}  // namespace

bool NavJson::Parse(const std::string& text, NavJson* out, size_t* error_at) {
    size_t i = 0;
    *out = NavJson{};
    const bool ok = ParseValue(text, &i, out, 0);
    if (!ok && error_at) *error_at = i;
    return ok;
}

bool NavJson::ParseString(const std::string& s, size_t* i, std::string* out) {
    if (*i >= s.size() || s[*i] != '"') return false;
    ++*i;
    out->clear();
    while (*i < s.size()) {
        const char c = s[(*i)++];
        if (c == '"') return true;
        if (c == '\\') {
            if (*i >= s.size()) return false;
            const char e = s[(*i)++];
            if (e == 'n') *out += '\n';
            else if (e == 't') *out += '\t';
            else if (e == '"' || e == '\\' || e == '/') *out += e;
            else return false;
        } else {
            *out += c;
        }
    }
    return false;
}

bool NavJson::ParseValue(const std::string& s, size_t* i, NavJson* out, int depth) {
    if (depth > 32) return false;
    SkipBlanks(s, i);
    if (*i >= s.size()) return false;
    const char c = s[*i];
    if (c == '{') {
        out->kind = Object;
        ++*i;
        SkipBlanks(s, i);
        if (*i < s.size() && s[*i] == '}') { ++*i; return true; }
        for (;;) {
            SkipBlanks(s, i);
            std::string key;
            if (!ParseString(s, i, &key)) return false;
            SkipBlanks(s, i);
            if (*i >= s.size() || s[*i] != ':') return false;
            ++*i;
            NavJson v;
            if (!ParseValue(s, i, &v, depth + 1)) return false;
            out->keys.push_back(key);
            out->items.push_back(std::move(v));
            SkipBlanks(s, i);
            if (*i >= s.size()) return false;
            if (s[*i] == ',') { ++*i; continue; }
            if (s[*i] == '}') { ++*i; return true; }
            return false;
        }
    }
    if (c == '[') {
        out->kind = Array;
        ++*i;
        SkipBlanks(s, i);
        if (*i < s.size() && s[*i] == ']') { ++*i; return true; }
        for (;;) {
            NavJson v;
            if (!ParseValue(s, i, &v, depth + 1)) return false;
            out->items.push_back(std::move(v));
            SkipBlanks(s, i);
            if (*i >= s.size()) return false;
            if (s[*i] == ',') { ++*i; continue; }
            if (s[*i] == ']') { ++*i; return true; }
            return false;
        }
    }
    if (c == '"') {
        out->kind = String;
        return ParseString(s, i, &out->str);
    }
    if (s.compare(*i, 4, "true") == 0) { out->kind = Bool; out->boolean = true; *i += 4; return true; }
    if (s.compare(*i, 5, "false") == 0) { out->kind = Bool; *i += 5; return true; }
    if (s.compare(*i, 4, "null") == 0) { out->kind = Null; *i += 4; return true; }
    char* end = nullptr;
    out->number = std::strtod(s.c_str() + *i, &end);
    if (!end || end == s.c_str() + *i) return false;
    out->kind = Number;
    *i = static_cast<size_t>(end - s.c_str());
    return true;
}

const NavJson* NavJson::Member(const char* key) const {
    if (kind != Object) return nullptr;
    for (size_t k = 0; k < keys.size(); k++)
        if (keys[k] == key) return &items[k];
    return nullptr;
}

bool NavJson::AsU32(uint32_t* out) const {
    if (kind == Number) { *out = static_cast<uint32_t>(static_cast<int64_t>(number)); return true; }
    if (kind != String || str.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(str.c_str(), &end, 0);
    if (!end || *end) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

}  // namespace ps2r::title::sly2
