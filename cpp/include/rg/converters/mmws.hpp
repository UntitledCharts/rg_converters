#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::mmws {

core::Score load_bytes(const std::string& data);

std::optional<std::string> detect(const std::string& data);

// export(path, score, format=...) -> the binary bytes written. Mutates score (like Python)
// for .mmws (ease/color/fake/damage/extended-lane stripping) and .ccmmws (fake stripping).
std::string export_bytes(core::Score& score, const std::string& format);

}  // namespace rg::converters::mmws
