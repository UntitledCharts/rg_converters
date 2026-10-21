#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rg/util/pynum.hpp"

namespace rg::util {

// A JSON value that preserves Python semantics needed for byte-exact json.dumps:
// insertion-ordered objects, and a strict int-vs-float distinction.
class Json {
public:
    enum class Type { Null, Bool, Int, Float, Str, Array, Object };

    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() : type_(Type::Null) {}
    Json(std::nullptr_t) : type_(Type::Null) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int v) : type_(Type::Int), int_(v) {}
    Json(long v) : type_(Type::Int), int_(v) {}
    Json(long long v) : type_(Type::Int), int_(v) {}
    Json(unsigned v) : type_(Type::Int), int_(static_cast<long long>(v)) {}
    Json(unsigned long v) : type_(Type::Int), int_(static_cast<long long>(v)) {}
    Json(unsigned long long v) : type_(Type::Int), int_(static_cast<long long>(v)) {}
    Json(double v) : type_(Type::Float), float_(v) {}
    Json(const char* s) : type_(Type::Str), str_(s) {}
    Json(std::string s) : type_(Type::Str), str_(std::move(s)) {}

    static Json array() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }
    // Build an int explicitly (avoids any ambiguity).
    static Json integer(long long v) { return Json(v); }
    static Json floating(double v) { return Json(v); }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }

    // Array ops
    void push_back(Json v) {
        ensure(Type::Array);
        arr_.push_back(std::move(v));
    }
    Array& arr() {
        ensure(Type::Array);
        return arr_;
    }
    const Array& arr() const { return arr_; }

    // Object ops (insertion order preserved; set appends if key absent)
    Json& operator[](const std::string& key) {
        ensure(Type::Object);
        for (auto& kv : obj_)
            if (kv.first == key) return kv.second;
        obj_.emplace_back(key, Json());
        return obj_.back().second;
    }
    void set(const std::string& key, Json v) { (*this)[key] = std::move(v); }
    bool contains(const std::string& key) const {
        if (type_ != Type::Object) return false;
        for (auto& kv : obj_)
            if (kv.first == key) return true;
        return false;
    }
    const Json* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        for (auto& kv : obj_)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Json& at(const std::string& key) const {
        const Json* p = find(key);
        if (!p) throw std::out_of_range("KeyError: " + key);
        return *p;
    }
    // Remove a key if present (Python dict.pop(key, default)).
    void erase(const std::string& key) {
        for (auto it = obj_.begin(); it != obj_.end(); ++it)
            if (it->first == key) {
                obj_.erase(it);
                return;
            }
    }
    Object& items() {
        ensure(Type::Object);
        return obj_;
    }
    const Object& items() const { return obj_; }

    // Accessors
    bool as_bool() const { return bool_; }
    long long as_int() const { return int_; }
    double as_float() const { return float_; }
    const std::string& as_str() const { return str_; }

    std::string dump(int indent = -1, bool ensure_ascii = true) const {
        std::string out;
        if (indent >= 0) {
            dump_indented(out, indent, 0, ensure_ascii);
        } else {
            dump_compact(out, ensure_ascii);
        }
        return out;
    }

