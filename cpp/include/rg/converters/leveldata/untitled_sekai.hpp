#pragma once

#include <string>

#include "rg/core/score.hpp"
#include "rg/util/size_limit.hpp"

namespace rg::converters::leveldata::untitled_sekai {

// load() raises NotImplementedError in Python.
[[noreturn]] core::Score load_text(const std::string& data);

struct ExportOptions {
    bool compressed = true;
    std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE;
};

// The bytes written, and the score as written (fake and damage notes deleted, extended eases
// replaced, note speed ratios as layers).
core::ExportResult export_bytes(const core::Score& score, const ExportOptions& opts = {});

}  // namespace rg::converters::leveldata::untitled_sekai
