#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "rg/converters/sus.hpp"
#include "rg/converters/sus_common.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pyops.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::sus {

using namespace rg::core;
using namespace rg::util;

static const long long FEVER_LANE = 15;

// forward decl
static core::Score _sus_to_score(
    std::vector<_SusNote> sus_taps, std::vector<_SusNote> sus_directionals,
    std::vector<std::vector<_SusNote>> sus_slides,
    std::vector<std::vector<_SusNote>> sus_guides,
    std::vector<std::pair<long long, double>> sus_bpms,
    std::vector<std::pair<long long, double>> bar_lengths,
    std::vector<std::vector<std::pair<long long, double>>> tils,
    std::vector<std::pair<long long, double>> sus_volumes, std::string title,
    std::string artist, std::string designer, double wave_offset,
    std::vector<std::string> requests);

static core::Score load_score(const std::string& data);

core::Score loads(const std::string& data, const ImportOptions& opts) {
    core::Score score = load_score(data);
    if (opts.skip_note_speed_ratios) score.strip_speed_ratios();
    return score;
}

static core::Score load_score(const std::string& data) {
    long long ticks_per_beat = TICKS_PER_BEAT;
    std::string title, artist, designer;
    double wave_offset = 0.0;
    std::vector<std::string> requests;

    std::vector<std::pair<long long, double>> bar_lengths;
    std::map<std::string, double> bpm_definitions;
    std::vector<std::pair<long long, double>> bpm_data_lines;
    std::map<std::string, long long> til_map;
    std::vector<std::vector<std::pair<long long, double>>> tils;
    long long til_data_index = 0;
    long long current_til = 0;
    std::vector<std::string> volume_data;

    std::vector<_SusNote> taps;
    std::vector<_SusNote> directionals;
    std::vector<long long> slide_channel_order;
    std::map<long long, std::vector<_SusNote>> slide_streams;
    std::vector<long long> guide_channel_order;
    std::map<long long, std::vector<_SusNote>> guide_streams;

    // PHASE 1: first pass to get bar_lengths, ticks_per_beat, and MEASUREBS
    long long measure_offset = 0;
    std::vector<std::pair<std::string, long long>> lines_to_process;
    for (auto& raw_line : splitlines(data)) {
        std::string line = strip(raw_line);
        if (!startswith(line, "#")) continue;

        if (_is_command(line)) {
            std::size_t space = line.find(' ', 1);
            if (space == std::string::npos) {
                lines_to_process.push_back({line, measure_offset});
                continue;
            }
            std::string key = to_upper(line.substr(1, space - 1));
            std::string value = strip(line.substr(space + 1));
            if (startswith(value, "\"") && endswith(value, "\"") && value.size() >= 2)
                value = value.substr(1, value.size() - 2);
            if (key == "REQUEST") {
                auto parts = split_ws(value);
                if (parts.size() == 2 && parts[0] == "ticks_per_beat")
                    ticks_per_beat = py_int(parts[1]);
            } else if (key == "MEASUREBS") {
                measure_offset = py_int(value);
            }
        } else {
            std::size_t colon = line.find(':', 1);
            if (colon == std::string::npos) {
                lines_to_process.push_back({line, measure_offset});
                continue;
            }
            std::string header = strip(line.substr(1, colon - 1));
            std::string line_data = strip(line.substr(colon + 1));
            if (header.size() == 5 && endswith(header, "02") &&
                is_digits(header.substr(0, 3))) {
                bar_lengths.push_back(
                    {py_int(header.substr(0, 3)) + measure_offset, py_float(line_data)});
            }
        }

        lines_to_process.push_back({line, measure_offset});
    }

    if (bar_lengths.empty()) bar_lengths.push_back({0, 4.0});

    std::vector<_Bar> bars = _get_bars(bar_lengths, ticks_per_beat);

    // PHASE 2: second pass for everything else (needs bars for tick calculation)
    for (auto& [line, m_offset] : lines_to_process) {
        if (_is_command(line)) {
            std::size_t space = line.find(' ', 1);
            if (space == std::string::npos) continue;
            std::string key = to_upper(line.substr(1, space - 1));
            std::string value = strip(line.substr(space + 1));
            if (startswith(value, "\"") && endswith(value, "\"") && value.size() >= 2)
                value = value.substr(1, value.size() - 2);

            if (key == "TITLE") {
                title = value;
            } else if (key == "ARTIST") {
                artist = value;
            } else if (key == "DESIGNER") {
                designer = value;
            } else if (key == "WAVEOFFSET") {
                wave_offset = py_float(value);
            } else if (key == "REQUEST") {
                requests.push_back(value);
            } else if (key == "HISPEED") {
                auto it = til_map.find(value);
                if (it != til_map.end()) current_til = it->second;
            }
            continue;
        }

        std::size_t colon = line.find(':', 1);
        if (colon == std::string::npos) continue;
        std::string header = strip(line.substr(1, colon - 1));
        std::string line_data = strip(line.substr(colon + 1));

        if (header.size() != 5 && header.size() != 6) {
            if (startswith(header, "VOLUME")) volume_data.push_back(line_data);
            continue;
        }

        if (header.size() == 5 && endswith(header, "02") && is_digits(header.substr(0, 3))) {
            // already handled in phase 1
        } else if (startswith(header, "BPM") && header.size() == 5) {
            bpm_definitions[header.substr(3)] = py_float(line_data);
        } else if (header.size() == 5 && endswith(header, "08")) {
            long long measure = py_int(header.substr(0, 3)) + m_offset;
            std::string stripped = replace_all(line_data, " ", "");
            std::vector<std::string> pairs;
            for (std::size_t j = 0; j + 2 <= stripped.size() - stripped.size() % 2; j += 2)
                pairs.push_back(stripped.substr(j, 2));
            for (std::size_t j = 0; j < pairs.size(); ++j) {
                if (pairs[j] == "00") continue;
                long long tick = _get_ticks(bars, measure, static_cast<long long>(j),
                                            static_cast<long long>(pairs.size()));
                double bpm = 120.0;
                auto it = bpm_definitions.find(pairs[j]);
                if (it != bpm_definitions.end()) bpm = it->second;
                bpm_data_lines.push_back({tick, bpm});
            }
        } else if (startswith(header, "TIL") && header.size() == 5) {
            std::string til_id = header.substr(3);
            til_map[til_id] = til_data_index;
            std::string stripped = replace_all(strip_chars(line_data, "\""), " ", "");
            std::vector<std::pair<long long, double>> new_til;
            for (auto& entry_str : split(stripped, ",")) {
                auto parsed = _parse_hispeed_entry(entry_str);
                if (parsed) {
                    auto [measure, tick_offset, value] = *parsed;
                    long long measure_ticks = _get_ticks(bars, measure, 0, 1);
                    new_til.push_back({measure_ticks + tick_offset, value});
                }
            }
            tils.push_back(new_til);
            til_data_index += 1;
        } else if (header.size() == 5 && header[3] == '1') {
            long long measure = py_int(header.substr(0, 3)) + m_offset;
            auto ns = _get_notes(header, line_data, bars, measure, current_til);
            taps.insert(taps.end(), ns.begin(), ns.end());
        } else if (header.size() == 5 && header[3] == '5') {
            long long measure = py_int(header.substr(0, 3)) + m_offset;
            auto ns = _get_notes(header, line_data, bars, measure, current_til);
            directionals.insert(directionals.end(), ns.begin(), ns.end());
        } else if (header.size() == 6 && header[3] == '3') {
            long long measure = py_int(header.substr(0, 3)) + m_offset;
            long long channel = b36_digit(header[5]);
            if (slide_streams.find(channel) == slide_streams.end())
                slide_channel_order.push_back(channel);
            auto ns = _get_notes(header, line_data, bars, measure, current_til);
            auto& v = slide_streams[channel];
            v.insert(v.end(), ns.begin(), ns.end());
        } else if (header.size() == 6 && header[3] == '9') {
            long long measure = py_int(header.substr(0, 3)) + m_offset;
            long long channel = b36_digit(header[5]);
            if (guide_streams.find(channel) == guide_streams.end())
                guide_channel_order.push_back(channel);
            auto ns = _get_notes(header, line_data, bars, measure, current_til);
            auto& v = guide_streams[channel];
            v.insert(v.end(), ns.begin(), ns.end());
        }
    }

    // Build slide/guide note streams
    std::vector<std::vector<_SusNote>> slides;
    for (long long ch : slide_channel_order) {
        auto s = _get_note_stream(slide_streams[ch]);
        slides.insert(slides.end(), s.begin(), s.end());
    }

    std::vector<std::vector<_SusNote>> guides;
    for (long long ch : guide_channel_order) {
        auto s = _get_note_stream(guide_streams[ch]);
        guides.insert(guides.end(), s.begin(), s.end());
    }

    // Deduplicate slides/guides sharing same start+end (tick, lane).
    // Game's noteInfoDict merges notes at same (time, lane), so duplicate
    // slides from different channels collapse into one.
    auto _dedup_holds = [](std::vector<std::vector<_SusNote>> holds) {
        std::set<std::tuple<long long, long long, long long, long long>> seen;
        std::vector<std::vector<_SusNote>> result;
        for (auto& hold : holds) {
            if (hold.size() < 2) continue;
            auto key = std::make_tuple(hold.front().tick, hold.front().lane,
                                       hold.back().tick, hold.back().lane);
            if (seen.count(key)) continue;
            seen.insert(key);
            result.push_back(hold);
        }
        return result;
    };

    slides = _dedup_holds(slides);
    guides = _dedup_holds(guides);

    // Parse volumes
    std::vector<std::pair<long long, double>> volumes;
    for (auto& vol_str : volume_data) {
        std::string stripped = replace_all(strip_chars(vol_str, "\""), " ", "");
        for (auto& entry_str : split(stripped, ",")) {
            auto parsed = _parse_hispeed_entry(entry_str);
            if (parsed) {
                auto [measure, tick_offset, value] = *parsed;
                long long measure_ticks = _get_ticks(bars, measure, 0, 1);
                volumes.push_back({measure_ticks + tick_offset, value});
            }
        }
    }

    std::stable_sort(bpm_data_lines.begin(), bpm_data_lines.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });

    // PHASE 3: SUS → Score (matching ChartMaker susToScore)
    return _sus_to_score(taps, directionals, slides, guides, bpm_data_lines, bar_lengths,
                         tils, volumes, title, artist, designer, wave_offset, requests);
}

