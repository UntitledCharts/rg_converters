#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace rg::util {

// Shortest round-trip decimal split into sign, integer digit string (no dot, no
// leading/trailing zeros), and the scientific exponent E such that the value is
// d[0].d[1..] * 10^E. Produced by std::to_chars (shortest), which matches both
// Python's repr (for double) and numpy's dragon4 unique (for float) digit choice.
struct DecimalParts {
    bool negative = false;
    std::string digits;  // e.g. "16777216"
    int exp = 0;         // scientific exponent E
    bool is_zero = false;
};

template <class T>
inline DecimalParts decompose_shortest(T value) {
    DecimalParts p;
    if (std::signbit(value)) {
        p.negative = true;
        value = -value;
    }
    if (value == T(0)) {
        p.is_zero = true;
        p.digits = "0";
        p.exp = 0;
        return p;
    }
    std::array<char, 64> buf{};
    auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value,
                             std::chars_format::scientific);
    std::string s(buf.data(), res.ptr);
    // s looks like "d[.ddd]e[+-]XX"
    std::size_t epos = s.find('e');
    std::string mant = s.substr(0, epos);
    p.exp = std::stoi(s.substr(epos + 1));
    std::string digs;
    for (char c : mant) {
        if (c >= '0' && c <= '9') digs.push_back(c);
    }
    // strip trailing zeros (shortest form should have none, but be safe)
    std::size_t last = digs.find_last_not_of('0');
    if (last == std::string::npos) {
        digs = "0";
    } else {
        digs.erase(last + 1);
    }
    p.digits = digs;
    return p;
}

inline std::string format_fixed(const DecimalParts& p) {
    // decpt = number of digits before the decimal point
    int decpt = p.exp + 1;
    int ndig = static_cast<int>(p.digits.size());
    std::string out;
    if (decpt <= 0) {
        out = "0.";
        out.append(static_cast<std::size_t>(-decpt), '0');
        out += p.digits;
    } else if (decpt >= ndig) {
        out = p.digits;
        out.append(static_cast<std::size_t>(decpt - ndig), '0');
        out += ".0";
    } else {
        out = p.digits.substr(0, static_cast<std::size_t>(decpt));
        out += ".";
        out += p.digits.substr(static_cast<std::size_t>(decpt));
    }
    return out;
}

inline std::string format_scientific(const DecimalParts& p) {
    std::string out;
    out += p.digits[0];
    if (p.digits.size() > 1) {
        out += ".";
        out += p.digits.substr(1);
    }
    out += "e";
    int e = p.exp;
    if (e < 0) {
        out += "-";
        e = -e;
    } else {
        out += "+";
    }
    std::string es = std::to_string(e);
    if (es.size() < 2) es = std::string(2 - es.size(), '0') + es;
    out += es;
    return out;
}

// Python repr(float) / json.dumps(float) for a finite double.
inline std::string py_float_repr(double x) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    DecimalParts p = decompose_shortest<double>(x);
    std::string body;
    if (p.is_zero) {
        body = "0.0";
    } else {
        bool sci = (p.exp <= -5) || (p.exp >= 16);
        body = sci ? format_scientific(p) : format_fixed(p);
    }
    return p.negative ? "-" + body : body;
}

// str(np.float32(x)) for a finite value; input taken as the float32 value.
inline std::string np_float32_str(float x) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    DecimalParts p = decompose_shortest<float>(x);
    std::string body;
    if (p.is_zero) {
        body = "0.0";
    } else {
        bool sci = (p.exp >= 6) || (p.exp <= -5) ||
                   (p.exp == -4 && p.digits == "1");
        body = sci ? format_scientific(p) : format_fixed(p);
    }
    return p.negative ? "-" + body : body;
}

}  // namespace rg::util
