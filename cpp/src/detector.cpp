#include "rg/detect.hpp"

#include <string>

#include "rg/converters/leveldata/detect.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

namespace rg {

namespace {

bool is_valid_utf8(const std::string& s) {
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80)
                return false;
            if (c < 0xC2) return false;  // overlong
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80)
                return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 3]) & 0xC0) != 0x80)
                return false;
            if (c > 0xF4) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<std::pair<std::string, std::string>> detect(const std::string& data) {
    // Check for formats with binary data and magic number first
    // Gzip data
    if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f &&
        static_cast<unsigned char>(data[1]) == 0x8b) {
        try {
            std::string decompressed = util::gzip_decompress(data);
            util::Json json_data = util::json_parse(decompressed);
            auto format_spec = converters::leveldata::detect_parsed(json_data);
            if (format_spec.has_value())
                return std::make_pair(std::string("lvd"), "compress_" + *format_spec);
        } catch (...) {
        }
    }
    // MMW data
    {
        auto format_spec = converters::mmws::detect(data);
        if (format_spec.has_value())
            return std::make_pair(std::string("mmw"), *format_spec);
    }

    // PJSK internal score (base64 encoded gzip json)
    if (converters::pjsk::detect(data)) return std::make_pair(std::string("pjsk"), std::string(""));

    // Binary dection over
    if (!is_valid_utf8(data)) return std::nullopt;
    // uncompressed Leveldata
    {
        auto format_spec = converters::leveldata::detect(data, true);
        if (format_spec.has_value())
            return std::make_pair(std::string("lvd"), *format_spec);
    }
    // usc
    {
        auto format_spec = converters::usc::detect(data);
        if (format_spec.has_value())
            return std::make_pair(std::string("usc"), *format_spec);
    }
    // sus
    {
        auto format_spec = converters::sus::detect(data);
        if (format_spec.has_value())
            return std::make_pair(std::string("sus"), *format_spec);
    }
    return std::nullopt;
}

}  // namespace rg
