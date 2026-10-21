#pragma once

#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::chart_cyanvas {

// Automatically deletes fake notes. Mutates score. Returns the bytes written.
// (The chart_cyanvas LOADER remains the legacy Python implementation; only export is ported.)
std::string export_bytes(core::Score& score, bool as_compressed = true);

}  // namespace rg::converters::leveldata::chart_cyanvas
