#pragma once

#include <cstdint>
#include <string>

namespace rg::util {

// Equivalent to the Python `base36` package: base36.dumps(n). Lowercase digits,
// dumps(0) == "0", negative values get a leading '-'.
inline std::string base36_dumps(long long n) {
    static const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (n == 0) return "0";
    bool neg = n < 0;
    unsigned long long v = neg ? static_cast<unsigned long long>(-(n + 1)) + 1ULL
                               : static_cast<unsigned long long>(n);
    std::string out;
    while (v > 0) {
        out.push_back(chars[v % 36]);
        v /= 36;
    }
    if (neg) out.push_back('-');
    std::string rev(out.rbegin(), out.rend());
    return rev;
}

}  // namespace rg::util
