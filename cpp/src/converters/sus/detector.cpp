#include <optional>
#include <string>

#include "rg/converters/sus.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::sus {

using namespace rg::util;

static bool is_word_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

// TODO: Detect base sus and chcy sus
std::optional<std::string> detect(const std::string& data) {
    bool has_metadata = false;
    bool has_scoredata = false;
    for (auto& raw : splitlines(data)) {
        if (!startswith(raw, "#")) continue;
        std::string line = strip(raw);
        // match r"^#(\w+):\s*(.*)$" : '#', one+ word chars, then ':'
        bool matched = false;
        if (!line.empty() && line[0] == '#') {
            std::size_t i = 1;
            std::size_t start = i;
            while (i < line.size() && is_word_ch(line[i])) ++i;
            if (i > start && i < line.size() && line[i] == ':') matched = true;
        }
        if (matched)
            has_scoredata = true;
        else
            has_metadata = true;
    }
    if (has_metadata && has_scoredata) return std::string("");
    return std::nullopt;
}

}  // namespace rg::converters::sus
