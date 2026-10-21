#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::sus {

core::Score loads(const std::string& data);

// detect: returns "" if the data looks like sus, else nullopt.
std::optional<std::string> detect(const std::string& data);

struct ExportOptions {
    bool allow_layers = false;
    bool allow_extended_lanes = false;
    bool delete_damage = true;
    bool keep_note_speed_ratios = false;
    bool measure_extensions = false;
    bool skip_shift = false;
};

// Produce the sus text for a score (what export() writes to a file).
std::string dumps(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::sus
