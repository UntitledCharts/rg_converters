#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rg/converters/leveldata/chart_cyanvas.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

// The reverse of export_bytes (exporter.cpp) and of the converter it mirrors, uscToLevelData in
// sonolus-pjsekai-engine-extended (lib/src/convert.ts). Only what they write is understood: any other
// archetype, data field or value is an unsupported feature.
//
// Slides keep their structure in refs: a connector names its slide's start and end and its own head
// and tail, an attached tick its connector, an end the slide's last connector. Guides do not: each
// segment is an entity of its own naming its head and tail and its guide's start and end, so guides
// are chained back together from their segments (load_guides).

namespace rg::converters::leveldata::chart_cyanvas {

using namespace rg::core;
using rg::util::Json;

namespace {

constexpr double EPSILON = 1e-6;

struct Field {
    const Json* value = nullptr;
    const std::string* ref = nullptr;
};

struct Entity {
    std::string name;
    std::string archetype;
    std::vector<std::pair<std::string, Field>> data;

    const Field* field(const std::string& key) const {
        for (auto& kv : data)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

[[noreturn]] void malformed(const std::string& what) {
    throw std::invalid_argument("Invalid Chart Cyanvas level data: " + what);
}

bool is_number(const Json& j) { return j.type() == Json::Type::Int || j.type() == Json::Type::Float; }

double to_double(const Json& j) {
    return j.type() == Json::Type::Int ? static_cast<double>(j.as_int()) : j.as_float();
}

std::string number_text(const Json& j) { return j.dump(-1, false); }

// ---- the data each archetype may carry --------------------------------------------------------

enum class Rule {
    Number,
    Ref,          // a reference to another entity
    RefAny,       // a reference the converters may leave dangling
    RefOrEnd,     // a reference, or the value -1 (the last of a chain)
    RefAnyOrEnd,  // either, the reference maybe dangling (uscToLevelData's group "next")
};

struct FieldSpec {
    const char* name;
    Rule rule;
    bool required = true;
};

using Specs = std::vector<FieldSpec>;

const Specs NO_FIELDS = {};
const Specs GROUP_FIELDS = {{"first", Rule::RefAny}, {"length", Rule::Number}, {"next", Rule::RefAnyOrEnd, false}};
// the stand-in group's one change names its group (uscToLevelData: "trg:0") in place of a next
const Specs CHANGE_FIELDS = {{"#BEAT", Rule::Number},
                             {"timeScale", Rule::Number},
                             {"next", Rule::RefOrEnd, false},
                             {"timeScaleGroup", Rule::RefAny, false}};
const Specs BPM_FIELDS = {{"#BEAT", Rule::Number}, {"#BPM", Rule::Number}};
const Specs NOTE_FIELDS = {
    {"#BEAT", Rule::Number}, {"lane", Rule::Number}, {"size", Rule::Number}, {"timeScaleGroup", Rule::Ref}};
const Specs FLICK_FIELDS = {{"#BEAT", Rule::Number},
                            {"lane", Rule::Number},
                            {"size", Rule::Number},
                            {"direction", Rule::Number},
                            {"timeScaleGroup", Rule::Ref}};
const Specs END_FIELDS = {{"#BEAT", Rule::Number},
                          {"lane", Rule::Number},
                          {"size", Rule::Number},
                          {"slide", Rule::Ref},
                          {"timeScaleGroup", Rule::Ref}};
const Specs END_FLICK_FIELDS = {{"#BEAT", Rule::Number}, {"lane", Rule::Number},  {"size", Rule::Number},
                                {"direction", Rule::Number}, {"slide", Rule::Ref}, {"timeScaleGroup", Rule::Ref}};
// a hidden slide end is a HiddenSlideTickNote naming its slide
const Specs HIDDEN_TICK_FIELDS = {{"#BEAT", Rule::Number},
                                  {"lane", Rule::Number},
                                  {"size", Rule::Number},
                                  {"slide", Rule::Ref, false},
                                  {"timeScaleGroup", Rule::Ref}};
// the generated combo ticks have no group; an attached tick of the chart's own keeps one
// (uscToLevelData; export_bytes leaves it out)
const Specs IGNORED_TICK_FIELDS = {
    {"#BEAT", Rule::Number}, {"attach", Rule::Ref}, {"timeScaleGroup", Rule::Ref, false}};
const Specs ATTACHED_TICK_FIELDS = {{"#BEAT", Rule::Number}, {"attach", Rule::Ref}, {"timeScaleGroup", Rule::Ref}};
const Specs CONNECTOR_FIELDS = {{"start", Rule::Ref}, {"end", Rule::Ref},     {"head", Rule::Ref},
                                {"tail", Rule::Ref},  {"ease", Rule::Number}, {"startType", Rule::Number}};
const Specs GUIDE_FIELDS = {{"color", Rule::Number},
                            {"fade", Rule::Number},
                            {"ease", Rule::Number},
                            {"startLane", Rule::Number},
                            {"startSize", Rule::Number},
                            {"startBeat", Rule::Number},
                            {"startTimeScaleGroup", Rule::Ref},
                            {"headLane", Rule::Number},
                            {"headSize", Rule::Number},
                            {"headBeat", Rule::Number},
                            {"headTimeScaleGroup", Rule::Ref},
                            {"tailLane", Rule::Number},
                            {"tailSize", Rule::Number},
                            {"tailBeat", Rule::Number},
                            {"tailTimeScaleGroup", Rule::Ref},
                            {"endLane", Rule::Number},
                            {"endSize", Rule::Number},
                            {"endBeat", Rule::Number},
                            {"endTimeScaleGroup", Rule::Ref}};
const Specs SIM_LINE_FIELDS = {{"a", Rule::Ref}, {"b", Rule::Ref}};

enum class Part {
    Other,   // Initialization, InputManager, Stage, SimLine
    Group,
    Change,
    BpmChange,
    Tap,
    Flick,
    NonDirectional,  // a trace flick of no direction
    Damage,
    SlideStart,
    SlideEnd,
    HiddenTick,   // a hidden tick, or a hidden end
    SlideTick,
    IgnoredTick,  // an attached tick of no combo
    AttachedTick,
    Connector,
    GuideSegment,
};

struct Archetype {
    Part part;
    const Specs* specs;
    bool critical = false;
    bool trace = false;
    const char* judge = "normal";  // slide starts and ends
};

const std::map<std::string, Archetype>& archetypes() {
    static const std::map<std::string, Archetype> m = {
        {"Initialization", {Part::Other, &NO_FIELDS}},
        {"InputManager", {Part::Other, &NO_FIELDS}},
        {"Stage", {Part::Other, &NO_FIELDS}},
        {"SimLine", {Part::Other, &SIM_LINE_FIELDS}},
        {"TimeScaleGroup", {Part::Group, &GROUP_FIELDS}},
        {"TimeScaleChange", {Part::Change, &CHANGE_FIELDS}},
        {"#BPM_CHANGE", {Part::BpmChange, &BPM_FIELDS}},
        {"NormalTapNote", {Part::Tap, &NOTE_FIELDS, false, false}},
        {"CriticalTapNote", {Part::Tap, &NOTE_FIELDS, true, false}},
        {"NormalTraceNote", {Part::Tap, &NOTE_FIELDS, false, true}},
        {"CriticalTraceNote", {Part::Tap, &NOTE_FIELDS, true, true}},
        {"NormalFlickNote", {Part::Flick, &FLICK_FIELDS, false, false}},
        {"CriticalFlickNote", {Part::Flick, &FLICK_FIELDS, true, false}},
        {"NormalTraceFlickNote", {Part::Flick, &FLICK_FIELDS, false, true}},
        {"CriticalTraceFlickNote", {Part::Flick, &FLICK_FIELDS, true, true}},
        {"NonDirectionalTraceFlickNote", {Part::NonDirectional, &NOTE_FIELDS, false, true}},
        {"DamageNote", {Part::Damage, &NOTE_FIELDS}},
        {"NormalSlideStartNote", {Part::SlideStart, &NOTE_FIELDS, false, false, "normal"}},
        {"CriticalSlideStartNote", {Part::SlideStart, &NOTE_FIELDS, true, false, "normal"}},
        {"NormalTraceSlideStartNote", {Part::SlideStart, &NOTE_FIELDS, false, true, "trace"}},
        {"CriticalTraceSlideStartNote", {Part::SlideStart, &NOTE_FIELDS, true, true, "trace"}},
        {"HiddenSlideStartNote", {Part::SlideStart, &NOTE_FIELDS, false, false, "none"}},
        {"NormalSlideEndNote", {Part::SlideEnd, &END_FIELDS, false, false, "normal"}},
        {"CriticalSlideEndNote", {Part::SlideEnd, &END_FIELDS, true, false, "normal"}},
        {"NormalTraceSlideEndNote", {Part::SlideEnd, &END_FIELDS, false, true, "trace"}},
        {"CriticalTraceSlideEndNote", {Part::SlideEnd, &END_FIELDS, true, true, "trace"}},
        {"NormalSlideEndFlickNote", {Part::SlideEnd, &END_FLICK_FIELDS, false, false, "normal"}},
        {"CriticalSlideEndFlickNote", {Part::SlideEnd, &END_FLICK_FIELDS, true, false, "normal"}},
        {"HiddenSlideTickNote", {Part::HiddenTick, &HIDDEN_TICK_FIELDS, false, false, "none"}},
        {"NormalSlideTickNote", {Part::SlideTick, &NOTE_FIELDS, false}},
        {"CriticalSlideTickNote", {Part::SlideTick, &NOTE_FIELDS, true}},
        {"IgnoredSlideTickNote", {Part::IgnoredTick, &IGNORED_TICK_FIELDS}},
        {"NormalAttachedSlideTickNote", {Part::AttachedTick, &ATTACHED_TICK_FIELDS, false}},
        {"CriticalAttachedSlideTickNote", {Part::AttachedTick, &ATTACHED_TICK_FIELDS, true}},
        {"NormalSlideConnector", {Part::Connector, &CONNECTOR_FIELDS, false}},
        {"CriticalSlideConnector", {Part::Connector, &CONNECTOR_FIELDS, true}},
        {"Guide", {Part::GuideSegment, &GUIDE_FIELDS}},
    };
    return m;
}

// the exporter's value maps, reversed
std::optional<std::string> ease_name(long long v) {
    switch (v) {
        case -2: return "outin";
        case -1: return "out";
        case 0: return "linear";
        case 1: return "in";
        case 2: return "inout";
        default: return std::nullopt;
    }
}
std::optional<std::string> direction_name(long long v) {
    switch (v) {
        case -1: return "left";
        case 0: return "up";
        case 1: return "right";
        default: return std::nullopt;
    }
}
std::optional<std::string> color_name(long long v) {
    static const char* COLORS[] = {"neutral", "red", "green", "blue", "yellow", "purple", "cyan", "black"};
    if (v >= 0 && v <= 7) return std::string(COLORS[v]);
    return std::nullopt;
}
std::optional<std::string> fade_name(long long v) {
    switch (v) {
        case 0: return "out";
        case 1: return "none";
        case 2: return "in";
        default: return std::nullopt;
    }
}
long long start_type(const std::string& judge) { return judge == "trace" ? 1 : judge == "none" ? 2 : 0; }

// Sonolus' quadratic eases, as the engine draws a connector
double eased(const std::string& ease, double s) {
    if (ease == "in") return s * s;
    if (ease == "out") return 1 - (1 - s) * (1 - s);
    if (ease == "inout") return s < 0.5 ? 2 * s * s : 1 - 2 * (1 - s) * (1 - s);
    if (ease == "outin") return s < 0.5 ? 0.5 * (1 - (1 - 2 * s) * (1 - 2 * s)) : 0.5 + 0.5 * (2 * s - 1) * (2 * s - 1);
    return s;
}

// a guide point, with the text of its numbers and group: the same point in two segments
struct GuidePlace {
    Num beat, lane, size;
    long long group = 0;
    std::string key;
};

struct GuideSegment {
    std::size_t entity;
    std::string color, fade, ease;
    GuidePlace start, head, tail, end;
    std::string guide_key;  // what the segments of one guide share
};

struct GuideChain {
    std::string guide_key, start_key, end_key;
    std::vector<GuidePlace> points;
    std::vector<std::string> eases;
    std::string color, fade;
    std::size_t first_entity;

    bool at_end() const { return points.back().key == end_key; }
};

class Loader {
public:
    explicit Loader(const LoadOptions& opts) : opts_(opts) {}

    core::Score load(const std::string& data);

private:
    LoadOptions opts_;
    std::vector<std::string> issue_order_;
    std::map<std::string, long long> issues_;
    std::vector<Entity> entities_;
    std::vector<const Archetype*> kinds_;
    std::unordered_map<std::string, std::size_t> by_name_;
    std::unordered_map<std::string, long long> group_index_;
    std::vector<std::pair<std::size_t, NodePtr>> notes_;  // each with the first entity it came from

    void unsupported(const std::string& what) {
        if (issues_[what]++ == 0) issue_order_.push_back(what);
    }

    void read_entities(const Json& root);
    void check_fields(const Entity& e, const Specs& specs);

    const Entity* resolve(const std::string& name) const {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &entities_[it->second];
    }
    std::size_t index_of(const Entity& e, const char* key) const {
        const Field* f = e.field(key);
        if (!f || !f->ref) malformed(e.archetype + " \"" + e.name + "\" has no ref " + key);
        auto it = by_name_.find(*f->ref);
        if (it == by_name_.end()) malformed(e.archetype + " \"" + e.name + "\": ref \"" + *f->ref + "\" not found");
        return it->second;
    }
    Part part_of(std::size_t i) const { return kinds_[i] ? kinds_[i]->part : Part::Other; }
    Num num(const Entity& e, const char* key) const {
        const Field* f = e.field(key);
        if (!f || !f->value || !is_number(*f->value))
            malformed(e.archetype + " \"" + e.name + "\" has no number " + key);
        return json_to_num(*f->value);
    }
    long long int_of(const Entity& e, const char* key) const { return std::llround((double)num(e, key)); }
    long long group_ref(const std::string& name) const {
        auto it = group_index_.find(name);
        if (it == group_index_.end()) malformed("timescale group \"" + name + "\" not found");
        return it->second;
    }
    long long group_of(const Entity& e, const char* key) const {
        const Field* f = e.field(key);
        return f && f->ref ? group_ref(*f->ref) : 0;
    }

    std::optional<std::string> direction_of(const Entity& e, const std::string& where);
    std::string ease_of(long long v, const std::string& where);

    void load_groups(std::vector<NodePtr>& groups);
    NodePtr make_single(const Entity& e, const Archetype& a);
    void load_slides();
    GuidePlace place(const Entity& e, const char* prefix);
    void load_guides();
};

void Loader::read_entities(const Json& root) {
    const Json* list = root.find("entities");
    if (!list || !list->is_array()) malformed("no entities");
    entities_.reserve(list->arr().size());
    for (const Json& item : list->arr()) {
        if (!item.is_object()) malformed("an entity is not an object");
        Entity e;
        for (auto& [key, value] : item.items()) {
            if (key == "name") {
                if (value.type() != Json::Type::Str) malformed("an entity name is not a string");
                e.name = value.as_str();
            } else if (key == "archetype") {
                if (value.type() != Json::Type::Str) malformed("an entity archetype is not a string");
                e.archetype = value.as_str();
            } else if (key == "data") {
                if (!value.is_array()) malformed("entity data is not an array");
                for (const Json& d : value.arr()) {
                    const Json* dname = d.find("name");
                    if (!dname || dname->type() != Json::Type::Str) malformed("entity data has no name");
                    Field f;
                    for (auto& [dkey, dvalue] : d.items()) {
                        if (dkey == "name") continue;
                        if (dkey == "value")
                            f.value = &dvalue;
                        else if (dkey == "ref" && dvalue.type() == Json::Type::Str)
                            f.ref = &dvalue.as_str();
                        else
                            malformed("entity data \"" + dname->as_str() + "\" has \"" + dkey + "\"");
                    }
                    e.data.emplace_back(dname->as_str(), f);
                }
            } else {
                unsupported("entity key \"" + key + "\"");
            }
        }
        if (e.archetype.empty()) malformed("an entity has no archetype");
        if (!e.name.empty() && !by_name_.emplace(e.name, entities_.size()).second)
            malformed("duplicate entity name \"" + e.name + "\"");
        entities_.push_back(std::move(e));
    }
}

void Loader::check_fields(const Entity& e, const Specs& specs) {
    for (auto& [key, f] : e.data) {
        const FieldSpec* spec = nullptr;
        for (auto& s : specs)
            if (key == s.name) {
                spec = &s;
                break;
            }
        if (!spec) {
            unsupported(e.archetype + " data \"" + key + "\"");
            continue;
        }
        bool end = f.value && is_number(*f.value) && to_double(*f.value) == -1;
        switch (spec->rule) {
            case Rule::Number:
                if (!f.value || !is_number(*f.value)) malformed(e.archetype + " data \"" + key + "\" is not a number");
                break;
            case Rule::Ref:
            case Rule::RefAny:
            case Rule::RefOrEnd:
            case Rule::RefAnyOrEnd: {
                bool may_end = spec->rule == Rule::RefOrEnd || spec->rule == Rule::RefAnyOrEnd;
                bool checked = spec->rule == Rule::Ref || spec->rule == Rule::RefOrEnd;
                if (may_end && end) break;
                if (!f.ref) malformed(e.archetype + " data \"" + key + "\" is not a ref");
                if (checked && !resolve(*f.ref))
                    malformed(e.archetype + " data \"" + key + "\": ref \"" + *f.ref + "\" not found");
                break;
            }
        }
    }
    for (auto& s : specs)
        if (s.required && !e.field(s.name)) malformed(e.archetype + " \"" + e.name + "\" has no " + s.name);
}

std::optional<std::string> Loader::direction_of(const Entity& e, const std::string& where) {
    long long v = int_of(e, "direction");
    if (auto name = direction_name(v)) return name;
    unsupported(where + " direction " + std::to_string(v));
    return std::string("up");
}

std::string Loader::ease_of(long long v, const std::string& where) {
    if (auto name = ease_name(v)) return *name;
    unsupported(where + " ease " + std::to_string(v));
    return "linear";
}

// Timescale groups in their order (notes name them "tsg:<index>"), each change found by following
// its group's first and next. A chart with none has a stand-in group of no changes whose one change
// names its group in place of a next.
void Loader::load_groups(std::vector<NodePtr>& groups) {
    std::vector<std::size_t> group_entities;
    for (std::size_t i = 0; i < entities_.size(); ++i)
        if (part_of(i) == Part::Group) group_entities.push_back(i);
    std::vector<bool> used(entities_.size(), false);
    auto stand_in = [&](const Entity& g) {
        if (int_of(g, "length") != 0) return false;
        const Entity* change = resolve(*g.field("first")->ref);
        return change && change->archetype == "TimeScaleChange" && change->field("timeScaleGroup") &&
               !change->field("next");
    };
    if (group_entities.size() == 1 && stand_in(entities_[group_entities[0]])) {
        const Entity& g = entities_[group_entities[0]];
        if (!g.name.empty()) group_index_[g.name] = 0;
        used[by_name_.at(*g.field("first")->ref)] = true;
    } else {
        for (std::size_t k = 0; k < group_entities.size(); ++k) {
            const Entity& g = entities_[group_entities[k]];
            if (!g.name.empty()) group_index_[g.name] = static_cast<long long>(k);
            auto group = std::make_shared<TimeScaleGroup>();
            long long length = int_of(g, "length");
            if (length < 0) malformed("TimeScaleGroup \"" + g.name + "\" of length " + std::to_string(length));
            const Entity* change = length > 0 ? resolve(*g.field("first")->ref) : nullptr;
            for (long long n = 0; n < length; ++n) {
                if (!change || change->archetype != "TimeScaleChange")
                    malformed("TimeScaleGroup \"" + g.name + "\" has fewer changes than its length");
                std::size_t index = by_name_.at(change->name);
                if (used[index]) malformed("TimeScaleChange \"" + change->name + "\" in two groups");
                used[index] = true;
                if (change->field("timeScaleGroup")) unsupported("TimeScaleChange naming its group in a chain");
                double scale = (double)num(*change, "timeScale");
                // the converters write a scale of 0 as 0.000001
                Num time_scale = scale == 0.000001 ? Num::F(0.0) : num(*change, "timeScale");
                group->append(std::make_shared<TimeScalePoint>(num(*change, "#BEAT"), time_scale));
                const Field* next = change->field("next");
                bool last = !next || !next->ref;
                if (last != (n + 1 == length))
                    malformed("TimeScaleGroup \"" + g.name + "\" has a chain of changes not its length");
                change = last ? nullptr : resolve(*next->ref);
            }
            std::stable_sort(group->changes.begin(), group->changes.end(),
                             [](auto& a, auto& b) { return (double)a->beat < (double)b->beat; });
            groups.push_back(group);
        }
    }
    for (std::size_t i = 0; i < entities_.size(); ++i)
        if (part_of(i) == Part::Change && !used[i]) unsupported("TimeScaleChange outside a group");
}

NodePtr Loader::make_single(const Entity& e, const Archetype& a) {
    auto s = std::make_shared<Single>();
    s->beat = num(e, "#BEAT");
    s->lane = num(e, "lane");
    s->size = num(e, "size");
    s->timeScaleGroup = group_of(e, "timeScaleGroup");
    if (a.part == Part::Damage) {
        s->type = "damage";
        return s;
    }
    s->critical = a.critical;
    s->trace = a.trace;
    if (a.part == Part::Flick) s->direction = direction_of(e, "flick");
    if (a.part == Part::NonDirectional) s->direction = std::string("none");
    return s;
}

// A slide: the connectors naming one start, chained head to tail from it to their end, and the
// attached ticks naming them. The combo ticks the converters add on every half beat inside the slide
// (IgnoredSlideTickNote, no group) are dropped, one per half beat; any other is the chart's own.
void Loader::load_slides() {
    std::map<std::size_t, std::vector<std::size_t>> by_start;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (part_of(i) != Part::Connector) continue;
        std::size_t start = index_of(entities_[i], "start");
        if (part_of(start) != Part::SlideStart)
            malformed("SlideConnector \"" + entities_[i].name + "\" starting at " + entities_[start].archetype);
        by_start[start].push_back(i);
    }
    std::vector<bool> used(entities_.size(), false);
    std::unordered_map<std::size_t, std::size_t> slide_of;  // connector -> its slide's start
    struct Built {
        std::vector<std::size_t> joints;
        std::vector<std::string> eases;
        std::vector<std::size_t> connectors;  // the one leaving each joint but the end
        std::vector<std::size_t> attaches;
        bool critical;
    };
    std::map<std::size_t, Built> built;

    for (auto& [start, conns] : by_start) {
        const Entity& first = entities_[conns.front()];
        Built b;
        b.critical = kinds_[conns.front()]->critical;
        std::size_t end = index_of(first, "end");
        Part end_part = part_of(end);
        if (end_part != Part::SlideEnd && end_part != Part::HiddenTick)
            malformed("SlideConnector \"" + first.name + "\" ending at " + entities_[end].archetype);
        long long type = int_of(first, "startType");
        std::unordered_map<std::size_t, std::size_t> by_head;
        for (std::size_t c : conns) {
            const Entity& ce = entities_[c];
            if (kinds_[c]->critical != b.critical) unsupported("slide with normal and critical connectors");
            if (index_of(ce, "end") != end) malformed("connectors of one slide naming different ends");
            if (int_of(ce, "startType") != type) unsupported("slide connectors of different startType");
            if (!by_head.emplace(index_of(ce, "head"), c).second)
                malformed("two connectors leaving one point of a slide");
        }
        if (type != start_type(kinds_[start]->judge))
            unsupported("slide startType " + std::to_string(type) + " on " + entities_[start].archetype);
        for (std::size_t cur = start;;) {
            b.joints.push_back(cur);
            if (cur == end) break;
            auto it = by_head.find(cur);
            if (it == by_head.end()) malformed("a slide's connectors not reaching its end");
            if (b.connectors.size() >= conns.size()) malformed("a slide looping back on itself");
            b.connectors.push_back(it->second);
            b.eases.push_back(ease_of(int_of(entities_[it->second], "ease"), "slide connector"));
            cur = index_of(entities_[it->second], "tail");
        }
        if (b.connectors.size() != conns.size()) malformed("a slide connector off its slide's path");
        for (std::size_t k = 1; k + 1 < b.joints.size(); ++k) {
            const Entity& j = entities_[b.joints[k]];
            Part p = part_of(b.joints[k]);
            if ((p != Part::SlideTick && p != Part::HiddenTick) || j.field("slide"))
                malformed(j.archetype + " \"" + j.name + "\" inside a slide");
        }
        const Entity& end_entity = entities_[end];
        if (!end_entity.field("slide")) malformed(end_entity.archetype + " \"" + end_entity.name + "\" ending a slide names none");
        if (index_of(end_entity, "slide") != b.connectors.back()) unsupported("slide end naming another connector");
        for (std::size_t j : b.joints) {
            if (used[j]) malformed(entities_[j].archetype + " \"" + entities_[j].name + "\" in two slides");
            used[j] = true;
        }
        for (std::size_t c : conns) {
            used[c] = true;
            slide_of[c] = start;
        }
        built.emplace(start, std::move(b));
    }
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Part p = part_of(i);
        if (p != Part::IgnoredTick && p != Part::AttachedTick) continue;
        std::size_t c = index_of(entities_[i], "attach");
        if (part_of(c) != Part::Connector)
            malformed(entities_[i].archetype + " \"" + entities_[i].name + "\" attached to " + entities_[c].archetype);
        built.at(slide_of.at(c)).attaches.push_back(i);
        used[i] = true;
    }
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Part p = part_of(i);
        if (!used[i] && (p == Part::SlideStart || p == Part::SlideEnd || p == Part::HiddenTick || p == Part::SlideTick))
            unsupported(entities_[i].archetype + " outside a slide");
    }

