#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace rg::util {

inline bool py_isspace_ch(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// str.splitlines(): split on \n, \r\n, \r (sufficient for the text formats here).
inline std::vector<std::string> splitlines(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0, n = s.size();
    std::size_t start = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\n') {
            out.push_back(s.substr(start, i - start));
            ++i;
            start = i;
        } else if (c == '\r') {
            out.push_back(s.substr(start, i - start));
            ++i;
            if (i < n && s[i] == '\n') ++i;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n) out.push_back(s.substr(start, n - start));
    return out;
}

inline std::string strip(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && py_isspace_ch(s[a])) ++a;
    while (b > a && py_isspace_ch(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string strip_chars(const std::string& s, const std::string& chars) {
    std::size_t a = 0, b = s.size();
    auto in = [&](char c) { return chars.find(c) != std::string::npos; };
    while (a < b && in(s[a])) ++a;
    while (b > a && in(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::string lstrip_chars(const std::string& s, const std::string& chars) {
    std::size_t a = 0;
    auto in = [&](char c) { return chars.find(c) != std::string::npos; };
    while (a < s.size() && in(s[a])) ++a;
    return s.substr(a);
}

inline std::string to_upper(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return r;
}

inline bool startswith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endswith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string replace_all(const std::string& s, const std::string& from,
                               const std::string& to) {
    if (from.empty()) return s;
    std::string out;
    std::size_t pos = 0, p;
    while ((p = s.find(from, pos)) != std::string::npos) {
        out.append(s, pos, p - pos);
        out += to;
        pos = p + from.size();
    }
    out.append(s, pos, std::string::npos);
    return out;
}

// str.split() with no args: split on runs of whitespace, no empty strings.
inline std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && py_isspace_ch(s[i])) ++i;
        if (i >= n) break;
        std::size_t start = i;
        while (i < n && !py_isspace_ch(s[i])) ++i;
        out.push_back(s.substr(start, i - start));
    }
    return out;
}

// str.split(sep): split on a separator string; keeps empty fields (Python semantics).
inline std::vector<std::string> split(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    if (sep.empty()) {
        out.push_back(s);
        return out;
    }
    std::size_t pos = 0, p;
    while ((p = s.find(sep, pos)) != std::string::npos) {
        out.push_back(s.substr(pos, p - pos));
        pos = p + sep.size();
    }
    out.push_back(s.substr(pos));
    return out;
}

inline bool is_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

inline bool is_digit_ch(char c) { return c >= '0' && c <= '9'; }

inline std::string zfill(const std::string& s, std::size_t width) {
    // Python str.zfill: pad with leading zeros after an optional sign.
    if (s.size() >= width) return s;
    std::size_t pad = width - s.size();
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        return s.substr(0, 1) + std::string(pad, '0') + s.substr(1);
    }
    return std::string(pad, '0') + s;
}

// int(s): base-10 parse with Python semantics (strips whitespace, optional sign).
inline long long py_int(const std::string& s) {
    std::string t = strip(s);
    if (t.empty()) throw std::invalid_argument("invalid int");
    return std::stoll(t);
}

// int(c, 36) for a single base-36 digit character.
inline int b36_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    throw std::invalid_argument("invalid base36 digit");
}

// int(s, 36): parse a (possibly multi-char) base-36 integer.
inline long long py_int_base36(const std::string& s) {
    std::string t = strip(s);
    bool neg = false;
    std::size_t i = 0;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
        neg = t[i] == '-';
        ++i;
    }
    if (i >= t.size()) throw std::invalid_argument("invalid base36");
    long long v = 0;
    for (; i < t.size(); ++i) v = v * 36 + b36_digit(t[i]);
    return neg ? -v : v;
}

// float(s): parse with Python semantics.
inline double py_float(const std::string& s) {
    std::string t = strip(s);
    const char* begin = t.c_str();
    char* endp = nullptr;
    double v = std::strtod(begin, &endp);
    if (endp == begin) throw std::invalid_argument("invalid float");
    return v;
}

}  // namespace rg::util
