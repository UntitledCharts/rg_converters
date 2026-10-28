#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::chart_cyanvas {

struct ImportOptions {
    // Anything in the level data the exporter does not produce (an archetype, a data field, a
    // value) is an error; with false it is skipped or mapped to the closest thing a Score holds.
    bool error_on_unsupported_feature = true;
};

// Every unsupported thing found, one per line (Python: UnsupportedFeatureError, a ValueError).
using core::UnsupportedFeatureError;

// Chart Cyanvas LevelData (sonolus-pjsekai-engine-extended), as json text or gzipped json: the
// reverse of export_bytes.
core::Score load_text(const std::string& data, const ImportOptions& opts = {});

struct ExportOptions {
    bool compressed = true;
};

// The bytes written, and the score as written (fake notes deleted, note speed ratios as layers).
core::ExportResult export_bytes(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::leveldata::chart_cyanvas
