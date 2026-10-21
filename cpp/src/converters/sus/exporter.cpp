#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "rg/converters/sus.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pynum.hpp"
#include "rg/util/pyops.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::sus {

using namespace rg::core;
using namespace rg::util;

static const char* RG_VERSION = "2.0.0";

static const long long TICKS_PER_BEAT = 480;

static long long _beat_to_tick(double beat) {
    return py_round_int(TICKS_PER_BEAT * beat);
}

static long long _usc_to_sus_lane(double lane, double size) {
    return py_trunc(lane - size + 8);
}

static long long _usc_to_sus_width(double size) {
    return static_cast<long long>(std::ceil(size * 2));
}

static std::string _note_key(long long tick, long long lane) {
    return std::to_string(tick) + "-" + std::to_string(lane);
}

static std::string _to_base36(long long n) {
    if (n < 0) return "0";
    static const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (n == 0) return "0";
    std::string result;
    while (n > 0) {
        result = std::string(1, chars[n % 36]) + result;
        n /= 36;
    }
    return result;
}

static std::string _format_number(double value) {
    long long iv = py_trunc(value);
    if (static_cast<double>(iv) == value) return std::to_string(iv);
    return py_float_repr(value);
}

// SUS INTERMEDIATE DATA

// File-local helper types (internal linkage so they don't clash with the sus loader's
// own _SusNote in the same namespace -- the Python originals live in separate modules).
namespace {

struct _SusNote {
    long long tick;
    long long lane;
    long long width;
    long long type;
    double speedRatio = 1.0;
};

struct _ChannelProvider {
    std::array<std::pair<long long, long long>, 36> channels{};
    long long generate(long long start_tick, long long end_tick) {
        for (long long key = 0; key < 36; ++key) {
            long long s = channels[static_cast<std::size_t>(key)].first;
            long long e = channels[static_cast<std::size_t>(key)].second;
            if ((s == 0 && e == 0) || end_tick < s || start_tick > e) {
                channels[static_cast<std::size_t>(key)] = {start_tick, end_tick};
                return key;
            }
        }
        throw std::runtime_error("No more slide channels available");
    }
};

// SUS WRITING

struct _NoteMap {
    std::vector<std::tuple<long long, std::string, double>> data;
    long long ticks_per_measure = 0;
};

// ordered dict of info -> _NoteMap
struct MeasureNotes {
    std::vector<std::string> order;
    std::map<std::string, _NoteMap> m;
    _NoteMap& setdefault(const std::string& info) {
        auto it = m.find(info);
        if (it == m.end()) {
            order.push_back(info);
            return m[info];
        }
        return it->second;
    }
};

using Pair = std::pair<long long, double>;

struct ScoreToSus {
    std::vector<_SusNote> taps;
    std::vector<_SusNote> directionals;
    std::vector<std::vector<_SusNote>> slides;
    std::vector<std::vector<_SusNote>> guides;
    std::vector<Pair> bpms;
    std::vector<Pair> bar_lengths;
    std::vector<std::vector<Pair>> tils;
    std::vector<Pair> volumes;
};

}  // anonymous namespace

