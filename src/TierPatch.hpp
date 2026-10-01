// Pure logic, no Android/SDK deps (so it can be tested on any host).
//
// tiers.bin is base64 of a JSON file: {"gpu": {"<renderer>": <tier>, ...}}.
// patchTiers() decodes it, changes one single-digit tier, re-encodes it, and
// returns the result ONLY if it is exactly the same size as the input. Same
// size matters: the hooks below patch bytes in place and never touch the
// asset's length or read position.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace vv {

inline std::string b64decode(std::string_view in) {
    static const std::string_view tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        size_t v = tbl.find(c);
        if (v == std::string_view::npos) continue;  // skip whitespace
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

inline std::string b64encode(std::string_view in) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8) |
                     static_cast<unsigned char>(in[i + 2]);
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], tbl[(v >> 6) & 63], tbl[v & 63]};
    }
    if (i + 1 == in.size()) {
        unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                     (static_cast<unsigned char>(in[i + 1]) << 8);
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], tbl[(v >> 6) & 63], '='};
    }
    return out;
}

// Returns the patched file (same size as `file`), or "" if the GPU entry was
// not found, wasn't a single digit, or the size would change.
inline std::string patchTiers(std::string_view file, std::string_view gpu, char tier) {
    std::string json = b64decode(file);

    std::string key = "\"" + std::string(gpu) + "\"";
    size_t p = json.find(key);
    if (p == std::string::npos) return {};
    p += key.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) ++p;  // ": "
    if (p + 1 >= json.size()) return {};
    if (json[p] < '0' || json[p] > '9') return {};
    if (json[p + 1] >= '0' && json[p + 1] <= '9') return {};  // multi-digit

    json[p] = tier;
    std::string out = b64encode(json);
    return out.size() == file.size() ? out : std::string{};
}

// Raises every GPU whose tier is below `minTier` up to `minTier`. Only touches
// single-digit values inside the "gpu" object. Same-size rule as above.
// `changed` (optional) receives how many entries were raised.
inline std::string patchAllTiers(std::string_view file, char minTier, int *changed = nullptr) {
    std::string json = b64decode(file);

    size_t g = json.find("\"gpu\"");
    if (g == std::string::npos) return {};
    size_t begin = json.find('{', g);
    size_t end = json.find('}', begin == std::string::npos ? g : begin);
    if (begin == std::string::npos || end == std::string::npos) return {};

    int n = 0;
    for (size_t p = begin; p < end; ++p) {
        if (json[p] != '"') continue;
        size_t q = json.find('"', p + 1);          // end of the key
        if (q == std::string::npos || q >= end) break;
        size_t v = q + 1;
        while (v < end && (json[v] == ' ' || json[v] == ':')) ++v;
        if (v + 1 < end && json[v] >= '0' && json[v] <= '9' &&
            !(json[v + 1] >= '0' && json[v + 1] <= '9')) {
            if (json[v] < minTier) { json[v] = minTier; ++n; }
            p = v;
        } else {
            p = q;
        }
    }
    if (changed) *changed = n;
    if (n == 0) return {};

    std::string out = b64encode(json);
    return out.size() == file.size() ? out : std::string{};
}

}  // namespace vv
