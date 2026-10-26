#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::next_sekai {

struct ExportOptions {
    bool as_compressed = true;
    bool smooth_guide_fade = false;
    bool use_guide_layer = false;
};

struct LoadOptions {
    // Anything in the level data the exporter does not produce (an archetype, a data field, a
    // value) is an error; with false it is skipped or mapped to the closest thing a Score holds.
    bool error_on_unsupported_feature = true;
    // A judged note riding a slide or guide (which a Score can't hold) becomes a note of its own,
    // with the line stopping a 480th of a beat before it and starting again a 480th after. Changes
    // the chart's structure, so such a chart does not round-trip; with false it is unsupported.
    bool attempt_attached_note_convert = true;
};

// Every unsupported thing found, one per line (Python: UnsupportedFeatureError, a ValueError).
struct UnsupportedFeatureError : std::runtime_error {
    explicit UnsupportedFeatureError(const std::string& message) : std::runtime_error(message) {}
};

// Next SEKAI / Next RUSH LevelData, as json text or gzipped json: the reverse of export_bytes.
core::Score load_text(const std::string& data, const LoadOptions& opts = {});

// Returns the bytes written (json, optionally gzipped).
std::string export_bytes(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::leveldata::next_sekai
