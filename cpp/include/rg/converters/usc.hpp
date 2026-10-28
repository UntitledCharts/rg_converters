#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::usc {

// load(fp): fp text is JSON; parse and build a Score.
core::Score load_text(const std::string& json_text);

std::optional<std::string> detect(const std::string& data);

// The JSON text written, and the score as written (fake notes deleted, a default Bpm added when
// none is present).
struct ExportOptions {
    bool minified = true;
};

core::ExportResult dumps(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::usc
