#include "rg/core/score.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "rg/util/pynum.hpp"
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
    out.format_metadata = s.format_metadata;
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

// The game merges notes on one tick by the lane they start in, whatever their widths: notes merely
// overlapping stay apart, and moving one of those only changes the chart (a slide's point moved past
// its neighbors bends the slide). The lane taken both as the SUS exporter truncates the left edge
// and as the floor of the edge snapped to a lane within float32 noise (GS6's LevelData reading; an
// edge left of the stage truncates the wrong way).
struct LeftLane {
    long long truncated, floored;
};
static LeftLane calc_left_lane(double lane, double size) {
    double edge = lane - size + LANE_OFFSET;
    double whole = std::nearbyint(edge);
    double snapped = std::fabs(edge - whole) < 1e-4 ? whole : edge;
    return {py_trunc(edge), static_cast<long long>(std::floor(snapped))};
}
static bool same_left_lane(const LeftLane& a, const LeftLane& b) {
    return a.truncated == b.truncated || a.floored == b.floored;
}

// The SUS tick a beat lands on (the SUS exporter's rounding). Points are told apart by tick, not by
// beat: a LevelData beat is a 6-decimal float, so a point moved by BEAT_PER_TICK can land on the tick
// of another point while its beat still differs from that point's.
static long long beat_tick(double beat) { return util::py_round_int(beat * 480); }