private:
    Type type_;
    bool bool_ = false;
    long long int_ = 0;
    double float_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;

    void ensure(Type t) {
        if (type_ == Type::Null) type_ = t;
        if (type_ != t) throw std::runtime_error("Json type mismatch");
    }

    static std::string format_number(const Json& j) {
        if (j.type_ == Type::Int) return std::to_string(j.int_);
        // float
        double v = j.float_;
        if (v != v) return "NaN";
        if (v > 0 && v * 0 != 0) return "Infinity";
        if (v < 0 && v * 0 != 0) return "-Infinity";
        return py_float_repr(v);
    }

    static void encode_string(std::string& out, const std::string& s, bool ascii) {
        out.push_back('"');
        std::size_t i = 0, n = s.size();
        while (i < n) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '"') {
                out += "\\\"";
                ++i;
            } else if (c == '\\') {
                out += "\\\\";
                ++i;
            } else if (c == '\n') {
                out += "\\n";
                ++i;
            } else if (c == '\r') {
                out += "\\r";
                ++i;
            } else if (c == '\t') {
                out += "\\t";
                ++i;
            } else if (c == '\b') {
                out += "\\b";
                ++i;
            } else if (c == '\f') {
                out += "\\f";
                ++i;
            } else if (c < 0x20) {
                append_u(out, c);
                ++i;
            } else if (c < 0x80) {
                out.push_back(static_cast<char>(c));
                ++i;
            } else {
                // multibyte UTF-8
                if (!ascii) {
                    out.push_back(static_cast<char>(c));
                    ++i;
                } else {
                    // decode a UTF-8 code point and emit \uXXXX (surrogate pair if needed)
                    std::uint32_t cp = 0;
                    int len = 0;
                    if ((c & 0xE0) == 0xC0) {
                        cp = c & 0x1F;
                        len = 1;
                    } else if ((c & 0xF0) == 0xE0) {
                        cp = c & 0x0F;
                        len = 2;
                    } else if ((c & 0xF8) == 0xF0) {
                        cp = c & 0x07;
                        len = 3;
                    } else {
                        cp = c;  // invalid; emit as-is codepoint
                        len = 0;
                    }
                    std::size_t j = i + 1;
                    for (int k = 0; k < len && j < n; ++k, ++j) {
                        cp = (cp << 6) | (static_cast<unsigned char>(s[j]) & 0x3F);
                    }
                    i = j;
                    if (cp <= 0xFFFF) {
                        append_u(out, cp);
                    } else {
                        cp -= 0x10000;
                        append_u(out, 0xD800 + (cp >> 10));
                        append_u(out, 0xDC00 + (cp & 0x3FF));
                    }
                }
            }
        }
        out.push_back('"');
    }

    static void append_u(std::string& out, std::uint32_t cp) {
        static const char* hex = "0123456789abcdef";
        out += "\\u";
        out.push_back(hex[(cp >> 12) & 0xF]);
        out.push_back(hex[(cp >> 8) & 0xF]);
        out.push_back(hex[(cp >> 4) & 0xF]);
        out.push_back(hex[cp & 0xF]);
    }

    void dump_compact(std::string& out, bool ascii) const {
        switch (type_) {
            case Type::Null:
                out += "null";
                break;
            case Type::Bool:
                out += bool_ ? "true" : "false";
                break;
            case Type::Int:
            case Type::Float:
                out += format_number(*this);
                break;
            case Type::Str:
                encode_string(out, str_, ascii);
                break;
            case Type::Array: {
                out.push_back('[');
                bool first = true;
                for (auto& e : arr_) {
                    if (!first) out.push_back(',');
                    first = false;
                    e.dump_compact(out, ascii);
                }
                out.push_back(']');
                break;
            }
            case Type::Object: {
                out.push_back('{');
                bool first = true;
                for (auto& kv : obj_) {
                    if (!first) out.push_back(',');
                    first = false;
                    encode_string(out, kv.first, ascii);
                    out.push_back(':');
                    kv.second.dump_compact(out, ascii);
                }
                out.push_back('}');
                break;
            }
        }
    }

    friend class JsonParser;

    void dump_indented(std::string& out, int indent, int level, bool ascii) const {
        switch (type_) {
            case Type::Null:
                out += "null";
                break;
            case Type::Bool:
                out += bool_ ? "true" : "false";
                break;
            case Type::Int:
            case Type::Float:
                out += format_number(*this);
                break;
            case Type::Str:
                encode_string(out, str_, ascii);
                break;
            case Type::Array: {
                if (arr_.empty()) {
                    out += "[]";
                    break;
                }
                out.push_back('[');
                std::string pad((level + 1) * indent, ' ');
                bool first = true;
                for (auto& e : arr_) {
                    if (!first) out.push_back(',');
                    first = false;
                    out.push_back('\n');
                    out += pad;
                    e.dump_indented(out, indent, level + 1, ascii);
                }
                out.push_back('\n');
                out.append(static_cast<std::size_t>(level * indent), ' ');
                out.push_back(']');
                break;
            }
            case Type::Object: {
                if (obj_.empty()) {
                    out += "{}";
                    break;
                }
                out.push_back('{');
                std::string pad((level + 1) * indent, ' ');
                bool first = true;
                for (auto& kv : obj_) {
                    if (!first) out.push_back(',');
                    first = false;
                    out.push_back('\n');
                    out += pad;
                    encode_string(out, kv.first, ascii);
                    out += ": ";
                    kv.second.dump_indented(out, indent, level + 1, ascii);
                }
                out.push_back('\n');
                out.append(static_cast<std::size_t>(level * indent), ' ');
                out.push_back('}');
                break;
            }
        }
    }
};

