#include "rg/core/score.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "rg/util/pyops.hpp"

namespace rg::core {

using util::py_floordiv_f;
using util::py_round;
using util::py_trunc;

long long usc_lanes_to_sus_lanes(double lane, double size) {
    return py_trunc(lane - size + 8);
}

NodePtr clone_node(const Node* n) {
    switch (n->kind) {
        case Kind::Bpm: {
            auto* o = static_cast<const Bpm*>(n);
            return std::make_shared<Bpm>(o->beat, o->bpm, o->type);
        }
        case Kind::TimeScalePoint: {
            auto* o = static_cast<const TimeScalePoint*>(n);
            return std::make_shared<TimeScalePoint>(o->beat, o->timeScale);
        }
        case Kind::TimeScaleGroup: {
            auto* o = static_cast<const TimeScaleGroup*>(n);
            vector<shared_ptr<TimeScalePoint>> ch;
            for (auto& c : o->changes)
                ch.push_back(std::make_shared<TimeScalePoint>(c->beat, c->timeScale));
            return std::make_shared<TimeScaleGroup>(ch, o->type);
        }
        case Kind::Volume: {
            auto* o = static_cast<const Volume*>(n);
            return std::make_shared<Volume>(o->beat, o->volume, o->type);
        }
        case Kind::Single: {
            auto* o = static_cast<const Single*>(n);
            auto p = std::make_shared<Single>(*o);
            return p;
        }
        case Kind::Skill:
            return std::make_shared<Skill>(static_cast<const Skill*>(n)->beat);
        case Kind::FeverChance:
            return std::make_shared<FeverChance>(static_cast<const FeverChance*>(n)->beat);
        case Kind::FeverStart:
            return std::make_shared<FeverStart>(static_cast<const FeverStart*>(n)->beat);
        case Kind::HolodoriSkill: {
            auto* o = static_cast<const HolodoriSkill*>(n);
            return std::make_shared<HolodoriSkill>(o->beat, o->slot);
        }
        case Kind::HolodoriChargeStart:
            return std::make_shared<HolodoriChargeStart>(
                static_cast<const HolodoriChargeStart*>(n)->beat);
        case Kind::HolodoriChargeEnd:
            return std::make_shared<HolodoriChargeEnd>(
                static_cast<const HolodoriChargeEnd*>(n)->beat);
        case Kind::HolodoriFeverStart:
            return std::make_shared<HolodoriFeverStart>(
                static_cast<const HolodoriFeverStart*>(n)->beat);
        case Kind::HolodoriFeverEnd:
            return std::make_shared<HolodoriFeverEnd>(
                static_cast<const HolodoriFeverEnd*>(n)->beat);
        case Kind::SlideStartPoint:
            return std::make_shared<SlideStartPoint>(*static_cast<const SlideStartPoint*>(n));
        case Kind::SlideRelayPoint:
            return std::make_shared<SlideRelayPoint>(*static_cast<const SlideRelayPoint*>(n));
        case Kind::SlideEndPoint:
            return std::make_shared<SlideEndPoint>(*static_cast<const SlideEndPoint*>(n));
        case Kind::Slide: {
            auto* o = static_cast<const Slide*>(n);
            vector<NodePtr> conns;
            for (auto& c : o->connections) conns.push_back(clone_node(c.get()));
            return std::make_shared<Slide>(o->critical, o->fake, conns, o->type);
        }
        case Kind::GuidePoint:
            return std::make_shared<GuidePoint>(*static_cast<const GuidePoint*>(n));
        case Kind::Guide: {
            auto* o = static_cast<const Guide*>(n);
            vector<shared_ptr<GuidePoint>> mps;
            for (auto& m : o->midpoints)
                mps.push_back(std::make_shared<GuidePoint>(*m));
            return std::make_shared<Guide>(o->color, o->fade, mps, o->type, o->color_code);
        }
    }
    return nullptr;
}

Score deep_copy(const Score& s) {
    Score out;
    out.metadata = std::make_shared<MetaData>(*s.metadata);
    out.notes.reserve(s.notes.size());
    for (auto& n : s.notes) out.notes.push_back(clone_node(n.get()));
    return out;
}

std::vector<NodePtr> convert_holodori_events(const std::vector<NodePtr>& notes) {
    std::vector<NodePtr> converted;
    std::set<double> skill_beats;
    for (auto& note : notes) {
        if (note->kind == Kind::HolodoriSkill) {
            // Skill has no slot, so members firing on the same beat collapse into one
            double b = static_cast<HolodoriSkill*>(note.get())->beat;
            if (!skill_beats.count(b)) {
                skill_beats.insert(b);
                converted.push_back(std::make_shared<Skill>(b));
            }
        } else if (note->kind == Kind::HolodoriChargeStart) {
            converted.push_back(std::make_shared<FeverChance>(
                static_cast<HolodoriChargeStart*>(note.get())->beat));
        } else if (note->kind == Kind::HolodoriFeverStart) {
            converted.push_back(std::make_shared<FeverStart>(
                static_cast<HolodoriFeverStart*>(note.get())->beat));
        } else if (note->kind != Kind::HolodoriChargeEnd &&
                   note->kind != Kind::HolodoriFeverEnd) {
            converted.push_back(note);
        }
    }
    return converted;
}

// ---- positioned-node field accessors ----
static bool has_ease(const Node* n) {
    return n->kind == Kind::SlideStartPoint || n->kind == Kind::SlideRelayPoint ||
           n->kind == Kind::GuidePoint;
}
static const string& pt_ease(const Node* n) {
    switch (n->kind) {
        case Kind::SlideStartPoint:
            return static_cast<const SlideStartPoint*>(n)->ease;
        case Kind::SlideRelayPoint:
            return static_cast<const SlideRelayPoint*>(n)->ease;
        case Kind::GuidePoint:
            return static_cast<const GuidePoint*>(n)->ease;
        default: {
            static const string linear = "linear";
            return linear;
        }
    }
}
static long long pt_tsg(const Node* n) {
    switch (n->kind) {
        case Kind::Single:
            return static_cast<const Single*>(n)->timeScaleGroup;
        case Kind::SlideStartPoint:
            return static_cast<const SlideStartPoint*>(n)->timeScaleGroup;
        case Kind::SlideRelayPoint:
            return static_cast<const SlideRelayPoint*>(n)->timeScaleGroup;
        case Kind::SlideEndPoint:
            return static_cast<const SlideEndPoint*>(n)->timeScaleGroup;
        case Kind::GuidePoint:
            return static_cast<const GuidePoint*>(n)->timeScaleGroup;
        default:
            return 0;
    }
}
static double pt_speed(const Node* n) {
    switch (n->kind) {
        case Kind::Single:
            return static_cast<const Single*>(n)->speedRatio;
        case Kind::SlideStartPoint:
            return static_cast<const SlideStartPoint*>(n)->speedRatio;
        case Kind::SlideRelayPoint:
            return static_cast<const SlideRelayPoint*>(n)->speedRatio;
        case Kind::SlideEndPoint:
            return static_cast<const SlideEndPoint*>(n)->speedRatio;
        case Kind::GuidePoint:
            return static_cast<const GuidePoint*>(n)->speedRatio;
        default:
            return 1.0;
    }
}

static bool is_skip_note(Kind k) {
    // isinstance(note, Bpm) or isinstance(note, TimeScaleGroup)
    //   or isinstance(note, _EVENTS + (Volume,))
    return k == Kind::Bpm || k == Kind::TimeScaleGroup || k == Kind::Volume ||
           is_event(k);
}

static bool has_beat(Kind k) {
    return k == Kind::Bpm || k == Kind::Volume || k == Kind::Single ||
           k == Kind::Skill || k == Kind::FeverChance || k == Kind::FeverStart ||
           is_holodori_event(k) || k == Kind::TimeScalePoint ||
           k == Kind::SlideStartPoint || k == Kind::SlideRelayPoint ||
           k == Kind::SlideEndPoint || k == Kind::GuidePoint;
}

// ノーツの範囲を計算する
// [ 1 2 3 4 5 6 7 8 9 10 11 12 ] で占有レーンを表記する
struct Range {
    long long lo;
    long long hi;
};
static Range calc_note_range(double lane, double size) {
    long long note_leftpos = py_trunc(lane - size + LANE_OFFSET);
    long long note_size = py_trunc(size * 2);
    return {note_leftpos, note_leftpos + note_size};
}
static bool ranges_intersect(const Range& a, const Range& b) {
    if (a.lo >= a.hi || b.lo >= b.hi) return false;
    return std::max(a.lo, b.lo) < std::min(a.hi, b.hi);
}

// ノーツを入れるリストの番号をbeatの値から計算する
static long long calc_notelist_index(const Node* note) {
    return py_trunc(py_floordiv_f(node_beat(const_cast<Node*>(note)), BAR_INTERVAL));
}

// スライド、ガイドのpointを入れたリストを返す
static vector<Node*> convert_tmp_notes(const vector<Node*>& tmp_notes) {
    vector<Node*> tmp;
    for (Node* note : tmp_notes) {
        if (note->kind == Kind::Slide) {
            for (auto& i : static_cast<Slide*>(note)->connections) tmp.push_back(i.get());
        } else if (note->kind == Kind::Guide) {
            for (auto& i : static_cast<Guide*>(note)->midpoints) tmp.push_back(i.get());
        } else {
            tmp.push_back(note);
        }
    }
    return tmp;
}

// ノーツの一部または全てが重なっているか調べる
static Node* get_overlap_note(Node* target_note,
                              const vector<vector<Node*>>& split_tmp_notes) {
    Range t_note_range = calc_note_range(node_lane(target_note), node_size(target_note));
    double t_note_beat = node_beat(target_note);

    long long index = calc_notelist_index(target_note);
    long long start = std::max(0LL, index - 1);
    long long end = std::min(static_cast<long long>(split_tmp_notes.size()), index + 2);

    for (long long si = start; si < end; ++si) {
        for (Node* note : split_tmp_notes[static_cast<std::size_t>(si)]) {
            // 同じインスタンス（ノーツ）の場合は飛ばす
            if (target_note == note) continue;
            // 同じ拍、かつノーツの一部または全てが重なっているか判定
            Range note_range = calc_note_range(node_lane(note), node_size(note));
            if (t_note_beat == node_beat(note) &&
                ranges_intersect(t_note_range, note_range)) {
                return note;
            }
        }
    }
    return nullptr;
}

// スライドが脱法（終点より後に中継点があるetc...）していないか調べ、修正する
static void check_slide(Slide* note, const vector<vector<Node*>>& split_tmp_notes) {
    Node* start_point = nullptr;
    Node* end_point = nullptr;
    for (auto& point : note->connections) {
        if (point->kind == Kind::SlideStartPoint) start_point = point.get();
        else if (point->kind == Kind::SlideEndPoint) end_point = point.get();
    }

    for (auto& point_sp : note->connections) {
        Node* point = point_sp.get();

        if (point->kind != Kind::SlideEndPoint) {
            while (node_beat(point) >= node_beat(end_point)) {
                node_beat(point) -= BEAT_PER_TICK;
                while (get_overlap_note(point, split_tmp_notes) != nullptr) {
                    node_beat(point) -= BEAT_PER_TICK;
                }
            }
        }

        if (point->kind != Kind::SlideStartPoint) {
            while (node_beat(point) <= node_beat(start_point)) {
                node_beat(point) += BEAT_PER_TICK;
                while (get_overlap_note(point, split_tmp_notes) != nullptr) {
                    node_beat(point) += BEAT_PER_TICK;
                }
            }
        }

        if (node_beat(point) > node_beat(end_point)) {
            std::swap(node_beat(point), node_beat(end_point));
        }
    }
}

static void shift_slide(Slide* note, const vector<vector<Node*>>& split_tmp_notes) {
    for (auto& point_sp : note->connections) {
        Node* point = point_sp.get();
        Node* overlap_note;
        while ((overlap_note = get_overlap_note(point, split_tmp_notes)) != nullptr) {
            Kind pk = point->kind;
            Kind ok = overlap_note->kind;
            if (pk == Kind::SlideStartPoint && ok == Kind::Single) {
                // スライド始点 + single
                auto* s = static_cast<Single*>(overlap_note);
                if (static_cast<SlideStartPoint*>(point)->judgeType != "none" &&
                    (s->trace.has_value() && s->trace.value())) {
                    node_beat(overlap_note) += BEAT_PER_TICK;
                } else {
                    node_beat(point) += BEAT_PER_TICK;
                }
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideStartPoint) {
                // スライド始点 + スライド始点
                if (static_cast<SlideStartPoint*>(point)->judgeType != "none" &&
                    static_cast<SlideStartPoint*>(overlap_note)->judgeType == "none") {
                    node_beat(overlap_note) += BEAT_PER_TICK;
                } else {
                    node_beat(point) += BEAT_PER_TICK;
                }
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideRelayPoint) {
                // スライド始点 + スライド中継点
                node_beat(overlap_note) += BEAT_PER_TICK;
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideEndPoint) {
                // スライド始点 + スライド終点
                node_beat(point) += BEAT_PER_TICK;
            } else if (pk == Kind::SlideRelayPoint) {
                // スライド中継点 + ノーツ
                node_beat(point) += BEAT_PER_TICK;
            } else if (pk == Kind::SlideEndPoint && ok == Kind::Single) {
                // スライド終点 + single
                node_beat(point) -= BEAT_PER_TICK;
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideStartPoint) {
                // スライド終点 + スライド始点
                node_beat(overlap_note) += BEAT_PER_TICK;
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideRelayPoint) {
                // スライド終点 + スライド中継点
                node_beat(overlap_note) += BEAT_PER_TICK;
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideEndPoint) {
                // スライド終点 + スライド終点
                auto* p = static_cast<SlideEndPoint*>(point);
                auto* o = static_cast<SlideEndPoint*>(overlap_note);
                if (p->judgeType == "none" || o->direction.has_value()) {
                    node_beat(point) -= BEAT_PER_TICK;
                } else if (o->judgeType == "none" || p->direction.has_value()) {
                    node_beat(overlap_note) -= BEAT_PER_TICK;
                } else {
                    node_beat(point) -= BEAT_PER_TICK;
                }
            } else if (pk == Kind::SlideEndPoint) {
                node_beat(point) -= BEAT_PER_TICK;
            } else {
                node_beat(point) += BEAT_PER_TICK;
            }
        }
    }

    check_slide(note, split_tmp_notes);
}

static void shift_guide(Guide* note, const vector<vector<Node*>>& split_tmp_notes) {
    for (auto& point : note->midpoints) {
        while (get_overlap_note(point.get(), split_tmp_notes) != nullptr) {
            point->beat += BEAT_PER_TICK;
        }
    }
    std::stable_sort(note->midpoints.begin(), note->midpoints.end(),
                     [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                         return a->beat < b->beat;
                     });
}

static void shift_single(Single* note, const vector<vector<Node*>>& split_tmp_notes) {
    Node* overlap_note;
    while ((overlap_note = get_overlap_note(note, split_tmp_notes)) != nullptr) {
        Kind ok = overlap_note->kind;
        if (ok == Kind::Single && static_cast<Single*>(overlap_note)->trace.has_value() &&
            static_cast<Single*>(overlap_note)->trace.value()) {
            node_beat(overlap_note) += BEAT_PER_TICK;
        } else if (ok == Kind::SlideStartPoint) {
            node_beat(overlap_note) += BEAT_PER_TICK;
        } else if (ok == Kind::SlideRelayPoint) {
            node_beat(overlap_note) += BEAT_PER_TICK;
        } else if (ok == Kind::SlideEndPoint) {
            node_beat(overlap_note) -= BEAT_PER_TICK;
        } else if (ok == Kind::GuidePoint) {
            node_beat(overlap_note) += BEAT_PER_TICK;
        } else {
            note->beat += BEAT_PER_TICK;
        }
    }
}

// CUT HELPERS
// When cutting slides/guides at a boundary:
// - Linear ease: interpolate lane/size at the cut beat
// - Non-linear ease: snap to nearest relay point, or delete if none
// - Truncated starts/ends become headless (judgeType="none")

static std::pair<double, double> lerp_point(const Node* a, const Node* b, double beat) {
    double ab = node_beat(const_cast<Node*>(a));
    double bb = node_beat(const_cast<Node*>(b));
    double ratio = (beat - ab) / (bb - ab);
    return {py_round(node_lane(a) + ratio * (node_lane(b) - node_lane(a)), 6),
            py_round(node_size(a) + ratio * (node_size(b) - node_size(a)), 6)};
}

static shared_ptr<SlideStartPoint> make_start(double beat, bool critical, string ease,
                                              string judgeType, double lane, double size,
                                              long long tsg, double speedRatio) {
    auto p = std::make_shared<SlideStartPoint>();
    p->beat = beat;
    p->critical = critical;
    p->ease = std::move(ease);
    p->judgeType = std::move(judgeType);
    p->lane = lane;
    p->size = size;
    p->timeScaleGroup = tsg;
    p->speedRatio = speedRatio;
    return p;
}
static shared_ptr<SlideEndPoint> make_end(double beat, bool critical, string judgeType,
                                          double lane, double size, long long tsg,
                                          double speedRatio) {
    auto p = std::make_shared<SlideEndPoint>();
    p->beat = beat;
    p->critical = critical;
    p->judgeType = std::move(judgeType);
    p->lane = lane;
    p->size = size;
    p->timeScaleGroup = tsg;
    p->speedRatio = speedRatio;
    return p;
}

static bool have_slide(const std::optional<vector<NodePtr>>& v) {
    return v.has_value() && v->size() >= 2;
}

static std::optional<vector<NodePtr>> truncate_slide_start(const vector<NodePtr>& conns,
                                                           double start_beat,
                                                           bool critical) {
    long long first_in = -1;
    long long last_before = -1;
    for (long long i = 0; i < static_cast<long long>(conns.size()); ++i) {
        if (node_beat(conns[static_cast<std::size_t>(i)].get()) >= start_beat) {
            first_in = i;
            break;
        }
        last_before = i;
    }

    if (first_in < 0) return std::nullopt;

    auto make_result_from = [&](long long from) {
        vector<NodePtr> r;
        for (long long i = from; i < static_cast<long long>(conns.size()); ++i)
            r.push_back(conns[static_cast<std::size_t>(i)]);
        return r;
    };

    if (node_beat(conns[static_cast<std::size_t>(first_in)].get()) == start_beat) {
        vector<NodePtr> result = make_result_from(first_in);
        Node* p = result[0].get();
        result[0] = make_start(start_beat, critical,
                               has_ease(p) ? pt_ease(p) : "linear", "none", node_lane(p),
                               node_size(p), pt_tsg(p), pt_speed(p));
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    Node* prev = last_before >= 0 ? conns[static_cast<std::size_t>(last_before)].get() : nullptr;
    Node* next_p = conns[static_cast<std::size_t>(first_in)].get();

    if (prev != nullptr && has_ease(prev) && pt_ease(prev) == "linear") {
        auto [lane, size] = lerp_point(prev, next_p, start_beat);
        auto new_start = make_start(start_beat, critical, "linear", "none", lane, size,
                                    pt_tsg(next_p), pt_speed(next_p));
        vector<NodePtr> result;
        result.push_back(new_start);
        for (long long i = first_in; i < static_cast<long long>(conns.size()); ++i)
            result.push_back(conns[static_cast<std::size_t>(i)]);
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    // Non-linear ease: snap to first in-range point.
    // If only the end remains, not enough for a valid slide.
    if (next_p->kind == Kind::SlideEndPoint) return std::nullopt;
    vector<NodePtr> result = make_result_from(first_in);
    Node* p = result[0].get();
    result[0] = make_start(node_beat(p), critical, has_ease(p) ? pt_ease(p) : "linear",
                           "none", node_lane(p), node_size(p), pt_tsg(p), pt_speed(p));
    if (result.size() >= 2) return result;
    return std::nullopt;
}

static std::optional<vector<NodePtr>> truncate_slide_end(const vector<NodePtr>& conns,
                                                         double end_beat, bool critical) {
    long long last_in = -1;
    long long first_after = -1;
    for (long long i = 0; i < static_cast<long long>(conns.size()); ++i) {
        if (node_beat(conns[static_cast<std::size_t>(i)].get()) <= end_beat) {
            last_in = i;
        } else if (first_after < 0) {
            first_after = i;
        }
    }

    if (last_in < 0) return std::nullopt;

    auto make_head = [&](long long upto) {
        vector<NodePtr> r;
        for (long long i = 0; i <= upto; ++i) r.push_back(conns[static_cast<std::size_t>(i)]);
        return r;
    };

    if (node_beat(conns[static_cast<std::size_t>(last_in)].get()) == end_beat) {
        vector<NodePtr> result = make_head(last_in);
        Node* p = result.back().get();
        result.back() = make_end(end_beat, critical, "none", node_lane(p), node_size(p),
                                 pt_tsg(p), pt_speed(p));
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    Node* last_p = conns[static_cast<std::size_t>(last_in)].get();
    Node* next_p = first_after >= 0 ? conns[static_cast<std::size_t>(first_after)].get() : nullptr;

    if (next_p != nullptr && has_ease(last_p) && pt_ease(last_p) == "linear") {
        auto [lane, size] = lerp_point(last_p, next_p, end_beat);
        auto new_end = make_end(end_beat, critical, "none", lane, size, pt_tsg(last_p),
                                pt_speed(last_p));
        vector<NodePtr> result = make_head(last_in);
        result.push_back(new_end);
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    // Non-linear ease or no next: snap to last in-range point.
    // If only the start remains, not enough for a valid slide.
    if (last_p->kind == Kind::SlideStartPoint) return std::nullopt;
    vector<NodePtr> result = make_head(last_in);
    Node* p = result.back().get();
    result.back() = make_end(node_beat(p), critical, "none", node_lane(p), node_size(p),
                             pt_tsg(p), pt_speed(p));
    if (result.size() >= 2) return result;
    return std::nullopt;
}

static std::optional<vector<shared_ptr<GuidePoint>>> truncate_guide_start(
    const vector<shared_ptr<GuidePoint>>& mps, double start_beat) {
    long long first_in = -1;
    long long last_before = -1;
    for (long long i = 0; i < static_cast<long long>(mps.size()); ++i) {
        if (mps[static_cast<std::size_t>(i)]->beat >= start_beat) {
            first_in = i;
            break;
        }
        last_before = i;
    }
    if (first_in < 0) return std::nullopt;

    auto from = [&](long long f) {
        vector<shared_ptr<GuidePoint>> r;
        for (long long i = f; i < static_cast<long long>(mps.size()); ++i)
            r.push_back(mps[static_cast<std::size_t>(i)]);
        return r;
    };

    if (mps[static_cast<std::size_t>(first_in)]->beat == start_beat) {
        vector<shared_ptr<GuidePoint>> result = from(first_in);
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    GuidePoint* prev = last_before >= 0 ? mps[static_cast<std::size_t>(last_before)].get() : nullptr;

    if (prev != nullptr && prev->ease == "linear") {
        auto [lane, size] = lerp_point(prev, mps[static_cast<std::size_t>(first_in)].get(), start_beat);
        auto new_start = std::make_shared<GuidePoint>();
        new_start->beat = start_beat;
        new_start->ease = "linear";
        new_start->lane = lane;
        new_start->size = size;
        new_start->timeScaleGroup = mps[static_cast<std::size_t>(first_in)]->timeScaleGroup;
        new_start->speedRatio = mps[static_cast<std::size_t>(first_in)]->speedRatio;
        vector<shared_ptr<GuidePoint>> result;
        result.push_back(new_start);
        for (long long i = first_in; i < static_cast<long long>(mps.size()); ++i)
            result.push_back(mps[static_cast<std::size_t>(i)]);
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    vector<shared_ptr<GuidePoint>> result = from(first_in);
    if (result.size() >= 2) return result;
    return std::nullopt;
}

static std::optional<vector<shared_ptr<GuidePoint>>> truncate_guide_end(
    const vector<shared_ptr<GuidePoint>>& mps, double end_beat) {
    long long last_in = -1;
    long long first_after = -1;
    for (long long i = 0; i < static_cast<long long>(mps.size()); ++i) {
        if (mps[static_cast<std::size_t>(i)]->beat <= end_beat) {
            last_in = i;
        } else if (first_after < 0) {
            first_after = i;
        }
    }
    if (last_in < 0) return std::nullopt;

    auto head = [&](long long upto) {
        vector<shared_ptr<GuidePoint>> r;
        for (long long i = 0; i <= upto; ++i) r.push_back(mps[static_cast<std::size_t>(i)]);
        return r;
    };

    if (mps[static_cast<std::size_t>(last_in)]->beat == end_beat) {
        vector<shared_ptr<GuidePoint>> result = head(last_in);
        if (result.size() >= 2) return result;
        return std::nullopt;
    }

    GuidePoint* last_mp = mps[static_cast<std::size_t>(last_in)].get();
    GuidePoint* next_mp = first_after >= 0 ? mps[static_cast<std::size_t>(first_after)].get() : nullptr;

    if (next_mp != nullptr && last_mp->ease == "linear") {
        auto [lane, size] = lerp_point(last_mp, next_mp, end_beat);
        auto new_end = std::make_shared<GuidePoint>();
        new_end->beat = end_beat;
        new_end->ease = "linear";
        new_end->lane = lane;
        new_end->size = size;
        new_end->timeScaleGroup = last_mp->timeScaleGroup;
        new_end->speedRatio = last_mp->speedRatio;
        vector<shared_ptr<GuidePoint>> result = head(last_in);
        result.push_back(new_end);
        return result;
    }

    vector<shared_ptr<GuidePoint>> result = head(last_in);
    if (result.size() >= 2) return result;
    return std::nullopt;
}

// ---------------- Score methods ----------------

void Score::delete_fake_notes() {
    vector<NodePtr> out;
    for (auto& note : notes) {
        bool fake = false;
        if (note->kind == Kind::Single)
            fake = static_cast<Single*>(note.get())->fake;
        else if (note->kind == Kind::Slide)
            fake = static_cast<Slide*>(note.get())->fake;
        else if (note->kind == Kind::SlideRelayPoint)
            fake = static_cast<SlideRelayPoint*>(note.get())->fake;
        if (fake) {
            // pass
        } else {
            out.push_back(note);
        }
    }
    notes = std::move(out);
}

void Score::delete_damage_notes() {
    vector<NodePtr> out;
    for (auto& note : notes) {
        if (note->kind == Kind::Single &&
            static_cast<Single*>(note.get())->type == "damage") {
            // pass
        } else {
            out.push_back(note);
        }
    }
    notes = std::move(out);
}

void Score::replace_extended_guide_colors() {
    static const std::map<string, string> color_map = {
        {"neutral", "green"}, {"red", "yellow"}, {"green", "green"}, {"blue", "green"},
        {"yellow", "yellow"}, {"purple", "yellow"}, {"cyan", "green"}, {"black", "green"}};
    for (auto& note : notes) {
        if (note->kind != Kind::Guide) continue;
        auto* g = static_cast<Guide*>(note.get());
        g->color = color_map.at(g->color);
    }
}

void Score::replace_extended_ease() {
    // XXX: Probably better to add an attach tick halfway, then switch ease
    // If anyone wants to PR this, feel free!
    static const std::map<string, string> ease_map = {{"outin", "in"},
                                                       {"out", "out"},
                                                       {"linear", "linear"},
                                                       {"in", "in"},
                                                       {"inout", "out"}};
    for (auto& note : notes) {
        if (note->kind != Kind::Slide && note->kind != Kind::Guide) continue;
        if (note->kind == Kind::Slide) {
            for (auto& c : static_cast<Slide*>(note.get())->connections) {
                if (has_ease(c.get())) {
                    if (c->kind == Kind::SlideStartPoint)
                        static_cast<SlideStartPoint*>(c.get())->ease =
                            ease_map.at(static_cast<SlideStartPoint*>(c.get())->ease);
                    else if (c->kind == Kind::SlideRelayPoint)
                        static_cast<SlideRelayPoint*>(c.get())->ease =
                            ease_map.at(static_cast<SlideRelayPoint*>(c.get())->ease);
                }
            }
        }
        if (note->kind == Kind::Guide) {
            for (auto& m : static_cast<Guide*>(note.get())->midpoints) {
                m->ease = ease_map.at(m->ease);
            }
        }
    }
}

void Score::sort_by_beat() {
    auto key = [](const NodePtr& x) -> double {
        if (has_beat(x->kind)) return node_beat(x.get());
        if (x->kind == Kind::Slide)
            return node_beat(static_cast<Slide*>(x.get())->connections[0].get());
        if (x->kind == Kind::Guide)
            return static_cast<Guide*>(x.get())->midpoints[0]->beat;
        return static_cast<TimeScaleGroup*>(x.get())->changes[0]->beat;
    };
    std::stable_sort(notes.begin(), notes.end(),
                     [&](const NodePtr& a, const NodePtr& b) { return key(a) < key(b); });
}

// フェードなしガイドの中継点を生成する
void Score::add_point_without_fade() {
    for (auto& note : notes) {
        if (note->kind != Kind::Guide) continue;
        auto* g = static_cast<Guide*>(note.get());
        if (g->fade == "none") {
            GuidePoint* end_point = g->midpoints.back().get();
            auto np = std::make_shared<GuidePoint>();
            np->beat = end_point->beat - BEAT_PER_TICK;
            np->ease = "linear";
            np->lane = end_point->lane;
            np->size = end_point->size;
            np->timeScaleGroup = end_point->timeScaleGroup;
            g->append(np);
        }
    }
}

void Score::strip_extended_lanes(bool resize_if_possible) {
    vector<NodePtr> out;
    double max_lane = 6;
    double min_lane = -6;
    for (auto& note_sp : notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
            is_event(note->kind) || note->kind == Kind::Volume) {
            out.push_back(note_sp);
            continue;
        }
        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            double lane_start = s->lane - s->size;
            double lane_end = s->lane + s->size;
            if (lane_start < min_lane) {
                if (resize_if_possible && (lane_end > min_lane)) {
                    double extra = std::abs(lane_start - min_lane);
                    s->size -= extra / 2;
                    s->lane += extra / 2;
                }
            }
            if (lane_end > max_lane) {
                if (resize_if_possible && (lane_start < max_lane) &&
                    (lane_start >= min_lane)) {
                    double extra = std::abs(lane_end - max_lane);
                    s->size -= extra / 2;
                    s->lane -= extra / 2;
                }
            }
            lane_start = s->lane - s->size;
            lane_end = s->lane + s->size;
            if (lane_start >= min_lane && lane_end <= max_lane) out.push_back(note_sp);
        }
        if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            vector<NodePtr> connectors;
            bool should_add = true;
            for (std::size_t i = 0; i < sl->connections.size(); ++i) {
                Node* connector = sl->connections[i].get();
                double lane_start = node_lane(connector) - node_size(connector);
                double lane_end = node_lane(connector) + node_size(connector);
                bool added = false;
                auto set_lane = [&](double v) {
                    switch (connector->kind) {
                        case Kind::SlideStartPoint: static_cast<SlideStartPoint*>(connector)->lane = v; break;
                        case Kind::SlideRelayPoint: static_cast<SlideRelayPoint*>(connector)->lane = v; break;
                        case Kind::SlideEndPoint: static_cast<SlideEndPoint*>(connector)->lane = v; break;
                        default: break;
                    }
                };
                auto set_size = [&](double v) {
                    switch (connector->kind) {
                        case Kind::SlideStartPoint: static_cast<SlideStartPoint*>(connector)->size = v; break;
                        case Kind::SlideRelayPoint: static_cast<SlideRelayPoint*>(connector)->size = v; break;
                        case Kind::SlideEndPoint: static_cast<SlideEndPoint*>(connector)->size = v; break;
                        default: break;
                    }
                };
                if (lane_start < min_lane) {
                    if (resize_if_possible && (lane_end > min_lane)) {
                        double extra = std::abs(lane_start - min_lane);
                        set_size(node_size(connector) - extra / 2);
                        set_lane(node_lane(connector) + extra / 2);
                    }
                }
                if (lane_end > max_lane) {
                    if (resize_if_possible && (lane_start < max_lane) &&
                        (lane_start >= min_lane)) {
                        double extra = std::abs(lane_end - max_lane);
                        set_size(node_size(connector) - extra / 2);
                        set_lane(node_lane(connector) - extra / 2);
                    }
                }
                lane_start = node_lane(connector) - node_size(connector);
                lane_end = node_lane(connector) + node_size(connector);
                if (lane_start >= min_lane && lane_end <= max_lane) {
                    connectors.push_back(sl->connections[i]);
                    added = true;
                }
                if ((i == 0 || i == sl->connections.size() - 1) && !added)
                    should_add = false;
            }
            if (should_add) {
                out.push_back(std::make_shared<Slide>(sl->critical, false, connectors,
                                                      sl->type));
            }
        }
        if (note->kind == Kind::Guide) {
            auto* gd = static_cast<Guide*>(note);
            vector<shared_ptr<GuidePoint>> midpoints;
            bool should_add = true;
            for (std::size_t i = 0; i < gd->midpoints.size(); ++i) {
                GuidePoint* midpoint = gd->midpoints[i].get();
                double lane_start = midpoint->lane - midpoint->size;
                double lane_end = midpoint->lane + midpoint->size;
                bool added = false;
                if (lane_start < min_lane) {
                    if (resize_if_possible && (lane_end > min_lane)) {
                        double extra = std::abs(lane_start - min_lane);
                        midpoint->size -= extra / 2;
                        midpoint->lane += extra / 2;
                    }
                }
                if (lane_end > max_lane) {
                    if (resize_if_possible && (lane_start < max_lane) &&
                        (lane_start >= min_lane)) {
                        double extra = std::abs(lane_end - max_lane);
                        midpoint->size -= extra / 2;
                        midpoint->lane -= extra / 2;
                    }
                }
                lane_start = midpoint->lane - midpoint->size;
                lane_end = midpoint->lane + midpoint->size;
                if (lane_start >= min_lane && lane_end <= max_lane) {
                    midpoints.push_back(gd->midpoints[i]);
                    added = true;
                }
                if ((i == 0 || i == gd->midpoints.size() - 1) && !added)
                    should_add = false;
            }
            if (should_add) {
                out.push_back(std::make_shared<Guide>(gd->color, gd->fade, midpoints,
                                                      gd->type, gd->color_code));
            }
        }
    }
    notes = std::move(out);
}

void Score::strip_speed_ratios() {
    for (auto& note : notes) {
        if (note->kind == Kind::Single) {
            static_cast<Single*>(note.get())->speedRatio = 1.0;
        } else if (note->kind == Kind::Slide) {
            for (auto& conn : static_cast<Slide*>(note.get())->connections) {
                switch (conn->kind) {
                    case Kind::SlideStartPoint: static_cast<SlideStartPoint*>(conn.get())->speedRatio = 1.0; break;
                    case Kind::SlideRelayPoint: static_cast<SlideRelayPoint*>(conn.get())->speedRatio = 1.0; break;
                    case Kind::SlideEndPoint: static_cast<SlideEndPoint*>(conn.get())->speedRatio = 1.0; break;
                    default: break;
                }
            }
        } else if (note->kind == Kind::Guide) {
            for (auto& mp : static_cast<Guide*>(note.get())->midpoints) mp->speedRatio = 1.0;
        }
    }
}

static void set_pt_tsg(Node* n, long long v) {
    switch (n->kind) {
        case Kind::Single: static_cast<Single*>(n)->timeScaleGroup = v; break;
        case Kind::SlideStartPoint: static_cast<SlideStartPoint*>(n)->timeScaleGroup = v; break;
        case Kind::SlideRelayPoint: static_cast<SlideRelayPoint*>(n)->timeScaleGroup = v; break;
        case Kind::SlideEndPoint: static_cast<SlideEndPoint*>(n)->timeScaleGroup = v; break;
        case Kind::GuidePoint: static_cast<GuidePoint*>(n)->timeScaleGroup = v; break;
        default: break;
    }
}
static void set_pt_speed(Node* n, double v) {
    switch (n->kind) {
        case Kind::Single: static_cast<Single*>(n)->speedRatio = v; break;
        case Kind::SlideStartPoint: static_cast<SlideStartPoint*>(n)->speedRatio = v; break;
        case Kind::SlideRelayPoint: static_cast<SlideRelayPoint*>(n)->speedRatio = v; break;
        case Kind::SlideEndPoint: static_cast<SlideEndPoint*>(n)->speedRatio = v; break;
        case Kind::GuidePoint: static_cast<GuidePoint*>(n)->speedRatio = v; break;
        default: break;
    }
}

void Score::flatten_speed_ratios_to_layers() {
    vector<long long> tsg_indices;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (notes[i]->kind == Kind::TimeScaleGroup)
            tsg_indices.push_back(static_cast<long long>(i));
    }

    std::set<double> unique_ratios;
    for (auto& note : notes) {
        if (note->kind == Kind::Single) {
            if (static_cast<Single*>(note.get())->speedRatio != 1.0)
                unique_ratios.insert(static_cast<Single*>(note.get())->speedRatio);
        } else if (note->kind == Kind::Slide) {
            for (auto& conn : static_cast<Slide*>(note.get())->connections)
                if (pt_speed(conn.get()) != 1.0) unique_ratios.insert(pt_speed(conn.get()));
        } else if (note->kind == Kind::Guide) {
            for (auto& mp : static_cast<Guide*>(note.get())->midpoints)
                if (mp->speedRatio != 1.0) unique_ratios.insert(mp->speedRatio);
        }
    }

    if (unique_ratios.empty()) return;

    long long next_group_idx = static_cast<long long>(tsg_indices.size());
    std::map<double, long long> ratio_to_group;
    vector<NodePtr> new_groups;
    for (double ratio : unique_ratios) {  // std::set iterates sorted ascending
        ratio_to_group[ratio] = next_group_idx;
        auto g = std::make_shared<TimeScaleGroup>();
        g->changes.push_back(std::make_shared<TimeScalePoint>(0.0, ratio));
        new_groups.push_back(g);
        ++next_group_idx;
    }

    for (auto& g : new_groups) notes.push_back(g);

    for (auto& note : notes) {
        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note.get());
            if (s->speedRatio != 1.0) {
                s->timeScaleGroup = ratio_to_group[s->speedRatio];
                s->speedRatio = 1.0;
            }
        } else if (note->kind == Kind::Slide) {
            for (auto& conn : static_cast<Slide*>(note.get())->connections) {
                if (pt_speed(conn.get()) != 1.0) {
                    set_pt_tsg(conn.get(), ratio_to_group[pt_speed(conn.get())]);
                    set_pt_speed(conn.get(), 1.0);
                }
            }
        } else if (note->kind == Kind::Guide) {
            for (auto& mp : static_cast<Guide*>(note.get())->midpoints) {
                if (mp->speedRatio != 1.0) {
                    mp->timeScaleGroup = ratio_to_group[mp->speedRatio];
                    mp->speedRatio = 1.0;
                }
            }
        }
    }
}

bool Score::check_skill_overlap() const {
    vector<double> skill_timings;
    for (auto& note : notes) {
        if (note->kind == Kind::Skill) {
            double b = static_cast<Skill*>(note.get())->beat;
            if (std::find(skill_timings.begin(), skill_timings.end(), b) !=
                skill_timings.end())
                return true;
            skill_timings.push_back(b);
        }
    }
    return false;
}

std::pair<Score, long long> Score::export_overlaps_score() const {
    vector<Node*> tmp_notes_raw;
    vector<NodePtr> useful_notes;
    // 一旦、中継点灯を含めた全部のノーツを入れたリストを作る（BPM, ソフランは除外）
    for (auto& note : notes) {
        if (note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
            is_event(note->kind) || note->kind == Kind::Volume) {
            useful_notes.push_back(note);
            continue;
        }
        tmp_notes_raw.push_back(note.get());
    }
    vector<Node*> tmp_notes = convert_tmp_notes(tmp_notes_raw);

    std::map<std::pair<double, long long>, std::tuple<double, double, double, long long>> used;
    std::set<std::pair<double, long long>> added_used;
    vector<std::tuple<double, double, double, long long>> overlaps_at;
    for (Node* note : tmp_notes) {
        std::pair<double, long long> key = {node_beat(note),
                                            usc_lanes_to_sus_lanes(node_lane(note),
                                                                   node_size(note))};
        if (used.count(key)) {
            if (!added_used.count(key)) {
                added_used.insert(key);
                overlaps_at.push_back(used[key]);
            }
            overlaps_at.emplace_back(node_beat(note), node_lane(note), node_size(note),
                                     pt_tsg(note));
        } else {
            used[key] = std::make_tuple(node_beat(note), node_lane(note), node_size(note),
                                        pt_tsg(note));
        }
    }
    vector<NodePtr> new_notes = useful_notes;
    for (auto& d : overlaps_at) {
        auto s = std::make_shared<Single>();
        s->beat = std::get<0>(d);
        s->critical = true;
        s->lane = std::get<1>(d);
        s->size = std::get<2>(d);
        s->timeScaleGroup = std::get<3>(d);
        s->trace = false;
        s->direction = std::nullopt;
        new_notes.push_back(s);
    }
    Score score(metadata, new_notes);
    return {score, static_cast<long long>(overlaps_at.size())};
}

// 重なっているノーツをずらす
void Score::shift() {
    vector<Node*> tmp_notes_raw;
    for (auto& note : notes) {
        if (note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
            is_event(note->kind) || note->kind == Kind::Volume) {
            continue;
        }
        tmp_notes_raw.push_back(note.get());
    }
    vector<Node*> tmp_notes = convert_tmp_notes(tmp_notes_raw);

    double max_beat = 0.0;
    bool any = false;
    for (Node* n : tmp_notes) {
        double b = node_beat(n);
        if (!any || b > max_beat) {
            max_beat = b;
            any = true;
        }
    }

    long long nlists = py_trunc(py_floordiv_f(max_beat, BAR_INTERVAL) + 1);
    if (nlists < 0) nlists = 0;
    vector<vector<Node*>> split_tmp_notes(static_cast<std::size_t>(nlists));

    for (Node* note : tmp_notes) {
        split_tmp_notes[static_cast<std::size_t>(calc_notelist_index(note))].push_back(note);
    }

    for (auto& note : notes) {
        if (note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
            is_event(note->kind) || note->kind == Kind::Volume) {
            continue;
        }
        if (note->kind == Kind::Single) {
            shift_single(static_cast<Single*>(note.get()), split_tmp_notes);
        } else if (note->kind == Kind::Slide) {
            shift_slide(static_cast<Slide*>(note.get()), split_tmp_notes);
        } else if (note->kind == Kind::Guide) {
            shift_guide(static_cast<Guide*>(note.get()), split_tmp_notes);
        }
    }
}

std::vector<std::pair<double, double>> Score::bpm_timeline() const {
    vector<Bpm*> bpms;
    for (auto& n : notes)
        if (n->kind == Kind::Bpm) bpms.push_back(static_cast<Bpm*>(n.get()));
    std::stable_sort(bpms.begin(), bpms.end(),
                     [](Bpm* a, Bpm* b) { return a->beat < b->beat; });
    if (bpms.empty()) return {{0.0, 120.0}};
    std::vector<std::pair<double, double>> out;
    if (bpms[0]->beat > 0) out.push_back({0.0, bpms[0]->bpm});
    for (Bpm* b : bpms) out.push_back({b->beat, b->bpm});
    return out;
}

double Score::time_at_beat(double target_beat) const {
    auto timeline = bpm_timeline();
    double elapsed = 0.0;
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        double beat = timeline[i].first;
        double bpm = timeline[i].second;
        double next_beat = (i + 1 < timeline.size()) ? timeline[i + 1].first : target_beat;
        double segment_end = std::min(next_beat, target_beat);
        if (segment_end > beat) elapsed += (segment_end - beat) / bpm * 60.0;
        if (segment_end >= target_beat) break;
    }
    return elapsed;
}

