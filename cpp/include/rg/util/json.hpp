#pragma once

#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rg/util/pynum.hpp"

namespace rg::util {

// A JSON value that preserves Python semantics needed for byte-exact json.dumps:
// insertion-ordered objects, and a strict int-vs-float distinction. 16 bytes: strings, arrays and
// objects live on the heap.
class Json {
public:
    enum class Type { Null, Bool, Int, Float, Str, Array, Object };

    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    Json() noexcept : type_(Type::Null), v_{.i = 0} {}
    Json(std::nullptr_t) noexcept : Json() {}
    Json(bool b) noexcept : type_(Type::Bool), v_{.b = b} {}
    Json(int v) noexcept : type_(Type::Int), v_{.i = v} {}
    Json(long v) noexcept : type_(Type::Int), v_{.i = v} {}
    Json(long long v) noexcept : type_(Type::Int), v_{.i = v} {}
    Json(unsigned v) noexcept : type_(Type::Int), v_{.i = static_cast<long long>(v)} {}
    Json(unsigned long v) noexcept : type_(Type::Int), v_{.i = static_cast<long long>(v)} {}
    Json(unsigned long long v) noexcept : type_(Type::Int), v_{.i = static_cast<long long>(v)} {}
    Json(double v) noexcept : type_(Type::Float), v_{.f = v} {}
    Json(const char* s) : type_(Type::Str), v_{.s = new Str{s}} {}
    Json(std::string s) : type_(Type::Str), v_{.s = new Str{std::move(s)}} {}

    Json(const Json& other) : type_(other.type_), v_(other.v_) {
        switch (type_) {
            case Type::Str: v_.s->refs.fetch_add(1, std::memory_order_relaxed); break;
            case Type::Array: v_.a = new Array(*other.v_.a); break;
            case Type::Object: v_.o = new Object(*other.v_.o); break;
            default: break;
        }
    }
    Json(Json&& other) noexcept : type_(other.type_), v_(other.v_) {
        other.type_ = Type::Null;
        other.v_.i = 0;
    }
    Json& operator=(const Json& other) {
        if (this != &other) {
            Json copy(other);
            swap(copy);
        }
        return *this;
    }
    Json& operator=(Json&& other) noexcept {
        Json taken(std::move(other));
        swap(taken);
        return *this;
    }
    ~Json() {
        switch (type_) {
            case Type::Str: release(v_.s); break;
            case Type::Array: delete v_.a; break;
            case Type::Object: delete v_.o; break;
            default: break;
        }
    }

    static Json array() {
        Json j;
        j.v_.a = new Array();
        j.type_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.v_.o = new Object();
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
    void push_back(Json v) { ensure_array().push_back(std::move(v)); }
    Array& arr() { return ensure_array(); }
    const Array& arr() const { return type_ == Type::Array ? *v_.a : empty<Array>(); }

    // Object ops (insertion order preserved; set appends if key absent)
    Json& operator[](const std::string& key) {
        Object& obj = ensure_object();
        for (auto& kv : obj)
            if (kv.first == key) return kv.second;
        obj.emplace_back(key, Json());
        return obj.back().second;
    }
    void set(const std::string& key, Json v) { (*this)[key] = std::move(v); }
    bool contains(const std::string& key) const { return find(key) != nullptr; }
    const Json* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        for (auto& kv : *v_.o)
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
        if (type_ != Type::Object) return;
        for (auto it = v_.o->begin(); it != v_.o->end(); ++it)
            if (it->first == key) {
                v_.o->erase(it);
                return;
            }
    }
    Object& items() { return ensure_object(); }
    const Object& items() const { return type_ == Type::Object ? *v_.o : empty<Object>(); }

    // Accessors
    bool as_bool() const { return type_ == Type::Bool && v_.b; }
    long long as_int() const { return type_ == Type::Int ? v_.i : 0; }
    // a number of either kind, as Python's float() takes it (an int's value, not 0)
    double as_float() const {
        if (type_ == Type::Int) return static_cast<double>(v_.i);
        if (type_ == Type::Bool) return v_.b ? 1.0 : 0.0;
        if (type_ == Type::Float) return v_.f;
        return 0.0;
    }
    const std::string& as_str() const { return type_ == Type::Str ? v_.s->text : empty<std::string>(); }

    std::string dump(int indent = -1, bool ensure_ascii = true) const {
        std::string out;
        dump_to(out, indent, ensure_ascii, 0);
        return out;
    }

private:
    // a string's text, shared by the copies of the value and by the equal strings one parse made
    struct Str {
        std::string text;
        std::atomic<std::size_t> refs{1};
    };
    union Value {
        bool b;
        long long i;
        double f;
        Str* s;
        Array* a;
        Object* o;
    };
    Type type_;
    Value v_;