// ノーツを入れるリストの番号をbeatの値から計算する (a note before beat 0 in the first list)
static long long calc_notelist_index(const Node* note) {
    return std::max(0LL, py_trunc(py_floordiv_f(node_beat(const_cast<Node*>(note)), BAR_INTERVAL)));
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

// ゲームが一つにまとめるノーツ（同じtick、同じ左端レーン）を探す
static Node* get_overlap_note(Node* target_note,
                              const vector<vector<Node*>>& split_tmp_notes) {
    LeftLane t_note_lane = calc_left_lane(node_lane(target_note), node_size(target_note));
    long long t_note_tick = beat_tick(node_beat(target_note));

    long long index = calc_notelist_index(target_note);
    long long start = std::max(0LL, index - 1);
    long long end = std::min(static_cast<long long>(split_tmp_notes.size()), index + 2);

    for (long long si = start; si < end; ++si) {
        for (Node* note : split_tmp_notes[static_cast<std::size_t>(si)]) {
            // 同じインスタンス（ノーツ）の場合は飛ばす
            if (target_note == note) continue;
            if (t_note_tick == beat_tick(node_beat(note)) &&
                same_left_lane(t_note_lane, calc_left_lane(node_lane(note), node_size(note)))) {
                return note;
            }
        }
    }
    return nullptr;
}

struct ShiftContext {
    vector<vector<Node*>> split_tmp_notes;
    std::unordered_map<const Node*, Slide*> owner;   // slide point -> its slide
    std::unordered_map<const Slide*, long long> hold;   // each slide's hold combo before shifting
    // slide or guide point -> its line's points (a slide's start first and end last, the rest by beat)
    // and its place among them
    std::unordered_map<const Node*, std::pair<const vector<Node*>*, std::size_t>> line;
    std::deque<vector<Node*>> lines;
    std::unordered_map<const Node*, long long> source;   // each line point's tick before shifting
    std::set<std::pair<const Node*, const Node*>> kept;   // points next to each other left on one tick
    vector<vector<std::pair<double, double>>> groups;   // each timescale group's (beat, timeScale), by beat
    // moves landing on no other note tried before any landing on one (a pass merged as many notes as
    // the one before it: notes put on each other in turn can go round in a ring)
    bool free_first = false;
};

static const long long HALF_BEAT_TICKS = 240;

static long long tick_of(Node* n) { return beat_tick(node_beat(n)); }
static double tick_beat(long long tick) { return static_cast<double>(tick) / 480; }

// A slide holds a combo on every half beat strictly between its start and its end.
static long long hold_between(Node* start_point, Node* end_point) {
    if (!start_point || !end_point) return 0;
    long long first = util::py_floordiv(tick_of(start_point), HALF_BEAT_TICKS) + 1;
    long long last = util::py_floordiv(tick_of(end_point) + HALF_BEAT_TICKS - 1, HALF_BEAT_TICKS) - 1;
    return std::max(0LL, last - first + 1);
}

static long long hold_ticks(Slide* note) {
    Node* start_point = nullptr;
    Node* end_point = nullptr;
    for (auto& point : note->connections) {
        if (point->kind == Kind::SlideStartPoint) start_point = point.get();
        else if (point->kind == Kind::SlideEndPoint) end_point = point.get();
    }
    return hold_between(start_point, end_point);
}

// A slide's hold combo by its line (start first, end last).
static long long line_hold(const ShiftContext& ctx, Slide* note) {
    auto it = ctx.line.find(note->connections.empty() ? nullptr : note->connections.front().get());
    if (it == ctx.line.end()) return hold_ticks(note);
    const vector<Node*>& points = *it->second.first;
    Node* start_point = points.front()->kind == Kind::SlideStartPoint ? points.front() : nullptr;
    Node* end_point = points.back()->kind == Kind::SlideEndPoint ? points.back() : nullptr;
    return hold_between(start_point, end_point);
}

// How far a note of timescale group `group` goes on screen from beat `from` to beat `to`: its group's
// timescale summed over the way, in ticks at timescale 1. A tick inside a 10000x spike goes 10000.
static double travel(const ShiftContext& ctx, long long group, double from, double to) {
    if (from > to) std::swap(from, to);
    if (group < 0 || group >= static_cast<long long>(ctx.groups.size())) return (to - from) * 480;
    const auto& changes = ctx.groups[static_cast<std::size_t>(group)];
    auto it = std::upper_bound(changes.begin(), changes.end(), from,
                               [](double beat, const std::pair<double, double>& c) { return beat < c.first; });
    double speed = it == changes.begin() ? 1.0 : std::prev(it)->second;
    double at = from;
    double sum = 0;
    for (; it != changes.end() && it->first < to; ++it) {
        sum += speed * (it->first - at);
        at = it->first;
        speed = it->second;
    }
    return std::fabs(sum + speed * (to - at)) * 480;
}

// The way a point goes: a slide start or end turns round where going `dir` would take it across a
// half beat and change its slide's hold combo (and the other way would not).
static int hold_dir(ShiftContext& ctx, Node* point, int dir) {
    if (point->kind != Kind::SlideStartPoint && point->kind != Kind::SlideEndPoint) return dir;
    auto it = ctx.owner.find(point);
    if (it == ctx.owner.end()) return dir;
    Num beat = node_beat(point);
    long long tick = beat_tick(beat);
    long long hold = ctx.hold[it->second];
    node_beat(point) = tick_beat(tick + dir);
    bool keeps = line_hold(ctx, it->second) == hold;
    node_beat(point) = tick_beat(tick - dir);
    bool other_keeps = line_hold(ctx, it->second) == hold;
    node_beat(point) = beat;
    return !keeps && other_keeps ? -dir : dir;
}

// Notes put on new ticks, and how far that takes them on screen all told.
struct Plan {
    vector<std::pair<Node*, long long>> moves;
    vector<std::pair<Node*, Node*>> kept;   // points next to each other it leaves on one tick
    double cost = 0;
    double weight = 0;   // the cost with each drawn note's way counted DRAWN_WEIGHT times
};

// A note drawn by itself (a tap, a slide's head or end, a tick) is read against the judge line: one
// put far from its place is seen off the line when it is hit. A point that only bends its line is not.
static const double DRAWN_WEIGHT = 4;

static bool drawn(const Node* n) {
    switch (n->kind) {
        case Kind::Single: return true;
        case Kind::SlideStartPoint: return static_cast<const SlideStartPoint*>(n)->judgeType != "none";
        case Kind::SlideEndPoint: return static_cast<const SlideEndPoint*>(n)->judgeType != "none";
        case Kind::SlideRelayPoint: return static_cast<const SlideRelayPoint*>(n)->critical.has_value();
        default: return false;
    }
}

static vector<std::pair<Node*, Num>> apply(const Plan& plan) {
    vector<std::pair<Node*, Num>> was;
    for (auto& [who, tick] : plan.moves) {
        was.push_back({who, node_beat(who)});
        node_beat(who) = tick_beat(tick);
    }
    return was;
}

static void undo(const vector<std::pair<Node*, Num>>& was) {
    for (auto it = was.rbegin(); it != was.rend(); ++it) node_beat(it->first) = it->second;
}

// The plan's moves made. Points left on one tick go exactly on it, so that sorting a line by beat
// keeps them in the line's order.
static void commit(ShiftContext& ctx, const Plan& plan) {
    apply(plan);
    for (auto& [a, b] : plan.kept) {
        ctx.kept.insert({a, b});
        node_beat(a) = tick_beat(tick_of(a));
        node_beat(b) = tick_beat(tick_of(b));
    }
}

static bool tie_kept(const ShiftContext& ctx, const Node* a, const Node* b, const Plan* plan) {
    if (ctx.kept.count({a, b})) return true;
    if (plan)
        for (auto& pair : plan->kept)
            if (pair.first == a && pair.second == b) return true;
    return false;
}

// Whether `who` on `tick` stays between the points before and after it in its line.
static bool in_line_order(const ShiftContext& ctx, Node* who, long long tick, const Plan* plan = nullptr) {
    auto it = ctx.line.find(who);
    if (it == ctx.line.end()) return true;
    const vector<Node*>& points = *it->second.first;
    std::size_t k = it->second.second;
    if (k > 0) {
        long long before = tick_of(points[k - 1]);
        if (before > tick || (before == tick && !tie_kept(ctx, points[k - 1], who, plan))) return false;
    }
    if (k + 1 < points.size()) {
        long long after = tick_of(points[k + 1]);
        if (after < tick || (after == tick && !tie_kept(ctx, who, points[k + 1], plan))) return false;
    }
    return true;
}

// whether a line's point `k` comes after the one before it
static bool in_order_at(const ShiftContext& ctx, const vector<Node*>& points, std::size_t k) {
    long long before = tick_of(points[k - 1]);
    long long tick = tick_of(points[k]);
    return tick > before || (tick == before && tie_kept(ctx, points[k - 1], points[k], nullptr));
}

static bool line_out_of_order(const ShiftContext& ctx, const vector<Node*>& points) {
    for (std::size_t k = 1; k < points.size(); ++k)
        if (!in_order_at(ctx, points, k)) return true;
    return false;
}

// Whether a plan keeps the order of each line it moves points of and each slide's hold combo, puts
// no note further before the chart's start, and with `strict` lands each of its notes on no other
// note; sets its cost.
static bool plan_ok(ShiftContext& ctx, Plan& plan, bool strict) {
    vector<std::pair<Slide*, long long>> holds;
    for (auto& move : plan.moves) {
        if (move.first->kind != Kind::SlideStartPoint && move.first->kind != Kind::SlideEndPoint) continue;
        auto owner = ctx.owner.find(move.first);
        if (owner != ctx.owner.end()) holds.push_back({owner->second, line_hold(ctx, owner->second)});
    }
    auto was = apply(plan);
    bool ok = true;
    plan.cost = plan.weight = 0;
    for (std::size_t i = 0; i < plan.moves.size(); ++i) {
        Node* who = plan.moves[i].first;
        double way = travel(ctx, pt_tsg(who), was[i].second, node_beat(who));
        plan.cost += way;
        plan.weight += drawn(who) ? way * DRAWN_WEIGHT : way;
        if (plan.moves[i].second < 0 && plan.moves[i].second < beat_tick(was[i].second)) ok = false;
        if (!in_line_order(ctx, who, tick_of(who), &plan)) ok = false;
        if (ok && strict && get_overlap_note(who, ctx.split_tmp_notes) != nullptr) ok = false;
    }
    // a hold combo already changed by a move that broke a line may stay as it is
    for (auto& [slide, before] : holds) {
        long long hold = line_hold(ctx, slide);
        if (hold != ctx.hold[slide] && hold != before) ok = false;
    }
    undo(was);
    return ok;
}

struct Move {
    Node* who;
    int dir;
};

// The moves that would do, in the order found. The first is taken unless it takes its notes far on
// screen (over this many ticks at timescale 1: a tick into a speed spike can throw a note across the
// screen); then the nearest landing on no other note, else the nearest, by weight.
static const double FAR_MOVE = 16;

struct Options {
    vector<Plan> plans;
    vector<char> free;
    vector<Move> moves;

    void add(Plan plan, bool lands_free, Move move = {nullptr, 0}) {
        plans.push_back(std::move(plan));
        free.push_back(lands_free);
        moves.push_back(move);
    }
    bool settled() const { return !plans.empty() && plans.front().cost <= FAR_MOVE; }
    int chosen() const {
        if (settled()) return 0;
        for (char want_free : {1, 0}) {
            int best = -1;
            for (std::size_t i = 0; i < plans.size(); ++i)
                if (free[i] == want_free && (best < 0 || plans[i].weight < plans[static_cast<std::size_t>(best)].weight))
                    best = static_cast<int>(i);
            if (best >= 0) return best;
        }
        return -1;
    }
};

// The points of a line from `from` on (`dir` 1) or back (`dir` -1) that come no further that way than
// `at`, the tick the point before them goes on, each going on past the one before (or onto its tick,
// where the two were left on one).
static void push_on(const ShiftContext& ctx, const vector<Node*>& points, std::size_t from, int dir, long long at,
                    Plan& plan) {
    for (std::size_t k = from; dir > 0 ? k < points.size() : k != static_cast<std::size_t>(-1); k += dir) {
        long long tick = tick_of(points[k]);
        bool kept = dir > 0 ? tie_kept(ctx, points[k - 1], points[k], nullptr) : tie_kept(ctx, points[k], points[k + 1], nullptr);
        if (dir > 0 ? tick > at || (tick == at && kept) : tick < at || (tick == at && kept)) break;
        if (!kept) at += dir;
        plan.moves.push_back({points[k], at});
    }
}

// `who` `ticks` ticks `dir` way; with `push`, the points of its line it reaches go on ahead of it.
static bool plan_move(const ShiftContext& ctx, Node* who, int dir, bool push, Plan& plan, int ticks = 1) {
    plan = Plan{};
    long long tick = tick_of(who) + static_cast<long long>(dir) * ticks;
    plan.moves.push_back({who, tick});
    auto it = ctx.line.find(who);
    if (it == ctx.line.end()) return true;
    const vector<Node*>& points = *it->second.first;
    std::size_t k = it->second.second;
    std::size_t next = k + dir;
    if (next < points.size() && !push) {
        long long t = tick_of(points[next]);
        bool kept = dir > 0 ? tie_kept(ctx, who, points[next], nullptr) : tie_kept(ctx, points[next], who, nullptr);
        if (dir > 0 ? !(t > tick || (t == tick && kept)) : !(t < tick || (t == tick && kept))) return false;
    }
    if (push && next < points.size()) push_on(ctx, points, next, dir, tick, plan);
    return true;
}

// The run of a line's points from `anchor` put on ticks of their own in the line's order, the anchor
// staying: the points after it each go after the one before, those before it each before the next.
static Plan spread(const ShiftContext& ctx, const vector<Node*>& points, std::size_t anchor) {
    Plan plan;
    if (anchor + 1 < points.size()) push_on(ctx, points, anchor + 1, 1, tick_of(points[anchor]), plan);
    if (anchor > 0) push_on(ctx, points, anchor - 1, -1, tick_of(points[anchor]), plan);
    return plan;
}

// A point that is nothing but its place in its line. A note's other parts (a start's or end's kind,
// an ease, an attached tick) are written ahead of the lines, so on one tick the game reads any other
// point's lane before the points ahead of it in its line.
static bool bare_point(const vector<Node*>& points, std::size_t k) {
    Node* p = points[k];
    if (p->kind == Kind::SlideRelayPoint) {
        auto* relay = static_cast<SlideRelayPoint*>(p);
        return relay->type == "tick" && relay->ease == "linear";
    }
    return p->kind == Kind::GuidePoint && k > 0 && k + 1 < points.size() &&
           static_cast<GuidePoint*>(p)->ease == "linear";
}

// A run of a line's points on one tick in blocks the game reads in the line's order: points next to
// each other the chart put on that tick in one timescale group (a layer's points are listed by
// themselves, in order), none of which the game would merge, each after the first a bare point. None
// unless the run is such a tick and some block has more than one point.
static vector<std::pair<std::size_t, std::size_t>> tie_blocks(const ShiftContext& ctx, const vector<Node*>& points,
                                                              std::size_t first, std::size_t last) {
    auto source = [&](Node* n) {
        auto it = ctx.source.find(n);
        return it == ctx.source.end() ? std::numeric_limits<long long>::min() : it->second;
    };
    long long tick = tick_of(points[first]);
    long long from = source(points[first]);
    vector<std::pair<std::size_t, std::size_t>> blocks;
    for (std::size_t k = first; k <= last; ++k) {
        if (tick_of(points[k]) != tick || source(points[k]) != from) return {};
        bool joins = !blocks.empty() && pt_tsg(points[k]) == pt_tsg(points[blocks.back().first]) && bare_point(points, k);
        LeftLane lane = calc_left_lane(node_lane(points[k]), node_size(points[k]));
        for (std::size_t j = joins ? blocks.back().first : k; j < k; ++j)
            if (same_left_lane(lane, calc_left_lane(node_lane(points[j]), node_size(points[j])))) joins = false;
        if (joins) blocks.back().second = k + 1;
        else blocks.push_back({k, k + 1});
    }
    if (blocks.size() == last - first + 1) return {};
    return blocks;
}

// The run block by block on ticks of their own, block `anchor` staying: the blocks after it each a
// tick after the one before, those before it each a tick before the next (points past the run going
// on ahead as in spread). A block's points stay on one tick.
static Plan spread_blocks(const ShiftContext& ctx, const vector<Node*>& points, std::size_t first, std::size_t last,
                          const vector<std::pair<std::size_t, std::size_t>>& blocks, std::size_t anchor) {
    Plan plan;
    long long at = tick_of(points[first]);
    for (std::size_t b = anchor + 1; b < blocks.size(); ++b) {
        ++at;
        for (std::size_t k = blocks[b].first; k < blocks[b].second; ++k) plan.moves.push_back({points[k], at});
    }
    if (last + 1 < points.size()) push_on(ctx, points, last + 1, 1, at, plan);
    at = tick_of(points[first]);
    for (std::size_t b = anchor; b-- > 0;) {
        --at;
        for (std::size_t k = blocks[b].first; k < blocks[b].second; ++k) plan.moves.push_back({points[k], at});
    }
    if (first > 0) push_on(ctx, points, first - 1, -1, at, plan);
    for (auto& [begin, end] : blocks)
        for (std::size_t k = begin + 1; k < end; ++k) plan.kept.push_back({points[k - 1], points[k]});
    return plan;
}

// A line's points that share a tick (or came out of order) go on ticks of their own, in the line's
// order. The game takes a line's points on one tick in the order it reads them, not the line's: a
// chart with layers lists each layer's points by themselves, and a tick's points in another lane
// may come before the ones in this one. The first of a run stays (the rest going later), else the
// last, else one between (Options); landing on no other note first. Where all of those go far, the
// points of one layer may stay on the tick together, each layer's on a tick of its own (tie_blocks).
static void order_line(ShiftContext& ctx, const vector<Node*>& points) {
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (in_order_at(ctx, points, i)) continue;
        std::size_t first = i - 1;
        std::size_t last = i;
        while (last + 1 < points.size() && !in_order_at(ctx, points, last + 1)) ++last;
        vector<std::size_t> anchors{first, last};
        for (std::size_t k = first + 1; k < last; ++k) anchors.push_back(k);
        Options found;
        for (bool strict : {true, false}) {
            for (std::size_t anchor : anchors) {
                Plan plan = spread(ctx, points, anchor);
                if (plan_ok(ctx, plan, strict)) found.add(std::move(plan), strict);
            }
            if (found.settled()) break;
        }
        if (!found.settled()) {
            auto blocks = tie_blocks(ctx, points, first, last);
            for (bool strict : {true, false})
                for (std::size_t anchor = 0; anchor < blocks.size(); ++anchor) {
                    Plan plan = spread_blocks(ctx, points, first, last, blocks, anchor);
                    if (plan_ok(ctx, plan, strict)) found.add(std::move(plan), strict);
                }
        }
        int chosen = found.chosen();
        if (chosen >= 0) commit(ctx, found.plans[static_cast<std::size_t>(chosen)]);
        i = last;
    }
}