double Score::beat_at_time(double target_time) const {
    auto timeline = bpm_timeline();
    double elapsed = 0.0;
    for (std::size_t i = 0; i < timeline.size(); ++i) {
        double beat = timeline[i].first;
        double bpm = timeline[i].second;
        double next_beat = (i + 1 < timeline.size())
                               ? timeline[i + 1].first
                               : std::numeric_limits<double>::infinity();
        double segment_duration = (next_beat - beat) / bpm * 60.0;
        if (elapsed + segment_duration >= target_time) {
            double remaining = target_time - elapsed;
            return beat + remaining * bpm / 60.0;
        }
        elapsed += segment_duration;
    }
    double last_beat = timeline.back().first;
    double last_bpm = timeline.back().second;
    double remaining = target_time - elapsed;
    return last_beat + remaining * last_bpm / 60.0;
}

double Score::note_beat(const Node* note) const {
    if (has_beat(note->kind)) return node_beat(const_cast<Node*>(note));
    if (note->kind == Kind::Slide)
        return node_beat(static_cast<const Slide*>(note)->connections[0].get());
    if (note->kind == Kind::Guide)
        return static_cast<const Guide*>(note)->midpoints[0]->beat;
    if (note->kind == Kind::TimeScaleGroup)
        return static_cast<const TimeScaleGroup*>(note)->changes[0]->beat;
    return 0.0;
}

