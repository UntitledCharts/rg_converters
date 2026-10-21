#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::usc {

// load(fp): fp text is JSON; parse and build a Score.
core::Score load_text(const std::string& json_text);

std::optional<std::string> detect(const std::string& data);

// export(path, score, minified=True) -> the JSON text written. Mutates score like Python
// (inserts a default Bpm when none is present).
std::string dumps(core::Score& score, bool minified = true);

}  // namespace rg::converters::usc
