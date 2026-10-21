#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "rg/converters/bdon.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::bdon {

using namespace rg::core;
using rg::util::Json;

namespace {

const long long TICKS_PER_BEAT = 480;

Num tick_to_beat(long long tick) {
    return Num::F(util::py_round(static_cast<double>(tick) / TICKS_PER_BEAT, 6));
}

const double DEFAULT_WIDTH = 6.0;

[[noreturn]] void fail(const std::string& message) { throw BdonFormatError(message); }

const Json* get(const Json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    return obj.find(std::string(key));
}

bool in_choices(const std::string& value, std::initializer_list<const char*> allowed) {
    for (const char* a : allowed)
        if (value == a) return true;
    return false;
}

double read_number(const Json& value) {
    Json::Type t = value.type();
    if (t == Json::Type::Bool || !(t == Json::Type::Int || t == Json::Type::Float))
        fail("expected a number");
    double number =
        t == Json::Type::Int ? static_cast<double>(value.as_int()) : value.as_float();
    if (!std::isfinite(number)) fail("expected a finite number");
    return number;
}

double read_number_or(const Json& obj, const char* key, double dflt) {
    const Json* p = get(obj, key);
    return p ? read_number(*p) : dflt;
}

long long read_tick(const Json& value) {
    double number = read_number(value);
    if (number < 0 || number != std::trunc(number)) fail("expected a non-negative integer");
    return static_cast<long long>(number);
}

long long read_tick_field(const Json& obj, const char* key) {
    const Json* p = get(obj, key);
    return read_tick(p ? *p : Json());
}

bool read_boolean(const Json* p, bool dflt) {
    if (!p || p->type() == Json::Type::Null) return dflt;
    if (p->type() == Json::Type::Bool) return p->as_bool();
    fail("expected a boolean");
}

std::string read_choice(const Json* p, std::initializer_list<const char*> allowed,
                        const char* dflt) {
    if (!p || p->type() == Json::Type::Null) return dflt;
    if (p->type() == Json::Type::Str && in_choices(p->as_str(), allowed)) return p->as_str();
    fail("expected one of a fixed set of values");
}

const std::initializer_list<const char*> NOTE_TYPES = {"tap",  "flick", "trace",
                                                       "long", "guide", "node"};
const std::initializer_list<const char*> EASES = {"linear", "in", "out"};
const std::initializer_list<const char*> DIRECTIONS = {"left", "up", "right"};
const std::initializer_list<const char*> ALPHAS = {"none", "fadeIn", "fadeOut"};

std::pair<std::string, std::string> read_eases(const Json* p) {
    if (p && p->is_array()) {
        if (p->arr().size() != 2) fail("expected a two element array");
        return {read_choice(&p->arr()[0], EASES, "linear"),
                read_choice(&p->arr()[1], EASES, "linear")};
    }
    std::string ease = read_choice(p, EASES, "linear");
    return {ease, ease};
}

std::string read_type(const Json& raw) {
    if (!raw.is_object()) fail("expected an object");
    return read_choice(get(raw, "type"), NOTE_TYPES, "tap");
}

struct Node {
    long long tick;
    std::string kind;  // Literal["tap", "flick", "trace"]
    double left;
    double width;
    bool critical;
    bool visible;
    bool is_auto;
    std::string ease_left;
    std::string ease_right;
    std::string direction;
    std::string alpha;
};

Node read_node(const Json& raw) {
    std::string note_type = read_type(raw);
    const Json* pos = get(raw, "pos");
    bool is_auto = pos && pos->type() == Json::Type::Str && pos->as_str() == "auto";
    double left = is_auto ? 0.0 : read_number(pos ? *pos : Json(static_cast<long long>(0)));
    auto [ease_left, ease_right] = read_eases(get(raw, "ease"));
    Node node;
    node.tick = read_tick_field(raw, "t");
    node.kind = (note_type == "flick" || note_type == "trace") ? note_type : "tap";
    node.left = left;
    node.width = read_number_or(raw, "size", DEFAULT_WIDTH);
    node.critical = read_boolean(get(raw, "crit"), false);
    node.visible = read_boolean(get(raw, "visible"), true);
    node.is_auto = is_auto;
    node.ease_left = ease_left;
    node.ease_right = ease_right;
    node.direction = read_choice(get(raw, "dir"), DIRECTIONS, "up");
    node.alpha = read_choice(get(raw, "alpha"), ALPHAS, "none");
    return node;
}

std::vector<Node> read_line(const Json& raw) {
    const Json* children = get(raw, "node");
    if (!children || !children->is_array()) fail("node: expected an array");
    std::vector<Node> nodes;
    if (raw.is_object() && raw.contains("t")) nodes.push_back(read_node(raw));
    for (const Json& child : children->arr()) nodes.push_back(read_node(child));
    if (nodes.size() < 2) fail("a line needs at least two nodes");
    return nodes;
}

const double BOARD_UNITS = 24.0;
const double SCORE_UNITS = 12.0;
const double SCALE = SCORE_UNITS / BOARD_UNITS;
const long long COLLIDE_TICK_TOLERANCE = 4;

double to_lane(double left, double width) {
    return util::py_round((left + width / 2.0 - BOARD_UNITS / 2.0) * SCALE, 6);
}

double to_size(double width) { return util::py_round(width * SCALE / 2.0, 6); }

double eased(double progress, const std::string& ease) {
    if (ease == "in") return progress * progress;
    if (ease == "out") return progress * (2.0 - progress);
    return progress;
}

std::pair<double, double> interpolate(const Node& before, const Node& after,
                                      long long tick) {
    long long span = after.tick - before.tick;
    double progress =
        span > 0 ? static_cast<double>(tick - before.tick) / static_cast<double>(span) : 0.0;
    double left = before.left + eased(progress, before.ease_left) * (after.left - before.left);
    double before_right = before.left + before.width;
    double after_right = after.left + after.width;
    double right =
        before_right + eased(progress, before.ease_right) * (after_right - before_right);
    return {left, right - left};
}

void resolve_auto(std::vector<Node>& nodes) {
    long long last = static_cast<long long>(nodes.size()) - 1;
    for (long long index = 0; index < static_cast<long long>(nodes.size()); ++index) {
        Node& node = nodes[index];
        if (!node.is_auto) continue;
        if (index == 0 || index == last) {
            node.left = nodes[0].left;
            node.width = nodes[0].width;
            continue;
        }
        long long before = index;
        while (before > 0 && nodes[before].is_auto) --before;
        long long after = index;
        while (after < last && nodes[after].is_auto) ++after;
        auto [left, width] = interpolate(nodes[before], nodes[after], node.tick);
        node.left = left;
        node.width = width;
    }
}

bool ranges_overlap(double a_left, double a_width, double b_left, double b_width) {
    return std::max(a_left, b_left) < std::min(a_left + a_width, b_left + b_width);
}

const double DEFAULT_BPM = 120.0;
const double MIN_BPM = 0.001;
const double MAX_BPM = 100000.0;

const Json::Array& _section(const Json& events, const char* key) {
    static const Json::Array empty;
    const Json* p = get(events, key);
    if (!p) return empty;
    if (!p->is_array()) fail("score.events: expected an array");
    return p->arr();
}

void _bpms(const Json& events, std::vector<NodePtr>& out) {
    std::vector<std::pair<long long, double>> points;
    for (const Json& raw : _section(events, "bpm")) {
        if (!raw.is_object()) fail("expected an object");
        double value = read_number_or(raw, "bpm", DEFAULT_BPM);
        if (!(MIN_BPM <= value && value <= MAX_BPM)) fail("bpm: out of range");
        points.emplace_back(read_tick_field(raw, "t"), value);
    }
    std::stable_sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    if (points.empty() || points[0].first > 0)
        points.insert(points.begin(), {0, DEFAULT_BPM});
    for (auto& [tick, value] : points)
        out.push_back(std::make_shared<Bpm>(tick_to_beat(tick), Num::F(value)));
}

void _skills(const Json& events, std::vector<NodePtr>& out) {
    const Json::Array& skills = _section(events, "skill");
    for (std::size_t index = 0; index < skills.size(); ++index)
        out.push_back(std::make_shared<HolodoriSkill>(
            tick_to_beat(read_tick(skills[index])), static_cast<long long>(index) + 1));
}

void _fevers(const Json& events, std::vector<NodePtr>& out) {
    for (const Json& raw : _section(events, "fever")) {
        if (!raw.is_array() || raw.arr().size() != 2)
            fail("expected a [start, end] array");
        long long start = read_tick(raw.arr()[0]);
        long long end = read_tick(raw.arr()[1]);
        if (end < start) fail("end precedes start");
        out.push_back(std::make_shared<HolodoriFeverStart>(tick_to_beat(start)));
        out.push_back(std::make_shared<HolodoriFeverEnd>(tick_to_beat(end)));
    }
}

std::vector<NodePtr> build_events(const Json* events) {
    Json empty = Json::object();
    const Json* ev = events;
    if (!ev || ev->type() == Json::Type::Null)
        ev = &empty;
    else if (!ev->is_object())
        fail("score.events: expected an object");
    std::vector<NodePtr> out;
    _bpms(*ev, out);
    _skills(*ev, out);
    _fevers(*ev, out);
    return out;
}

using Range = std::tuple<long long, double, double>;

std::string _judge(const Node& node) {
    if (!node.visible) return "none";
    if (node.kind == "trace") return "trace";
    return "normal";
}

std::string _end_judge(const Node& node, bool trace_ends) {
    if (!node.visible) return "none";
    if (trace_ends || node.kind == "trace") return "trace";
    return "normal";
}

bool _collides(const Node& node, const std::vector<Range>& ranges) {
    for (auto& [tick, left, width] : ranges)
        if (std::llabs(node.tick - tick) <= COLLIDE_TICK_TOLERANCE &&
            ranges_overlap(node.left, node.width, left, width))
            return true;
    return false;
}

std::shared_ptr<Single> _single(const Node& node, const std::vector<Range>& ranges = {}) {
    bool trace =
        node.kind == "trace" || (node.kind == "flick" && _collides(node, ranges));
    auto s = std::make_shared<Single>();
    s->beat = tick_to_beat(node.tick);
    s->critical = node.critical;
    s->lane = Num::F(to_lane(node.left, node.width));
    s->size = Num::F(to_size(node.width));
    s->timeScaleGroup = 0;
    s->trace = trace;
    if (node.kind == "flick") s->direction = node.direction;
    return s;
}

std::shared_ptr<Slide> _slide(std::vector<Node>& nodes, bool trace_ends) {
    resolve_auto(nodes);
    bool critical =
        std::any_of(nodes.begin(), nodes.end(), [](const Node& n) { return n.critical; });
    long long last = static_cast<long long>(nodes.size()) - 1;
    auto slide = std::make_shared<Slide>(critical);
    for (long long index = 0; index < static_cast<long long>(nodes.size()); ++index) {
        const Node& node = nodes[index];
        Num beat = tick_to_beat(node.tick);
        Num lane = Num::F(to_lane(node.left, node.width));
        Num size = Num::F(to_size(node.width));
        if (index == 0) {
            auto p = std::make_shared<SlideStartPoint>();
            p->beat = beat;
            p->critical = critical;
            p->ease = node.ease_left;
            p->judgeType = _judge(node);
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            slide->connections.push_back(p);
        } else if (index == last) {
            auto p = std::make_shared<SlideEndPoint>();
            p->beat = beat;
            p->critical = critical;
            p->judgeType = _end_judge(node, trace_ends);
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            if (node.kind == "flick") p->direction = node.direction;
            slide->connections.push_back(p);
        } else {
            auto p = std::make_shared<SlideRelayPoint>();
            p->beat = beat;
            p->ease = node.ease_left;
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            p->type = node.is_auto ? "attach" : "tick";
            if (node.is_auto || node.visible) p->critical = critical;
            slide->connections.push_back(p);
        }
    }
    slide->sort();
    return slide;
}

std::string _fade(const std::vector<Node>& nodes) {
    if (nodes.front().alpha == "fadeIn") return "in";
    if (nodes.back().alpha == "fadeOut") return "out";
    return "none";
}

std::vector<const Node*> _judged_guide_nodes(const std::vector<Node>& nodes) {
    std::vector<const Node*> out;
    long long last = static_cast<long long>(nodes.size()) - 1;
    for (long long index = 0; index < static_cast<long long>(nodes.size()); ++index) {
        const Node& node = nodes[index];
        if (index == 0) continue;
        if (index == last) {
            if (node.visible && node.kind == "trace") out.push_back(&node);
        } else if (node.is_auto || node.visible) {
            out.push_back(&node);
        }
    }
    return out;
}

std::vector<NodePtr> _guide(std::vector<Node>& nodes, bool guide_traces) {
    resolve_auto(nodes);
    bool any_critical =
        std::any_of(nodes.begin(), nodes.end(), [](const Node& n) { return n.critical; });
    auto guide = std::make_shared<Guide>(any_critical ? "yellow" : "green", _fade(nodes));
    for (const Node& node : nodes) {
        auto gp = std::make_shared<GuidePoint>();
        gp->beat = tick_to_beat(node.tick);
        gp->ease = node.ease_left;
        gp->lane = Num::F(to_lane(node.left, node.width));
        gp->size = Num::F(to_size(node.width));
        gp->timeScaleGroup = 0;
        guide->midpoints.push_back(gp);
    }
    guide->sort();
    std::vector<NodePtr> items;
    items.push_back(guide);
    if (guide_traces)
        for (const Node* node : _judged_guide_nodes(nodes)) {
            Node traced = *node;
            traced.kind = "trace";
            items.push_back(_single(traced));
        }
    return items;
}

std::vector<Range> collect_body_ranges(const Json::Array& raw_notes) {
    std::vector<Range> ranges;
    for (const Json& raw : raw_notes) {
        if (read_type(raw) != "long") continue;
        std::vector<Node> nodes = read_line(raw);
        resolve_auto(nodes);
        for (std::size_t i = 1; i < nodes.size(); ++i)
            ranges.emplace_back(nodes[i].tick, nodes[i].left, nodes[i].width);
    }
    return ranges;
}

std::vector<NodePtr> build_note(const Json& raw, bool guide_traces, bool trace_ends,
                                const std::vector<Range>& trace_flick_ranges) {
    std::string note_type = read_type(raw);
    if (note_type == "long") {
        std::vector<Node> nodes = read_line(raw);
        return {_slide(nodes, trace_ends)};
    }
    if (note_type == "guide") {
        std::vector<Node> nodes = read_line(raw);
        return _guide(nodes, guide_traces);
    }
    return {_single(read_node(raw), trace_flick_ranges)};
}

std::shared_ptr<TimeScaleGroup> _time_scale_group() {
    auto group = std::make_shared<TimeScaleGroup>();
    group->append(std::make_shared<TimeScalePoint>(Num::F(0.0), Num::F(1.0)));
    return group;
}

Json decode_document(const std::string& data) {
    std::string raw = data;
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0x1f &&
        static_cast<unsigned char>(raw[1]) == 0x8b) {
        try {
            raw = util::gzip_decompress(raw);
        } catch (...) {
            fail("invalid gzip stream");
        }
    }
    Json document;
    try {
        document = util::json_parse(raw);
    } catch (...) {
        fail("invalid JSON");
    }
    if (!document.is_object()) fail("root: expected an object");
    return document;
}