double Score::note_max_beat(const Node* note) const {
    if (note->kind == Kind::Slide)
        return node_beat(static_cast<const Slide*>(note)->connections.back().get());
    if (note->kind == Kind::Guide)
        return static_cast<const Guide*>(note)->midpoints.back()->beat;
    if (note->kind == Kind::TimeScaleGroup)
        return static_cast<const TimeScaleGroup*>(note)->changes.back()->beat;
    return note_beat(note);
}

double Score::duration() const {
    double max_beat = 0.0;
    for (auto& note : notes) {
        double mb = note_max_beat(note.get());
        if (mb > max_beat) max_beat = mb;
    }
    return time_at_beat(max_beat);
}

std::vector<Score::ComboEvent> Score::combo_events() const {
    const long long TICKS_PER_BEAT = 480;
    const long long HALF_BEAT = TICKS_PER_BEAT / 2;
    std::vector<ComboEvent> out;
    for (auto& note_sp : notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            out.push_back({s->direction.has_value() ? "flicks" : "taps",
                           s->critical.value_or(false), s->beat});
        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            vector<Node*> connections;
            for (auto& c : sl->connections) connections.push_back(c.get());
            std::stable_sort(connections.begin(), connections.end(),
                             [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
            long long start_tick = util::py_round_int(node_beat(connections.front()) * TICKS_PER_BEAT);
            long long eighth_tick = start_tick + HALF_BEAT;
            if (util::py_mod(eighth_tick, HALF_BEAT))
                eighth_tick -= util::py_mod(eighth_tick, HALF_BEAT);
            long long end_tick = util::py_round_int(node_beat(connections.back()) * TICKS_PER_BEAT);
            bool has_ticks = eighth_tick != start_tick && eighth_tick != end_tick;
            bool crit = sl->critical;
            Node* prev_joint = nullptr;
            for (Node* conn : connections) {
                if (conn->kind == Kind::SlideStartPoint) {
                    auto* c = static_cast<SlideStartPoint*>(conn);
                    if (c->judgeType != "none")
                        out.push_back({"long_starts", c->critical, c->beat});
                    prev_joint = conn;
                } else if (conn->kind == Kind::SlideEndPoint) {
                    auto* c = static_cast<SlideEndPoint*>(conn);
                    if (c->judgeType != "none") {
                        string cat = c->direction.has_value() ? "long_flick_ends"
                                                              : "long_ends";
                        out.push_back({cat, c->critical, c->beat});
                    }
                    if (prev_joint != nullptr && has_ticks) {
                        long long adj_end = util::py_round_int(c->beat * TICKS_PER_BEAT);
                        if (util::py_mod(adj_end, HALF_BEAT))
                            adj_end += HALF_BEAT - util::py_mod(adj_end, HALF_BEAT);
                        while (eighth_tick < adj_end) {
                            out.push_back({"long_continuations", crit,
                                           static_cast<double>(eighth_tick) / TICKS_PER_BEAT});
                            eighth_tick += HALF_BEAT;
                        }
                    }
                } else if (conn->kind == Kind::SlideRelayPoint) {
                    auto* c = static_cast<SlideRelayPoint*>(conn);
                    if (c->type == "attach") {
                        if (c->critical.has_value())
                            out.push_back({"long_relays", c->critical.value(), c->beat});
                    } else {
                        if (c->critical.has_value())
                            out.push_back({"long_relays", c->critical.value(), c->beat});
                        if (prev_joint != nullptr && has_ticks) {
                            long long adj_end = util::py_round_int(c->beat * TICKS_PER_BEAT);
                            if (util::py_mod(adj_end, HALF_BEAT))
                                adj_end += HALF_BEAT - util::py_mod(adj_end, HALF_BEAT);
                            while (eighth_tick < adj_end) {
                                out.push_back({"long_continuations", crit,
                                               static_cast<double>(eighth_tick) / TICKS_PER_BEAT});
                                eighth_tick += HALF_BEAT;
                            }
                        }
                        prev_joint = conn;
                    }
                }
            }
        }
    }
    return out;
}

long long Score::combo_count() const { return static_cast<long long>(combo_events().size()); }

long long Score::note_count() const { return combo_count(); }

long long Score::combo_before_beat(double cutoff_beat) const {
    const long long TICKS_PER_BEAT = 480;
    const long long HALF_BEAT = TICKS_PER_BEAT / 2;
    long long cutoff_tick = util::py_round_int(cutoff_beat * TICKS_PER_BEAT);
    long long count = 0;
    for (auto& note_sp : notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Single) {
            if (util::py_round_int(static_cast<Single*>(note)->beat * TICKS_PER_BEAT) < cutoff_tick)
                ++count;
        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            vector<Node*> connections;
            for (auto& c : sl->connections) connections.push_back(c.get());
            std::stable_sort(connections.begin(), connections.end(),
                             [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
            long long start_tick = util::py_round_int(node_beat(connections.front()) * TICKS_PER_BEAT);
            long long eighth_tick = start_tick + HALF_BEAT;
            if (util::py_mod(eighth_tick, HALF_BEAT))
                eighth_tick -= util::py_mod(eighth_tick, HALF_BEAT);
            long long end_tick = util::py_round_int(node_beat(connections.back()) * TICKS_PER_BEAT);
            (void)end_tick;
            bool has_ticks = eighth_tick != start_tick && eighth_tick != end_tick;
            Node* prev_joint = nullptr;
            for (Node* conn : connections) {
                long long conn_tick = util::py_round_int(node_beat(conn) * TICKS_PER_BEAT);
                if (conn->kind == Kind::SlideStartPoint) {
                    if (static_cast<SlideStartPoint*>(conn)->judgeType != "none" &&
                        conn_tick < cutoff_tick)
                        ++count;
                    prev_joint = conn;
                } else if (conn->kind == Kind::SlideEndPoint) {
                    if (static_cast<SlideEndPoint*>(conn)->judgeType != "none" &&
                        conn_tick < cutoff_tick)
                        ++count;
                    if (prev_joint != nullptr && has_ticks) {
                        long long adj_end = conn_tick;
                        if (util::py_mod(adj_end, HALF_BEAT))
                            adj_end += HALF_BEAT - util::py_mod(adj_end, HALF_BEAT);
                        long long effective_end = std::min(adj_end, cutoff_tick);
                        if (eighth_tick < effective_end) {
                            long long steps =
                                util::py_floordiv(effective_end - eighth_tick - 1, HALF_BEAT) + 1;
                            count += steps;
                            eighth_tick += steps * HALF_BEAT;
                        }
                    }
                } else if (conn->kind == Kind::SlideRelayPoint) {
                    auto* c = static_cast<SlideRelayPoint*>(conn);
                    if (c->type == "attach") {
                        if (c->critical.has_value() && conn_tick < cutoff_tick) ++count;
                    } else {
                        // visible tick + 8th hold tick at same beat = 2 combo (correct)
                        if (c->critical.has_value() && conn_tick < cutoff_tick) ++count;
                        if (prev_joint != nullptr && has_ticks) {
                            long long adj_end = conn_tick;
                            if (util::py_mod(adj_end, HALF_BEAT))
                                adj_end += HALF_BEAT - util::py_mod(adj_end, HALF_BEAT);
                            long long effective_end = std::min(adj_end, cutoff_tick);
                            if (eighth_tick < effective_end) {
                                long long steps =
                                    util::py_floordiv(effective_end - eighth_tick - 1, HALF_BEAT) + 1;
                                count += steps;
                                eighth_tick += steps * HALF_BEAT;
                            }
                        }
                        prev_joint = conn;
                    }
                }
            }
        }
    }
    return count;
}

std::tuple<long long, std::pair<long long, long long>> Score::cut(
    std::optional<double> start_at, std::optional<double> end_at, bool keep_position) {
    if (!start_at.has_value() && !end_at.has_value())
        throw std::invalid_argument("At least one of start_at or end_at must be specified");

    const long long TICKS_PER_BEAT = 480;

    double start_beat = start_at.has_value() ? beat_at_time(*start_at) : 0.0;
    double end_beat =
        end_at.has_value() ? beat_at_time(*end_at) : std::numeric_limits<double>::infinity();

    if (start_at.has_value())
        start_beat = static_cast<double>(util::py_round_int(start_beat * TICKS_PER_BEAT)) / TICKS_PER_BEAT;
    if (end_at.has_value())
        end_beat = static_cast<double>(util::py_round_int(end_beat * TICKS_PER_BEAT)) / TICKS_PER_BEAT;

    long long cut_start_tick = util::py_round_int(start_beat * TICKS_PER_BEAT);
    long long cut_end_tick;
    if (end_at.has_value()) {
        cut_end_tick = util::py_round_int(end_beat * TICKS_PER_BEAT);
    } else {
        double max_beat = 0.0;
        for (auto& note : notes) {
            if (note->kind == Kind::Single)
                max_beat = std::max(max_beat,
                                    static_cast<double>(static_cast<Single*>(note.get())->beat));
            else if (note->kind == Kind::Slide)
                for (auto& c : static_cast<Slide*>(note.get())->connections)
                    max_beat = std::max(max_beat, static_cast<double>(node_beat(c.get())));
        }
        cut_end_tick = util::py_round_int(max_beat * TICKS_PER_BEAT);
    }

    long long original_total = combo_count();
    long long combo_after_raw = 0;
    if (end_at.has_value() && start_at.has_value()) {
        long long end_tick = util::py_round_int(end_beat * TICKS_PER_BEAT);
        combo_after_raw =
            original_total - combo_before_beat(static_cast<double>(end_tick + 1) / TICKS_PER_BEAT);
    }

    vector<TimeScaleGroup*> tsg_list;
    for (auto& note : notes)
        if (note->kind == Kind::TimeScaleGroup)
            tsg_list.push_back(static_cast<TimeScaleGroup*>(note.get()));

    vector<NodePtr> kept;
    std::set<long long> used_tsg;
    NodePtr last_bpm_before = nullptr;
    NodePtr last_vol_before = nullptr;

    for (auto& note_sp : notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::TimeScaleGroup) continue;

        if (note->kind == Kind::Bpm) {
            auto* b = static_cast<Bpm*>(note);
            if (b->beat < start_beat)
                last_bpm_before = std::make_shared<Bpm>(start_beat, b->bpm);
            else if (b->beat <= end_beat)
                kept.push_back(note_sp);
            continue;
        }

        if (note->kind == Kind::Volume) {
            auto* v = static_cast<Volume*>(note);
            if (v->beat < start_beat)
                last_vol_before = std::make_shared<Volume>(start_beat, v->volume);
            else if (v->beat <= end_beat)
                kept.push_back(note_sp);
            continue;
        }

        if (is_event(note->kind)) {
            double b = node_beat(note);
            if (start_beat <= b && b <= end_beat) kept.push_back(note_sp);
            continue;
        }

        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            if (start_beat <= s->beat && s->beat <= end_beat) {
                kept.push_back(note_sp);
                used_tsg.insert(s->timeScaleGroup);
            }
            continue;
        }

        if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            vector<NodePtr> conns = sl->connections;
            std::stable_sort(conns.begin(), conns.end(), [](const NodePtr& a, const NodePtr& b) {
                return node_beat(a.get()) < node_beat(b.get());
            });
            if (node_beat(conns.front().get()) > end_beat ||
                node_beat(conns.back().get()) < start_beat)
                continue;
            if (node_beat(conns.front().get()) >= start_beat &&
                node_beat(conns.back().get()) <= end_beat) {
                kept.push_back(note_sp);
                for (auto& c : sl->connections) used_tsg.insert(pt_tsg(c.get()));
                continue;
            }
            std::optional<vector<NodePtr>> result_conns = conns;
            if (node_beat(conns.front().get()) < start_beat)
                result_conns = truncate_slide_start(*result_conns, start_beat, sl->critical);
            if (result_conns.has_value() &&
                node_beat(result_conns->back().get()) > end_beat)
                result_conns = truncate_slide_end(*result_conns, end_beat, sl->critical);
            if (result_conns.has_value() && result_conns->size() >= 2) {
                auto new_slide = std::make_shared<Slide>(sl->critical, sl->fake,
                                                         *result_conns, "slide");
                kept.push_back(new_slide);
                for (auto& c : *result_conns) used_tsg.insert(pt_tsg(c.get()));
            }
            continue;
        }

        if (note->kind == Kind::Guide) {
            auto* gd = static_cast<Guide*>(note);
            vector<shared_ptr<GuidePoint>> mps = gd->midpoints;
            std::stable_sort(mps.begin(), mps.end(),
                             [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                                 return a->beat < b->beat;
                             });
            if (mps.front()->beat > end_beat || mps.back()->beat < start_beat) continue;
            if (mps.front()->beat >= start_beat && mps.back()->beat <= end_beat) {
                kept.push_back(note_sp);
                for (auto& m : gd->midpoints) used_tsg.insert(m->timeScaleGroup);
                continue;
            }
            std::optional<vector<shared_ptr<GuidePoint>>> result_mps = mps;
            if (mps.front()->beat < start_beat)
                result_mps = truncate_guide_start(*result_mps, start_beat);
            if (result_mps.has_value() && result_mps->back()->beat > end_beat)
                result_mps = truncate_guide_end(*result_mps, end_beat);
            if (result_mps.has_value() && result_mps->size() >= 2) {
                auto new_guide = std::make_shared<Guide>(gd->color, gd->fade, *result_mps,
                                                         "guide", gd->color_code);
                kept.push_back(new_guide);
                for (auto& m : *result_mps) used_tsg.insert(m->timeScaleGroup);
            }
            continue;
        }
    }

    if (last_bpm_before != nullptr) {
        bool found = false;
        for (auto& n : kept)
            if (n->kind == Kind::Bpm && static_cast<Bpm*>(n.get())->beat == start_beat) {
                found = true;
                break;
            }
        if (!found) kept.insert(kept.begin(), last_bpm_before);
    }

    if (last_vol_before != nullptr) {
        bool found = false;
        for (auto& n : kept)
            if (n->kind == Kind::Volume && static_cast<Volume*>(n.get())->beat == start_beat) {
                found = true;
                break;
            }
        if (!found) kept.insert(kept.begin(), last_vol_before);
    }

    // Remove layers with no notes, renumber remaining
    std::map<long long, long long> old_to_new;
    vector<NodePtr> new_tsgs;
    for (long long old_idx = 0; old_idx < static_cast<long long>(tsg_list.size()); ++old_idx) {
        if (used_tsg.count(old_idx)) {
            old_to_new[old_idx] = static_cast<long long>(new_tsgs.size());
            // keep the shared_ptr for this tsg
            for (auto& note : notes) {
                if (note.get() == tsg_list[static_cast<std::size_t>(old_idx)]) {
                    new_tsgs.push_back(note);
                    break;
                }
            }
        }
    }

    auto remap = [&](long long tsg) {
        auto it = old_to_new.find(tsg);
        return it != old_to_new.end() ? it->second : 0LL;
    };

    for (auto& note : kept) {
        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note.get());
            s->timeScaleGroup = remap(s->timeScaleGroup);
        } else if (note->kind == Kind::Slide) {
            for (auto& conn : static_cast<Slide*>(note.get())->connections)
                set_pt_tsg(conn.get(), remap(pt_tsg(conn.get())));
        } else if (note->kind == Kind::Guide) {
            for (auto& mp : static_cast<Guide*>(note.get())->midpoints)
                mp->timeScaleGroup = remap(mp->timeScaleGroup);
        }
    }

    vector<NodePtr> combined = new_tsgs;
    for (auto& n : kept) combined.push_back(n);
    notes = combined;

    long long combo_before;
    if (!start_at.has_value()) {
        combo_before = 0;
    } else {
        combo_before = original_total - combo_count() - combo_after_raw;
    }

    if (!keep_position && start_at.has_value() && start_beat > 0) {
        double shift = start_beat;
        for (auto& note : notes) {
            if (note->kind == Kind::TimeScaleGroup) {
                auto* tsg = static_cast<TimeScaleGroup*>(note.get());
                vector<shared_ptr<TimeScalePoint>> trimmed;
                TimeScalePoint* last_before = nullptr;
                for (auto& tp : tsg->changes) {
                    if (tp->beat < shift)
                        last_before = tp.get();
                    else
                        trimmed.push_back(
                            std::make_shared<TimeScalePoint>(tp->beat - shift, tp->timeScale));
                }
                if (last_before != nullptr && (trimmed.empty() || trimmed[0]->beat > 0)) {
                    trimmed.insert(trimmed.begin(),
                                   std::make_shared<TimeScalePoint>(0.0, last_before->timeScale));
                }
                tsg->changes = trimmed;
            } else if (note->kind == Kind::Bpm) {
                auto* b = static_cast<Bpm*>(note.get());
                b->beat = std::max(0.0, b->beat - shift);
            } else if (note->kind == Kind::Volume) {
                auto* v = static_cast<Volume*>(note.get());
                v->beat = std::max(0.0, v->beat - shift);
            } else if (note->kind == Kind::Single || is_event(note->kind)) {
                node_beat(note.get()) -= shift;
            } else if (note->kind == Kind::Slide) {
                for (auto& conn : static_cast<Slide*>(note.get())->connections)
                    node_beat(conn.get()) -= shift;
            } else if (note->kind == Kind::Guide) {
                for (auto& mp : static_cast<Guide*>(note.get())->midpoints)
                    mp->beat -= shift;
            }
        }
    }

    return {combo_before, {cut_start_tick, cut_end_tick}};
}

}  // namespace rg::core