    auto beat_of = [&](std::size_t i) { return (double)num(entities_[i], "#BEAT"); };
    for (auto& [start, b] : built) {
        // the half beats the converters fill with combo ticks: strictly inside the slide's points
        std::vector<std::size_t> own, generated_candidates;
        for (std::size_t a : b.attaches) {
            bool candidate = part_of(a) == Part::IgnoredTick && !entities_[a].field("timeScaleGroup");
            (candidate ? generated_candidates : own).push_back(a);
        }
        std::stable_sort(generated_candidates.begin(), generated_candidates.end(),
                         [&](std::size_t x, std::size_t y) { return beat_of(x) < beat_of(y); });
        auto span = [&](const std::vector<std::size_t>& extra) {
            double lo = beat_of(b.joints.front()), hi = lo;
            for (std::size_t j : b.joints) lo = std::min(lo, beat_of(j)), hi = std::max(hi, beat_of(j));
            for (std::size_t a : extra) lo = std::min(lo, beat_of(a)), hi = std::max(hi, beat_of(a));
            return std::make_pair(lo, hi);
        };
        std::vector<std::size_t> kept;
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<std::size_t> extra = own;
            extra.insert(extra.end(), kept.begin(), kept.end());
            auto [lo, hi] = span(extra);
            std::vector<bool> taken(generated_candidates.size(), false);
            double first = std::floor((lo + EPSILON) / 0.5 + 1) * 0.5;
            std::size_t k = 0;
            for (double g = first; g + EPSILON < hi && k < generated_candidates.size(); g += 0.5) {
                while (k < generated_candidates.size() && beat_of(generated_candidates[k]) < g - EPSILON) ++k;
                if (k < generated_candidates.size() && std::abs(beat_of(generated_candidates[k]) - g) <= EPSILON)
                    taken[k++] = true;
            }
            std::vector<std::size_t> rest;
            for (std::size_t k = 0; k < generated_candidates.size(); ++k)
                if (!taken[k]) rest.push_back(generated_candidates[k]);
            auto [lo2, hi2] = span([&] {
                std::vector<std::size_t> all = own;
                all.insert(all.end(), rest.begin(), rest.end());
                return all;
            }());
            kept = rest;
            if (lo2 == lo && hi2 == hi) break;
        }
        std::vector<std::size_t> attaches = own;
        attaches.insert(attaches.end(), kept.begin(), kept.end());

