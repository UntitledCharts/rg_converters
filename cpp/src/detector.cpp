#include "rg/detect.hpp"

#include <cctype>
#include <string>
#include <vector>

#include "rg/converters/leveldata/detect.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"
#include "rg/util/pystr.hpp"

namespace rg {

namespace {

using util::Json;

bool is_valid_utf8(const std::string& s) {
    std::size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80)
                return false;
            if (c < 0xC2) return false;  // overlong
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80)
                return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= n || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(s[i + 3]) & 0xC0) != 0x80)
                return false;
            if (c > 0xF4) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool is_gzip(const std::string& data) {
    return data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0x1f &&
           static_cast<unsigned char>(data[1]) == 0x8b;
}

bool has_archetype(const Json& level_data, const std::string& archetype) {
    for (auto& entity : level_data.at("entities").arr()) {
        const Json* a = entity.find("archetype");
        if (a && a->type() == Json::Type::Str && a->as_str() == archetype) return true;
    }
    return false;
}

std::vector<std::string> detect_json(const Json& document) {
    if (auto spec = converters::leveldata::detect_parsed(document)) {
        if (*spec == "pysekai") return {"next_sekai"};
        // Untitled SEKAI LevelData uses Chart Cyanvas' archetypes, minus damage notes.
        if (has_archetype(document, "DamageNote")) return {"chart_cyanvas"};
        return {"chart_cyanvas", "untitled_sekai"};
    }
    if (!document.is_object()) return {};
    if (document.contains("usc") && document.contains("version")) return {"usc"};
    if (document.contains("NoteList") && document.contains("MusicScoreEventDataList")) return {"pjsk"};
    const Json* score = document.find("score");
    if (score && score->is_object()) {
        const Json* notes = score->find("notes");
        if (notes && notes->is_array()) return {"bdon"};
    }
    return {};
}

struct SusLine {
    std::string key;    // a command's name, or a data line's header
    std::string value;  // what follows it
    bool data = false;  // "#header: value" rather than "#COMMAND value"
};

std::vector<SusLine> sus_lines(const std::string& text) {
    std::vector<SusLine> lines;
    for (auto& raw : util::splitlines(text)) {
        std::string line = util::strip(raw);
        if (!util::startswith(line, "#")) continue;
        std::size_t colon = line.find(':');
        if (colon != std::string::npos && line.substr(1, colon - 1).find(' ') == std::string::npos) {
            lines.push_back({line.substr(1, colon - 1), util::strip(line.substr(colon + 1)), true});
            continue;
        }
        std::size_t space = line.find(' ');
        std::size_t end = space == std::string::npos ? line.size() : space;
        lines.push_back({line.substr(1, end - 1), end < line.size() ? util::strip(line.substr(end + 1)) : ""});
    }
    return lines;
}

bool is_measure_header(const std::string& header) {
    return (header.size() == 5 || header.size() == 6) && util::is_digits(header.substr(0, 3));
}

bool has_notes(const std::string& data) {
    for (std::size_t i = 0; i + 1 < data.size(); i += 2)
        if (data.compare(i, 2, "00") != 0) return true;
    return false;
}

// hololive Dream's dialect: #NMSxx / #COLORwwww definitions, #MUSIC_ID / #SP_SKILL, the 0A-0D
// channels, and "0x" run-length tokens in note data.
bool is_holodori_sus(const std::vector<SusLine>& lines) {
    for (auto& l : lines) {
        if (!l.data) {
            if (l.key == "MUSIC_ID" || l.key == "SP_SKILL") return true;
            continue;
        }
        if ((util::startswith(l.key, "NMS") && l.key.size() == 5) ||
            (util::startswith(l.key, "COLOR") && l.key.size() == 9))
            return true;
        if (!is_measure_header(l.key)) continue;
        std::string channel = l.key.substr(3, 2);
        if (l.key.size() == 5 && (channel == "0A" || channel == "0B" || channel == "0C" || channel == "0D"))
            return true;
        std::string cells = util::replace_all(l.value, " ", "");
        for (std::size_t i = 0; i + 1 < cells.size(); i += 2)
            if (cells[i] == '0' && cells[i + 1] == 'x') return true;
    }
    return false;
}

// What PJSK sus can hold and Yume's loader can't: layers, #MEASUREBS, #VOLUME, note speed ratios,
// skill and fever (and anything else on lanes 0, 1, e and f, which Yume drops).
bool uses_pjsk_only_features(const std::vector<SusLine>& lines) {
    for (auto& l : lines) {
        std::string key = util::to_upper(l.key);
        if (!l.data) {
            if (key == "HISPEED" && l.value != "00") return true;
            if (key == "MEASUREBS" && util::strip(l.value) != "0") return true;
            if (key == "VOLUME") return true;
            continue;
        }
        if (util::startswith(key, "VOLUME")) return true;
        if (util::startswith(key, "TIL") && key.size() == 5 && key.substr(3) != "00") return true;
        if (!is_measure_header(l.key)) continue;
        if (l.key.size() == 5 && (l.key.substr(3) == "02" || l.key.substr(3) == "08")) continue;
        if (l.value.find(',') != std::string::npos) return true;
        char channel = l.key[3];
        char lane = static_cast<char>(std::tolower(static_cast<unsigned char>(l.key[4])));
        std::string cells = util::replace_all(l.value, " ", "");
        if ((lane == '0' || lane == '1' || lane == 'e' || lane == 'f') && has_notes(cells)) return true;
        if (channel == '1' && l.key.size() == 5)
            for (std::size_t i = 0; i + 1 < cells.size(); i += 2)
                if (cells[i] == '4') return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> detect(const std::string& data) {
    std::string text = data;
    if (is_gzip(data)) {
        try {
            text = util::gzip_decompress(data);
        } catch (...) {
            return {};
        }
    } else {
        if (auto spec = converters::mmws::detect(data)) {
            if (*spec == "base") return {"mmws"};
            if (*spec == "chcy") return {"ccmmws"};
            return {"unchmmws"};
        }
        if (converters::pjsk::detect(data)) return {"pjsk"};
    }
    if (!is_valid_utf8(text)) return {};
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    try {
        return detect_json(util::json_parse(text));
    } catch (...) {
    }
    if (!converters::sus::detect(text)) return {};
    std::vector<SusLine> lines = sus_lines(text);
    if (is_holodori_sus(lines)) return {"holodori_sus"};
    if (uses_pjsk_only_features(lines)) return {"sus"};
    return {"sus", "yume_sus"};
}

}  // namespace rg
