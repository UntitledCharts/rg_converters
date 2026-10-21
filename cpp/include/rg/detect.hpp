#pragma once

#include <optional>
#include <string>
#include <utility>

namespace rg {

// Parse the data and determine the format of the score.
// Returns (format, specifier) if detected, else nullopt.
std::optional<std::pair<std::string, std::string>> detect(const std::string& data);

}  // namespace rg
