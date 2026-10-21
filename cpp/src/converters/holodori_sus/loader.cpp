// holodori sus -> Score. a sekai-sus dialect decoded off the game's SusConverter.

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "rg/converters/holodori_sus.hpp"
#include "rg/converters/sus_common.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::holodori_sus {

using namespace rg::core;
using namespace rg::util;
using rg::converters::sus::_Bar;
using rg::converters::sus::_get_bars;
using rg::converters::sus::_get_note_stream;
using rg::converters::sus::_get_ticks;
using rg::converters::sus::_note_key;
using rg::converters::sus::_sus_to_usc_lane;
using rg::converters::sus::_sus_to_usc_size;
using rg::converters::sus::_SusNote;
using rg::converters::sus::_tick_to_beat;
using rg::converters::sus::MAX_LANE;
using rg::converters::sus::MIN_LANE;
using rg::converters::sus::TICKS_PER_BEAT;

namespace {

// SusUtility.CharToInt: case-sensitive base62 (0-9, a-z = 10-35, A-Z = 36-61)
long long _char_to_int(char c) {
    int o = static_cast<unsigned char>(c);
    if (48 <= o && o <= 57) return o - 48;
    if (97 <= o && o <= 122) return o - 87;
    if (65 <= o && o <= 90) return o - 29;
    return 0;
}

// parse a note line. the data is a grid of positions: a "0x" token skips (base62 d1*62 + d2)
// empty positions (compressed empties); any other 2-char cell is one position (kind = char0,
// width = char1; kind 0 = empty). beat = position / total positions.
std::vector<_SusNote> _holodori_notes(const std::string& /*header*/, char lane_char,
                                      const std::string& data, const std::vector<_Bar>& bars,
                                      long long measure, long long til) {
    std::vector<_SusNote> notes;
    long long lane = _char_to_int(lane_char);
    std::vector<std::tuple<long long, long long, long long>> parsed;  // (position, kind, width)
    std::size_t i = 0;
    long long pos = 0;
    std::size_t n = data.size();
    while (i < n) {
        if (i + 4 <= n && data[i] == '0' && data[i + 1] == 'x') {
            pos += _char_to_int(data[i + 2]) * 62 + _char_to_int(data[i + 3]);
            i += 4;
        } else if (i + 2 <= n) {
            if (data[i] != '0')
                parsed.push_back({pos, _char_to_int(data[i]), _char_to_int(data[i + 1])});
            pos += 1;
            i += 2;
        } else {
            break;
        }
    }
    long long total = pos;
    if (total <= 0) return notes;
    for (auto& [p, kind, width] : parsed)
        notes.push_back({_get_ticks(bars, measure, p, total), lane, width, kind, til, 1.0});
    return notes;
}

// bpm (08) / nms (0A) grid -> (position, id, total). same 0x-compressed grid, but a leading-0
// cell like "05" is id 5 (only "00" is empty), so it can't reuse _holodori_notes.
std::vector<std::tuple<long long, std::string, long long>> _meta_cells(std::string data) {
    data = strip(data);
    std::vector<std::pair<long long, std::string>> cells;
    std::size_t i = 0;
    long long pos = 0;
    std::size_t n = data.size();
    while (i < n) {
        if (i + 4 <= n && data[i] == '0' && data[i + 1] == 'x') {
            pos += _char_to_int(data[i + 2]) * 62 + _char_to_int(data[i + 3]);
            i += 4;
        } else if (i + 2 <= n) {
            std::string cell = data.substr(i, 2);
            if (cell != "00") cells.push_back({pos, cell});
            pos += 1;
            i += 2;
        } else {
            break;
        }
    }
    std::vector<std::tuple<long long, std::string, long long>> out;
    if (pos > 0)
        for (auto& [p, c] : cells) out.push_back({p, c, pos});
    return out;
}

// 0B/0C/0D grid -> (position, value, total). the cell is a base62 pair, so "10" is 62.
std::vector<std::tuple<long long, long long, long long>> _meta_values(const std::string& data) {
    std::vector<std::tuple<long long, long long, long long>> out;
    for (auto& [pos, cell, total] : _meta_cells(data))
        out.push_back({pos, _char_to_int(cell[0]) * 62 + _char_to_int(cell[1]), total});
    return out;
}

std::optional<std::string> fever_point_type(char lane_char, long long value) {
    if (lane_char == 'C' && value == 1) return std::string("chargeStart");
    if (lane_char == 'C' && value == 2) return std::string("chargeEnd");
    if (lane_char == 'D' && value == 1) return std::string("feverStart");
    if (lane_char == 'D' && value == 2) return std::string("feverEnd");
    return std::nullopt;
}

std::pair<core::Score, long long> _holodori_to_score(
    std::vector<_SusNote> taps, std::vector<_SusNote> directionals,
    std::vector<std::vector<_SusNote>> slides,
    std::vector<std::pair<std::vector<_SusNote>, std::optional<std::string>>> ghosts,
    std::vector<std::pair<long long, double>> bpms,
    std::vector<std::pair<long long, double>> nms,
    std::vector<std::pair<long long, long long>> sp_skill_points,
    std::map<std::string, std::vector<long long>> fever_ticks, std::string music_id,
    double wave_offset, long long ticks_per_beat);

}  // namespace

