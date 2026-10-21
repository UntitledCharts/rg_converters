#include <cstdio>
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
            switch (v) {
                case 1:
                    return std::string("v1");
                case 2:
                    return std::string("v2");
                default: {
                    if (ver.type() == Json::Type::Int)
                        std::printf("Unknown usc version %lld\n", v);
                    else
                        std::printf("Unknown usc version\n");
                }
            }
        }
    } catch (const JsonParseError&) {
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace rg::converters::usc
