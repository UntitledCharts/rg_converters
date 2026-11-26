#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"
#include "rg/util/json.hpp"
#include "rg/util/size_limit.hpp"

namespace rg::converters::pjsk {

struct ImportOptions {
    std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE;
};

util::Json load_raw(const std::string& raw,
                    std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE);
core::Score load_bytes(const std::string& raw, const ImportOptions& opts = {});
bool detect(const std::string& data, std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE);
struct ExportOptions {
    // Required.
    std::optional<long long> music_id;
    // base64(gzip(json)) as the game stores it; false writes plain json.
    bool compressed = true;
    // pjsk holds one layer: without this, a score with more throws.
    bool strip_layers = false;
    std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE;
};

// The bytes written to the file, and the score as written (fake and damage notes deleted,
// extended eases and guide colours replaced).
core::ExportResult export_bytes(const core::Score& score, const ExportOptions& opts);

}  // namespace rg::converters::pjsk