// a slide end usually goes earlier, anything else later
static int usual_dir(const Node* n) { return n->kind == Kind::SlideEndPoint ? -1 : 1; }

// how far past the notes in its way a note going far is taken straight to a tick it lands free on
static const int JUMP_TICKS = 32;

// Separates `point` from `other`, notes the game would take for one, by a tick. Tried in turn: the
// rule's move (which of them goes, which way), the same note going the other way, the other note
// going its usual way or the other way; each keeping every line in order and each slide's hold
// combo, landing on no other note, then landing on one, then pushing the points of its line ahead
// of it; and while those go far (Options), each going straight past the notes in its way. Where the
// notes go round in a ring (free_first), landing free comes first, the jumps pushing the points of
// their line ahead of them too. Last, the rule's move as it is. A slide start or end turns round where its way would cross a half beat
// (hold_dir), and `last` keeps `point` from going back the way it came while it is moved off a run
// of notes.
static void separate(ShiftContext& ctx, Node* point, Node* other, Move rule, int& last) {
    Node* rest = rule.who == point ? other : point;
    const Move tries[] = {rule, {rule.who, -rule.dir}, {rest, usual_dir(rest)}, {rest, -usual_dir(rest)}};
    Options found;
    auto steps = [&](bool push, bool strict) {
        for (Move m : tries) {
            m.dir = hold_dir(ctx, m.who, m.dir);
            if (m.who == point && last && m.dir != last) continue;
            Plan plan;
            if (plan_move(ctx, m.who, m.dir, push, plan) && plan_ok(ctx, plan, strict)) found.add(std::move(plan), strict, m);
        }
    };
    auto jumps = [&](bool push) {
        for (Move m : tries) {
            m.dir = hold_dir(ctx, m.who, m.dir);
            if (m.who == point && last && m.dir != last) continue;
            for (int k = 2; k <= JUMP_TICKS; ++k) {
                Plan plan;
                if (!plan_move(ctx, m.who, m.dir, push, plan, k)) break;
                if (plan_ok(ctx, plan, true)) {
                    found.add(std::move(plan), true, m);
                    break;
                }
            }
        }
    };
    if (ctx.free_first) {
        steps(false, true);
        if (!found.settled()) steps(true, true);
        if (!found.settled()) jumps(false);
        if (found.plans.empty()) jumps(true);
        if (found.plans.empty()) steps(false, false);
        if (found.plans.empty()) steps(true, false);
    } else {
        for (bool push : {false, true}) {
            for (bool strict : {true, false}) {
                steps(push, strict);
                if (found.settled()) break;
            }
            if (found.settled()) break;
        }
        if (!found.settled()) jumps(false);
    }
    int picked = found.chosen();
    if (picked >= 0) {
        commit(ctx, found.plans[static_cast<std::size_t>(picked)]);
        if (found.moves[static_cast<std::size_t>(picked)].who == point) last = found.moves[static_cast<std::size_t>(picked)].dir;
        return;
    }
    Move chosen = rule;
    chosen.dir = rule.who == point && last ? last : hold_dir(ctx, rule.who, rule.dir);
    node_beat(chosen.who) = tick_beat(tick_of(chosen.who) + chosen.dir);
    if (chosen.who == point) last = chosen.dir;
}

