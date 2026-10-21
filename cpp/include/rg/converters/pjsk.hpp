#pragma once

#include <string>

#include "rg/core/score.hpp"
#include "rg/util/json.hpp"

namespace rg::converters::pjsk {

util::Json load_raw(const std::string& raw);
core::Score load_bytes(const std::string& raw);
bool detect(const std::string& data);
// export(path, score, music_id) -> the base64 bytes written to the file.
std::string export_bytes(core::Score& score, long long music_id);

}  // namespace rg::converters::pjsk
