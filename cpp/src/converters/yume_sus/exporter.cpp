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
#include <utility>
#include <vector>

#include "rg/converters/yume_sus.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pynum.hpp"
#include "rg/util/pyops.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::yume_sus {

using namespace rg::core;
using namespace rg::util;

#ifndef RG_VERSION
#error "RG_VERSION must be defined by the build (it comes from rg_converters/version.py)"
#endif

namespace {

const long long TICKS_PER_BEAT = 480;
const long long MAX_MEASURE = 999;

long long beat_to_tick(double beat) { return py_round_int(TICKS_PER_BEAT * beat); }

long long usc_to_sus_lane(double lane, double size) { return py_trunc(lane - size + 8); }

long long usc_to_sus_width(double size) { return static_cast<long long>(std::ceil(size * 2)); }

std::string to_base36(long long n) {
    static const char* chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (n <= 0) return "0";
    std::string result;
    while (n > 0) {
        result = std::string(1, chars[n % 36]) + result;
        n /= 36;
    }
    return result;
}

std::string format_number(double value) {
    long long iv = py_trunc(value);
    if (static_cast<double>(iv) == value) return std::to_string(iv);
    return py_float_repr(value);
}

struct SusNote {
    long long tick;
    long long lane;
    long long width;
    long long type;
};

struct ChannelProvider {
    std::array<std::optional<std::pair<long long, long long>>, 36> channels{};
    long long generate(long long start_tick, long long end_tick) {
        for (long long key = 0; key < 36; ++key) {
            auto& channel = channels[static_cast<std::size_t>(key)];
            if (!channel || end_tick < channel->first || start_tick > channel->second) {
                channel = {start_tick, end_tick};
                return key;
            }
        }
        throw std::runtime_error("No more slide channels available");
    }
};

struct NoteMap {
    std::vector<std::pair<long long, std::string>> data;
    long long ticks_per_measure = 0;
};

struct MeasureNotes {
    std::vector<std::string> order;
    std::map<std::string, NoteMap> m;
    NoteMap& setdefault(const std::string& info) {
        auto it = m.find(info);
        if (it == m.end()) {
            order.push_back(info);
            return m[info];
        }
        return it->second;
    }
};

using Pair = std::pair<long long, double>;
using SlotKey = std::pair<long long, long long>;

struct ScoreToSus {
    std::vector<SusNote> taps;
    std::vector<SusNote> directionals;
    std::vector<std::vector<SusNote>> slides;
    std::vector<std::vector<SusNote>> guides;
    std::vector<Pair> bpms;
    std::vector<Pair> til;
};

long long direction_type(const std::string& direction) {
    if (direction == "up") return 1;
    if (direction == "left") return 3;
    return 4;
}

long long ease_type(const std::string& ease) { return ease == "in" ? 2 : 6; }

// Yume has a single #TIL00 and no per-note speed ratios, so every note goes to the first group.
bool is_lone_slide(const Node* note) {
    auto& points = static_cast<const Slide*>(note)->connections;
    return points.size() == 1 && points[0]->kind == Kind::SlideStartPoint;
}

// Yume plays a lone slide head as it is; any other slide without an end gets one.
void close_open_slides(Score& score) {
    std::vector<NodePtr> lone, rest;
    for (auto& note : score.notes)
        (note->kind == Kind::Slide && is_lone_slide(note.get()) ? lone : rest).push_back(note);
    score.notes = rest;
    score.close_open_slides();
    score.notes.insert(score.notes.end(), lone.begin(), lone.end());
}

}  // namespace

// What a guide marker would change in an occupied slot: SLOT_SHAPE takes an ease into a slide
// curve, SLOT_LOCKED takes a critical into a played note (an end's critical comes from its head).
enum : int { SLOT_NOTE = 1, SLOT_SHAPE = 2, SLOT_LOCKED = 4 };