static core::Score _sus_to_score(
    std::vector<_SusNote> sus_taps, std::vector<_SusNote> sus_directionals,
    std::vector<std::vector<_SusNote>> sus_slides,
    std::vector<std::vector<_SusNote>> sus_guides,
    std::vector<std::pair<long long, double>> sus_bpms,
    std::vector<std::pair<long long, double>> /*bar_lengths*/,
    std::vector<std::vector<std::pair<long long, double>>> tils,
    std::vector<std::pair<long long, double>> sus_volumes, std::string title,
    std::string artist, std::string designer, double wave_offset,
    std::vector<std::string> requests) {
    // BUILD LOOKUP SETS (matching ChartMaker exactly)
    std::map<std::string, std::string> flicks;
    std::set<std::string> criticals;
    std::set<std::string> step_ignore;
    std::set<std::string> ease_ins;
    std::set<std::string> ease_outs;
    std::set<std::string> slide_keys;
    std::set<std::string> frictions;
    std::set<std::string> hidden_holds;

    for (auto& d : sus_directionals) {
        std::string key = _note_key(d.tick, d.lane);
        if (d.type == 1)
            flicks[key] = "up";
        else if (d.type == 3)
            flicks[key] = "left";
        else if (d.type == 4)
            flicks[key] = "right";
        else if (d.type == 2)
            ease_ins.insert(key);
        else if (d.type == 5 || d.type == 6)
            ease_outs.insert(key);
    }

    for (auto& t : sus_taps) {
        std::string key = _note_key(t.tick, t.lane);
        if (t.type == 2)
            criticals.insert(key);
        else if (t.type == 3)
            step_ignore.insert(key);
        else if (t.type == 5)
            frictions.insert(key);
        else if (t.type == 6) {
            criticals.insert(key);
            frictions.insert(key);
        } else if (t.type == 7)
            hidden_holds.insert(key);
        else if (t.type == 8) {
            hidden_holds.insert(key);
            criticals.insert(key);
        }
    }

    for (auto& slide : sus_slides)
        for (auto& note : slide)
            if (note.type == 1 || note.type == 2 || note.type == 3 || note.type == 5)
                slide_keys.insert(_note_key(note.tick, note.lane));

    // Slots something other than a directional puts a note in (skill and fever are events).
    std::set<std::string> occupied = slide_keys;
    for (auto& t : sus_taps)
        if (t.type != 4 && !(t.lane == FEVER_LANE && t.width == 1))
            occupied.insert(_note_key(t.tick, t.lane));
    for (auto& guide : sus_guides)
        for (auto& note : guide)
            if (note.type == 1 || note.type == 2 || note.type == 3 || note.type == 5)
                occupied.insert(_note_key(note.tick, note.lane));

    std::vector<NodePtr> notes;

    // BPM
    if (!sus_bpms.empty()) {
        for (auto& [tick, bpm] : sus_bpms)
            notes.push_back(std::make_shared<Bpm>(_tick_to_beat(tick), bpm));
    } else {
        notes.push_back(std::make_shared<Bpm>(0, 120.0));
    }

    // Volume
    for (auto& [tick, vol] : sus_volumes)
        notes.push_back(std::make_shared<Volume>(_tick_to_beat(tick), vol));

    // TimeScale (TIL layers)
    if (!tils.empty()) {
        for (auto& til : tils) {
            auto tsg = std::make_shared<TimeScaleGroup>();
            bool has_initial = false;
            std::vector<std::pair<long long, double>> sorted_til = til;
            std::stable_sort(sorted_til.begin(), sorted_til.end(),
                             [](auto& a, auto& b) { return a.first < b.first; });
            for (auto& [tick, speed] : sorted_til) {
                if (tick == 0) has_initial = true;
                tsg->append(std::make_shared<TimeScalePoint>(_tick_to_beat(tick), speed));
            }
            if (!has_initial)
                tsg->insert(0, std::make_shared<TimeScalePoint>(0.0, 1.0));
            notes.push_back(tsg);
        }
    } else {
        auto tsg = std::make_shared<TimeScaleGroup>();
        tsg->append(std::make_shared<TimeScalePoint>(0.0, 1.0));
        notes.push_back(tsg);
    }

    // TAPS → Singles/Skills/Fever
    // Game's AddNormalNoteInfo merges taps at same (tick, lane) via Update,
    // so we deduplicate by (tick, lane) to avoid double-counting.
    std::set<std::string> seen_tap_keys;

    std::vector<_SusNote> sorted_taps = sus_taps;
    std::stable_sort(sorted_taps.begin(), sorted_taps.end(),
                     [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });

    for (auto& note : sorted_taps) {
        if (note.type == 4) {
            notes.push_back(std::make_shared<Skill>(_tick_to_beat(note.tick)));
            continue;
        }

        if (note.lane == FEVER_LANE && note.width == 1) {
            if (note.type == 1) {
                notes.push_back(std::make_shared<FeverChance>(_tick_to_beat(note.tick)));
            } else if (note.type == 2) {
                notes.push_back(std::make_shared<FeverStart>(_tick_to_beat(note.tick)));
            }
            continue;
        }

        // 3=step_ignore marker (Skip category, dropped by game), 7/8=hidden hold markers
        if (note.type == 3 || note.type == 7 || note.type == 8) continue;

        if (note.lane < MIN_LANE || note.lane > MAX_LANE) continue;

        std::string key = _note_key(note.tick, note.lane);
        if (slide_keys.count(key)) continue;

        if (seen_tap_keys.count(key)) continue;
        seen_tap_keys.insert(key);

        bool is_critical = criticals.count(key) != 0;
        bool is_friction = frictions.count(key) != 0;

        auto s = std::make_shared<Single>();
        s->beat = _tick_to_beat(note.tick);
        s->critical = is_critical;
        s->lane = _sus_to_usc_lane(note.lane, note.width);
        s->size = _sus_to_usc_size(note.width);
        s->timeScaleGroup = note.til;
        s->speedRatio = note.speedRatio;
        s->trace = is_friction;
        auto fit = flicks.find(key);
        if (fit != flicks.end()) s->direction = fit->second;
        notes.push_back(s);
    }

    // A directional alone in its slot is a note too (the game turns every slot into one): a flick,
    // or a tap when it only sets an ease.
    std::vector<_SusNote> sorted_directionals = sus_directionals;
    std::stable_sort(sorted_directionals.begin(), sorted_directionals.end(),
                     [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
    for (auto& note : sorted_directionals) {
        if (note.type < 1 || note.type > 6) continue;
        if (note.lane < MIN_LANE || note.lane > MAX_LANE) continue;
        std::string key = _note_key(note.tick, note.lane);
        if (occupied.count(key) || seen_tap_keys.count(key)) continue;
        seen_tap_keys.insert(key);

        auto s = std::make_shared<Single>();
        s->beat = _tick_to_beat(note.tick);
        s->critical = false;
        s->lane = _sus_to_usc_lane(note.lane, note.width);
        s->size = _sus_to_usc_size(note.width);
        s->timeScaleGroup = note.til;
        s->speedRatio = note.speedRatio;
        s->trace = false;
        auto fit = flicks.find(key);
        if (fit != flicks.end()) s->direction = fit->second;
        notes.push_back(s);
    }

    // SLIDES & GUIDES (matching ChartMaker slideFillFunc)
    auto _process_holds = [&](std::vector<std::vector<_SusNote>>& hold_list, bool is_guide) {
        for (auto& hold : hold_list) {
            bool has_start = false;
            for (auto& n : hold)
                if (n.type == 1 || n.type == 2) {
                    has_start = true;
                    break;
                }
            if (!has_start || hold.size() < 2) continue;

            std::string start_key = _note_key(hold.front().tick, hold.front().lane);
            bool critical = criticals.count(start_key) != 0;

            if (is_guide) {
                auto guide_note = std::make_shared<Guide>(critical ? "yellow" : "green", "out");
                for (auto& note : hold) {
                    std::string key = _note_key(note.tick, note.lane);
                    std::string ease = "linear";
                    if (ease_ins.count(key))
                        ease = "in";
                    else if (ease_outs.count(key))
                        ease = "out";

                    auto gp = std::make_shared<GuidePoint>();
                    gp->beat = _tick_to_beat(note.tick);
                    gp->ease = ease;
                    gp->lane = _sus_to_usc_lane(note.lane, note.width);
                    gp->size = _sus_to_usc_size(note.width);
                    gp->timeScaleGroup = note.til;
                    gp->speedRatio = note.speedRatio;
                    guide_note->append(gp);
                }
                notes.push_back(guide_note);
            } else {
                auto slide_note = std::make_shared<Slide>(critical);
                for (auto& note : hold) {
                    std::string key = _note_key(note.tick, note.lane);
                    std::string ease = "linear";
                    if (ease_ins.count(key))
                        ease = "in";
                    else if (ease_outs.count(key))
                        ease = "out";
                    double beat = _tick_to_beat(note.tick);
                    double lane = _sus_to_usc_lane(note.lane, note.width);
                    double size = _sus_to_usc_size(note.width);

                    if (note.type == 1) {  // start
                        bool is_hidden = hidden_holds.count(key) != 0;
                        bool is_friction = frictions.count(key) != 0;
                        std::string judge_type;
                        if (is_hidden)
                            judge_type = "none";
                        else if (is_friction)
                            judge_type = "trace";
                        else
                            judge_type = "normal";

                        auto sp = std::make_shared<SlideStartPoint>();
                        sp->beat = beat;
                        sp->critical = critical;
                        sp->ease = ease;
                        sp->judgeType = judge_type;
                        sp->lane = lane;
                        sp->size = size;
                        sp->timeScaleGroup = note.til;
                        sp->speedRatio = note.speedRatio;
                        slide_note->append(sp);

                    } else if (note.type == 2) {  // end
                        bool is_hidden = hidden_holds.count(key) != 0;
                        bool is_friction = frictions.count(key) != 0;
                        std::string judge_type;
                        if (is_hidden)
                            judge_type = "none";
                        else if (is_friction)
                            judge_type = "trace";
                        else
                            judge_type = "normal";

                        bool end_critical = critical || (criticals.count(key) != 0);

                        auto ep = std::make_shared<SlideEndPoint>();
                        ep->beat = beat;
                        ep->critical = end_critical;
                        ep->judgeType = judge_type;
                        ep->lane = lane;
                        ep->size = size;
                        ep->timeScaleGroup = note.til;
                        ep->speedRatio = note.speedRatio;
                        auto fit = flicks.find(key);
                        if (fit != flicks.end()) ep->direction = fit->second;
                        slide_note->append(ep);

                    } else if (note.type == 3 || note.type == 5) {  // mid
                        // Normal (type 3, no step_ignore): changes shape + adds combo (tick + critical)
                        // Hidden (type 5): changes shape, no combo (tick + critical=None)
                        // Skip (type 3 + step_ignore): no shape change, adds combo (attach + critical)
                        std::string step_type = "tick";
                        std::optional<bool> mid_critical = critical;

                        if (note.type == 5)
                            mid_critical = std::nullopt;
                        else if (step_ignore.count(key))
                            step_type = "attach";

                        auto rp = std::make_shared<SlideRelayPoint>();
                        rp->beat = beat;
                        rp->ease = ease;
                        rp->lane = lane;
                        rp->size = size;
                        rp->timeScaleGroup = note.til;
                        rp->type = step_type;
                        rp->critical = mid_critical;
                        rp->speedRatio = note.speedRatio;
                        slide_note->append(rp);
                    }
                }
                notes.push_back(slide_note);
            }
        }
    };

    _process_holds(sus_slides, false);
    _process_holds(sus_guides, true);

    std::stable_sort(notes.begin(), notes.end(), [](const NodePtr& a, const NodePtr& b) {
        return get_sus_sort_number(a.get()) < get_sus_sort_number(b.get());
    });

    auto metadata = std::make_shared<MetaData>();
    metadata->title = title;
    metadata->artist = artist;
    metadata->designer = designer;
    metadata->waveoffset = -wave_offset;
    if (!requests.empty())
        metadata->requests = requests;
    else
        metadata->requests = std::nullopt;

    return core::Score(metadata, notes);
}

}  // namespace rg::converters::sus
