#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rg/core/model.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::mmws {

// ==== Binary IO ====
class BinReader {
public:
    explicit BinReader(std::string data) : data_(std::move(data)) {}

    std::string read_cstr() {
        std::string out;
        while (pos_ < data_.size()) {
            char c = data_[pos_++];
            if (c == '\0') break;
            out.push_back(c);
        }
        return out;
    }

    long long read_int(int bit_size = 32, bool sign = false) {
        int nbytes = bit_size / 8;
        std::uint64_t v = 0;
        for (int i = 0; i < nbytes; ++i)
            if (pos_ + static_cast<std::size_t>(i) < data_.size())
                v |= static_cast<std::uint64_t>(static_cast<unsigned char>(data_[pos_ + i]))
                     << (8 * i);
        pos_ += static_cast<std::size_t>(nbytes);
        if (sign) {
            // sign-extend from bit_size
            if (bit_size < 64 && (v & (1ULL << (bit_size - 1))))
                v |= ~((1ULL << bit_size) - 1);
            return static_cast<long long>(v);
        }
        return static_cast<long long>(v);
    }

    double read_float(int bit_size = 32) {
        int n = bit_size / 8;
        char tmp[8] = {0};
        for (int i = 0; i < n; ++i)
            if (pos_ + static_cast<std::size_t>(i) < data_.size()) tmp[i] = data_[pos_ + i];
        pos_ += static_cast<std::size_t>(n);
        if (bit_size == 32) {
            float f;
            std::memcpy(&f, tmp, 4);
            return static_cast<double>(f);
        }
        double d;
        std::memcpy(&d, tmp, 8);
        return d;
    }

    void seek_set(std::size_t off) { pos_ = off; }
    void seek_cur(long long off) { pos_ = static_cast<std::size_t>(static_cast<long long>(pos_) + off); }
    std::size_t tell() const { return pos_; }

private:
    std::string data_;
    std::size_t pos_ = 0;
};

class BinWriter {
public:
    std::string buf;

    void write_bytes(const char* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            if (pos_ < buf.size())
                buf[pos_] = p[i];
            else
                buf.push_back(p[i]);
            ++pos_;
        }
    }

    void write_cstr(const std::string& value) {
        if (value.find('\0') != std::string::npos)
            throw std::runtime_error("C-string cannot contain null bytes");
        write_bytes(value.data(), value.size());
        char z = '\0';
        write_bytes(&z, 1);
    }

    void fill_zero(std::size_t byte_count) {
        for (std::size_t i = 0; i < byte_count; ++i) {
            char z = '\0';
            write_bytes(&z, 1);
        }
    }

    void write_int(long long value, int bit_size = 32, bool /*sign*/ = false) {
        int nbytes = bit_size / 8;
        std::uint64_t v = static_cast<std::uint64_t>(value);
        char bytes[8];
        for (int i = 0; i < nbytes; ++i) bytes[i] = static_cast<char>((v >> (8 * i)) & 0xff);
        write_bytes(bytes, static_cast<std::size_t>(nbytes));
    }

    void write_float(double value, int bit_size = 32) {
        if (bit_size == 64) {
            char b[8];
            std::memcpy(b, &value, 8);
            write_bytes(b, 8);
        } else {
            float f = static_cast<float>(value);
            char b[4];
            std::memcpy(b, &f, 4);
            write_bytes(b, 4);
        }
    }

    std::size_t tell() const { return pos_; }
    void seek_set(std::size_t off) { pos_ = off; }

private:
    std::size_t pos_ = 0;
};

// ==== Details about mmw ====
namespace Signature {
inline const std::string MikuMikuWorld = "MMWS";
inline const std::string MikuMikuWorld4ChartCyanvas = "CCMMWS";
inline const std::string MikuMikuWorld4UntitledChart = "UCMMWS";
}  // namespace Signature

struct Version {
    bool has_skill_fever, has_jacket, has_address, has_hispeed, has_guideNote;
    bool has_damageNote, has_laneExtension, has_fadeType, has_guideColor, has_layers;
    bool has_waypoints, has_floatLaneWidth, has_dummyNote;
    long long value;

    Version(long long uc_version = 1, long long cc_version = 6, long long version = 4) {
        has_skill_fever = version >= 2;
        has_jacket = version >= 2;
        has_address = version >= 3;
        has_hispeed = version >= 3;
        has_guideNote = version >= 4;
        has_damageNote = cc_version >= 1;
        has_laneExtension = cc_version >= 1;
        has_fadeType = cc_version >= 2;
        has_guideColor = cc_version >= 3;
        has_layers = cc_version >= 4;
        has_waypoints = cc_version >= 5;
        has_floatLaneWidth = cc_version >= 6;
        has_dummyNote = cc_version >= 6 && uc_version >= 1;
        if (uc_version)
            value = uc_version;
        else if (cc_version)
            value = (version & 0xffff) | ((cc_version & 0xffff) << 16);
        else
            value = version;
    }
};