// a note taken off the notes it would merge with at most this many times a pass (a push can put a
// line's point back on it)
static const int SEPARATE_LIMIT = 256;

// スライドが脱法（終点より後に中継点があるetc...）していないか調べ、修正する
static void order_slide(ShiftContext& ctx, Slide* note, Node* start_point, Node* end_point) {
    for (auto& point_sp : note->connections) {
        Node* point = point_sp.get();

        if (point->kind != Kind::SlideEndPoint) {
            while (beat_tick(node_beat(point)) >= beat_tick(node_beat(end_point))) {
                node_beat(point) -= BEAT_PER_TICK;
                while (get_overlap_note(point, ctx.split_tmp_notes) != nullptr) {
                    node_beat(point) -= BEAT_PER_TICK;
                }
            }
        }

        if (point->kind != Kind::SlideStartPoint) {
            while (beat_tick(node_beat(point)) <= beat_tick(node_beat(start_point))) {
                node_beat(point) += BEAT_PER_TICK;
                while (get_overlap_note(point, ctx.split_tmp_notes) != nullptr) {
                    node_beat(point) += BEAT_PER_TICK;
                }
            }
        }

        if (node_beat(point) > node_beat(end_point)) {
            std::swap(node_beat(point), node_beat(end_point));
        }
    }
}

