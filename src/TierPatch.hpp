// Pure logic, no Android/SDK deps (so it can be tested on any host).
//
// tiers.bin is base64 of a JSON file: {"gpu": {"<renderer>": <tier>, ...}}.
// patchTiers() decodes it, changes one single-digit tier, re-encodes it, and
// returns the result. We pad with newlines to ensure the output size exactly
// matches the input size, which keeps asset offsets/lengths consistent.
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
        if (v == std::string_view::npos) continue;  // safely skips newlines/spaces
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

// Returns the patched file (padded to match original size), or "" if not found.
inline std::string patchTiers(std::string_view file, std::string_view gpu, char tier) {
    std::string json = b64decode(file);

    std::string key = "\"" + std::string(gpu) + "\"";
    size_t p = json.find(key);
    if (p == std::string::npos) return {};
    p += key.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':')) ++p;
    if (p + 1 >= json.size()) return {};
    if (json[p] < '0' || json[p] > '9') return {};
    if (json[p + 1] >= '0' && json[p + 1] <= '9') return {};  // skip multi-digit

    json[p] = tier;
    std::string out = b64encode(json);
    
    // Pad with newlines to perfectly match original file size. 
    // The b64decode function safely ignores trailing newlines.
    if (out.size() <= file.size()) {
        out.append(file.size() - out.size(), '\n');
        return out;
    }
    return {};
}

// Raises every GPU whose tier is below `minTier` up to `minTier`. 
// Uses robust brace-counting to handle nested JSON structures safely.
inline std::string patchAllTiers(std::string_view file, char minTier, int *changed = nullptr) {
    std::string json = b64decode(file);

    size_t g = json.find("\"gpu\"");
    if (g == std::string::npos) return {};
    
    size_t begin = json.find('{', g);
    if (begin == std::string::npos) return {};

    // Robustly find the matching closing brace for the "gpu" object
    int brace_count = 1;
    size_t end = begin + 1;
    while (end < json.size() && brace_count > 0) {
        if (json[end] == '{') brace_count++;
        else if (json[end] == '}') brace_count--;
        end++;
    }
    if (brace_count != 0) return {}; // Malformed JSON
    end--; // Point to the actual closing '}'

    int n = 0;
    for (size_t p = begin; p < end; ++p) {
        if (json[p] != '"') continue;
        
        size_t q = json.find('"', p + 1);          // end of the GPU name key
        if (q == std::string::npos || q >= end) break;
        
        size_t v = q + 1;
        while (v < end && (json[v] == ' ' || json[v] == ':')) ++v; // skip to value
        
        // Check if it's a single-digit tier value
        if (v + 1 < end && json[v] >= '0' && json[v] <= '9' &&
            !(json[v + 1] >= '0' && json[v + 1] <= '9')) {
            if (json[v] < minTier) { 
                json[v] = minTier; 
                ++n; 
            }
            p = v; // Skip past the digit we just processed
        } else {
            p = q; // Skip to the end of the key if no valid tier found
        }
    }
    
    if (changed) *changed = n;
    if (n == 0) return {};

    std::string out = b64encode(json);
    
    // Pad with newlines to perfectly match original file size.
    if (out.size() <= file.size()) {
        out.append(file.size() - out.size(), '\n');
        return out;
    }
    return {};
}

}  // namespace vv