        auto slide = std::make_shared<Slide>(b.critical);
        std::size_t first_entity = start;
        const Entity& se = entities_[start];
        const Archetype& sa = *kinds_[start];
        auto sp = std::make_shared<SlideStartPoint>();
        sp->beat = num(se, "#BEAT");
        sp->lane = num(se, "lane");
        sp->size = num(se, "size");
        sp->timeScaleGroup = group_of(se, "timeScaleGroup");
        sp->ease = b.eases.front();
        sp->judgeType = sa.judge;
        sp->critical = std::string(sa.judge) == "none" ? b.critical : sa.critical;
        slide->append(sp);

        struct Relay {
            double beat;
            NodePtr node;
        };
        std::vector<Relay> relays;
        for (std::size_t k = 1; k + 1 < b.joints.size(); ++k) {
            const Entity& te = entities_[b.joints[k]];
            auto rp = std::make_shared<SlideRelayPoint>();
            rp->beat = num(te, "#BEAT");
            rp->lane = num(te, "lane");
            rp->size = num(te, "size");
            rp->timeScaleGroup = group_of(te, "timeScaleGroup");
            rp->ease = b.eases[k];
            rp->type = "tick";
            if (part_of(b.joints[k]) == Part::SlideTick) rp->critical = kinds_[b.joints[k]]->critical;
            relays.push_back({(double)rp->beat, rp});
            first_entity = std::min(first_entity, b.joints[k]);
        }
        for (std::size_t a : attaches) {
            const Entity& ae = entities_[a];
            // an attached tick sits on its connector's curve: its lane and size are the curve's there
            std::size_t c = index_of(ae, "attach");
            std::size_t k = static_cast<std::size_t>(
                std::find(b.connectors.begin(), b.connectors.end(), c) - b.connectors.begin());
            const Entity& head = entities_[b.joints[k]];
            const Entity& tail = entities_[b.joints[k + 1]];
            double hb = (double)num(head, "#BEAT"), tb = (double)num(tail, "#BEAT");
            double beat = (double)num(ae, "#BEAT");
            double s = tb > hb ? std::clamp((beat - hb) / (tb - hb), 0.0, 1.0) : 0.0;
            double e = eased(b.eases[k], s);
            auto rp = std::make_shared<SlideRelayPoint>();
            rp->beat = num(ae, "#BEAT");
            rp->lane = Num::F((double)num(head, "lane") + ((double)num(tail, "lane") - (double)num(head, "lane")) * e);
            rp->size = Num::F((double)num(head, "size") + ((double)num(tail, "size") - (double)num(head, "size")) * e);
            rp->timeScaleGroup = group_of(ae, "timeScaleGroup");
            rp->ease = "linear";
            rp->type = "attach";
            if (part_of(a) == Part::AttachedTick) rp->critical = kinds_[a]->critical;
            relays.push_back({beat, rp});
            first_entity = std::min(first_entity, a);
        }
        std::stable_sort(relays.begin(), relays.end(), [](const Relay& x, const Relay& y) { return x.beat < y.beat; });
        for (auto& r : relays) slide->append(r.node);

