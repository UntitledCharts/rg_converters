#pragma once

#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::next_sekai {

struct ExportOptions {
    bool as_compressed = true;
    bool smooth_guide_fade = false;
    bool use_guide_layer = false;
};

// load() raises NotImplementedError in Python.
[[noreturn]] core::Score load_text(const std::string& data);

// Returns the bytes written (json, optionally gzipped).
std::string export_bytes(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::leveldata::next_sekai
