#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"
#include "rg/util/size_limit.hpp"

namespace rg::converters::bdon {

struct BdonFormatError : std::runtime_error {
    explicit BdonFormatError(const std::string& message) : std::runtime_error(message) {}
};

struct ImportOptions {
    bool guide_traces = true;
    bool trace_ends = true;
    bool flick_body_collide = true;
    std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE;
};

core::Score loads(const std::string& data, const ImportOptions& options = {});

}  // namespace rg::converters::bdon