        std::size_t end = b.joints.back();
        const Entity& ee = entities_[end];
        const Archetype& ea = *kinds_[end];
        auto ep = std::make_shared<SlideEndPoint>();
        ep->beat = num(ee, "#BEAT");
        ep->lane = num(ee, "lane");
        ep->size = num(ee, "size");
        ep->timeScaleGroup = group_of(ee, "timeScaleGroup");
        ep->judgeType = ea.judge;
        ep->critical = std::string(ea.judge) == "none" ? b.critical : ea.critical;
        if (ee.field("direction")) ep->direction = direction_of(ee, "slide end flick");
        slide->append(ep);
        first_entity = std::min(first_entity, end);
        notes_.push_back({first_entity, slide});
    }
}

GuidePlace Loader::place(const Entity& e, const char* prefix) {
    std::string p = prefix;
    GuidePlace g;
    g.beat = num(e, (p + "Beat").c_str());
    g.lane = num(e, (p + "Lane").c_str());
    g.size = num(e, (p + "Size").c_str());
    const std::string& group = *e.field(p + "TimeScaleGroup")->ref;
    g.group = group_ref(group);
    g.key = number_text(*e.field(p + "Beat")->value) + "|" + number_text(*e.field(p + "Lane")->value) + "|" +
            number_text(*e.field(p + "Size")->value) + "|" + group;
    return g;
}