class JsonParseError : public std::runtime_error {
public:
    explicit JsonParseError(const std::string& m) : std::runtime_error(m) {}
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    Json parse() {
        skip_ws();
        Json v = parse_value();
        skip_ws();
        if (pos_ != s_.size()) throw JsonParseError("Extra data");
        return v;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;

    [[noreturn]] void err(const std::string& m) { throw JsonParseError(m); }

    void skip_ws() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    char peek() {
        if (pos_ >= s_.size()) err("Unexpected end");
        return s_[pos_];
    }

    bool match_lit(const char* lit) {
        std::size_t len = std::strlen(lit);
        if (s_.compare(pos_, len, lit) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    Json parse_value() {
        skip_ws();
        if (pos_ >= s_.size()) err("Expecting value");
        char c = s_[pos_];
        switch (c) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return Json(parse_string());
            case 't':
                if (match_lit("true")) return Json(true);
                err("Expecting value");
            case 'f':
                if (match_lit("false")) return Json(false);
                err("Expecting value");
            case 'n':
                if (match_lit("null")) return Json(nullptr);
                err("Expecting value");
            case 'N':
                if (match_lit("NaN")) return Json(std::nan(""));
                err("Expecting value");
            case 'I':
                if (match_lit("Infinity"))
                    return Json(std::numeric_limits<double>::infinity());
                err("Expecting value");
            default:
                if (c == '-' && s_.compare(pos_, 9, "-Infinity") == 0) {
                    pos_ += 9;
                    return Json(-std::numeric_limits<double>::infinity());
                }
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
                err("Expecting value");
        }
    }

    Json parse_object() {
        ++pos_;  // {
        Json obj = Json::object();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return obj;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') err("Expecting property name");
            std::string key = parse_string();
            skip_ws();
            if (peek() != ':') err("Expecting ':'");
            ++pos_;
            Json val = parse_value();
            obj.items().emplace_back(std::move(key), std::move(val));
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                break;
            }
            err("Expecting ',' or '}'");
        }
        return obj;
    }

    Json parse_array() {
        ++pos_;  // [
        Json arr = Json::array();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return arr;
        }
        while (true) {
            Json val = parse_value();
            arr.push_back(std::move(val));
            skip_ws();
            char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                break;
            }
            err("Expecting ',' or ']'");
        }
        return arr;
    }

    std::string parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) err("Unterminated string");
            char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= s_.size()) err("Unterminated string");
                char e = s_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // high surrogate; look for low
                            if (pos_ + 1 < s_.size() && s_[pos_] == '\\' &&
                                s_[pos_ + 1] == 'u') {
                                pos_ += 2;
                                std::uint32_t lo = parse_hex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) +
                                         (lo - 0xDC00);
                                } else {
                                    append_utf8(out, cp);
                                    cp = lo;
                                }
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        err("Invalid \\escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    std::uint32_t parse_hex4() {
        if (pos_ + 4 > s_.size()) err("Invalid \\uXXXX");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                err("Invalid \\uXXXX");
        }
        return v;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    Json parse_number() {
        std::size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        bool is_float = false;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_float = true;
            ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            is_float = true;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        std::string num = s_.substr(start, pos_ - start);
        if (is_float) {
            return Json(std::strtod(num.c_str(), nullptr));
        }
        try {
            return Json(static_cast<long long>(std::stoll(num)));
        } catch (...) {
            return Json(std::strtod(num.c_str(), nullptr));
        }
    }
};

inline Json json_parse(const std::string& s) { return JsonParser(s).parse(); }

}  // namespace rg::util