static ScoreToSus score_to_sus(const Score& score) {
    ScoreToSus R;
    std::map<SlotKey, int> occupied;

    for (auto& note_sp : score.notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Bpm) {
            auto* b = static_cast<Bpm*>(note);
            R.bpms.push_back({beat_to_tick(b->beat), b->bpm});

        } else if (note->kind == Kind::TimeScaleGroup) {
            for (auto& cp : static_cast<TimeScaleGroup*>(note)->changes)
                R.til.push_back({beat_to_tick(cp->beat), cp->timeScale});

        } else if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            long long tick = beat_to_tick(s->beat);
            long long lane = usc_to_sus_lane(s->lane, s->size);
            long long width = usc_to_sus_width(s->size);
            occupied[{tick, lane}] |= SLOT_NOTE | SLOT_LOCKED;

            long long tap_type = (s->trace.has_value() && s->trace.value()) ? 5 : 1;
            if (s->critical.has_value() && s->critical.value()) tap_type += 1;
            R.taps.push_back({tick, lane, width, tap_type});
            if (s->direction.has_value())
                R.directionals.push_back({tick, lane, width, direction_type(*s->direction)});

        } else if (note->kind == Kind::Slide && is_lone_slide(note)) {
            // A slide stream ends at its first end, so a lone end note is a slide head on its own.
            auto* p = static_cast<SlideStartPoint*>(static_cast<Slide*>(note)->connections[0].get());
            if (p->judgeType == "none") continue;
            long long tick = beat_to_tick(p->beat);
            long long lane = usc_to_sus_lane(p->lane, p->size);
            long long width = usc_to_sus_width(p->size);
            occupied[{tick, lane}] |= SLOT_NOTE | SLOT_LOCKED;
            long long tap_type = (p->judgeType == "trace" ? 5 : 1) + (p->critical ? 1 : 0);
            if (tap_type > 1) R.taps.push_back({tick, lane, width, tap_type});
            R.slides.push_back({{tick, lane, width, 2}});

        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            std::vector<NodePtr> conns = sl->connections;
            std::stable_sort(conns.begin(), conns.end(), [](const NodePtr& a, const NodePtr& b) {
                return node_beat(a.get()) < node_beat(b.get());
            });

            std::vector<SusNote> slide;
            // The whole slide takes its critical from the start slot.
            bool head_critical = false;
            for (auto& step_sp : conns) {
                Node* step = step_sp.get();
                long long tick = beat_to_tick(node_beat(step));
                long long lane = usc_to_sus_lane(node_lane(step), node_size(step));
                long long width = usc_to_sus_width(node_size(step));
                int& slot = occupied[{tick, lane}];
                slot |= SLOT_NOTE;
                if (step->kind != Kind::SlideEndPoint)
                    slot |= SLOT_SHAPE | SLOT_LOCKED;
                else if (static_cast<SlideEndPoint*>(step)->direction.has_value())
                    slot |= SLOT_LOCKED;

                if (step->kind == Kind::SlideStartPoint) {
                    auto* p = static_cast<SlideStartPoint*>(step);
                    slide.push_back({tick, lane, width, 1});
                    if (p->ease != "linear") R.directionals.push_back({tick, lane, width, ease_type(p->ease)});
                    long long tap_type = p->judgeType == "none" ? 7 : (p->judgeType == "trace" ? 5 : 1);
                    head_critical = p->critical;
                    if (p->critical) tap_type += 1;
                    if (tap_type > 1) R.taps.push_back({tick, lane, width, tap_type});

                } else if (step->kind == Kind::SlideRelayPoint) {
                    auto* p = static_cast<SlideRelayPoint*>(step);
                    slide.push_back({tick, lane, width, p->critical.has_value() ? 3 : 5});
                    if (p->type == "attach") R.taps.push_back({tick, lane, width, 3});
                    if (p->ease != "linear") R.directionals.push_back({tick, lane, width, ease_type(p->ease)});

                } else if (step->kind == Kind::SlideEndPoint) {
                    auto* p = static_cast<SlideEndPoint*>(step);
                    slide.push_back({tick, lane, width, 2});
                    if (p->direction.has_value())
                        R.directionals.push_back({tick, lane, width, direction_type(*p->direction)});
                    long long end_type = p->judgeType == "none" ? 7 : (p->judgeType == "trace" ? 5 : 1);
                    if (p->critical) end_type += 1;
                    // An end's own critical only shows on a flick of a non-critical slide.
                    if (end_type == 2 ? (p->direction.has_value() && !head_critical) : end_type != 1)
                        R.taps.push_back({tick, lane, width, end_type});
                }
            }
            R.slides.push_back(slide);
        }
    }

    // A guide reads its critical and eases from the tap/directional in the same slot. A bare
    // directional would be played as a tap, so empty slots get a Skip tap (3) underneath.
    std::set<std::pair<SlotKey, long long>> written;
    auto add_marker = [&](std::vector<SusNote>& dest, const SusNote& n) {
        if (written.insert({{n.tick, n.lane}, (&dest == &R.taps ? 0 : 10) + n.type}).second)
            dest.push_back(n);
    };
    for (auto& note_sp : score.notes) {
        if (note_sp->kind != Kind::Guide) continue;
        auto* gd = static_cast<Guide*>(note_sp.get());
        std::vector<shared_ptr<GuidePoint>> points = gd->midpoints;
        std::stable_sort(points.begin(), points.end(),
                         [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                             return a->beat < b->beat;
                         });
        bool critical = gd->color == "yellow";

        std::vector<SusNote> guide;
        for (std::size_t idx = 0; idx < points.size(); ++idx) {
            GuidePoint* step = points[idx].get();
            long long tick = beat_to_tick(step->beat);
            long long lane = usc_to_sus_lane(step->lane, step->size);
            long long width = usc_to_sus_width(step->size);
            bool last = idx + 1 == points.size();
            guide.push_back({tick, lane, width, idx == 0 ? 1 : (last ? 2 : 5)});

            auto it = occupied.find({tick, lane});
            int slot = it == occupied.end() ? 0 : it->second;
            bool needs_critical = idx == 0 && critical && !(slot & SLOT_LOCKED);
            bool needs_ease = !last && step->ease != "linear" && !(slot & SLOT_SHAPE);
            if (!(needs_critical || needs_ease)) continue;
            if (!(slot & SLOT_NOTE)) add_marker(R.taps, {tick, lane, width, 3});
            if (needs_critical) add_marker(R.taps, {tick, lane, width, 2});
            if (needs_ease) add_marker(R.directionals, {tick, lane, width, ease_type(step->ease)});
        }
        R.guides.push_back(guide);
    }

    if (R.bpms.empty()) R.bpms.push_back({0, 120.0});
    if (R.til.empty()) R.til.push_back({0, 1.0});
    return R;
}

