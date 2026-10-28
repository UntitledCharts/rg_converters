#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"
#include "rg/util/json.hpp"

namespace rg::converters::pjsk {

util::Json load_raw(const std::string& raw);
core::Score load_bytes(const std::string& raw);
bool detect(const std::string& data);
struct ExportOptions {
    // Required.
    std::optional<long long> music_id;
    // base64(gzip(json)) as the game stores it; false writes plain json.
    bool compressed = true;
    // pjsk holds one layer: without this, a score with more throws.
    bool strip_layers = false;
};

// The bytes written to the file, and the score as written (fake and damage notes deleted,
// extended eases and guide colours replaced).
core::ExportResult export_bytes(const core::Score& score, const ExportOptions& opts);

}  // namespace rg::converters::pjsk
