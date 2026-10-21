#pragma once

#include <optional>
#include <string>

#include "rg/util/json.hpp"

namespace rg::converters::leveldata {

std::optional<std::string> detect_parsed(const util::Json& level_data);
std::optional<std::string> detect(const std::string& data, bool skip_gzip = false,
                                  bool skip_json = false);

}  // namespace rg::converters::leveldata
