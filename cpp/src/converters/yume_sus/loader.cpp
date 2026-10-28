#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "rg/converters/sus_common.hpp"
#include "rg/converters/yume_sus.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pyops.hpp"
#include "rg/util/pystr.hpp"

// Mirrors Yume's Plumnet.Chart.Sus.SusLoader (parsing), SekaiConverter (merging notes that
// share a (tick, lane) slot) and the parts of ChartCompiler that decide what gets played.
namespace rg::converters::yume_sus {

using namespace rg::core;
using namespace rg::util;

namespace {

// Plumnet.Chart.NoteCategory
enum class Cat : int {
    Normal = 0,
    Long = 1,
    Connection = 2,
    Flick = 3,
    Friction = 4,
    FrictionHide = 5,
    FrictionLong = 6,
    FrictionHideLong = 7,
    FrictionFlick = 8,
    Guide = 9,
    GuideEnd = 10,
    GuideHidden = 11,
    Hidden = 13,
    Skip = 14,
};

struct RawNote {
    long long tick;
    long long lane;
    long long width;
    long long type;
};

struct Bar {
    long long measure;
    long long ticks_per_measure;
    long long start_tick;
};

struct Arm {
    Cat category = Cat::Normal;
    int critical = 0;
    int direction = 0;
    int ease = 0;
};

struct Slot {
    long long tick;
    long long lane;
    long long width;
    Cat category;
    int critical;
    int direction;
    int ease;
    long long group;
    bool skip;
};

using SlotMap = std::map<std::pair<long long, long long>, Slot>;
using Til = std::tuple<long long, long long, double>;

bool is_b36_lower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'); }

std::string trim_end_cr(const std::string& s) {
    std::size_t end = s.size();
    while (end > 0 && s[end - 1] == '\r') --end;
    return s.substr(0, end);
}

std::string clean_data(const std::string& s) {
    return trim_end_cr(replace_all(replace_all(s, " ", ""), "\t", ""));
}

std::optional<long long> try_parse_int(const std::string& s) {
    std::string t = strip(s);
    std::size_t sign = (!t.empty() && (t[0] == '+' || t[0] == '-')) ? 1 : 0;
    if (t.size() == sign || !is_digits(t.substr(sign))) return std::nullopt;
    long long v = std::strtoll(t.c_str(), nullptr, 10);
    if (v < INT32_MIN || v > INT32_MAX) return std::nullopt;
    return v;
}

std::optional<double> try_parse_double(const std::string& s) {
    std::string t = strip(s);
    if (t.empty() || t.find_first_not_of("0123456789+-.eE") != std::string::npos) return std::nullopt;
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return std::nullopt;
    return v;
}

// ^#(?<bar>\d{3})(?<event_type>[0-9a-z]{2}):(?<data>.+)
bool match_event(const std::string& line, std::string& header, std::string& data) {
    if (line.size() < 8 || line[0] != '#' || line[6] != ':') return false;
    if (!is_digits(line.substr(1, 3)) || !is_b36_lower(line[4]) || !is_b36_lower(line[5]))
        return false;
    header = line.substr(1, 5);
    data = line.substr(7);
    return true;
}

// ^#(?<bar>\d{3})(?<noteType>\d{1})(?<lineNo>[0-9a-z]{1})(?<longNo>[0-9a-z]{1}):(?<data>.+)
bool match_long_event(const std::string& line, std::string& header, std::string& data) {
    if (line.size() < 9 || line[0] != '#' || line[7] != ':') return false;
    if (!is_digits(line.substr(1, 4)) || !is_b36_lower(line[5]) || !is_b36_lower(line[6]))
        return false;
    header = line.substr(1, 6);
    data = line.substr(8);
    return true;
}

bool is_note_channel(char c) { return c == '1' || c == '2' || c == '3' || c == '5' || c == '9'; }

void load_bpm(const std::string& line, std::map<std::string, double>& bpm_map) {
    std::size_t hash = line.find('#');
    if (hash == std::string::npos) return;
    std::size_t colon = line.find(':', hash);
    if (colon == std::string::npos || colon < hash + 6) return;
    std::string key = line.substr(hash + 1, colon - hash - 1);
    if (key.size() < 5 || !startswith(key, "BPM")) return;
    if (auto bpm = try_parse_double(line.substr(colon + 1))) bpm_map[key.substr(3)] = *bpm;
}

// One item of (?<barIndex>\d*)'(?<tickOffset>[0-9]*):(?<speedRatio>[-+]?\d*\.?\d*)\r, which
// must run to the end of the item.
std::optional<Til> match_til_item(const std::string& item) {
    std::size_t bar_end = 0;
    while (bar_end < item.size() && is_digit_ch(item[bar_end])) ++bar_end;
    if (bar_end == item.size() || item[bar_end] != '\'') return std::nullopt;
    std::size_t tick_end = bar_end + 1;
    while (tick_end < item.size() && is_digit_ch(item[tick_end])) ++tick_end;
    if (tick_end == item.size() || item[tick_end] != ':') return std::nullopt;
    std::string speed = item.substr(tick_end + 1);
    std::size_t k = (!speed.empty() && (speed[0] == '+' || speed[0] == '-')) ? 1 : 0;
    while (k < speed.size() && is_digit_ch(speed[k])) ++k;
    if (k < speed.size() && speed[k] == '.') ++k;
    while (k < speed.size() && is_digit_ch(speed[k])) ++k;
    if (k != speed.size()) return std::nullopt;
    return Til{try_parse_int(item.substr(0, bar_end)).value_or(0),
               try_parse_int(item.substr(bar_end + 1, tick_end - bar_end - 1)).value_or(0),
               try_parse_double(speed).value_or(0.0)};
}

void load_til(const std::string& line, std::vector<Til>& entries) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) return;
    std::string items = replace_all(line.substr(colon + 1), "\"", "");
    items = replace_all(items, "\r\n", "\r");
    items = replace_all(items, ", ", "\r");
    items = replace_all(items, ",", "\r");
    if (!endswith(items, "\r")) items += "\r";
    std::size_t begin = 0;
    while (begin < items.size()) {
        std::size_t end = items.find('\r', begin);
        std::string item = items.substr(begin, end - begin);
        begin = end + 1;
        for (std::size_t start = 0; start < item.size(); ++start) {
            if (auto entry = match_til_item(item.substr(start))) {
                entries.push_back(*entry);
                break;
            }
        }
    }
}