static ScoreToSus _score_to_sus(const Score& score) {
    ScoreToSus R;
    R.bar_lengths = {{0, 4.0}};

    std::set<std::string> critical_keys;

    long long til_index = 0;
    for (auto& note_sp : convert_holodori_events(score.notes)) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Bpm) {
            auto* b = static_cast<Bpm*>(note);
            R.bpms.push_back({_beat_to_tick(b->beat), b->bpm});

        } else if (note->kind == Kind::Volume) {
            auto* v = static_cast<Volume*>(note);
            R.volumes.push_back({_beat_to_tick(v->beat), v->volume});

        } else if (note->kind == Kind::TimeScaleGroup) {
            std::vector<Pair> til;
            for (auto& cp : static_cast<TimeScaleGroup*>(note)->changes) {
                til.push_back({_beat_to_tick(cp->beat), cp->timeScale});
            }
            R.tils.push_back(til);
            til_index += 1;

        } else if (note->kind == Kind::Skill) {
            R.taps.push_back({_beat_to_tick(static_cast<Skill*>(note)->beat), 0, 1, 4});

        } else if (note->kind == Kind::FeverChance) {
            R.taps.push_back({_beat_to_tick(static_cast<FeverChance*>(note)->beat), 15, 1, 1});

        } else if (note->kind == Kind::FeverStart) {
            R.taps.push_back({_beat_to_tick(static_cast<FeverStart*>(note)->beat), 15, 1, 2});

        } else if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            long long tick = _beat_to_tick(s->beat);
            long long lane = _usc_to_sus_lane(s->lane, s->size);
            long long width = _usc_to_sus_width(s->size);
            double sr = s->speedRatio;

            long long tap_type = (s->trace.has_value() && s->trace.value()) ? 5 : 1;
            if (s->critical.has_value() && s->critical.value()) {
                tap_type += 1;
                critical_keys.insert(_note_key(tick, lane));
            }

            R.taps.push_back({tick, lane, width, tap_type, sr});

            if (s->direction.has_value()) {
                long long dir_type = *s->direction == "up"     ? 1
                                     : *s->direction == "left"  ? 3
                                                                : 4;
                R.directionals.push_back({tick, lane, width, dir_type, sr});
            }

        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            std::vector<_SusNote> slide;
            std::vector<NodePtr> conns = sl->connections;
            std::stable_sort(conns.begin(), conns.end(), [](const NodePtr& a, const NodePtr& b) {
                return node_beat(a.get()) < node_beat(b.get());
            });

            for (auto& step_sp : conns) {
                Node* step = step_sp.get();
                long long tick = _beat_to_tick(node_beat(step));
                long long lane = _usc_to_sus_lane(node_lane(step), node_size(step));
                long long width = _usc_to_sus_width(node_size(step));
                double sr = (step->kind == Kind::SlideStartPoint)
                                ? static_cast<SlideStartPoint*>(step)->speedRatio
                            : (step->kind == Kind::SlideRelayPoint)
                                ? static_cast<SlideRelayPoint*>(step)->speedRatio
                                : static_cast<SlideEndPoint*>(step)->speedRatio;

                if (step->kind == Kind::SlideStartPoint) {
                    auto* p = static_cast<SlideStartPoint*>(step);
                    slide.push_back({tick, lane, width, 1, sr});

                    bool has_ease = p->ease != "linear";
                    bool is_hidden = p->judgeType == "none";
                    bool is_trace = p->judgeType == "trace";

                    if (has_ease) {
                        long long dir_type = p->ease == "in" ? 2 : 6;
                        R.directionals.push_back({tick, lane, width, dir_type, sr});
                    }

                    long long tap_type = is_hidden ? 7 : (is_trace ? 5 : 1);
                    bool already_critical = critical_keys.count(_note_key(tick, lane)) != 0;
                    if (p->critical && !already_critical) {
                        tap_type += 1;
                        critical_keys.insert(_note_key(tick, lane));
                    }

                    if (tap_type > 1) R.taps.push_back({tick, lane, width, tap_type, sr});

                } else if (step->kind == Kind::SlideRelayPoint) {
                    auto* p = static_cast<SlideRelayPoint*>(step);
                    // Normal (type 3): changes shape + adds combo (tick + critical)
                    // Hidden (type 5): changes shape, no combo (tick + critical=None)
                    // Skip (type 3 + tap): no shape change, adds combo (attach + critical)
                    long long slide_type = !p->critical.has_value() ? 5 : 3;
                    slide.push_back({tick, lane, width, slide_type, sr});

                    if (p->type == "attach") {
                        R.taps.push_back({tick, lane, width, 3, sr});
                    } else if (p->critical.has_value() && p->ease != "linear") {
                        R.taps.push_back({tick, lane, width, 1, sr});
                    }

                    // Ease directional is written regardless of visibility
                    if (p->ease != "linear") {
                        long long dir_type = p->ease == "in" ? 2 : 6;
                        R.directionals.push_back({tick, lane, width, dir_type, sr});
                    }

                } else if (step->kind == Kind::SlideEndPoint) {
                    auto* p = static_cast<SlideEndPoint*>(step);
                    slide.push_back({tick, lane, width, 2, sr});

                    bool is_hidden = p->judgeType == "none";
                    bool is_trace = p->judgeType == "trace";

                    if (p->direction.has_value()) {
                        long long dir_type = *p->direction == "up"     ? 1
                                             : *p->direction == "left"  ? 3
                                                                        : 4;
                        R.directionals.push_back({tick, lane, width, dir_type, sr});

                        if (p->critical && !sl->critical && !is_trace) {
                            R.taps.push_back({tick, lane, width, 2, sr});
                        }
                    }

                    long long end_type = is_hidden ? 7 : (is_trace ? 5 : 1);
                    if (p->critical) {
                        end_type += 1;
                        critical_keys.insert(_note_key(tick, lane));
                    }

                    if (end_type != 1 && end_type != 2)
                        R.taps.push_back({tick, lane, width, end_type, sr});
                }
            }

            R.slides.push_back(slide);

        } else if (note->kind == Kind::Guide) {
            auto* gd = static_cast<Guide*>(note);
            std::vector<_SusNote> guide_slide;
            std::vector<shared_ptr<GuidePoint>> points = gd->midpoints;
            std::stable_sort(points.begin(), points.end(),
                             [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                                 return a->beat < b->beat;
                             });
            bool is_critical = gd->color == "yellow";

            for (std::size_t idx = 0; idx < points.size(); ++idx) {
                GuidePoint* step = points[idx].get();
                long long tick = _beat_to_tick(step->beat);
                long long lane = _usc_to_sus_lane(step->lane, step->size);
                long long width = _usc_to_sus_width(step->size);
                double sr = step->speedRatio;

                if (idx == 0) {
                    guide_slide.push_back({tick, lane, width, 1, sr});

                    bool has_ease = step->ease != "linear";
                    if (has_ease) {
                        long long dir_type = step->ease == "in" ? 2 : 6;
                        R.directionals.push_back({tick, lane, width, dir_type, sr});
                    }

                    bool already_critical = critical_keys.count(_note_key(tick, lane)) != 0;
                    long long tap_type = 7;
                    if (is_critical && !already_critical) {
                        tap_type = 8;
                        critical_keys.insert(_note_key(tick, lane));
                    }

                    bool guide_already_on_critical = already_critical;
                    if (!(((tap_type == 7 || tap_type == 8)) && guide_already_on_critical)) {
                        R.taps.push_back({tick, lane, width, tap_type, sr});
                    }

                } else if (idx == points.size() - 1) {
                    guide_slide.push_back({tick, lane, width, 2, sr});
                    // guide ends: no flick, no friction
                    // but need critical marker if guide is critical
                    if (is_critical) {
                        bool already_critical = critical_keys.count(_note_key(tick, lane)) != 0;
                        if (!already_critical) {
                            R.taps.push_back({tick, lane, width, 8, sr});
                            critical_keys.insert(_note_key(tick, lane));
                        }
                    }

                } else {
                    guide_slide.push_back({tick, lane, width, 5, sr});

                    if (step->ease != "linear") {
                        long long tap_type = is_critical ? 8 : 7;
                        bool already_critical = critical_keys.count(_note_key(tick, lane)) != 0;
                        if (!(((tap_type == 7 || tap_type == 8)) && already_critical)) {
                            R.taps.push_back({tick, lane, width, tap_type, sr});
                        }
                        long long dir_type = step->ease == "in" ? 2 : 6;
                        R.directionals.push_back({tick, lane, width, dir_type, sr});
                    }
                }
            }

            R.guides.push_back(guide_slide);
        }
    }

    if (R.bpms.empty()) R.bpms.push_back({0, 120.0});
    (void)til_index;

    return R;
}

