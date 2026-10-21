#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rg::util {

inline std::string b64encode(const std::string& data) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0, n = data.size();
    while (i + 3 <= n) {
        std::uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                          (static_cast<unsigned char>(data[i + 1]) << 8) |
                          static_cast<unsigned char>(data[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back(tbl[(v >> 6) & 0x3f]);
        out.push_back(tbl[v & 0x3f]);
        i += 3;
    }
    std::size_t rem = n - i;
    if (rem == 1) {
        std::uint32_t v = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        std::uint32_t v = (static_cast<unsigned char>(data[i]) << 16) |
                          (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back(tbl[(v >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

// base64.b64decode(data, validate). On invalid input throws std::runtime_error
// (mirrors binascii.Error, which the callers catch).
inline std::string b64decode(const std::string& data, bool validate = false) {
    auto dval = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    std::uint32_t val = 0;
    int bits = 0;
    long long datachars = 0;
    for (char c : data) {
        int d = dval(c);
        if (d >= 0) {
            val = (val << 6) | static_cast<std::uint32_t>(d);
            bits += 6;
            ++datachars;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<char>((val >> bits) & 0xff));
            }
        } else if (c == '=') {
            continue;
        } else {
            if (validate) throw std::runtime_error("Invalid base64 character");
            // else discard
        }
    }
    if (datachars % 4 == 1) throw std::runtime_error("Invalid base64 length");
    return out;
}

}  // namespace rg::util