struct Metadata {
    std::string title, artist, designer;
    double wave_offset = 0.0;
    std::vector<std::string> requests;
};

Metadata extract_metadata(const std::vector<std::string>& lines) {
    Metadata meta;
    for (auto& raw : lines) {
        if (!startswith(raw, "#")) continue;
        std::string line = trim_end_cr(raw);
        std::size_t colon = line.find(':');
        if (colon != std::string::npos && colon < 8) continue;
        std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string key = line.substr(1, space - 1);
        std::string value = strip_chars(strip(line.substr(space + 1)), "\"");
        if (key == "TITLE") {
            meta.title = value;
        } else if (key == "ARTIST") {
            meta.artist = value;
        } else if (key == "DESIGNER") {
            meta.designer = value;
        } else if (key == "WAVEOFFSET") {
            if (auto v = try_parse_double(value)) meta.wave_offset = *v;
        } else if (key == "REQUEST") {
            meta.requests.push_back(value);
        }
    }
    return meta;
}

std::vector<Bar> build_bars(std::vector<std::pair<long long, double>> lengths, long long ticks_per_beat) {
    std::stable_sort(lengths.begin(), lengths.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<Bar> bars;
    long long start_tick = 0;
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        if (i > 0)
            start_tick += py_trunc(lengths[i - 1].second *
                                   static_cast<double>(lengths[i].first - lengths[i - 1].first) *
                                   static_cast<double>(ticks_per_beat));
        bars.push_back({lengths[i].first,
                        py_trunc(static_cast<double>(ticks_per_beat) * lengths[i].second),
                        start_tick});
    }
    return bars;
}

const Bar& find_bar(const std::vector<Bar>& bars, long long measure) {
    for (auto it = bars.rbegin(); it != bars.rend(); ++it)
        if (it->measure <= measure) return *it;
    throw std::invalid_argument("Measure " + std::to_string(measure) + " is out of range.");
}

long long to_tick(const std::vector<Bar>& bars, long long measure, long long slot, long long total) {
    const Bar& bar = find_bar(bars, measure);
    return bar.ticks_per_measure * slot / total + bar.start_tick +
           (measure - bar.measure) * bar.ticks_per_measure;
}

