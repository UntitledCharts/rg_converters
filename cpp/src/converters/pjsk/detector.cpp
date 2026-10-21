#include <string>

#include "rg/converters/pjsk.hpp"
#include "rg/util/base64.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

namespace rg::converters::pjsk {

using rg::util::Json;

bool detect(const std::string& data) {
    std::string decoded;
    try {
        decoded = util::b64decode(data, true);
    } catch (...) {
        return false;
    }

    if (decoded.size() < 2 || static_cast<unsigned char>(decoded[0]) != 0x1f ||
        static_cast<unsigned char>(decoded[1]) != 0x8b)
        return false;

    Json parsed;
    try {
        parsed = util::json_parse(util::gzip_decompress(decoded));
    } catch (...) {
        return false;
    }

    return parsed.contains("NoteList") && parsed.contains("MusicScoreEventDataList");
}

}  // namespace rg::converters::pjsk
