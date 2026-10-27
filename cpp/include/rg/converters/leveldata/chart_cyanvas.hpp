#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::chart_cyanvas {

struct LoadOptions {
    // Anything in the level data the exporter does not produce (an archetype, a data field, a
    // value) is an error; with false it is skipped or mapped to the closest thing a Score holds.
    bool error_on_unsupported_feature = true;
};

// Every unsupported thing found, one per line (Python: UnsupportedFeatureError, a ValueError).
struct UnsupportedFeatureError : std::runtime_error {
    explicit UnsupportedFeatureError(const std::string& message) : std::runtime_error(message) {}
};

// Chart Cyanvas LevelData (sonolus-pjsekai-engine-extended), as json text or gzipped json: the
// reverse of export_bytes.
core::Score load_text(const std::string& data, const LoadOptions& opts = {});

// Automatically deletes fake notes. Mutates score. Returns the bytes written.
std::string export_bytes(core::Score& score, bool as_compressed = true);

}  // namespace rg::converters::leveldata::chart_cyanvas