std::vector<std::pair<long long, std::string>> to_raw_objects(const std::vector<Bar>& bars,
                                                              const std::string& header,
                                                              const std::string& data) {
    long long measure = py_int(header.substr(0, 3));
    long long count = static_cast<long long>(data.size() / 2);
    std::vector<std::pair<long long, std::string>> cells;
    for (long long i = 0; i < count; ++i) {
        std::string cell = data.substr(static_cast<std::size_t>(i) * 2, 2);
        if (cell == "00") continue;
        cells.push_back({to_tick(bars, measure, i, count), cell});
    }
    return cells;
}

std::vector<RawNote> to_note_objects(const std::vector<Bar>& bars, const std::string& header,
                                     const std::string& data) {
    char lane_ch = header[4];
    if (lane_ch == '0' || lane_ch == '1' || lane_ch == 'e' || lane_ch == 'f') return {};
    long long lane = b36_digit(lane_ch);
    std::vector<RawNote> notes;
    for (auto& [tick, cell] : to_raw_objects(bars, header, data))
        notes.push_back({tick, lane, b36_digit(cell[1]), b36_digit(cell[0])});
    return notes;
}

std::vector<std::vector<RawNote>> to_slides(std::vector<RawNote> stream) {
    std::stable_sort(stream.begin(), stream.end(),
                     [](const RawNote& a, const RawNote& b) { return a.tick < b.tick; });
    std::vector<std::vector<RawNote>> slides;
    bool open = false;
    for (auto& note : stream) {
        if (!open) {
            slides.emplace_back();
            open = true;
        }
        slides.back().push_back(note);
        if (note.type == 2) open = false;
    }
    return slides;
}

std::optional<Arm> tap_arm(long long type) {
    switch (type) {
        case 1: return Arm{};
        case 2: return Arm{Cat::Normal, 1};
        case 3: return Arm{Cat::Skip};
        case 5: return Arm{Cat::Friction};
        case 6: return Arm{Cat::Friction, 1};
        case 7: return Arm{Cat::FrictionHide};
        case 8: return Arm{Cat::FrictionHide, 1};
        default: return std::nullopt;
    }
}

std::optional<Arm> directional_arm(long long type) {
    switch (type) {
        case 1: return Arm{Cat::Flick};
        case 2: return Arm{Cat::Normal, 0, 0, 2};
        case 3: return Arm{Cat::Flick, 0, 1};
        case 4: return Arm{Cat::Flick, 0, 2};
        case 5:
        case 6: return Arm{Cat::Normal, 0, 0, 1};
        default: return std::nullopt;
    }
}

std::optional<Cat> slide_category(long long type) {
    switch (type) {
        case 1: return Cat::Long;
        case 2: return Cat::Normal;
        case 3: return Cat::Connection;
        case 5: return Cat::Hidden;
        default: return std::nullopt;
    }
}

std::optional<Cat> guide_category(long long type) {
    switch (type) {
        case 1: return Cat::Guide;
        case 2: return Cat::GuideEnd;
        case 3:
        case 5: return Cat::GuideHidden;
        default: return std::nullopt;
    }
}

void add_to_dict(SlotMap& dict, const RawNote& note, Arm arm, long long group) {
    auto key = std::make_pair(note.tick, note.lane);
    auto it = dict.find(key);
    if (it == dict.end()) {
        dict.emplace(key, Slot{note.tick, note.lane, note.width, arm.category, arm.critical,
                               arm.direction, arm.ease, group, arm.category == Cat::Skip});
        return;
    }
    Slot& slot = it->second;
    if (arm.category != Cat::Normal) {
        if (arm.category == Cat::Long && slot.category == Cat::Friction)
            arm.category = Cat::FrictionLong;
        else if (arm.category == Cat::Flick && slot.category == Cat::Friction)
            arm.category = Cat::FrictionFlick;
        else if (slot.category == Cat::FrictionHide && arm.category == Cat::Long)
            arm.category = Cat::FrictionHideLong;
        slot.category = arm.category;
    }
    if (arm.critical) slot.critical = arm.critical;
    if (arm.direction) slot.direction = arm.direction;
    if (arm.ease) slot.ease = arm.ease;
    if (note.width != 1) slot.width = note.width;
    if (group != -1) slot.group = group;
}

std::vector<std::vector<const Slot*>> collect_groups(const SlotMap& dict, std::size_t count) {
    std::vector<std::vector<const Slot*>> groups(count);
    for (auto& [key, slot] : dict)
        if (slot.group >= 0 && slot.group < static_cast<long long>(count))
            groups[static_cast<std::size_t>(slot.group)].push_back(&slot);
    return groups;
}

