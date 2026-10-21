#pragma once

#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::leveldata::untitled_sekai {

// load() raises NotImplementedError in Python.
[[noreturn]] core::Score load_text(const std::string& data);

// Automatically replaces extended eases. Mutates score. Returns the bytes written.
std::string export_bytes(core::Score& score, bool as_compressed = true);

}  // namespace rg::converters::leveldata::untitled_sekai