Score _to_score(const Json& document, const Options& options) {
    const Json* section = get(document, "score");
    if (!section || !section->is_object()) fail("score: expected an object");
    const Json* raw_notes_p = get(*section, "notes");
    static const Json::Array empty;
    const Json::Array* raw_notes = &empty;
    if (raw_notes_p) {
        if (!raw_notes_p->is_array()) fail("score.notes: expected an array");
        raw_notes = &raw_notes_p->arr();
    }
    std::vector<Range> body_ranges =
        options.flick_body_collide ? collect_body_ranges(*raw_notes) : std::vector<Range>{};
    std::vector<NodePtr> notes = build_events(get(*section, "events"));
    notes.push_back(_time_scale_group());
    for (const Json& raw : *raw_notes) {
        std::vector<NodePtr> part = build_note(raw, options.guide_traces,
                                               options.trace_ends, body_ranges);
        notes.insert(notes.end(), part.begin(), part.end());
    }
    auto metadata = std::make_shared<MetaData>();
    metadata->title = "";
    metadata->artist = "";
    metadata->designer = "";
    metadata->waveoffset = Num::I(0);
    metadata->requests = std::vector<std::string>{
        "ticks_per_beat " + std::to_string(TICKS_PER_BEAT)};
    Score score(metadata, notes);
    score.sort_by_beat();
    return score;
}

}  // namespace

Score loads(const std::string& data, const Options& options) {
    return _to_score(decode_document(data), options);
}

}  // namespace rg::converters::bdon
