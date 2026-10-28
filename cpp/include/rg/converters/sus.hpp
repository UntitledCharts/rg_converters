#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::sus {

struct ImportOptions {
    // Note speed ratios in the file are not loaded (every note gets 1).
    bool skip_note_speed_ratios = false;
};

core::Score loads(const std::string& data, const ImportOptions& opts = {});

// detect: returns "" if the data looks like sus, else nullopt.
std::optional<std::string> detect(const std::string& data);

struct ExportOptions {
    bool allow_layers = false;
    bool allow_damage = false;
    bool allow_note_speed_ratios = false;
    bool allow_measure_extensions = false;
    bool skip_shift = false;
};

// The sus text for a score (what export() writes to a file), and the score as written.
core::ExportResult dumps(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::sus