enum NoteFlag { NF_NONE = 0, NOTE_CRITICAL = 1, NOTE_FRICTION = 2, NOTE_DUMMY = 4 };
enum HoldFlag {
    HF_NONE = 0,
    HOLD_START_HIDDEN = 1,
    HOLD_END_HIDDEN = 2,
    HOLD_GUIDE = 4,
    HOLD_FAKE = 8
};
enum FlickType { FLICK_NONE = 0, FLICK_DEFAULT = 1, FLICK_LEFT = 2, FLICK_RIGHT = 3 };
enum EaseType { E_LINEAR = 0, E_EASEIN = 1, E_EASEOUT = 2, E_EASEINOUT = 3, E_EASEOUTIN = 4 };
enum GuideColorE {
    GC_NEUTRAL = 0, GC_RED, GC_GREEN, GC_BLUE, GC_YELLOW, GC_PURPLE, GC_CYAN, GC_BLACK
};
enum FadeTypeE { FT_OUT = 0, FT_NONE = 1, FT_IN = 2 };

// ==== Conversion ====
inline double tick_to_beat(long long tick, long long ticks_per_beat = 480) {
    return util::py_round(static_cast<double>(tick) / ticks_per_beat, 6);
}
inline long long beat_to_tick(double beat, long long ticks_per_beat = 480) {
    return util::py_round_int(beat * ticks_per_beat);
}
inline double to_usc_lane(double mmw_lane, double width) {
    return mmw_lane - 6 + width / 2;
}
inline double to_mmw_lane_f(double usc_lane, double size) { return usc_lane + 6 - size; }
inline long long to_mmw_lane_i(double usc_lane, double size) {
    return util::py_round_int(usc_lane + 6 - size);
}
inline double width_to_size(double width) { return width / 2; }
inline double size_to_width_f(double size) { return size * 2; }
inline long long size_to_width_i(double size) { return util::py_round_int(size * 2); }

inline std::optional<std::string> flick_to_direction(long long flick_type) {
    switch (flick_type) {
        case FLICK_NONE: return std::nullopt;
        case FLICK_DEFAULT: return std::string("up");
        case FLICK_LEFT: return std::string("left");
        case FLICK_RIGHT: return std::string("right");
        default: throw std::runtime_error("Unknown flick type");
    }
}
inline long long direction_to_flick(const std::optional<std::string>& direction) {
    if (!direction.has_value()) return FLICK_NONE;
    if (*direction == "up") return FLICK_DEFAULT;
    if (*direction == "left") return FLICK_LEFT;
    if (*direction == "right") return FLICK_RIGHT;
    throw std::invalid_argument("Unknown flick direction '" + *direction + "'");
}
inline std::string ease_num_to_ease(long long n) {
    switch (n) {
        case E_LINEAR: return "linear";
        case E_EASEIN: return "in";
        case E_EASEOUT: return "out";
        case E_EASEINOUT: return "inout";
        case E_EASEOUTIN: return "outin";
        default: throw std::runtime_error("Unknown ease type");
    }
}
inline long long ease_to_ease_num(const std::string& ease) {
    if (ease == "linear") return E_LINEAR;
    if (ease == "in") return E_EASEIN;
    if (ease == "out") return E_EASEOUT;
    if (ease == "inout") return E_EASEINOUT;
    if (ease == "outin") return E_EASEOUTIN;
    throw std::invalid_argument("Unknown ease '" + ease + "'");
}
inline std::string guide_color_to_color(long long c) {
    static const char* names[] = {"neutral", "red",    "green", "blue",
                                  "yellow",  "purple", "cyan",  "black"};
    if (c < 0 || c > 7) throw std::runtime_error("Unknown guide color index");
    return names[c];
}
inline long long color_to_guide_color(const std::string& color) {
    if (color == "neutral") return 0;
    if (color == "red") return 1;
    if (color == "green") return 2;
    if (color == "blue") return 3;
    if (color == "yellow") return 4;
    if (color == "purple") return 5;
    if (color == "cyan") return 6;
    if (color == "black") return 7;
    throw std::invalid_argument("Unknown guide color '" + color + "'");
}
inline std::string fade_type_to_fade(long long f) {
    if (f == 0) return "out";
    if (f == 1) return "none";
    if (f == 2) return "in";
    throw std::runtime_error("Unknown fade type index");
}
inline long long fade_to_fade_type(const std::string& fade) {
    if (fade == "out") return 0;
    if (fade == "none") return 1;
    if (fade == "in") return 2;
    throw std::invalid_argument("Unknown guide fade '" + fade + "'");
}

// NoteGroups: group notes by Kind preserving insertion order.
class NoteGroups {
public:
    explicit NoteGroups(const std::vector<core::NodePtr>& notes) {
        for (auto& n : notes) groups_[n->kind].push_back(n);
    }
    const std::vector<core::NodePtr>& by(core::Kind k) const {
        static const std::vector<core::NodePtr> empty;
        auto it = groups_.find(k);
        return it != groups_.end() ? it->second : empty;
    }

private:
    std::map<core::Kind, std::vector<core::NodePtr>> groups_;
};

}  // namespace rg::converters::mmws
