#pragma once

#include <string>
#include <utility>

#include "rg/core/score.hpp"

namespace rg::converters::holodori_sus {

// Returns (score, bezier_tick_count). load/loads in Python return just the score unless
// count_bezier=True, in which case they return (score, count).
std::pair<core::Score, long long> loads(const std::string& data);

}  // namespace rg::converters::holodori_sus