// Guides from their segments. The segments of one guide share its color, fade, start and end, and
// each starts where the one before it ended; the converters write a guide's segments one after
// another, in its order. So a segment continues the guide the segment before it went to when it starts
// where that guide is (unless that guide is complete and the segment could as well start a guide of
// its own), else another unfinished guide of its kind that is where it starts (the latest), else it
// starts a guide, at that guide's start. Guides of one kind that overlap (stacked copies, or shapes
// sharing a start, an end and points between) keep their own segments that way, where looking at the
// places alone could not tell which segment goes on which guide.
void Loader::load_guides() {
    std::vector<GuideChain> chains;
    std::unordered_map<std::string, std::vector<std::size_t>> open;  // guide key -> chains not at their end
    long long last = -1;
    auto set_open = [&](std::size_t c) {
        auto& list = open[chains[c].guide_key];
        auto it = std::find(list.begin(), list.end(), c);
        if (chains[c].at_end()) {
            if (it != list.end()) list.erase(it);
        } else if (it == list.end()) {
            list.push_back(c);
        }
    };
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (part_of(i) != Part::GuideSegment) continue;
        const Entity& e = entities_[i];
        GuideSegment seg;
        seg.entity = i;
        long long color = int_of(e, "color"), fade = int_of(e, "fade");
        if (auto name = color_name(color))
            seg.color = *name;
        else {
            unsupported("guide color " + std::to_string(color));
            seg.color = "neutral";
        }
        if (auto name = fade_name(fade))
            seg.fade = *name;
        else {
            unsupported("guide fade " + std::to_string(fade));
            seg.fade = "none";
        }
        seg.ease = ease_of(int_of(e, "ease"), "guide");
        seg.start = place(e, "start");
        seg.head = place(e, "head");
        seg.tail = place(e, "tail");
        seg.end = place(e, "end");
        seg.guide_key = std::to_string(color) + "|" + std::to_string(fade) + "|" + seg.start.key + "|" + seg.end.key;

        long long target = -1;
        if (last >= 0) {
            const GuideChain& c = chains[static_cast<std::size_t>(last)];
            if (c.guide_key == seg.guide_key && c.points.back().key == seg.head.key &&
                !(c.at_end() && seg.head.key == seg.start.key))
                target = last;
        }
        if (target < 0) {
            auto& list = open[seg.guide_key];
            for (auto it = list.rbegin(); it != list.rend(); ++it)
                if (chains[*it].points.back().key == seg.head.key) {
                    target = static_cast<long long>(*it);
                    break;
                }
        }
        if (target < 0) {
            if (seg.head.key != seg.start.key) unsupported("guide segment joining none of its guide");
            GuideChain c;
            c.guide_key = seg.guide_key;
            c.start_key = seg.start.key;
            c.end_key = seg.end.key;
            c.points.push_back(seg.head);
            c.color = seg.color;
            c.fade = seg.fade;
            c.first_entity = i;
            chains.push_back(std::move(c));
            target = static_cast<long long>(chains.size() - 1);
        }
        GuideChain& c = chains[static_cast<std::size_t>(target)];
        c.eases.push_back(seg.ease);
        c.points.push_back(seg.tail);
        set_open(static_cast<std::size_t>(target));
        last = target;
    }
    for (GuideChain& c : chains) {
        if (!c.at_end()) unsupported("guide not reaching its end");
        auto guide = std::make_shared<Guide>(c.color, c.fade);
        for (std::size_t k = 0; k < c.points.size(); ++k) {
            auto p = std::make_shared<GuidePoint>();
            p->beat = c.points[k].beat;
            p->lane = c.points[k].lane;
            p->size = c.points[k].size;
            p->timeScaleGroup = c.points[k].group;
            p->ease = k < c.eases.size() ? c.eases[k] : "linear";
            guide->append(p);
        }
        notes_.push_back({c.first_entity, guide});
    }
}

