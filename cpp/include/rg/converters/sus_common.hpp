#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "rg/util/pyops.hpp"
#include "rg/util/pystr.hpp"

// Shared SUS text parsing helpers, used by both the sus loader and the holodori_sus
// loader (mirrors `from ..sus.loader import ...`).
namespace rg::converters::sus {

inline const long long TICKS_PER_BEAT = 480;
inline const long long MIN_LANE = 2;
inline const long long MAX_LANE = 13;

inline double _sus_to_usc_lane(long long lane, long long width) {
    return static_cast<double>(lane) + static_cast<double>(width) / 2 - 8;
}

inline double _sus_to_usc_size(long long width) {
    return static_cast<double>(width) / 2;
}

struct _SusNote {
    long long tick;
    long long lane;
    long long width;
    long long type;
    long long til = 0;
    double speedRatio = 1.0;
};

struct _Bar {
    long long measure;
    long long ticks_per_measure;
    long long ticks;
};

inline double _tick_to_beat(long long tick) {
    return util::py_round(static_cast<double>(tick) / TICKS_PER_BEAT, 6);
}

inline std::string _note_key(long long tick, long long lane) {
    return std::to_string(tick) + "-" + std::to_string(lane);
}

inline std::vector<_Bar> _get_bars(std::vector<std::pair<long long, double>> bar_lengths,
                                   long long ticks_per_beat) {
    if (bar_lengths.empty()) bar_lengths = {{0, 4.0}};
    std::vector<std::pair<long long, double>> sorted_bl = bar_lengths;
    std::stable_sort(sorted_bl.begin(), sorted_bl.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<_Bar> bars;
    bars.push_back(
        {sorted_bl[0].first, util::py_trunc(sorted_bl[0].second * ticks_per_beat), 0});
    for (std::size_t i = 1; i < sorted_bl.size(); ++i) {
        long long measure = sorted_bl[i].first;
        long long tpm = util::py_trunc(sorted_bl[i].second * ticks_per_beat);
        long long ticks = util::py_trunc((measure - sorted_bl[i - 1].first) *
                                         sorted_bl[i - 1].second * ticks_per_beat);
        bars.push_back({measure, tpm, ticks});
    }
    return bars;
}

inline long long _get_ticks(const std::vector<_Bar>& bars, long long measure, long long i,
                            long long total) {
    long long b_index = 0;
    long long acc_ticks = 0;
    for (std::size_t idx = 0; idx < bars.size(); ++idx) {
        if (bars[idx].measure > measure) break;
        b_index = static_cast<long long>(idx);
        acc_ticks += bars[idx].ticks;
    }
    return acc_ticks +
           (measure - bars[static_cast<std::size_t>(b_index)].measure) *
               bars[static_cast<std::size_t>(b_index)].ticks_per_measure +
           util::py_floordiv(
               i * bars[static_cast<std::size_t>(b_index)].ticks_per_measure, total);
}

inline std::vector<std::pair<std::string, double>> _parse_note_cells(const std::string& data) {
    std::vector<std::pair<std::string, double>> cells;
    if (data.find(',') == std::string::npos) {
        std::size_t end = data.size() - data.size() % 2;
        for (std::size_t i = 0; i < end; i += 2)
            cells.push_back({data.substr(i, 2), 1.0});
        return cells;
    }

    std::size_t i = 0;
    std::size_t n = data.size();
    while (i < n) {
        while (i < n && util::py_isspace_ch(data[i])) ++i;
        if (i + 1 >= n) break;
        std::string note_data = data.substr(i, 2);
        i += 2;
        double speed_ratio = 1.0;
        if (i < n && data[i] == ',') {
            ++i;
            std::size_t start = i;
            while (i < n && !util::py_isspace_ch(data[i]) && data[i] != ',') ++i;
            if (i > start) {
                double sr = util::py_float(data.substr(start, i - start));
                if (sr > 0.0) speed_ratio = sr;
            }
        }
        cells.push_back({note_data, speed_ratio});
        while (i < n && (util::py_isspace_ch(data[i]) || data[i] == ',')) ++i;
    }
    return cells;
}

inline std::vector<_SusNote> _get_notes(const std::string& header, const std::string& data,
                                        const std::vector<_Bar>& bars, long long measure,
                                        long long til) {
    std::vector<_SusNote> notes;
    auto cells = _parse_note_cells(data);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const std::string& cell = cells[i].first;
        double speed = cells[i].second;
        if (cell.size() < 2 || cell == "00") continue;
        long long tick = _get_ticks(bars, measure, static_cast<long long>(i) * 2,
                                    static_cast<long long>(cells.size()) * 2);
        long long lane = util::b36_digit(header[4]);
        long long width = util::b36_digit(cell[1]);
        long long ntype = util::b36_digit(cell[0]);
        notes.push_back({tick, lane, width, ntype, til, speed});
    }
    return notes;
}

inline std::vector<std::vector<_SusNote>> _get_note_stream(std::vector<_SusNote> stream) {
    std::stable_sort(stream.begin(), stream.end(),
                     [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
    std::vector<std::vector<_SusNote>> slides;
    std::vector<_SusNote> current;
    bool new_slide = true;
    for (auto& note : stream) {
        if (new_slide) {
            current = {};
            new_slide = false;
        }
        current.push_back(note);
        if (note.type == 2) {
            slides.push_back(current);
            new_slide = true;
        }
    }
    return slides;
}

inline std::optional<std::tuple<long long, long long, double>> _parse_hispeed_entry(
    const std::string& entry) {
    std::size_t apos = entry.find('\'');
    if (apos == std::string::npos) return std::nullopt;
    std::size_t colon = entry.find(':', apos + 1);
    if (colon == std::string::npos) return std::nullopt;
    try {
        long long a = util::py_int(entry.substr(0, apos));
        long long b = util::py_int(entry.substr(apos + 1, colon - (apos + 1)));
        double c = util::py_float(entry.substr(colon + 1));
        return std::make_tuple(a, b, c);
    } catch (...) {
        return std::nullopt;
    }
}

inline bool _is_command(const std::string& line) {
    std::string sub = line.substr(1, 1);  // line[1:2]
    if (util::is_digits(sub)) return false;
    std::size_t first_quote = line.find('"');
    if (first_quote != std::string::npos) {
        std::size_t last_quote = line.rfind('"');
        if (first_quote != last_quote) {
            std::size_t space = line.find(' ');
            if (space != std::string::npos &&
                line.substr(0, space).find(':') != std::string::npos)
                return false;
            return true;
        }
    }
    return line.find(':') == std::string::npos;
}

}  // namespace rg::converters::sus
