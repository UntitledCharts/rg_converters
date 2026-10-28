#pragma once

#include <optional>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::mmws {

// `format` (".mmws", ".ccmmws" or ".unchmmws") rejects any other variant; empty loads any.
core::Score load_bytes(const std::string& data, const std::string& format = "");

std::optional<std::string> detect(const std::string& data);

struct ExportOptions {
    // .mmws holds one layer: without this, a score with more (note speed ratios count) throws.
    bool strip_layers = false;
};

// The binary bytes written, and the score as written: .mmws strips extended eases/colors, fake
// and damage notes and extended lanes, .ccmmws strips fake notes. Note speed ratios become layers.
core::ExportResult export_bytes(const core::Score& score, const std::string& format,
                                const ExportOptions& opts = {});

}  // namespace rg::converters::mmws