core::Score Loader::load(const std::string& data) {
    std::string text = data;
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f && static_cast<unsigned char>(text[1]) == 0x8b)
        text = util::gzip_decompress(text);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    Json root = util::json_parse(text);
    if (!root.is_object()) malformed("not a json object");
    for (auto& [key, value] : root.items())
        if (key != "bgmOffset" && key != "entities") unsupported("level data key \"" + key + "\"");
    read_entities(root);
    kinds_.resize(entities_.size(), nullptr);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        auto it = archetypes().find(entities_[i].archetype);
        if (it == archetypes().end()) {
            unsupported("archetype \"" + entities_[i].archetype + "\"");
            continue;
        }
        kinds_[i] = &it->second;
        check_fields(entities_[i], *it->second.specs);
    }

    auto metadata = std::make_shared<MetaData>();
    metadata->title = "";
    metadata->artist = "";
    metadata->designer = "";
    const Json* offset = root.find("bgmOffset");
    metadata->waveoffset = offset && is_number(*offset) ? json_to_num(*offset) : Num::I(0);
    metadata->requests = std::vector<std::string>{"ticks_per_beat 480"};

    std::vector<NodePtr> groups;
    load_groups(groups);
    bool any_bpm = false;
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        Part p = part_of(i);
        if (p == Part::BpmChange) {
            any_bpm = true;
            notes_.push_back({i, std::make_shared<Bpm>(num(entities_[i], "#BEAT"), num(entities_[i], "#BPM"))});
        } else if (p == Part::Tap || p == Part::Flick || p == Part::NonDirectional || p == Part::Damage) {
            notes_.push_back({i, make_single(entities_[i], *kinds_[i])});
        }
    }
    load_slides();
    load_guides();

    if (!issue_order_.empty() && opts_.error_on_unsupported_feature) {
        std::string message =
            "Unsupported Chart Cyanvas level data (pass error_on_unsupported_feature=False to skip these):";
        for (const std::string& what : issue_order_) {
            message += "\n  " + what;
            long long count = issues_[what];
            if (count > 1) message += " (x" + std::to_string(count) + ")";
        }
        throw UnsupportedFeatureError(message);
    }

    // the notes in the order the level data has them, which is the exporter's (sorted by beat)
    std::stable_sort(notes_.begin(), notes_.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<NodePtr> notes;
    if (!any_bpm) notes.push_back(std::make_shared<Bpm>(Num::I(0), Num::F(160.0)));
    notes.insert(notes.end(), groups.begin(), groups.end());
    for (auto& [index, node] : notes_) notes.push_back(node);
    return core::Score(metadata, notes);
}

}  // namespace

core::Score load_text(const std::string& data, const LoadOptions& opts) { return Loader(opts).load(data); }

}  // namespace rg::converters::leveldata::chart_cyanvas
