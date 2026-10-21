#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"

namespace rg::converters::bdon {

struct BdonFormatError : std::runtime_error {
    explicit BdonFormatError(const std::string& message) : std::runtime_error(message) {}
};

struct Options {
    bool guide_traces = true;
    bool trace_ends = true;
    bool flick_body_collide = true;
};

core::Score loads(const std::string& data, const Options& options = {});

}  // namespace rg::converters::bdon
