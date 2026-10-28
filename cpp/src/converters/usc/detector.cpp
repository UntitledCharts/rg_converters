#include <optional>
#include <string>

#include "rg/converters/usc.hpp"
#include "rg/util/json.hpp"

namespace rg::converters::usc {

using rg::util::Json;
using rg::util::json_parse;
using rg::util::JsonParseError;

std::optional<std::string> detect(const std::string& data) {
    try {
        Json usc = json_parse(data);
        if (usc.contains("usc") && usc.contains("version")) {
            const Json& ver = usc.at("version");
            long long v = ver.type() == Json::Type::Int ? ver.as_int() : -999999;
            if (v == 1) return std::string("v1");
            if (v == 2) return std::string("v2");
        }
    } catch (const JsonParseError&) {
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace rg::converters::usc