    friend class JsonParser;
    friend class JsonWriter;

    template <class T>
    static const T& empty() {
        static const T value;
        return value;
    }

    static void release(Str* s) noexcept {
        if (s->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete s;
    }

    void swap(Json& other) noexcept {
        std::swap(type_, other.type_);
        std::swap(v_, other.v_);
    }

    Array& ensure_array() {
        if (type_ == Type::Null) {
            v_.a = new Array();
            type_ = Type::Array;
        }
        if (type_ != Type::Array) throw std::runtime_error("Json type mismatch");
        return *v_.a;
    }
    Object& ensure_object() {
        if (type_ == Type::Null) {
            v_.o = new Object();
            type_ = Type::Object;
        }
        if (type_ != Type::Object) throw std::runtime_error("Json type mismatch");
        return *v_.o;
    }

    void dump_to(std::string& out, int indent, bool ascii, int level) const {
        if (indent >= 0) {
            dump_indented(out, indent, level, ascii);
        } else {
            dump_compact(out, ascii);
        }
    }

    void append_number(std::string& out) const {
        if (type_ == Type::Int) {
            char buf[24];
            auto res = std::to_chars(buf, buf + sizeof buf, v_.i);
            out.append(buf, res.ptr);
            return;
        }
        double v = v_.f;
        if (v != v)
            out += "NaN";
        else if (v > 0 && v * 0 != 0)
            out += "Infinity";
        else if (v < 0 && v * 0 != 0)
            out += "-Infinity";
        else
            out += py_float_repr(v);
    }

    static void encode_string(std::string& out, std::string_view s, bool ascii) {
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
                out += v_.b ? "true" : "false";
                break;
            case Type::Int:
            case Type::Float:
                append_number(out);
                break;
            case Type::Str:
                encode_string(out, v_.s->text, ascii);
                break;
            case Type::Array: {
                out.push_back('[');
                bool first = true;
                for (auto& e : *v_.a) {
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
                for (auto& kv : *v_.o) {
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

    void dump_indented(std::string& out, int indent, int level, bool ascii) const {
        switch (type_) {
            case Type::Null:
                out += "null";
                break;
            case Type::Bool:
                out += v_.b ? "true" : "false";
                break;
            case Type::Int:
            case Type::Float:
                append_number(out);
                break;
            case Type::Str:
                encode_string(out, v_.s->text, ascii);
                break;
            case Type::Array: {
                if (v_.a->empty()) {
                    out += "[]";
                    break;
                }
                out.push_back('[');
                std::size_t pad = static_cast<std::size_t>((level + 1) * indent);
                bool first = true;
                for (auto& e : *v_.a) {
                    if (!first) out.push_back(',');
                    first = false;
                    out.push_back('\n');
                    out.append(pad, ' ');
                    e.dump_indented(out, indent, level + 1, ascii);
                }
                out.push_back('\n');
                out.append(static_cast<std::size_t>(level * indent), ' ');
                out.push_back(']');
                break;
            }
            case Type::Object: {
                if (v_.o->empty()) {
                    out += "{}";
                    break;
                }
                out.push_back('{');
                std::size_t pad = static_cast<std::size_t>((level + 1) * indent);
                bool first = true;
                for (auto& kv : *v_.o) {
                    if (!first) out.push_back(',');
                    first = false;
                    out.push_back('\n');
                    out.append(pad, ' ');
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

// Writes json piece by piece, laid out exactly as Json::dump lays out the same value, for json too
// big to build whole first. Inside an object each value follows its key().
class JsonWriter {
public:
    explicit JsonWriter(std::string& out, int indent = -1, bool ensure_ascii = true)
        : out_(out), indent_(indent), ascii_(ensure_ascii) {}

    void begin_object() { open('}'); }
    void begin_array() { open(']'); }
    // closes the innermost object or array
    void end() {
        Open closed = open_.back();
        open_.pop_back();
        if (closed.items && indent_ >= 0) newline();
        out_.push_back(closed.close);
    }
    void key(std::string_view k) {
        item();
        Json::encode_string(out_, k, ascii_);
        out_ += indent_ >= 0 ? ": " : ":";
        keyed_ = true;
    }
    void value(const Json& v) {
        item();
        v.dump_to(out_, indent_, ascii_, static_cast<int>(open_.size()));
    }
    void string(std::string_view s) {
        item();
        Json::encode_string(out_, s, ascii_);
    }

private:
    struct Open {
        char close;
        std::size_t items;
    };
    std::string& out_;
    int indent_;
    bool ascii_;
    bool keyed_ = false;
    std::vector<Open> open_;

    void open(char close) {
        item();
        out_.push_back(close == '}' ? '{' : '[');
        open_.push_back({close, 0});
    }
    // what goes before the next value (or key) in the innermost object or array
    void item() {
        if (keyed_) {
            keyed_ = false;
            return;
        }
        if (open_.empty()) return;
        if (open_.back().items++) out_.push_back(',');
        if (indent_ >= 0) newline();
    }
    void newline() {
        out_.push_back('\n');
        out_.append(open_.size() * static_cast<std::size_t>(indent_), ' ');
    }
};

class JsonParseError : public std::runtime_error {
public:
    explicit JsonParseError(const std::string& m) : std::runtime_error(m) {}
};

class JsonParser {
public:
    static constexpr std::size_t kMaxDepth = 512;

    explicit JsonParser(std::string_view s) : s_(s) {}
    ~JsonParser() {
        for (auto& kv : strings_) Json::release(kv.second);
    }
    JsonParser(const JsonParser&) = delete;
    JsonParser& operator=(const JsonParser&) = delete;

    Json parse() {
        skip_ws();
        Json v = parse_value();
        skip_ws();
        if (pos_ != s_.size()) throw JsonParseError("Extra data");
        return v;
    }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    // the items of the objects and arrays being parsed, so each is made at its exact size
    Json::Object members_;
    Json::Array elements_;
    // the short strings met so far, which equal strings share
    std::unordered_map<std::string_view, Json::Str*> strings_;

    class DepthGuard {
    public:
        explicit DepthGuard(JsonParser& p) : p_(p) {
            if (++p_.depth_ > kMaxDepth) {
                --p_.depth_;
                p_.err("Maximum nesting depth exceeded");
            }
        }
        ~DepthGuard() { --p_.depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;

    private:
        JsonParser& p_;
    };

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
                return string_value(parse_string());
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
        DepthGuard guard(*this);
        ++pos_;  // {
        Json obj = Json::object();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return obj;
        }
        std::size_t first = members_.size();
        while (true) {
            skip_ws();
            if (peek() != '"') err("Expecting property name");
            std::string key = parse_string();
            skip_ws();
            if (peek() != ':') err("Expecting ':'");
            ++pos_;
            Json val = parse_value();
            members_.emplace_back(std::move(key), std::move(val));
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
        auto begin = members_.begin() + static_cast<std::ptrdiff_t>(first);
        obj.items().assign(std::make_move_iterator(begin), std::make_move_iterator(members_.end()));
        members_.erase(begin, members_.end());
        return obj;
    }

    Json parse_array() {
        DepthGuard guard(*this);
        ++pos_;  // [
        Json arr = Json::array();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return arr;
        }
        std::size_t first = elements_.size();
        while (true) {
            Json val = parse_value();
            elements_.push_back(std::move(val));
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
        auto begin = elements_.begin() + static_cast<std::ptrdiff_t>(first);
        arr.arr().assign(std::make_move_iterator(begin), std::make_move_iterator(elements_.end()));
        elements_.erase(begin, elements_.end());
        return arr;
    }

    Json string_value(std::string s) {
        if (s.size() > 32) return Json(std::move(s));
        Json j;
        if (auto found = strings_.find(s); found != strings_.end()) {
            found->second->refs.fetch_add(1, std::memory_order_relaxed);
            j.v_.s = found->second;
            j.type_ = Json::Type::Str;
            return j;
        }
        j = Json(std::move(s));
        strings_.emplace(j.v_.s->text, j.v_.s);
        j.v_.s->refs.fetch_add(1, std::memory_order_relaxed);
        return j;
    }

    std::string parse_string() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            std::size_t run = pos_;
            while (run < s_.size() && s_[run] != '"' && s_[run] != '\\') ++run;
            out.append(s_.data() + pos_, run - pos_);
            pos_ = run;
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
        std::string_view num = s_.substr(start, pos_ - start);
        if (!is_float) {
            long long v = 0;
            auto res = std::from_chars(num.data(), num.data() + num.size(), v);
            if (res.ec == std::errc() && res.ptr == num.data() + num.size()) return Json(v);
        }
        return Json(to_double(num));
    }

    // strtod on the number's text (an int too big for a long long becomes a float)
    static double to_double(std::string_view num) {
        char buf[64];
        if (num.size() < sizeof buf) {
            std::memcpy(buf, num.data(), num.size());
            buf[num.size()] = '\0';
            return std::strtod(buf, nullptr);
        }
        return std::strtod(std::string(num).c_str(), nullptr);
    }
};

inline Json json_parse(std::string_view s) { return JsonParser(s).parse(); }

}  // namespace rg::util
