#pragma once

#include <stdexcept>
#include <string>

#include "rg/core/score.hpp"
#include "rg/util/json.hpp"

// The Score <-> JSON model documented in rg_converters.h.
namespace rg::capi {

struct SchemaError : std::invalid_argument {
    explicit SchemaError(const std::string& message) : std::invalid_argument(message) {}
};

util::Json value_to_json(const core::Value& value);
util::Json format_metadata_to_json(const core::Score& score);

util::Json score_to_json(const core::Score& score);
core::Score score_from_json(const util::Json& json);

}  // namespace rg::capi
