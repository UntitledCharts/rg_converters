#pragma once

#include <optional>
#include <string>

#include "rg/util/json.hpp"
#include "rg/util/size_limit.hpp"

namespace rg::converters::leveldata {

std::optional<std::string> detect_parsed(const util::Json& level_data);
std::optional<std::string> detect(const std::string& data, bool skip_gzip = false,
                                  bool skip_json = false,
                                  std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE);

}  // namespace rg::converters::leveldata
