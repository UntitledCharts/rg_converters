#pragma once

#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::yume_sus {

core::Score loads(const std::string& data);

struct ExportOptions {
    bool skip_shift = false;
    // Yume plays one layer: without this, a score with more (note speed ratios count) throws.
    bool strip_layers = false;
};

// The Yume sus text for a score (what export() writes to a file), and the score as written.
core::ExportResult dumps(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::yume_sus