bool is_flick(Cat c) { return c == Cat::Flick || c == Cat::FrictionFlick; }

// Category a slide member is played with (ChartCompiler.MakeRecordFromMerged).
Cat member_category(Cat c) {
    if (c == Cat::Normal || (c >= Cat::Guide && static_cast<int>(c) <= 12)) return Cat::Long;
    return c;
}

bool is_trace(Cat c) {
    return c == Cat::Friction || c == Cat::FrictionLong || c == Cat::FrictionFlick;
}

bool is_unplayed(Cat c) {
    return c == Cat::FrictionHide || c == Cat::FrictionHideLong || c == Cat::Hidden ||
           c == Cat::Skip;
}

std::string judge_type(Cat c) {
    if (is_trace(c)) return "trace";
    if (is_unplayed(c)) return "none";
    return "normal";
}

std::string ease_name(int ease) {
    if (ease == 1) return "out";
    if (ease == 2) return "in";
    return "linear";
}

std::string direction_name(int direction) {
    if (direction == 1) return "left";
    if (direction == 2) return "right";
    return "up";
}

}  // namespace

core::Score loads(const std::string& data) {
    std::vector<std::string> lines = split(data, "\n");
    long long ticks_per_beat = 480;
    std::vector<std::pair<long long, double>> bar_lengths;
    std::map<std::string, double> bpm_map;
    std::vector<std::pair<std::string, std::string>> bpm_lines;
    std::vector<std::pair<std::string, std::string>> note_lines;
    std::vector<Til> til_entries;

    for (auto& line : lines) {
        if (line.empty() || line[0] != '#') continue;
        if (line.find("#REQUEST \"ticks_per_beat") != std::string::npos) {
            std::string request = replace_all(line, "\"", "");
            const std::string prefix = "#REQUEST ticks_per_beat ";
            if (startswith(request, prefix)) {
                std::size_t end = prefix.size();
                while (end < request.size() && is_digit_ch(request[end])) ++end;
                if (end > prefix.size())
                    if (auto v = try_parse_int(request.substr(prefix.size(), end - prefix.size())))
                        ticks_per_beat = *v;
            }
            continue;
        }
        if (line.find("#TIL00") != std::string::npos) {
            load_til(line, til_entries);
            continue;
        }
        if (line.find("#VOLUME") != std::string::npos) continue;
        if (line.find("#BPM") != std::string::npos) {
            load_bpm(line, bpm_map);
            continue;
        }

        std::string trimmed = trim_end_cr(line);
        std::string header, line_data;
        if (match_event(trimmed, header, line_data)) {
            line_data = clean_data(line_data);
            std::string event = header.substr(3, 2);
            if (event == "02") {
                if (auto length = try_parse_double(line_data))
                    bar_lengths.push_back({py_int(header.substr(0, 3)), *length});
            } else if (event == "08") {
                bpm_lines.push_back({header, line_data});
            } else if (is_note_channel(event[0])) {
                note_lines.push_back({header, line_data});
            }
        } else if (match_long_event(trimmed, header, line_data)) {
            if (is_note_channel(header[3])) note_lines.push_back({header, clean_data(line_data)});
        }
    }
    if (ticks_per_beat <= 0) throw std::invalid_argument("ticks_per_beat must be positive");
    if (bar_lengths.empty()) bar_lengths.push_back({0, 4.0});
    std::vector<Bar> bars = build_bars(bar_lengths, ticks_per_beat);

    std::vector<RawNote> taps, directionals;
    std::vector<std::pair<char, std::vector<RawNote>>> slide_streams, guide_streams;
    auto stream_for = [](std::vector<std::pair<char, std::vector<RawNote>>>& streams,
                         char channel) -> std::vector<RawNote>& {
        for (auto& [ch, stream] : streams)
            if (ch == channel) return stream;
        streams.push_back({channel, {}});
        return streams.back().second;
    };
    for (auto& [header, line_data] : note_lines) {
        std::vector<RawNote>* dest = nullptr;
        if (header.size() == 5) {
            if (header[3] == '1') dest = &taps;
            else if (header[3] == '5') dest = &directionals;
        } else if (header[3] == '3') {
            dest = &stream_for(slide_streams, header[5]);
        } else if (header[3] == '9') {
            dest = &stream_for(guide_streams, header[5]);
        }
        if (!dest) continue;
        auto parsed = to_note_objects(bars, header, line_data);
        dest->insert(dest->end(), parsed.begin(), parsed.end());
    }

    std::vector<std::vector<RawNote>> slides, guides;
    for (auto& [ch, stream] : slide_streams) {
        auto split_slides = to_slides(stream);
        slides.insert(slides.end(), split_slides.begin(), split_slides.end());
    }
    for (auto& [ch, stream] : guide_streams) {
        auto split_guides = to_slides(stream);
        guides.insert(guides.end(), split_guides.begin(), split_guides.end());
    }

    SlotMap main_slots, guide_slots;
    for (auto& tap : taps)
        if (auto arm = tap_arm(tap.type)) add_to_dict(main_slots, tap, *arm, -1);
    for (auto& directional : directionals)
        if (auto arm = directional_arm(directional.type))
            add_to_dict(main_slots, directional, *arm, -1);
    for (std::size_t i = 0; i < slides.size(); ++i)
        for (auto& note : slides[i])
            if (auto category = slide_category(note.type))
                add_to_dict(main_slots, note, Arm{*category}, static_cast<long long>(i));
    // A guide point takes its critical/direction/ease from whatever sits in the same slot.
    for (std::size_t i = 0; i < guides.size(); ++i) {
        for (auto& note : guides[i]) {
            auto category = guide_category(note.type);
            if (!category) continue;
            Arm arm{*category};
            auto it = main_slots.find({note.tick, note.lane});
            if (it != main_slots.end()) {
                arm.critical = it->second.critical;
                arm.direction = it->second.direction;
                arm.ease = it->second.ease;
            }
            add_to_dict(guide_slots, note, arm, static_cast<long long>(i));
        }
    }

    auto tick_to_beat = [&](long long tick) {
        return py_round(static_cast<double>(tick) / static_cast<double>(ticks_per_beat), 6);
    };

    std::vector<NodePtr> notes;

    std::vector<std::pair<long long, std::string>> bpm_cells;
    for (auto& [header, line_data] : bpm_lines) {
        auto cells = to_raw_objects(bars, header, line_data);
        bpm_cells.insert(bpm_cells.end(), cells.begin(), cells.end());
    }
    std::stable_sort(bpm_cells.begin(), bpm_cells.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [tick, id] : bpm_cells) {
        auto it = bpm_map.find(id);
        notes.push_back(std::make_shared<Bpm>(tick_to_beat(tick), it != bpm_map.end() ? it->second : 120.0));
    }
    if (bpm_cells.empty()) notes.push_back(std::make_shared<Bpm>(0, 120.0));

    std::vector<std::pair<long long, double>> speeds;
    for (auto& [measure, tick_offset, speed] : til_entries) {
        const Bar& bar = find_bar(bars, measure);
        speeds.push_back({bar.start_tick + tick_offset + (measure - bar.measure) * bar.ticks_per_measure, speed});
    }
    std::stable_sort(speeds.begin(), speeds.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });
    auto tsg = std::make_shared<TimeScaleGroup>();
    if (speeds.empty() || speeds.front().first != 0)
        tsg->append(std::make_shared<TimeScalePoint>(0.0, 1.0));
    for (auto& [tick, speed] : speeds)
        tsg->append(std::make_shared<TimeScalePoint>(tick_to_beat(tick), speed));
    notes.push_back(tsg);

    auto make_single = [&](const Slot& slot, Cat category, bool critical) {
        auto s = std::make_shared<Single>();
        s->beat = tick_to_beat(slot.tick);
        s->critical = critical;
        s->lane = sus::_sus_to_usc_lane(slot.lane, slot.width);
        s->size = sus::_sus_to_usc_size(slot.width);
        s->timeScaleGroup = 0;
        s->trace = is_trace(category);
        if (is_flick(category)) s->direction = direction_name(slot.direction);
        return s;
    };

    // Standalone slots (ChartCompiler.EmitStandaloneNormals): only taps and flicks are played.
    for (auto& [key, slot] : main_slots) {
        if (slot.group >= 0) continue;
        Cat c = slot.category;
        if (c == Cat::Normal || c == Cat::Long || c == Cat::Friction || is_flick(c))
            notes.push_back(make_single(slot, c, slot.critical != 0));
    }

    // Slide groups are split after any flick that is not their last point (SplitGroupAtFlickTerminators).
    for (auto& group : collect_groups(main_slots, slides.size())) {
        std::vector<std::vector<const Slot*>> parts;
        std::vector<const Slot*> current;
        for (std::size_t i = 0; i < group.size(); ++i) {
            current.push_back(group[i]);
            if (i + 1 < group.size() && is_flick(group[i]->category)) {
                parts.push_back(current);
                current.clear();
            }
        }
        if (!current.empty()) parts.push_back(current);

        for (auto& part : parts) {
            const Slot& head = *part.front();
            bool critical = head.critical != 0;
            auto make_start = [&](Cat c, const std::string& ease) {
                auto sp = std::make_shared<SlideStartPoint>();
                sp->beat = tick_to_beat(head.tick);
                sp->critical = critical;
                sp->ease = ease;
                sp->judgeType = judge_type(c);
                sp->lane = sus::_sus_to_usc_lane(head.lane, head.width);
                sp->size = sus::_sus_to_usc_size(head.width);
                sp->timeScaleGroup = 0;
                return sp;
            };

            // A lone point is played as a slide head with no body; a flick there also gets its own note.
            if (part.size() == 1) {
                Cat c = member_category(head.category);
                if (is_flick(head.category)) {
                    c = head.category == Cat::FrictionFlick ? Cat::FrictionLong : Cat::Long;
                    notes.push_back(make_single(head, head.category, critical));
                }
                if (is_unplayed(c)) continue;
                auto slide = std::make_shared<Slide>(critical);
                slide->append(make_start(c, "linear"));
                notes.push_back(slide);
                continue;
            }

            auto slide = std::make_shared<Slide>(critical);
            for (std::size_t i = 0; i < part.size(); ++i) {
                const Slot& slot = *part[i];
                double beat = tick_to_beat(slot.tick);
                double lane = sus::_sus_to_usc_lane(slot.lane, slot.width);
                double size = sus::_sus_to_usc_size(slot.width);
                Cat c = member_category(slot.category);

                if (i == 0) {
                    slide->append(make_start(c, ease_name(slot.ease)));
                } else if (i + 1 == part.size()) {
                    auto ep = std::make_shared<SlideEndPoint>();
                    ep->beat = beat;
                    ep->critical = critical;
                    ep->lane = lane;
                    ep->size = size;
                    ep->timeScaleGroup = 0;
                    if (is_flick(slot.category)) {
                        c = slot.category == Cat::FrictionFlick ? Cat::FrictionLong : Cat::Long;
                        ep->critical = critical || slot.critical != 0;
                        ep->direction = direction_name(slot.direction);
                    }
                    ep->judgeType = judge_type(c);
                    slide->append(ep);
                } else {
                    auto rp = std::make_shared<SlideRelayPoint>();
                    rp->beat = beat;
                    rp->ease = ease_name(slot.ease);
                    rp->lane = lane;
                    rp->size = size;
                    rp->timeScaleGroup = 0;
                    rp->type = "tick";
                    rp->critical = critical;
                    if (c == Cat::Hidden)
                        rp->critical = std::nullopt;
                    else if (slot.skip)
                        rp->type = "attach";
                    slide->append(rp);
                }
            }
            notes.push_back(slide);
        }
    }

    for (auto& group : collect_groups(guide_slots, guides.size())) {
        if (group.size() < 2) continue;
        auto guide = std::make_shared<Guide>(group.front()->critical ? "yellow" : "green", "out");
        for (const Slot* slot : group) {
            auto gp = std::make_shared<GuidePoint>();
            gp->beat = tick_to_beat(slot->tick);
            gp->ease = ease_name(slot->ease);
            gp->lane = sus::_sus_to_usc_lane(slot->lane, slot->width);
            gp->size = sus::_sus_to_usc_size(slot->width);
            gp->timeScaleGroup = 0;
            guide->append(gp);
        }
        notes.push_back(guide);
    }

    std::stable_sort(notes.begin(), notes.end(), [](const NodePtr& a, const NodePtr& b) {
        return get_sus_sort_number(a.get()) < get_sus_sort_number(b.get());
    });

    Metadata meta = extract_metadata(lines);
    auto metadata = std::make_shared<MetaData>();
    metadata->title = meta.title;
    metadata->artist = meta.artist;
    metadata->designer = meta.designer;
    metadata->waveoffset = -meta.wave_offset;
    if (!meta.requests.empty())
        metadata->requests = meta.requests;
    else
        metadata->requests = std::nullopt;

    return core::Score(metadata, notes);
}

}  // namespace rg::converters::yume_sus
