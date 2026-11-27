#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

// Reading and writing LevelData json, shared by the LevelData loaders and exporters.
namespace rg::converters::leveldata::detail {

// The level data json, gzipped or not, with or without a UTF-8 BOM.
inline util::Json parse_level_data(const std::string& data, std::size_t max_decompressed_size) {
    std::string inflated;
    std::string_view text = data;
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f &&
        static_cast<unsigned char>(text[1]) == 0x8b) {
        inflated = util::gzip_decompress(data, max_decompressed_size);
        text = inflated;
    }
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    return util::json_parse(text);
}

// The level data as written: indented json, or minified json gzipped as it is made. Built one
// entity at a time (each written to entity(), then finish()), so the whole json is never held.
class LevelDataWriter {
public:
    LevelDataWriter(const util::Json& bgm_offset, bool as_compressed, std::size_t max_decompressed_size)
        : json_(text_, as_compressed ? -1 : 4, false) {
        if (as_compressed) gzip_.emplace(0, 9, max_decompressed_size);
        json_.begin_object();
        json_.key("bgmOffset");
        json_.value(bgm_offset);
        json_.key("entities");
        json_.begin_array();
    }

    util::JsonWriter& entity() {
        if (gzip_ && text_.size() >= kChunk) {
            gzip_->write(text_);
            text_.clear();
        }
        return json_;
    }

    std::string finish() {
        json_.end();
        json_.end();
        if (!gzip_) return std::move(text_);
        gzip_->write(text_);
        return gzip_->finish();
    }

private:
    static constexpr std::size_t kChunk = 1 << 20;
    std::string text_;
    util::JsonWriter json_;
    std::optional<util::GzipWriter> gzip_;
};

}  // namespace rg::converters::leveldata::detail