// The slide's points in order the other ways: the start stays and each later point goes after the
// one before it (the end last), or the end stays and each earlier point goes before the one after it.
static void push_slide(ShiftContext& ctx, Slide* note, Node* start_point, Node* end_point) {
    vector<Node*> later;
    for (auto& point : note->connections)
        if (point.get() != start_point && point.get() != end_point) later.push_back(point.get());
    std::stable_sort(later.begin(), later.end(), [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
    later.push_back(end_point);
    long long prev = beat_tick(node_beat(start_point));
    for (Node* point : later) {
        while (beat_tick(node_beat(point)) <= prev || get_overlap_note(point, ctx.split_tmp_notes) != nullptr) {
            node_beat(point) += BEAT_PER_TICK;
        }
        prev = beat_tick(node_beat(point));
    }
}

static void pull_slide(ShiftContext& ctx, Slide* note, Node* start_point, Node* end_point) {
    vector<Node*> earlier;
    for (auto& point : note->connections)
        if (point.get() != start_point && point.get() != end_point) earlier.push_back(point.get());
    std::stable_sort(earlier.begin(), earlier.end(), [](Node* a, Node* b) { return node_beat(a) > node_beat(b); });
    earlier.push_back(start_point);
    long long next = beat_tick(node_beat(end_point));
    for (Node* point : earlier) {
        while (beat_tick(node_beat(point)) >= next || get_overlap_note(point, ctx.split_tmp_notes) != nullptr) {
            node_beat(point) -= BEAT_PER_TICK;
        }
        next = beat_tick(node_beat(point));
    }
}

// Puts a slide's points in order (start first, end last) the way that keeps the slide's hold combo.
static void check_slide(ShiftContext& ctx, Slide* note) {
    Node* start_point = nullptr;
    Node* end_point = nullptr;
    for (auto& point : note->connections) {
        if (point->kind == Kind::SlideStartPoint) start_point = point.get();
        else if (point->kind == Kind::SlideEndPoint) end_point = point.get();
    }
    if (!start_point || !end_point) return;
    auto line = ctx.line.find(start_point);
    if (line != ctx.line.end() && !line_out_of_order(ctx, *line->second.first)) return;

    vector<Num> beats;
    for (auto& point : note->connections) beats.push_back(node_beat(point.get()));
    auto restore = [&] {
        for (std::size_t i = 0; i < beats.size(); ++i) node_beat(note->connections[i].get()) = beats[i];
    };
    long long hold = ctx.hold[note];
    order_slide(ctx, note, start_point, end_point);
    if (hold_ticks(note) == hold) return;
    restore();
    push_slide(ctx, note, start_point, end_point);
    if (hold_ticks(note) == hold) return;
    restore();
    pull_slide(ctx, note, start_point, end_point);
    if (hold_ticks(note) == hold) return;
    restore();
    order_slide(ctx, note, start_point, end_point);
}

static void shift_slide(ShiftContext& ctx, Slide* note) {
    for (auto& point_sp : note->connections) {
        Node* point = point_sp.get();
        Node* overlap_note;
        int last = 0;
        for (int i = 0; i < SEPARATE_LIMIT && (overlap_note = get_overlap_note(point, ctx.split_tmp_notes)); ++i) {
            Kind pk = point->kind;
            Kind ok = overlap_note->kind;
            Move rule{point, 1};
            if (pk == Kind::SlideStartPoint && ok == Kind::Single) {
                // スライド始点 + single
                auto* s = static_cast<Single*>(overlap_note);
                if (static_cast<SlideStartPoint*>(point)->judgeType != "none" &&
                    (s->trace.has_value() && s->trace.value()))
                    rule = {overlap_note, 1};
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideStartPoint) {
                // スライド始点 + スライド始点
                if (static_cast<SlideStartPoint*>(point)->judgeType != "none" &&
                    static_cast<SlideStartPoint*>(overlap_note)->judgeType == "none")
                    rule = {overlap_note, 1};
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideRelayPoint) {
                // スライド始点 + スライド中継点
                rule = {overlap_note, 1};
            } else if (pk == Kind::SlideStartPoint && ok == Kind::SlideEndPoint) {
                // スライド始点 + スライド終点
                rule = {point, 1};
            } else if (pk == Kind::SlideRelayPoint) {
                // スライド中継点 + ノーツ
                rule = {point, 1};
            } else if (pk == Kind::SlideEndPoint && ok == Kind::Single) {
                // スライド終点 + single
                rule = {point, -1};
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideStartPoint) {
                // スライド終点 + スライド始点
                rule = {overlap_note, 1};
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideRelayPoint) {
                // スライド終点 + スライド中継点
                rule = {overlap_note, 1};
            } else if (pk == Kind::SlideEndPoint && ok == Kind::SlideEndPoint) {
                // スライド終点 + スライド終点
                auto* p = static_cast<SlideEndPoint*>(point);
                auto* o = static_cast<SlideEndPoint*>(overlap_note);
                if (p->judgeType == "none" || o->direction.has_value())
                    rule = {point, -1};
                else if (o->judgeType == "none" || p->direction.has_value())
                    rule = {overlap_note, -1};
                else
                    rule = {point, -1};
            } else if (pk == Kind::SlideEndPoint) {
                rule = {point, -1};
            }
            separate(ctx, point, overlap_note, rule, last);
        }
    }

    check_slide(ctx, note);
}

static void shift_guide(ShiftContext& ctx, Guide* note) {
    for (auto& point : note->midpoints) {
        Node* overlap_note;
        int last = 0;
        for (int i = 0; i < SEPARATE_LIMIT && (overlap_note = get_overlap_note(point.get(), ctx.split_tmp_notes)); ++i)
            separate(ctx, point.get(), overlap_note, {point.get(), 1}, last);
    }
    std::stable_sort(note->midpoints.begin(), note->midpoints.end(),
                     [](const shared_ptr<GuidePoint>& a, const shared_ptr<GuidePoint>& b) {
                         return a->beat < b->beat;
                     });
}

static void shift_single(ShiftContext& ctx, Single* note) {
    Node* overlap_note;
    int last = 0;
    for (int i = 0; i < SEPARATE_LIMIT && (overlap_note = get_overlap_note(note, ctx.split_tmp_notes)); ++i) {
        Kind ok = overlap_note->kind;
        Move rule{note, 1};
        if (ok == Kind::Single && static_cast<Single*>(overlap_note)->trace.has_value() &&
            static_cast<Single*>(overlap_note)->trace.value())
            rule = {overlap_note, 1};
        else if (ok == Kind::SlideStartPoint || ok == Kind::SlideRelayPoint || ok == Kind::GuidePoint)
            rule = {overlap_note, 1};
        else if (ok == Kind::SlideEndPoint)
            rule = {overlap_note, -1};
        separate(ctx, note, overlap_note, rule, last);
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
        if (note->kind == Kind::Single && static_cast<Single*>(note.get())->fake) continue;
        if (note->kind == Kind::Slide) {
            auto* slide = static_cast<Slide*>(note.get());
            if (slide->fake) continue;
            // A fake relay in a real slide keeps the slide's shape but is never judged; a fake
            // attach changes nothing.
            vector<NodePtr> points;
            for (auto& point : slide->connections) {
                if (point->kind == Kind::SlideRelayPoint) {
                    auto* relay = static_cast<SlideRelayPoint*>(point.get());
                    if (relay->fake) {
                        if (relay->type == "attach") continue;
                        relay->fake = false;
                        relay->critical = std::nullopt;
                    }
                }
                points.push_back(point);
            }
            slide->connections = std::move(points);
        }
        out.push_back(note);
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

static string describe_at(const char* what, double beat) {
    return string(what) + " at beat " + util::py_float_repr(beat);
}

void Score::replace_extended_guide_colors() {
    static const std::map<string, string> color_map = {
        {"neutral", "green"}, {"red", "yellow"}, {"green", "green"}, {"blue", "green"},
        {"yellow", "yellow"}, {"purple", "yellow"}, {"cyan", "green"}, {"black", "green"}};
    for (auto& note : notes) {
        if (note->kind != Kind::Guide) continue;
        auto* g = static_cast<Guide*>(note.get());
        auto it = color_map.find(g->color);
        if (it == color_map.end())
            throw std::invalid_argument(
                "Unknown guide color '" + g->color + "' on the " +
                describe_at("guide", g->midpoints.empty() ? 0.0 : static_cast<double>(g->midpoints[0]->beat)));
        g->color = it->second;
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
    auto replaced = [&](string& ease, const char* what, double beat) {
        auto it = ease_map.find(ease);
        if (it == ease_map.end())
            throw std::invalid_argument("Unknown ease '" + ease + "' on the " + describe_at(what, beat));
        ease = it->second;
    };
    for (auto& note : notes) {
        if (note->kind == Kind::Slide) {
            for (auto& c : static_cast<Slide*>(note.get())->connections) {
                if (c->kind == Kind::SlideStartPoint) {
                    auto* p = static_cast<SlideStartPoint*>(c.get());
                    replaced(p->ease, "slide start", p->beat);
                } else if (c->kind == Kind::SlideRelayPoint) {
                    auto* p = static_cast<SlideRelayPoint*>(c.get());
                    replaced(p->ease, "slide relay point", p->beat);
                }
            }
        } else if (note->kind == Kind::Guide) {
            for (auto& m : static_cast<Guide*>(note.get())->midpoints)
                replaced(m->ease, "guide point", m->beat);
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
        auto* g = static_cast<TimeScaleGroup*>(x.get());
        return g->changes.empty() ? 0.0 : static_cast<double>(g->changes[0]->beat);
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
                out.push_back(std::make_shared<Slide>(sl->critical, sl->fake, connectors,
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

static bool is_open_slide(const Node* note) {
    if (note->kind != Kind::Slide) return false;
    auto& points = static_cast<const Slide*>(note)->connections;
    return !points.empty() && std::none_of(points.begin(), points.end(), [](const NodePtr& p) {
        return p->kind == Kind::SlideEndPoint;
    });
}

void Score::close_open_slides() {
    for (auto& note : notes) {
        if (!is_open_slide(note.get())) continue;
        auto* slide = static_cast<Slide*>(note.get());
        Node* last = slide->connections.front().get();
        for (auto& point : slide->connections)
            if (node_beat(point.get()) >= node_beat(last)) last = point.get();
        slide->append(make_end(tick_beat(tick_of(last) + 1), slide->critical, "none",
                               node_lane(last), node_size(last), pt_tsg(last), pt_speed(last)));
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

static vector<Node*> note_points(const vector<NodePtr>& notes) {
    vector<Node*> points;
    for (auto& note : notes) {
        if (note->kind == Kind::Single) {
            points.push_back(note.get());
        } else if (note->kind == Kind::Slide) {
            for (auto& conn : static_cast<Slide*>(note.get())->connections) points.push_back(conn.get());
        } else if (note->kind == Kind::Guide) {
            for (auto& mp : static_cast<Guide*>(note.get())->midpoints) points.push_back(mp.get());
        }
    }
    return points;
}

long long Score::layer_count() const {
    return std::count_if(notes.begin(), notes.end(),
                         [](const NodePtr& n) { return n->kind == Kind::TimeScaleGroup; });
}

bool Score::has_speed_ratios() const {
    auto points = note_points(notes);
    return std::any_of(points.begin(), points.end(), [](Node* p) { return pt_speed(p) != 1.0; });
}

void Score::strip_layers() {
    bool kept = false;
    vector<NodePtr> out;
    for (auto& note : notes) {
        if (note->kind == Kind::TimeScaleGroup) {
            if (kept) continue;
            kept = true;
        }
        out.push_back(note);
    }
    notes = std::move(out);
    for (Node* point : note_points(notes)) set_pt_tsg(point, 0);
}

// A note in group g with speed ratio r moves to a new group: g's changes scaled by r.
void Score::flatten_speed_ratios_to_layers() {
    auto points = note_points(notes);
    std::set<std::pair<long long, double>> keys;
    for (Node* p : points)
        if (pt_speed(p) != 1.0) keys.insert({pt_tsg(p), pt_speed(p)});
    if (keys.empty()) return;

    vector<TimeScaleGroup*> groups;
    for (auto& n : notes)
        if (n->kind == Kind::TimeScaleGroup) groups.push_back(static_cast<TimeScaleGroup*>(n.get()));
    if (groups.empty()) {
        // Notes without a group follow the default one, which has to stay group 0.
        auto g = std::make_shared<TimeScaleGroup>();
        notes.insert(notes.begin(), g);
        groups.push_back(g.get());
    }

    std::map<std::pair<long long, double>, long long> group_of;
    long long next_group = static_cast<long long>(groups.size());
    for (auto& [group, ratio] : keys) {
        auto g = std::make_shared<TimeScaleGroup>();
        TimeScaleGroup* source = group >= 0 && group < static_cast<long long>(groups.size())
                                     ? groups[static_cast<std::size_t>(group)]
                                     : nullptr;
        if (!source || source->changes.empty() || source->changes.front()->beat > 0)
            g->append(std::make_shared<TimeScalePoint>(Num::I(0), Num::F(ratio)));
        if (source)
            for (auto& c : source->changes)
                g->append(std::make_shared<TimeScalePoint>(c->beat, Num::F(c->timeScale * ratio)));
        notes.push_back(g);
        group_of[{group, ratio}] = next_group++;
    }

    for (Node* p : points) {
        if (pt_speed(p) == 1.0) continue;
        set_pt_tsg(p, group_of[{pt_tsg(p), pt_speed(p)}]);
        set_pt_speed(p, 1.0);
    }
}

void limit_to_one_layer(Score& score, const string& format, bool strip, const string& allow,
                        bool speed_ratios_are_layers) {
    bool had_speed_ratios = speed_ratios_are_layers && score.has_speed_ratios();
    if (speed_ratios_are_layers) score.flatten_speed_ratios_to_layers();
    long long layers = score.layer_count();
    if (layers <= 1) return;
    if (strip) {
        score.strip_layers();
        return;
    }
    throw std::invalid_argument(
        format + " holds one layer, but the score has " + std::to_string(layers) +
        (had_speed_ratios ? " (counting its note speed ratios as layers)" : "") + ". " + allow +
        ", or remove them first with the strip_layers" +
        (had_speed_ratios ? " and strip_speed_ratios transforms." : " transform."));
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
    score.format_metadata = format_metadata;
    return {score, static_cast<long long>(overlaps_at.size())};
}

static bool is_shiftable(const NodePtr& note) {
    return !(note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
             is_event(note->kind) || note->kind == Kind::Volume);
}

// every note and slide / guide point, listed by BAR_INTERVAL
static vector<vector<Node*>> split_shift_points(const vector<NodePtr>& notes) {
    vector<Node*> tmp_notes_raw;
    for (auto& note : notes) {
        if (is_shiftable(note)) tmp_notes_raw.push_back(note.get());
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
    if (nlists < 1) nlists = any ? 1 : 0;
    vector<vector<Node*>> split_tmp_notes(static_cast<std::size_t>(nlists));

    for (Node* note : tmp_notes) {
        split_tmp_notes[static_cast<std::size_t>(calc_notelist_index(note))].push_back(note);
    }
    return split_tmp_notes;
}

// the notes the game would merge with another
static std::size_t merged_notes(const vector<NodePtr>& notes) {
    vector<vector<Node*>> split_tmp_notes = split_shift_points(notes);
    std::size_t merged = 0;
    for (auto& list : split_tmp_notes)
        for (Node* note : list)
            if (get_overlap_note(note, split_tmp_notes) != nullptr) ++merged;
    return merged;
}

static bool lines_out_of_order(const ShiftContext& ctx) {
    for (auto& points : ctx.lines)
        if (line_out_of_order(ctx, points)) return true;
    return false;
}

// A pass also moves the other note of a pair it separates, and that can put the other note on a
// third one or a slide point past its slide's end after that slide was checked: passes repeat until
// one leaves nothing to move.
static const int SHIFT_PASSES = 8;

// 重なっているノーツをずらす
void Score::shift() {
    ShiftContext ctx;
    for (auto& note : notes) {
        if (note->kind != Kind::TimeScaleGroup) continue;
        vector<std::pair<double, double>> changes;
        for (auto& change : static_cast<TimeScaleGroup*>(note.get())->changes)
            changes.push_back({change->beat, change->timeScale});
        std::stable_sort(changes.begin(), changes.end(), [](auto& a, auto& b) { return a.first < b.first; });
        ctx.groups.push_back(std::move(changes));
    }
    vector<Slide*> slides;
    for (auto& note : notes) {
        vector<Node*> points;
        if (note->kind == Kind::Slide) {
            auto* slide = static_cast<Slide*>(note.get());
            Node* start_point = nullptr;
            Node* end_point = nullptr;
            for (auto& point : slide->connections) {
                ctx.owner[point.get()] = slide;
                if (point->kind == Kind::SlideStartPoint && !start_point) start_point = point.get();
                else if (point->kind == Kind::SlideEndPoint && !end_point) end_point = point.get();
                else points.push_back(point.get());
            }
            std::stable_sort(points.begin(), points.end(), [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
            if (start_point) points.insert(points.begin(), start_point);
            if (end_point) points.push_back(end_point);
            slides.push_back(slide);
        } else if (note->kind == Kind::Guide) {
            for (auto& point : static_cast<Guide*>(note.get())->midpoints) points.push_back(point.get());
            std::stable_sort(points.begin(), points.end(), [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
        } else {
            continue;
        }
        ctx.lines.push_back(std::move(points));
        for (std::size_t k = 0; k < ctx.lines.back().size(); ++k) {
            Node* point = ctx.lines.back()[k];
            ctx.line[point] = {&ctx.lines.back(), k};
            ctx.source[point] = tick_of(point);
        }
    }
    for (Slide* slide : slides) ctx.hold[slide] = line_hold(ctx, slide);
    std::size_t merged = std::numeric_limits<std::size_t>::max();
    for (int pass = 0; pass < SHIFT_PASSES; ++pass) {
        ctx.split_tmp_notes = split_shift_points(notes);
        for (auto& points : ctx.lines) order_line(ctx, points);
        for (auto& note : notes) {
            if (!is_shiftable(note)) continue;
            if (note->kind == Kind::Single) {
                shift_single(ctx, static_cast<Single*>(note.get()));
            } else if (note->kind == Kind::Slide) {
                shift_slide(ctx, static_cast<Slide*>(note.get()));
            } else if (note->kind == Kind::Guide) {
                shift_guide(ctx, static_cast<Guide*>(note.get()));
            }
        }
        std::size_t now = merged_notes(notes);
        if (!now && !lines_out_of_order(ctx)) break;
        if (now >= merged) ctx.free_first = true;
        merged = now;
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
    if (note->kind == Kind::TimeScaleGroup) {
        auto* g = static_cast<const TimeScaleGroup*>(note);   // an unnamed group may have no changes
        return g->changes.empty() ? 0.0 : static_cast<double>(g->changes[0]->beat);
    }
    return 0.0;
}

double Score::note_max_beat(const Node* note) const {
    if (note->kind == Kind::Slide)
        return node_beat(static_cast<const Slide*>(note)->connections.back().get());
    if (note->kind == Kind::Guide)
        return static_cast<const Guide*>(note)->midpoints.back()->beat;
    if (note->kind == Kind::TimeScaleGroup) {
        auto* g = static_cast<const TimeScaleGroup*>(note);
        return g->changes.empty() ? 0.0 : static_cast<double>(g->changes.back()->beat);
    }
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
            if (s->fake || s->type == "damage") continue;  // never judged into the combo
            out.push_back({s->direction.has_value() ? "flicks" : "taps",
                           s->critical.value_or(false), s->beat});
        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            if (sl->fake) continue;
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
            auto* s = static_cast<Single*>(note);
            if (s->fake || s->type == "damage") continue;
            if (util::py_round_int(s->beat * TICKS_PER_BEAT) < cutoff_tick)
                ++count;
        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            if (sl->fake) continue;
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