// SUS TEXT GENERATION

static std::string _dump_sus(const ScoreToSus& S, const Score& metadata,
                             const std::string& comment, bool measure_extensions) {
    std::vector<std::string> lines;
    long long tpb = TICKS_PER_BEAT;

    lines.push_back(comment);
    if (!metadata.metadata->title.empty())
        lines.push_back("#TITLE \"" + metadata.metadata->title + "\"");
    if (!metadata.metadata->artist.empty())
        lines.push_back("#ARTIST \"" + metadata.metadata->artist + "\"");
    if (!metadata.metadata->designer.empty())
        lines.push_back("#DESIGNER \"" + metadata.metadata->designer + "\"");
    lines.push_back("#WAVEOFFSET " + _format_number(-metadata.metadata->waveoffset));
    lines.push_back("");
    if (metadata.metadata->requests.has_value() && !metadata.metadata->requests->empty()) {
        for (auto& req : *metadata.metadata->requests)
            lines.push_back("#REQUEST \"" + req + "\"");
    } else {
        lines.push_back("#REQUEST \"ticks_per_beat " + std::to_string(tpb) + "\"");
    }
    lines.push_back("");

    // MEASUREBS: for measures >= 1000, emit #MEASUREBS <base> and use offset
    long long mbs_base = 0;
    auto zpad3 = [](long long v) {
        std::string s = std::to_string(v);
        return zfill(s, 3);
    };
    auto _mbs = [&](long long measure, std::vector<std::string>& out) -> std::string {
        if (measure_extensions) {
            long long base = (measure / 1000) * 1000;
            if (base != mbs_base) {
                out.push_back("#MEASUREBS " + std::to_string(base));
                mbs_base = base;
            }
            return zpad3(measure - base);
        }
        return zpad3(measure);
    };

    std::vector<Pair> sorted_bl = S.bar_lengths;
    std::stable_sort(sorted_bl.begin(), sorted_bl.end(),
                     [](const Pair& a, const Pair& b) { return a.first < b.first; });
    std::vector<Pair> sorted_bpms = S.bpms;
    std::stable_sort(sorted_bpms.begin(), sorted_bpms.end(),
                     [](const Pair& a, const Pair& b) { return a.first < b.first; });

    for (auto& [measure, length] : sorted_bl) {
        std::string mstr = _mbs(measure, lines);
        lines.push_back("#" + mstr + "02: " + _format_number(length));
    }
    lines.push_back("");

    // Build bar tick lookup (reversed for searching)
    long long acc = 0;
    std::vector<std::tuple<long long, double, long long>> bar_tick_list;  // (measure, length, start_tick)
    for (std::size_t i = 0; i < sorted_bl.size(); ++i) {
        long long measure = sorted_bl[i].first;
        double length = sorted_bl[i].second;
        long long start_tick = acc;
        if (i + 1 < sorted_bl.size())
            acc += py_trunc((sorted_bl[i + 1].first - measure) * length * tpb);
        bar_tick_list.push_back({measure, length, start_tick});
    }
    std::reverse(bar_tick_list.begin(), bar_tick_list.end());

    auto get_measure_from_tick = [&](long long tick) -> long long {
        for (auto& [measure, length, start_tick] : bar_tick_list) {
            if (tick >= start_tick)
                return measure +
                       py_trunc(static_cast<double>(tick - start_tick) / tpb / length);
        }
        return 0;
    };

    auto get_tick_from_measure = [&](long long m) -> long long {
        for (auto& [measure, length, start_tick] : bar_tick_list) {
            if (measure <= m)
                return start_tick + py_trunc((m - measure) * length * tpb);
        }
        return 0;
    };

    // BPM definitions
    std::map<double, std::string> bpm_ids;
    std::vector<std::pair<double, std::string>> bpm_ids_order;  // preserve insertion
    for (auto& [tick, bpm_val] : sorted_bpms) {
        if (bpm_ids.find(bpm_val) == bpm_ids.end()) {
            std::string ident = zfill(to_upper(_to_base36(static_cast<long long>(bpm_ids.size()) + 1)), 2);
            bpm_ids[bpm_val] = ident;
            lines.push_back("#BPM" + ident + ": " + _format_number(bpm_val));
        }
    }

    // BPM placements grouped by measure
    std::map<long long, std::vector<Pair>> bpm_by_measure;
    for (auto& [tick, bpm_val] : sorted_bpms) {
        long long m = get_measure_from_tick(tick);
        bpm_by_measure[m].push_back({tick, bpm_val});
    }

    for (auto& [m, bpm_entries] : bpm_by_measure) {  // std::map sorts by key
        long long m_tick = get_tick_from_measure(m);
        long long next_tick = get_tick_from_measure(m + 1);
        long long tpm = next_tick - m_tick;

        long long gcd = tpm;
        for (auto& [btick, bval] : bpm_entries) gcd = std::gcd(btick - m_tick, gcd);

        long long count = tpm / gcd;
        std::vector<std::string> bpm_data(static_cast<std::size_t>(count), "00");
        for (auto& [btick, bval] : bpm_entries) {
            long long idx = (btick - m_tick) / gcd;
            bpm_data[static_cast<std::size_t>(idx)] = bpm_ids[bval];
        }

        std::string joined;
        for (auto& x : bpm_data) joined += x;
        lines.push_back("#" + _mbs(m, lines) + "08: " + joined);
    }
    lines.push_back("");

    // TIL layers
    for (std::size_t i = 0; i < S.tils.size(); ++i) {
        std::vector<Pair> til = S.tils[i];
        std::stable_sort(til.begin(), til.end(),
                         [](const Pair& a, const Pair& b) { return a.first < b.first; });
        std::vector<std::string> entries;
        for (auto& [tick, speed] : til) {
            long long m = get_measure_from_tick(tick);
            long long offset = tick - get_tick_from_measure(m);
            entries.push_back(std::to_string(m) + "'" + std::to_string(offset) + ":" +
                              _format_number(speed));
        }
        std::string til_id = zfill(to_upper(_to_base36(static_cast<long long>(i))), 2);
        std::string joined;
        for (std::size_t k = 0; k < entries.size(); ++k) {
            if (k) joined += ", ";
            joined += entries[k];
        }
        lines.push_back("#TIL" + til_id + ": \"" + joined + "\"");
    }

    // Volume
    if (!S.volumes.empty()) {
        std::vector<Pair> vols = S.volumes;
        std::stable_sort(vols.begin(), vols.end(),
                         [](const Pair& a, const Pair& b) { return a.first < b.first; });
        std::vector<std::string> vol_entries;
        for (auto& [tick, vol] : vols) {
            long long m = get_measure_from_tick(tick);
            long long offset = tick - get_tick_from_measure(m);
            vol_entries.push_back(std::to_string(m) + "'" + std::to_string(offset) + ":" +
                                  _format_number(vol));
        }
        std::string joined;
        for (std::size_t k = 0; k < vol_entries.size(); ++k) {
            if (k) joined += ", ";
            joined += vol_entries[k];
        }
        lines.push_back("#VOLUME: \"" + joined + "\"");
    }

    lines.push_back("#HISPEED 00");
    lines.push_back("#MEASUREHS 00");
    lines.push_back("");

    // NOTE DATA
    std::map<long long, MeasureNotes> measures_map;

    auto append_data = [&](long long tick, const std::string& info,
                           const std::string& data_str, double speed) {
        for (auto& [measure, length, start_tick] : bar_tick_list) {
            if (tick >= start_tick) {
                long long cur_measure =
                    measure + py_trunc(static_cast<double>(tick - start_tick) / tpb / length);
                _NoteMap& nm = measures_map[cur_measure].setdefault(info);
                nm.data.push_back({tick - start_tick, data_str, speed});
                nm.ticks_per_measure = py_trunc(length * tpb);
                break;
            }
        }
    };

    auto append_note = [&](const _SusNote& n, const std::string& prefix,
                           const std::string& channel) {
        std::string info = prefix + _to_base36(n.lane) + channel;
        append_data(n.tick, info, std::to_string(n.type) + _to_base36(n.width), n.speedRatio);
    };

    auto write_note_lines = [&]() -> std::vector<std::string> {
        std::vector<std::string> result;
        for (auto& [measure, note_map] : measures_map) {  // std::map sorts keys
            std::string mstr = _mbs(measure, result);
            for (auto& info : note_map.order) {
                _NoteMap& nm = note_map.m[info];
                std::vector<std::tuple<long long, std::string, double>> conflicts;

                long long gcd = nm.ticks_per_measure;
                for (auto& [tick_off, d, sr] : nm.data) gcd = std::gcd(tick_off, gcd);

                long long count = nm.ticks_per_measure / gcd;
                bool has_speed = false;
                for (auto& [to, d, sr] : nm.data)
                    if (std::abs(sr - 1.0) > 0.0001) {
                        has_speed = true;
                        break;
                    }

                std::vector<std::string> data(static_cast<std::size_t>(count),
                                              has_speed ? "00,1.0" : "00");
                for (auto& [tick_off, d, sr] : nm.data) {
                    long long idx = (tick_off % nm.ticks_per_measure) / gcd;
                    if (data[static_cast<std::size_t>(idx)].substr(0, 2) != "00") {
                        conflicts.push_back({tick_off, d, sr});
                    } else {
                        data[static_cast<std::size_t>(idx)] =
                            has_speed ? (d + "," + _format_number(sr)) : d;
                    }
                }

                std::string sep = has_speed ? " " : "";
                std::string joined;
                for (std::size_t k = 0; k < data.size(); ++k) {
                    if (k) joined += sep;
                    joined += data[k];
                }
                result.push_back("#" + mstr + info + ":" + joined);

                while (!conflicts.empty()) {
                    std::vector<std::tuple<long long, std::string, double>> temp;
                    std::vector<std::string> data2(static_cast<std::size_t>(count),
                                                   has_speed ? "00,1.0" : "00");
                    for (auto& [tick_off, d, sr] : conflicts) {
                        long long idx = (tick_off % nm.ticks_per_measure) / gcd;
                        if (data2[static_cast<std::size_t>(idx)].substr(0, 2) != "00") {
                            temp.push_back({tick_off, d, sr});
                        } else {
                            data2[static_cast<std::size_t>(idx)] =
                                has_speed ? (d + "," + _format_number(sr)) : d;
                        }
                    }
                    std::string joined2;
                    for (std::size_t k = 0; k < data2.size(); ++k) {
                        if (k) joined2 += sep;
                        joined2 += data2[k];
                    }
                    result.push_back("#" + mstr + info + ":" + joined2);
                    conflicts = temp;
                }
            }
        }
        return result;
    };

    // Taps
    measures_map.clear();
    {
        std::vector<_SusNote> t = S.taps;
        std::stable_sort(t.begin(), t.end(),
                         [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
        for (auto& tap : t) append_note(tap, "1", "");
    }
    {
        auto nl = write_note_lines();
        lines.insert(lines.end(), nl.begin(), nl.end());
    }

    // Directionals
    measures_map.clear();
    {
        std::vector<_SusNote> t = S.directionals;
        std::stable_sort(t.begin(), t.end(),
                         [](const _SusNote& a, const _SusNote& b) { return a.tick < b.tick; });
        for (auto& d : t) append_note(d, "5", "");
    }
    {
        auto nl = write_note_lines();
        lines.insert(lines.end(), nl.begin(), nl.end());
    }

    // Slides
    measures_map.clear();
    {
        _ChannelProvider ch_prov;
        std::vector<std::vector<_SusNote>> ss = S.slides;
        std::stable_sort(ss.begin(), ss.end(),
                         [](const std::vector<_SusNote>& a, const std::vector<_SusNote>& b) {
                             return a[0].tick < b[0].tick;
                         });
        for (auto& slide : ss) {
            long long ch = ch_prov.generate(slide.front().tick, slide.back().tick);
            std::string ch_str = _to_base36(ch);
            for (auto& n : slide) append_note(n, "3", ch_str);
        }
    }
    {
        auto nl = write_note_lines();
        lines.insert(lines.end(), nl.begin(), nl.end());
    }

    // Guides
    measures_map.clear();
    {
        _ChannelProvider ch_prov_g;
        std::vector<std::vector<_SusNote>> gg = S.guides;
        std::stable_sort(gg.begin(), gg.end(),
                         [](const std::vector<_SusNote>& a, const std::vector<_SusNote>& b) {
                             return a[0].tick < b[0].tick;
                         });
        for (auto& guide : gg) {
            long long ch = ch_prov_g.generate(guide.front().tick, guide.back().tick);
            std::string ch_str = _to_base36(ch);
            for (auto& n : guide) append_note(n, "9", ch_str);
        }
    }
    {
        auto nl = write_note_lines();
        lines.insert(lines.end(), nl.begin(), nl.end());
    }

    lines.push_back("");
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

std::optional<std::string> detect(const std::string& data);

std::string dumps(const core::Score& score_in, const ExportOptions& opts) {
    Score score = deep_copy(score_in);
    if (!opts.skip_shift) score.shift();
    score.replace_extended_ease();
    score.replace_extended_guide_colors();
    score.delete_fake_notes();
    if (opts.delete_damage) score.delete_damage_notes();
    if (!opts.allow_extended_lanes) score.strip_extended_lanes();
    if (!opts.keep_note_speed_ratios) score.strip_speed_ratios();

    if (!opts.allow_layers) {
        long long tsg_count = 0;
        for (auto& n : score.notes)
            if (n->kind == Kind::TimeScaleGroup) ++tsg_count;
        if (tsg_count > 1) throw std::runtime_error("Layers found where allow_layers is false");
    }

    ScoreToSus S = _score_to_sus(score);

    std::string sus_text = _dump_sus(
        S, score,
        std::string("This file was generated by sonolus-level-converters ") + RG_VERSION,
        opts.measure_extensions);

    return sus_text;
}

}  // namespace rg::converters::sus