static std::string dump_sus(const ScoreToSus& S, const Score& score) {
    std::vector<std::string> lines;
    long long tpb = TICKS_PER_BEAT;
    std::string version = RG_VERSION;

    lines.push_back("This file was generated by rg-converters " + version);
    if (!score.metadata->title.empty()) lines.push_back("#TITLE \"" + score.metadata->title + "\"");
    if (!score.metadata->artist.empty()) lines.push_back("#ARTIST \"" + score.metadata->artist + "\"");
    lines.push_back("#DESIGNER \"rg-converters v" + version + "\"");
    lines.push_back("#WAVEOFFSET " + format_number(-score.metadata->waveoffset));
    lines.push_back("");
    lines.push_back("#REQUEST \"ticks_per_beat " + std::to_string(tpb) + "\"");
    lines.push_back("");

    auto measure_str = [](long long measure) {
        if (measure > MAX_MEASURE)
            throw std::invalid_argument("Yume SUS supports measures up to " +
                                     std::to_string(MAX_MEASURE) + " (got " +
                                     std::to_string(measure) + ")");
        return zfill(std::to_string(measure), 3);
    };

    // Every bar is 4 beats long.
    lines.push_back("#00002: 4");
    lines.push_back("");
    long long tpm = 4 * tpb;
    auto measure_of = [&](long long tick) { return tick / tpm; };

    std::vector<Pair> sorted_bpms = S.bpms;
    std::stable_sort(sorted_bpms.begin(), sorted_bpms.end(),
                     [](const Pair& a, const Pair& b) { return a.first < b.first; });

    std::map<double, std::string> bpm_ids;
    for (auto& [tick, bpm] : sorted_bpms) {
        if (bpm_ids.count(bpm)) continue;
        long long index = static_cast<long long>(bpm_ids.size()) + 1;
        if (index >= 36 * 36) throw std::runtime_error("Too many distinct BPMs for Yume SUS");
        std::string ident = zfill(to_upper(to_base36(index)), 2);
        bpm_ids[bpm] = ident;
        lines.push_back("#BPM" + ident + ": " + format_number(bpm));
    }

    std::map<long long, std::vector<Pair>> bpm_by_measure;
    for (auto& entry : sorted_bpms) bpm_by_measure[measure_of(entry.first)].push_back(entry);
    for (auto& [m, entries] : bpm_by_measure) {
        long long m_tick = m * tpm;
        long long gcd = tpm;
        for (auto& [tick, bpm] : entries) gcd = std::gcd(tick - m_tick, gcd);
        std::vector<std::string> cells(static_cast<std::size_t>(tpm / gcd), "00");
        for (auto& [tick, bpm] : entries) cells[static_cast<std::size_t>((tick - m_tick) / gcd)] = bpm_ids[bpm];
        std::string joined;
        for (auto& cell : cells) joined += cell;
        lines.push_back("#" + measure_str(m) + "08: " + joined);
    }
    lines.push_back("");

    std::vector<Pair> til = S.til;
    std::stable_sort(til.begin(), til.end(),
                     [](const Pair& a, const Pair& b) { return a.first < b.first; });
    std::string til_entries;
    for (std::size_t k = 0; k < til.size(); ++k) {
        long long m = measure_of(til[k].first);
        if (k) til_entries += ", ";
        til_entries += std::to_string(m) + "'" + std::to_string(til[k].first - m * tpm) + ":" +
                       format_number(til[k].second);
    }
    lines.push_back("#TIL00: \"" + til_entries + "\"");
    lines.push_back("");

    std::map<long long, MeasureNotes> measures_map;
    auto append_note = [&](const SusNote& n, const std::string& prefix, const std::string& channel) {
        long long m = measure_of(n.tick);
        NoteMap& nm = measures_map[m].setdefault(prefix + to_base36(n.lane) + channel);
        nm.data.push_back({n.tick - m * tpm, std::to_string(n.type) + to_base36(n.width)});
        nm.ticks_per_measure = tpm;
    };

    auto flush = [&]() {
        for (auto& [measure, note_map] : measures_map) {
            std::string mstr = measure_str(measure);
            for (auto& info : note_map.order) {
                NoteMap& nm = note_map.m[info];
                long long gcd = nm.ticks_per_measure;
                for (auto& [tick_off, d] : nm.data) gcd = std::gcd(tick_off, gcd);
                std::size_t count = static_cast<std::size_t>(nm.ticks_per_measure / gcd);

                // Notes landing on the same cell go on extra lines with the same header.
                std::vector<std::pair<long long, std::string>> pending = nm.data;
                while (!pending.empty()) {
                    std::vector<std::pair<long long, std::string>> conflicts;
                    std::vector<std::string> cells(count, "00");
                    for (auto& [tick_off, d] : pending) {
                        std::string& cell = cells[static_cast<std::size_t>(tick_off / gcd)];
                        if (cell != "00")
                            conflicts.push_back({tick_off, d});
                        else
                            cell = d;
                    }
                    std::string joined;
                    for (auto& cell : cells) joined += cell;
                    lines.push_back("#" + mstr + info + ":" + joined);
                    pending = conflicts;
                }
            }
        }
        measures_map.clear();
    };

    std::vector<SusNote> taps = S.taps;
    std::stable_sort(taps.begin(), taps.end(),
                     [](const SusNote& a, const SusNote& b) { return a.tick < b.tick; });
    std::vector<SusNote> directionals = S.directionals;
    std::stable_sort(directionals.begin(), directionals.end(),
                     [](const SusNote& a, const SusNote& b) { return a.tick < b.tick; });

    auto by_start = [](const std::vector<SusNote>& a, const std::vector<SusNote>& b) {
        return a[0].tick < b[0].tick;
    };
    std::vector<std::vector<SusNote>> slides = S.slides;
    slides.erase(std::remove_if(slides.begin(), slides.end(), [](auto& s) { return s.empty(); }), slides.end());
    std::stable_sort(slides.begin(), slides.end(), by_start);
    std::vector<std::vector<SusNote>> guides = S.guides;
    guides.erase(std::remove_if(guides.begin(), guides.end(), [](auto& g) { return g.empty(); }), guides.end());
    std::stable_sort(guides.begin(), guides.end(), by_start);

    for (auto& tap : taps) append_note(tap, "1", "");
    flush();
    for (auto& d : directionals) append_note(d, "5", "");
    flush();
    ChannelProvider slide_channels;
    for (auto& slide : slides) {
        std::string channel = to_base36(slide_channels.generate(slide.front().tick, slide.back().tick));
        for (auto& n : slide) append_note(n, "3", channel);
    }
    flush();
    ChannelProvider guide_channels;
    for (auto& guide : guides) {
        std::string channel = to_base36(guide_channels.generate(guide.front().tick, guide.back().tick));
        for (auto& n : guide) append_note(n, "9", channel);
    }
    flush();

    lines.push_back("");
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

ExportResult dumps(const core::Score& score_in, const ExportOptions& opts) {
    Score score = deep_copy(score_in);
    score.delete_fake_notes();
    score.delete_damage_notes();
    limit_to_one_layer(score, "Yume SUS", opts.strip_layers, "Set the strip_layers export option to strip them");
    close_open_slides(score);
    if (!opts.skip_shift) score.shift();
    score.replace_extended_ease();
    score.replace_extended_guide_colors();
    score.strip_extended_lanes();
    std::string text = dump_sus(score_to_sus(score), score);
    return {std::move(text), std::move(score)};
}

}  // namespace rg::converters::yume_sus
