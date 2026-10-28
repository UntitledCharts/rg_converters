#pragma once

#include <string>

#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

// Reading and writing LevelData json, shared by the LevelData loaders and exporters.
namespace rg::converters::leveldata::detail {

// The level data json, gzipped or not, with or without a UTF-8 BOM.
inline util::Json parse_level_data(const std::string& data) {
    std::string text = data;
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f &&
        static_cast<unsigned char>(text[1]) == 0x8b)
        text = util::gzip_decompress(text);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return util::json_parse(text);
}

// The level data as written: indented json, or minified json gzipped.
inline std::string dump_level_data(const util::Json& level_data, bool as_compressed) {
    if (!as_compressed) return level_data.dump(4, false);
    return util::gzip_compress(level_data.dump(-1, false), 0, 9);
}

}  // namespace rg::converters::leveldata::detail
