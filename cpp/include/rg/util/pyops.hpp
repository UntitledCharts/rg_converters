#pragma once

#include <array>
#include <charconv>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace rg::util {

// int(x) for a float: truncate toward zero (matches Python int()).
inline long long py_trunc(double x) { return static_cast<long long>(x); }

// round(x) with no ndigits: nearest integer, ties to even (matches Python round()).
inline long long py_round_int(double x) {
    return static_cast<long long>(std::nearbyint(x));
}

// round(x, ndigits): correctly-rounded decimal rounding, ties to even, returning a
// double -- matches CPython's double_round (via dtoa) for the digit counts used here.
inline double py_round(double x, int ndigits) {
    if (!std::isfinite(x)) return x;
    std::array<char, 64> buf{};
    auto res = std::to_chars(buf.data(), buf.data() + buf.size(), x,
                             std::chars_format::fixed, ndigits);
    std::string s(buf.data(), res.ptr);
    return std::strtod(s.c_str(), nullptr);
}

// a // b for integers: floor division (Python semantics, differs from C++ for
// mixed signs).
inline long long py_floordiv(long long a, long long b) {
    long long q = a / b;
    long long r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

// a % b for integers: result has the sign of b (Python semantics).
inline long long py_mod(long long a, long long b) {
    long long r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

// a // b for floats: floor(a / b) (Python float floor division).
inline double py_floordiv_f(double a, double b) { return std::floor(a / b); }

}  // namespace rg::util