std::pair<core::Score, long long> loads(const std::string& data) {
    long long ticks_per_beat = TICKS_PER_BEAT;  // positions are fractional i/N; the file's ticks_per_beat is ignored
    std::string music_id;
    double wave_offset = 0.0;
    double base_bpm = 120.0;

    std::vector<std::pair<long long, double>> bar_lengths;
    std::map<std::string, double> bpm_definitions;
    std::vector<std::pair<long long, double>> bpm_data_lines;
    std::map<std::string, double> nms_definitions;
    std::vector<std::pair<long long, double>> nms_data_lines;
    std::map<std::string, std::string> color_definitions;

    std::vector<std::pair<long long, long long>> sp_skill_points;  // (tick, deck slot)
    std::map<std::string, std::vector<long long>> fever_ticks;
    for (const char* name : {"chargeStart", "chargeEnd", "feverStart", "feverEnd"})
        fever_ticks[name] = {};

    std::vector<_SusNote> taps;
    std::vector<_SusNote> directionals;
    std::vector<long long> slide_channel_order;
    std::map<long long, std::vector<_SusNote>> slide_streams;
    std::vector<long long> ghost_channel_order;
    std::map<long long, std::vector<_SusNote>> ghost_streams;
    std::map<long long, std::string> ghost_colors;

    static const std::regex ghost_re(R"(^(\d{3})[0-9a-z]([0-9a-z])([0-9a-zA-Z])([0-9A-Fa-f]{4})$)");
    static const std::regex long_re(R"(^#(\d{3})3([0-9a-z])([0-9a-zA-Z]):(.+)$)");
    static const std::regex sp_skill_re(R"(^(\d{3}) (\d+)/(\d+) (\d+))");

    auto lines = splitlines(data);

    // PHASE 1: bar lengths
    for (auto& raw : lines) {
        std::string line = strip(raw);
        if (!startswith(line, "#")) continue;
        std::size_t colon = line.find(':', 1);
        if (colon != std::string::npos) {
            std::string header = strip(line.substr(1, colon - 1));
            if (header.size() == 5 && header.substr(3) == "02" && is_digits(header.substr(0, 3)))
                bar_lengths.push_back(
                    {py_int(header.substr(0, 3)), py_float(strip(line.substr(colon + 1)))});
        }
    }
    if (bar_lengths.empty()) bar_lengths.push_back({0, 4.0});
    std::vector<_Bar> bars = _get_bars(bar_lengths, ticks_per_beat);

    // PHASE 2: everything else
    for (auto& raw : lines) {
        std::string line = strip(raw);
        if (!startswith(line, "#")) continue;
        std::size_t colon = line.find(':', 1);

        if (colon == std::string::npos) {  // space-separated meta directive
            std::string rest = line.substr(1);
            // split(None, 1)
            std::size_t ws = 0;
            while (ws < rest.size() && !py_isspace_ch(rest[ws])) ++ws;
            std::string key = to_upper(rest.substr(0, ws));
            std::string val;
            if (ws < rest.size()) {
                std::size_t vs = ws;
                while (vs < rest.size() && py_isspace_ch(rest[vs])) ++vs;
                val = strip_chars(strip(rest.substr(vs)), "\"");
            }
            if (key == "MUSIC_ID") {
                music_id = val;
            } else if (key == "WAVEOFFSET") {
                wave_offset = py_float(val.empty() ? "0" : val);
            } else if (key == "BASEBPM") {
                base_bpm = py_float(val.empty() ? "120" : val);
            } else if (key == "SP_SKILL") {  // legacy sp skill line: #SP_SKILL mmm num/den slot
                std::smatch sm;
                if (std::regex_search(val, sm, sp_skill_re)) {
                    long long den = py_int(sm[3].str());
                    long long tick = _get_ticks(bars, py_int(sm[1].str()),
                                                den ? py_int(sm[2].str()) : 0, den ? den : 1);
                    sp_skill_points.push_back({tick, py_int(sm[4].str())});
                }
            }
            continue;
        }

        std::string header = strip(line.substr(1, colon - 1));
        std::string line_data = strip(line.substr(colon + 1));

        // ghost: #mmm 9 lane channel + 4-hex color id
        std::smatch gm;
        if (std::regex_match(header, gm, ghost_re)) {
            long long measure = py_int(gm[1].str());
            long long channel = py_int_base36(gm[3].str());
            ghost_colors[channel] = gm[4].str();
            if (ghost_streams.find(channel) == ghost_streams.end())
                ghost_channel_order.push_back(channel);
            auto ns = _holodori_notes(header, gm[2].str()[0], line_data, bars, measure, 0);
            auto& v = ghost_streams[channel];
            v.insert(v.end(), ns.begin(), ns.end());
            continue;
        }

        if (startswith(header, "COLOR") && header.size() == 9) {  // #COLORwwww: rgba (8 hex incl alpha)
            color_definitions[header.substr(5)] = strip(line_data);
            continue;
        }
        if (startswith(header, "BPM") && header.size() == 5) {
            bpm_definitions[header.substr(3)] = py_float(line_data);
            continue;
        }
        if (startswith(header, "NMS") && header.size() == 5) {
            nms_definitions[header.substr(3)] = py_float(line_data);
            continue;
        }
        if (header.size() != 5 || !is_digits(header.substr(0, 3))) continue;
        long long measure = py_int(header.substr(0, 3));
        char event = header[3], lane_char = header[4];
        std::string chan = header.substr(3);  // header[3:]

        if (chan == "02") continue;  // bar length, handled in phase 1
        if (chan == "08") {          // bpm change: each cell is a #BPMxx id
            for (auto& [pos, cell, total] : _meta_cells(line_data)) {
                double bpm = base_bpm;
                auto it = bpm_definitions.find(cell);
                if (it != bpm_definitions.end()) bpm = it->second;
                bpm_data_lines.push_back({_get_ticks(bars, measure, pos, total), bpm});
            }
            continue;
        }
        if (chan == "0A") {  // NMS (hispeed) change: each cell is a #NMSxx id
            for (auto& [pos, cell, total] : _meta_cells(line_data)) {
                double mul = 1.0;
                auto it = nms_definitions.find(cell);
                if (it != nms_definitions.end()) mul = it->second;
                nms_data_lines.push_back({_get_ticks(bars, measure, pos, total), mul});
            }
            continue;
        }
        if (chan == "0B") {  // sp skill: each cell is a deck slot number (1-5)
            for (auto& [pos, value, total] : _meta_values(line_data))
                if (value >= 1)
                    sp_skill_points.push_back({_get_ticks(bars, measure, pos, total), value});
            continue;
        }
        if (chan == "0C" || chan == "0D") {  // fever charge / fever section
            for (auto& [pos, value, total] : _meta_values(line_data)) {
                auto pt = fever_point_type(lane_char, value);
                if (pt.has_value())
                    fever_ticks[*pt].push_back(_get_ticks(bars, measure, pos, total));
            }
            continue;
        }

        if (event == '1') {  // tap channel
            auto ns = _holodori_notes(header, lane_char, line_data, bars, measure, 0);
            taps.insert(taps.end(), ns.begin(), ns.end());
        } else if (event == '5') {  // flick / directional channel
            auto ns = _holodori_notes(header, lane_char, line_data, bars, measure, 0);
            directionals.insert(directionals.end(), ns.begin(), ns.end());
        }
    }

    // long notes: #mmm 3 lane channel  (channel groups one hold's segments)
    for (auto& raw : lines) {
        std::string line = strip(raw);
        std::smatch m;
        if (!std::regex_match(line, m, long_re)) continue;
        long long measure = py_int(m[1].str());
        long long channel = py_int_base36(m[3].str());
        if (slide_streams.find(channel) == slide_streams.end())
            slide_channel_order.push_back(channel);
        auto ns = _holodori_notes(m[1].str() + "3" + m[2].str() + m[3].str(), m[2].str()[0],
                                  strip(m[4].str()), bars, measure, 0);
        auto& v = slide_streams[channel];
        v.insert(v.end(), ns.begin(), ns.end());
    }

    // group each channel into holds: FIFO-match each end (kind 2) to the OLDEST open start (kind 1),
    // taking the relays between them. LIFO would fuse interleaved slides into chart-long holds. de-dup
    // exact repeats first, since the dense multi-line encoding repeats points.
    std::vector<std::vector<_SusNote>> slides;
    for (long long ch : slide_channel_order) {
        auto& stream = slide_streams[ch];
        std::set<std::tuple<long long, long long, long long, long long>> seen;
        std::vector<_SusNote> notes_sorted;
        std::vector<_SusNote> stream_sorted = stream;
        std::stable_sort(stream_sorted.begin(), stream_sorted.end(),
                         [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
        for (auto& nn : stream_sorted) {
            auto key = std::make_tuple(nn.tick, nn.lane, nn.type, nn.width);
            if (!seen.count(key)) {
                seen.insert(key);
                notes_sorted.push_back(nn);
            }
        }
        std::vector<_SusNote> start_stack;
        std::vector<_SusNote> relay_stock;
        for (auto& nn : notes_sorted) {
            if (nn.type == 1) {  // start
                start_stack.push_back(nn);
            } else if (nn.type == 2) {  // end
                std::optional<_SusNote> matched;
                for (std::size_t j = 0; j < start_stack.size(); ++j) {
                    if (start_stack[j].tick <= nn.tick) {
                        matched = start_stack[j];
                        start_stack.erase(start_stack.begin() + static_cast<long long>(j));
                        break;
                    }
                }
                if (!matched.has_value()) continue;  // unmatched end is dropped
                std::vector<_SusNote> relays;
                for (auto it = relay_stock.begin(); it != relay_stock.end();) {
                    if (matched->tick <= it->tick && it->tick <= nn.tick) {
                        relays.push_back(*it);
                        it = relay_stock.erase(it);
                    } else {
                        ++it;
                    }
                }
                std::stable_sort(relays.begin(), relays.end(),
                                 [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
                std::vector<_SusNote> hold;
                hold.push_back(*matched);
                hold.insert(hold.end(), relays.begin(), relays.end());
                hold.push_back(nn);
                slides.push_back(hold);
            } else {  // relay
                relay_stock.push_back(nn);
            }
        }
    }

    std::vector<std::pair<std::vector<_SusNote>, std::optional<std::string>>> ghosts;
    for (long long ch : ghost_channel_order) {
        auto& stream = ghost_streams[ch];
        std::string color_id = ghost_colors.count(ch) ? ghost_colors[ch] : "";
        std::optional<std::string> rgba;
        auto it = color_definitions.find(color_id);
        if (it != color_definitions.end())
            rgba = it->second;
        else if (!color_id.empty())
            rgba = color_id;
        else
            rgba = std::nullopt;
        for (auto& hold : _get_note_stream(stream)) ghosts.push_back({hold, rgba});
    }

    std::stable_sort(bpm_data_lines.begin(), bpm_data_lines.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });

    return _holodori_to_score(taps, directionals, slides, ghosts, bpm_data_lines,
                              nms_data_lines, sp_skill_points, fever_ticks, music_id,
                              wave_offset, ticks_per_beat);
}

namespace {

std::pair<core::Score, long long> _holodori_to_score(
    std::vector<_SusNote> taps, std::vector<_SusNote> directionals,
    std::vector<std::vector<_SusNote>> slides,
    std::vector<std::pair<std::vector<_SusNote>, std::optional<std::string>>> ghosts,
    std::vector<std::pair<long long, double>> bpms,
    std::vector<std::pair<long long, double>> nms,
    std::vector<std::pair<long long, long long>> sp_skill_points,
    std::map<std::string, std::vector<long long>> fever_ticks, std::string music_id,
    double wave_offset, long long ticks_per_beat) {
    // returns (score, bezier_tick_count). overlay lookup sets keyed by (tick, lane)
    std::set<std::string> criticals;
    std::set<std::string> step_ignore;  // a relay marked here is attach/skip, not a shape point
    std::map<std::string, std::string> flicks;
    std::set<std::string> ease_ins;
    std::set<std::string> ease_outs;

    for (auto& d : directionals) {
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

    for (auto& t : taps) {
        std::string key = _note_key(t.tick, t.lane);
        if (t.type == 2 || t.type == 6)
            criticals.insert(key);
        else if (t.type == 3)
            step_ignore.insert(key);
    }

    std::vector<NodePtr> notes;

    std::vector<std::pair<long long, double>> bpm_src = bpms;
    if (bpm_src.empty()) bpm_src.push_back({0, 120.0});
    for (auto& [tick, bpm] : bpm_src)
        notes.push_back(std::make_shared<Bpm>(Num::F(_tick_to_beat(tick)), Num::F(bpm)));

    // NMS is a single hispeed layer -> one TimeScaleGroup
    auto tsg = std::make_shared<TimeScaleGroup>();
    if (!nms.empty()) {
        std::vector<std::pair<long long, double>> nms_sorted = nms;
        std::stable_sort(nms_sorted.begin(), nms_sorted.end(),
                         [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [tick, mul] : nms_sorted)
            tsg->append(std::make_shared<TimeScalePoint>(Num::F(_tick_to_beat(tick)), Num::F(mul)));
        bool has0 = false;
        for (auto& p : tsg->changes)
            if ((double)p->beat == 0.0) has0 = true;
        if (!has0) tsg->insert(0, std::make_shared<TimeScalePoint>(Num::F(0.0), Num::F(1.0)));
    } else {
        tsg->append(std::make_shared<TimeScalePoint>(Num::F(0.0), Num::F(1.0)));
    }
    notes.push_back(tsg);

    std::set<std::pair<long long, long long>> sp_set;
    for (auto& sp : sp_skill_points) sp_set.insert(sp);
    for (auto& [tick, slot] : sp_set)
        notes.push_back(std::make_shared<HolodoriSkill>(Num::F(_tick_to_beat(tick)), slot));

    // the game drops the whole section unless it has exactly one of each point, in order
    std::vector<std::vector<long long>*> points;
    std::vector<std::string> pnames = {"chargeStart", "chargeEnd", "feverStart", "feverEnd"};
    bool all_one = true;
    for (auto& nm : pnames)
        if (fever_ticks[nm].size() != 1) all_one = false;
    if (all_one) {
        long long charge_start = fever_ticks["chargeStart"][0];
        long long charge_end = fever_ticks["chargeEnd"][0];
        long long fever_start = fever_ticks["feverStart"][0];
        long long fever_end = fever_ticks["feverEnd"][0];
        if (charge_start <= charge_end && charge_end <= fever_start && fever_start <= fever_end) {
            notes.push_back(std::make_shared<HolodoriChargeStart>(Num::F(_tick_to_beat(charge_start))));
            notes.push_back(std::make_shared<HolodoriChargeEnd>(Num::F(_tick_to_beat(charge_end))));
            notes.push_back(std::make_shared<HolodoriFeverStart>(Num::F(_tick_to_beat(fever_start))));
            notes.push_back(std::make_shared<HolodoriFeverEnd>(Num::F(_tick_to_beat(fever_end))));
        }
    }

    // SINGLES. NORMAL (tap kind-1) and FLICK (directional kind-1/3/4) are disjoint; a tap with a
    // co-located directional is one flick. a directional on a hold start/end belongs to that slide.
    std::set<std::string> slide_end_keys;
    std::set<std::string> slide_start_keys;
    for (auto& hold : slides)
        if (hold.size() >= 2) {
            slide_end_keys.insert(_note_key(hold.back().tick, hold.back().lane));
            slide_start_keys.insert(_note_key(hold.front().tick, hold.front().lane));
        }
    std::set<std::string> tap_keys;
    for (auto& nn : taps)
        if (nn.type == 1) tap_keys.insert(_note_key(nn.tick, nn.lane));

    auto flick_get = [&](const std::string& key) -> std::optional<std::string> {
        auto it = flicks.find(key);
        if (it != flicks.end()) return it->second;
        return std::nullopt;
    };

    for (auto& nn : taps) {
        if (nn.type != 1 || nn.lane < MIN_LANE || nn.lane > MAX_LANE) continue;
        std::string key = _note_key(nn.tick, nn.lane);
        if (slide_start_keys.count(key)) continue;  // this tap is the hold head (emitted by the slide)
        auto direction = flick_get(key);  // a co-located directional makes this one flick, not tap + flick
        auto s = std::make_shared<Single>();
        s->beat = Num::F(_tick_to_beat(nn.tick));
        s->critical = criticals.count(key) != 0;
        s->lane = Num::F(_sus_to_usc_lane(nn.lane, nn.width));
        s->size = Num::F(_sus_to_usc_size(nn.width));
        s->timeScaleGroup = 0;
        s->speedRatio = Num::F(nn.speedRatio);
        s->trace = direction.has_value();
        s->direction = direction;
        notes.push_back(s);
    }
    for (auto& nn : directionals) {  // standalone flicks (no tap / hold under them)
        std::optional<std::string> direction;
        if (nn.type == 1) direction = "up";
        else if (nn.type == 3) direction = "left";
        else if (nn.type == 4) direction = "right";  // 2/5/6 are slide-ease markers
        if (!direction.has_value() || nn.lane < MIN_LANE || nn.lane > MAX_LANE) continue;
        std::string key = _note_key(nn.tick, nn.lane);
        if (slide_end_keys.count(key) || slide_start_keys.count(key) || tap_keys.count(key))
            continue;
        auto s = std::make_shared<Single>();
        s->beat = Num::F(_tick_to_beat(nn.tick));
        s->critical = criticals.count(key) != 0;
        s->lane = Num::F(_sus_to_usc_lane(nn.lane, nn.width));
        s->size = Num::F(_sus_to_usc_size(nn.width));
        s->timeScaleGroup = 0;
        s->speedRatio = Num::F(nn.speedRatio);
        s->trace = true;
        s->direction = direction;
        notes.push_back(s);
    }

    // tap kind-2 = critical taps. a co-located one only upgrades that note (via `criticals`); the
    // standalone rest are their own critical taps.
    std::set<std::string> emitted;
    for (auto& nn : taps)
        if (nn.type == 1) emitted.insert(_note_key(nn.tick, nn.lane));
    for (auto& nn : directionals)
        if (nn.type == 1 || nn.type == 3 || nn.type == 4)
            emitted.insert(_note_key(nn.tick, nn.lane));
    for (auto& hold : slides)
        for (auto& p : hold) emitted.insert(_note_key(p.tick, p.lane));
    for (auto& t : taps) {
        if (t.type != 2 || t.lane < MIN_LANE || t.lane > MAX_LANE) continue;
        std::string key = _note_key(t.tick, t.lane);
        if (emitted.count(key)) continue;
        auto s = std::make_shared<Single>();
        s->beat = Num::F(_tick_to_beat(t.tick));
        s->critical = true;
        s->lane = Num::F(_sus_to_usc_lane(t.lane, t.width));
        s->size = Num::F(_sus_to_usc_size(t.width));
        s->timeScaleGroup = 0;
        s->speedRatio = Num::F(t.speedRatio);
        s->trace = false;
        s->direction = std::nullopt;
        notes.push_back(s);
    }

    // HOLDS. first point = start (trace-tapped head), last = end (trace), rest = relays by kind:
    //   3 LongRelayActive: visible + combo -> attach (no shape change) if step_ignore, else tick (bends)
    //   4 LongBezierPoint / 5 LongRelayDeActive: hidden, shape, no combo (bezier pull is lost in usc)
    long long bezier_ticks = 0;
    for (auto& hold : slides) {
        if (hold.size() < 2) continue;
        std::string start_key = _note_key(hold.front().tick, hold.front().lane);
        bool critical = criticals.count(start_key) != 0;
        auto slide = std::make_shared<Slide>(critical);
        long long last = static_cast<long long>(hold.size()) - 1;
        for (long long idx = 0; idx < static_cast<long long>(hold.size()); ++idx) {
            _SusNote& nn = hold[static_cast<std::size_t>(idx)];
            std::string key = _note_key(nn.tick, nn.lane);
            std::string ease = ease_ins.count(key) ? "in"
                               : (ease_outs.count(key) ? "out" : "linear");
            Num beat = Num::F(_tick_to_beat(nn.tick));
            Num lane = Num::F(_sus_to_usc_lane(nn.lane, nn.width));
            Num size = Num::F(_sus_to_usc_size(nn.width));

            if (idx == 0) {
                auto p = std::make_shared<SlideStartPoint>();
                p->beat = beat;
                p->critical = critical;
                p->ease = ease;
                p->judgeType = "normal";
                p->lane = lane;
                p->size = size;
                p->timeScaleGroup = 0;
                p->speedRatio = Num::F(nn.speedRatio);
                slide->append(p);
            } else if (idx == last) {
                auto p = std::make_shared<SlideEndPoint>();
                p->beat = beat;
                p->critical = critical || criticals.count(key) != 0;
                p->judgeType = "trace";
                p->lane = lane;
                p->size = size;
                p->timeScaleGroup = 0;
                p->speedRatio = Num::F(nn.speedRatio);
                p->direction = flick_get(key);
                slide->append(p);
            } else if (nn.type == 3) {
                auto p = std::make_shared<SlideRelayPoint>();
                p->beat = beat;
                p->ease = ease;
                p->lane = lane;
                p->size = size;
                p->timeScaleGroup = 0;
                p->type = step_ignore.count(key) ? "attach" : "tick";
                p->critical = critical;
                p->speedRatio = Num::F(nn.speedRatio);
                slide->append(p);
            } else if (nn.type == 4) {
                // LongBezierPoint: a hidden bezier shape control - no combo, no score, and the bezier
                // pull is already lost in usc, so drop it from the note stream entirely. it is tallied
                // only because the live-score perfect-coefficient sum still counts it (a game bug).
                bezier_ticks += 1;
                continue;
            } else {
                // LongRelayDeActive: an invisible tick - hidden, no combo, no score
                auto p = std::make_shared<SlideRelayPoint>();
                p->beat = beat;
                p->ease = ease;
                p->lane = lane;
                p->size = size;
                p->timeScaleGroup = 0;
                p->type = "tick";
                p->critical = std::nullopt;
                p->speedRatio = Num::F(nn.speedRatio);
                slide->append(p);
            }
        }
        slide->sort();
        notes.push_back(slide);
    }

    // ghost notes -> guides. holodori guides hold at the #COLOR alpha (no fade); rgba in color_code.
    for (auto& [hold, color_code] : ghosts) {
        if (hold.size() < 2) continue;
        auto guide = std::make_shared<Guide>("green", "none", std::vector<shared_ptr<GuidePoint>>{}, "guide", color_code);
        for (auto& nn : hold) {
            std::string key = _note_key(nn.tick, nn.lane);
            std::string ease = ease_ins.count(key) ? "in"
                               : (ease_outs.count(key) ? "out" : "linear");
            auto p = std::make_shared<GuidePoint>();
            p->beat = Num::F(_tick_to_beat(nn.tick));
            p->ease = ease;
            p->lane = Num::F(_sus_to_usc_lane(nn.lane, nn.width));
            p->size = Num::F(_sus_to_usc_size(nn.width));
            p->timeScaleGroup = 0;
            p->speedRatio = Num::F(nn.speedRatio);
            guide->append(p);
        }
        notes.push_back(guide);
    }

    std::stable_sort(notes.begin(), notes.end(), [](const NodePtr& a, const NodePtr& b) {
        return get_sus_sort_number(a.get()) < get_sus_sort_number(b.get());
    });
    auto metadata = std::make_shared<MetaData>();
    metadata->title = music_id;
    metadata->artist = "";
    metadata->designer = "";
    metadata->waveoffset = -wave_offset;
    metadata->requests = std::vector<std::string>{"ticks_per_beat " + std::to_string(ticks_per_beat)};
    return {core::Score(metadata, notes), bezier_ticks};
}

}  // namespace

}  // namespace rg::converters::holodori_sus
