#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

// The reverse of export_bytes (exporter.cpp). Only what the exporter writes is understood: every
// archetype, data field, value and ref outside that is an unsupported feature, so whatever the
// engines add later is reported rather than silently lost. The exceptions are fields that change
// nothing: [Editor]-only data and engine fields left at their documented defaults
// (sonolus-next-rush-engine docs/level_data.md, level_data_extended.md).

namespace rg::converters::leveldata::next_sekai {

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
    throw std::invalid_argument("Invalid Next SEKAI level data: " + what);
}

bool is_number(const Json& j) { return j.type() == Json::Type::Int || j.type() == Json::Type::Float; }

double to_double(const Json& j) {
    return j.type() == Json::Type::Int ? static_cast<double>(j.as_int()) : j.as_float();
}

std::string number_text(const Json& j) { return j.dump(-1, false); }

// ---- the data each archetype may carry --------------------------------------------------------

enum class Rule {
    Number,   // read below, or editor-only
    Ref,      // a reference to another entity
    Text,     // [Editor] literal text stored in "ref"
    Default,  // an engine feature: understood only at its default value
    AtMost,   // a version: understood up to the value given
};

struct FieldSpec {
    const char* name;
    Rule rule;
    double dflt = 0;
};

using Specs = std::vector<FieldSpec>;

// revision 1 changes the score mode, 2 how stage elevation scales with tilt; neither touches the notes
const Specs INITIALIZATION_FIELDS = {{"initialLife", Rule::Default, 1000}, {"revision", Rule::AtMost, 2}};
const Specs BPM_FIELDS = {{"#BEAT", Rule::Number}, {"#BPM", Rule::Number}, {"meter", Rule::Number}};
const Specs GROUP_FIELDS = {{"first", Rule::Ref},
                            {"forceNoteSpeed", Rule::Default, 0},
                            {"editorName", Rule::Text},
                            {"editorFolder", Rule::Ref}};
const Specs GROUP_FOLDER_FIELDS = {{"editorName", Rule::Text}, {"editorIndex", Rule::Number}};
const Specs TIMESCALE_FIELDS = {{"#BEAT", Rule::Number},
                                {"#TIMESCALE", Rule::Number},
                                {"#TIMESCALE_SKIP", Rule::Default, 0},
                                {"#TIMESCALE_GROUP", Rule::Ref},
                                {"#TIMESCALE_EASE", Rule::Default, 0},
                                {"transitionStyle", Rule::Default, 0},
                                {"next", Rule::Ref},
                                {"hideNotes", Rule::Default, 0},
                                {"editorLane", Rule::Number}};
const Specs SKILL_FIELDS = {{"#BEAT", Rule::Number},       {"effect", Rule::Default, 0},
                            {"level", Rule::Default, 1},    {"value", Rule::Default, 250},
                            {"scale", Rule::Default, 1},    {"duration", Rule::Default, 6}};
const Specs FEVER_CHANCE_FIELDS = {{"#BEAT", Rule::Number}, {"force", Rule::Default, 0}};
const Specs FEVER_START_FIELDS = {{"#BEAT", Rule::Number}};
const Specs NOTE_FIELDS = {{"#BEAT", Rule::Number},
                           {"#TIMESCALE_GROUP", Rule::Ref},
                           {"lane", Rule::Number},
                           {"size", Rule::Number},
                           {"direction", Rule::Number},
                           {"isAttached", Rule::Number},
                           {"connectorEase", Rule::Number},
                           {"isSeparator", Rule::Number},
                           {"segmentKind", Rule::Number},
                           {"segmentAlpha", Rule::Number},
                           {"segmentLayer", Rule::Number},
                           {"next", Rule::Ref},
                           {"activeHead", Rule::Ref},
                           {"attachHead", Rule::Ref},
                           {"attachTail", Rule::Ref},
                           {"elevation", Rule::Default, 0},
                           {"style", Rule::Default, 0},
                           {"effectKind", Rule::Default, 0},
                           {"segmentThroughJudgeLine", Rule::Default, 0},
                           {"segmentPresentation", Rule::Default, 0}};
const Specs CONNECTOR_FIELDS = {{"head", Rule::Ref},       {"tail", Rule::Ref},
                                {"segmentHead", Rule::Ref}, {"segmentTail", Rule::Ref},
                                {"activeHead", Rule::Ref},  {"activeTail", Rule::Ref},
                                {"legacyHiddenPop", Rule::Default, 0}};
const Specs SIM_LINE_FIELDS = {{"left", Rule::Ref}, {"right", Rule::Ref}};

// ---- note archetype names ([Fake](Normal|Critical)[Head|Tail](Tap|Flick|Trace|TraceFlick|Release)Note,
// [Fake](Normal|Critical)TickNote, [Fake]AnchorNote, [Fake]DamageNote, TransientHidden[Damage]TickNote)

enum class NoteBase { Judged, Tick, Anchor, Damage, HiddenTick, HiddenDamageTick };
enum class NotePos { Plain, Head, Tail };
enum class NoteAct { None, Tap, Flick, Trace, TraceFlick, Release };

struct NoteName {
    NoteBase base = NoteBase::Judged;
    bool fake = false;
    bool critical = false;
    NotePos pos = NotePos::Plain;
    NoteAct act = NoteAct::None;

    bool traced() const { return act == NoteAct::Trace || act == NoteAct::TraceFlick; }
    bool flicked() const { return act == NoteAct::Flick || act == NoteAct::TraceFlick; }
};

std::optional<NoteName> parse_note_name(std::string s) {
    NoteName n;
    auto eat = [&s](const char* prefix) {
        std::size_t len = std::strlen(prefix);
        if (s.compare(0, len, prefix) != 0) return false;
        s.erase(0, len);
        return true;
    };
    if (eat("Fake")) n.fake = true;
    if (s == "AnchorNote") { n.base = NoteBase::Anchor; return n; }
    if (s == "DamageNote") { n.base = NoteBase::Damage; return n; }
    if (s == "TransientHiddenTickNote") { n.base = NoteBase::HiddenTick; return n; }
    if (s == "TransientHiddenDamageTickNote") { n.base = NoteBase::HiddenDamageTick; return n; }
    if (eat("Critical"))
        n.critical = true;
    else if (!eat("Normal"))
        return std::nullopt;
    if (s == "TickNote") { n.base = NoteBase::Tick; return n; }
    if (eat("Head"))
        n.pos = NotePos::Head;
    else if (eat("Tail"))
        n.pos = NotePos::Tail;
    static const std::pair<const char*, NoteAct> ACTS[] = {{"TapNote", NoteAct::Tap},
                                                           {"FlickNote", NoteAct::Flick},
                                                           {"TraceNote", NoteAct::Trace},
                                                           {"TraceFlickNote", NoteAct::TraceFlick},
                                                           {"ReleaseNote", NoteAct::Release}};
    for (auto& [text, act] : ACTS)
        if (s == text) {
            n.act = act;
            return n;
        }
    return std::nullopt;
}

// the exporter's value maps, reversed
std::optional<std::string> ease_name(long long v) {
    switch (v) {
        case 1: return "linear";
        case 2: return "in";
        case 3: return "out";
        case 4: return "inout";
        case 5: return "outin";
        default: return std::nullopt;
    }
}
std::optional<std::string> direction_name(long long v) {
    switch (v) {
        case 0: return "up";
        case 1: return "left";
        case 2: return "right";
        default: return std::nullopt;
    }
}
std::optional<std::string> guide_color_name(long long v) {
    static const char* COLORS[] = {"neutral", "red", "green", "blue", "yellow", "purple", "cyan", "black"};
    if (v >= 101 && v <= 108) return std::string(COLORS[v - 101]);
    return std::nullopt;
}

struct Keyframe {
    double beat, alpha;
};

// How a guide built from points looks: its fade, and (when its alphas are no fade the exporter
// writes) the keyframes, either each separator's own or the whole line's evaluated at every point.
struct GuideLook {
    std::string fade;
    const std::vector<Keyframe>* keys = nullptr;
    bool at_separators = false;
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
    std::unordered_map<std::string, std::size_t> by_name_;
    std::unordered_map<std::string, long long> group_index_;
    std::unordered_set<std::size_t> generated_ticks_;
    std::deque<Entity> split_points_;   // made-up line points (stable addresses for the chains)
    std::deque<Json> split_values_;

    void unsupported(const std::string& what) {
        if (issues_[what]++ == 0) issue_order_.push_back(what);
    }

    void read_entities(const Json& root);
    void check_fields(const Entity& e, const Specs& specs);
    void validate(const Entity& e);

    const Entity* resolve(const std::string& name) const {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &entities_[it->second];
    }

    Num num(const Entity& e, const char* key) const {
        const Field* f = e.field(key);
        if (!f || !f->value || !is_number(*f->value))
            malformed(e.archetype + " \"" + e.name + "\" has no number " + key);
        return json_to_num(*f->value);
    }
    double num_or(const Entity& e, const char* key, double dflt) const {
        const Field* f = e.field(key);
        return f && f->value && is_number(*f->value) ? to_double(*f->value) : dflt;
    }
    long long int_of(const Entity& e, const char* key, long long dflt) const {
        double v = num_or(e, key, static_cast<double>(dflt));
        return std::llround(v);
    }
    const std::string* ref(const Entity& e, const char* key) const {
        const Field* f = e.field(key);
        return f ? f->ref : nullptr;
    }
    long long group_of(const Entity& e) const {
        const std::string* r = ref(e, "#TIMESCALE_GROUP");
        if (!r) malformed(e.archetype + " \"" + e.name + "\" has no #TIMESCALE_GROUP");
        auto it = group_index_.find(*r);
        if (it == group_index_.end()) malformed("#TIMESCALE_GROUP \"" + *r + "\" not found");
        return it->second;
    }

    std::string ease_of(const Entity& e, const std::string& where);
    std::optional<std::string> flick_direction(const Entity& e, const NoteName& n, const std::string& where);

    NodePtr make_single(const Entity& e, const NoteName& n);
    NodePtr make_slide(const std::vector<const Entity*>& chain, long long kind);
    NodePtr make_guide(const std::vector<const Entity*>& chain, long long kind, const GuideLook& look);
    std::vector<Keyframe> keyframes(const std::vector<const Entity*>& chain);
    bool exporter_alphas(const std::vector<const Entity*>& chain, const std::string& fade);
    const Entity* split_point(const Entity& note, int ticks);
    const Entity* with_kind(const Entity& note, long long kind);
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
        switch (spec->rule) {
            case Rule::Number:
                if (!f.value || !is_number(*f.value)) malformed(e.archetype + " data \"" + key + "\" is not a number");
                break;
            case Rule::Ref:
            case Rule::Text:
                if (!f.ref) malformed(e.archetype + " data \"" + key + "\" is not a ref");
                if (spec->rule == Rule::Ref && !resolve(*f.ref))
                    malformed(e.archetype + " data \"" + key + "\": ref \"" + *f.ref + "\" not found");
                break;
            case Rule::Default:
                if (!f.value || !is_number(*f.value)) {
                    unsupported(e.archetype + " data \"" + key + "\" (not a number)");
                } else if (std::abs(to_double(*f.value) - spec->dflt) > EPSILON) {
                    unsupported(e.archetype + " data \"" + key + "\" = " + number_text(*f.value));
                }
                break;
            case Rule::AtMost:
                if (!f.value || !is_number(*f.value)) {
                    unsupported(e.archetype + " data \"" + key + "\" (not a number)");
                } else if (to_double(*f.value) > spec->dflt + EPSILON) {
                    unsupported(e.archetype + " data \"" + key + "\" = " + number_text(*f.value));
                }
                break;
        }
    }
}

void Loader::validate(const Entity& e) {
    const std::string& a = e.archetype;
    if (a == "Initialization") return check_fields(e, INITIALIZATION_FIELDS);
    if (a == "#BPM_CHANGE") return check_fields(e, BPM_FIELDS);
    if (a == "#TIMESCALE_GROUP") return check_fields(e, GROUP_FIELDS);
    if (a == "EditorGroupFolder") return check_fields(e, GROUP_FOLDER_FIELDS);
    if (a == "#TIMESCALE_CHANGE") return check_fields(e, TIMESCALE_FIELDS);
    if (a == "Skill") return check_fields(e, SKILL_FIELDS);
    if (a == "FeverChance") return check_fields(e, FEVER_CHANCE_FIELDS);
    if (a == "FeverStart") return check_fields(e, FEVER_START_FIELDS);
    if (a == "Connector") return check_fields(e, CONNECTOR_FIELDS);
    if (a == "SimLine") return check_fields(e, SIM_LINE_FIELDS);
    if (parse_note_name(a)) return check_fields(e, NOTE_FIELDS);
    unsupported("archetype \"" + a + "\"");
}

std::string Loader::ease_of(const Entity& e, const std::string& where) {
    long long v = int_of(e, "connectorEase", 1);
    if (auto name = ease_name(v)) return *name;
    unsupported(where + " connectorEase " + std::to_string(v));
    return "linear";
}

std::optional<std::string> Loader::flick_direction(const Entity& e, const NoteName& n, const std::string& where) {
    if (!n.flicked()) return std::nullopt;
    long long v = int_of(e, "direction", 0);
    if (auto name = direction_name(v)) return name;
    unsupported(where + " flick direction " + std::to_string(v));
    if (v >= 3 && v <= 5) return direction_name(v - 3);  // down flicks: the matching up flick
    return std::string("up");
}

NodePtr Loader::make_single(const Entity& e, const NoteName& n) {
    auto s = std::make_shared<Single>();
    s->beat = num(e, "#BEAT");
    s->lane = num(e, "lane");
    s->size = num(e, "size");
    s->timeScaleGroup = group_of(e);
    s->fake = n.fake;
    if (n.base == NoteBase::Damage) {
        s->type = "damage";
        return s;
    }
    if (n.act == NoteAct::Release) unsupported("archetype \"" + e.archetype + "\" outside a slide");
    s->critical = n.critical;
    s->trace = n.traced();
    s->direction = flick_direction(e, n, "note");
    return s;
}

NodePtr Loader::make_slide(const std::vector<const Entity*>& chain, long long kind) {
    bool critical = kind == 2 || kind == 52;
    bool fake = kind > 50;
    auto slide = std::make_shared<Slide>(critical, fake);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const Entity& e = *chain[i];
        NoteName n = *parse_note_name(e.archetype);
        // a slide's last note starts no segment, so its kind says nothing
        if (i + 1 < chain.size() && int_of(e, "segmentKind", kind) != kind)
            unsupported("slide changing connector kind part way");
        bool hidden = n.base == NoteBase::HiddenTick;
        if (!hidden && n.fake != fake) unsupported("fake and real notes in one slide");
        if (hidden && n.fake) unsupported("archetype \"" + e.archetype + "\"");
        bool attached = int_of(e, "isAttached", 0) != 0;
        bool first = i == 0;
        bool last = i + 1 == chain.size() && !first;

        if (first) {
            auto p = std::make_shared<SlideStartPoint>();
            p->beat = num(e, "#BEAT");
            p->lane = num(e, "lane");
            p->size = num(e, "size");
            p->timeScaleGroup = group_of(e);
            p->ease = ease_of(e, "slide start");
            if (n.base == NoteBase::Anchor) {
                p->critical = critical;
                p->judgeType = "none";
            } else {
                if (n.base != NoteBase::Judged || n.pos != NotePos::Head ||
                    (n.act != NoteAct::Tap && n.act != NoteAct::Trace))
                    unsupported("slide start archetype \"" + e.archetype + "\"");
                p->critical = n.critical;
                p->judgeType = hidden ? "none" : n.traced() ? "trace" : "normal";
            }
            if (attached) unsupported("attached slide start");
            slide->append(p);
        } else if (last) {
            auto p = std::make_shared<SlideEndPoint>();
            p->beat = num(e, "#BEAT");
            p->lane = num(e, "lane");
            p->size = num(e, "size");
            p->timeScaleGroup = group_of(e);
            if (n.base == NoteBase::Anchor) {
                p->critical = critical;
                p->judgeType = "none";
            } else {
                if (n.base != NoteBase::Judged || n.pos != NotePos::Tail || n.act == NoteAct::Tap)
                    unsupported("slide end archetype \"" + e.archetype + "\"");
                p->critical = n.critical;
                p->judgeType = hidden ? "none" : n.traced() ? "trace" : "normal";
                p->direction = flick_direction(e, n, "slide end");
            }
            if (attached) unsupported("attached slide end");
            slide->append(p);
        } else {
            auto p = std::make_shared<SlideRelayPoint>();
            p->beat = num(e, "#BEAT");
            p->lane = num(e, "lane");
            p->size = num(e, "size");
            p->timeScaleGroup = group_of(e);
            p->ease = ease_of(e, "slide point");
            p->type = attached ? "attach" : "tick";
            if (n.base == NoteBase::Tick) {
                p->critical = n.critical;
            } else if (n.base == NoteBase::Anchor) {
                if (attached) unsupported("attached AnchorNote in a slide");
            } else if (hidden) {
                if (!attached) unsupported("unattached TransientHiddenTickNote in a slide");
            } else {
                unsupported("slide point archetype \"" + e.archetype + "\"");
                if (n.base == NoteBase::Judged) p->critical = n.critical;
            }
            slide->append(p);
        }
    }
    return slide;
}

// A point standing in for a note on a slide or guide, `ticks` 480ths of a beat from it: an anchor at
// the note's place, so the line leaves the note's moment clear.
const Entity* Loader::split_point(const Entity& note, int ticks) {
    Entity p;
    p.name = note.name;
    p.archetype = parse_note_name(note.archetype)->fake ? "FakeAnchorNote" : "AnchorNote";
    for (auto& [key, f] : note.data) {
        if (key == "isAttached" || key == "attachHead" || key == "attachTail" || key == "next") continue;
        if (key == "#BEAT") {
            split_values_.emplace_back(to_double(*f.value) + ticks / 480.0);
            Field moved;
            moved.value = &split_values_.back();
            p.data.emplace_back(key, moved);
            continue;
        }
        p.data.emplace_back(key, f);
    }
    split_points_.push_back(std::move(p));
    return &split_points_.back();
}

// `note` with the connector after it of another kind
const Entity* Loader::with_kind(const Entity& note, long long kind) {
    Entity p = note;
    split_values_.emplace_back(Json::integer(kind));
    Field f;
    f.value = &split_values_.back();
    bool found = false;
    for (auto& [key, field] : p.data)
        if (key == "segmentKind") {
            field = f;
            found = true;
        }
    if (!found) p.data.emplace_back("segmentKind", f);
    split_points_.push_back(std::move(p));
    return &split_points_.back();
}

bool separator(const std::vector<const Entity*>& chain, std::size_t i, double flag) {
    return i == 0 || i + 1 == chain.size() || flag != 0;
}

// a guide's alpha keyframes: its separators (head, tail and isSeparator points, attached ones too)
std::vector<Keyframe> Loader::keyframes(const std::vector<const Entity*>& chain) {
    std::vector<Keyframe> out;
    for (std::size_t i = 0; i < chain.size(); ++i)
        if (separator(chain, i, num_or(*chain[i], "isSeparator", 0)))
            out.push_back({num_or(*chain[i], "#BEAT", 0), num_or(*chain[i], "segmentAlpha", 1)});
    return out;
}

// the fade nearest an alpha profile: from its head to its tail
std::string fade_of(const std::vector<Keyframe>& keys) {
    double head = keys.front().alpha, tail = keys.back().alpha;
    if (keys.size() == 1) return head < 1 - EPSILON ? "in" : "none";
    if (std::abs(head - tail) <= EPSILON) return "none";
    return head < tail ? "in" : "out";
}

double alpha_at(const std::vector<Keyframe>& keys, double beat) {
    if (beat <= keys.front().beat) return keys.front().alpha;
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (beat <= keys[i].beat) {
            double span = keys[i].beat - keys[i - 1].beat;
            if (span <= 0) return keys[i].alpha;
            return keys[i - 1].alpha + (keys[i].alpha - keys[i - 1].alpha) * (beat - keys[i - 1].beat) / span;
        }
    return keys.back().alpha;
}

// Whether a guide's alphas are what the exporter writes for `fade`: its plain form (no separators,
// the faded end at 0) or its smooth_guide_fade form (every point a separator, 1 -> 0.2). Such a guide
// keeps `fade`, so exported charts round-trip; any other profile keeps its keyframes.
bool Loader::exporter_alphas(const std::vector<const Entity*>& chain, const std::string& fade) {
    std::size_t n = chain.size();
    bool plain = true, smooth = true;
    for (std::size_t i = 0; i < n; ++i) {
        bool flag = num_or(*chain[i], "isSeparator", 0) != 0;
        plain = plain && !flag;
        smooth = smooth && flag;
    }
    auto alpha = [&](std::size_t i) { return num_or(*chain[i], "segmentAlpha", 1); };
    auto near = [](double a, double b) { return std::abs(a - b) <= EPSILON; };
    if (plain) {
        if (n == 1) return near(alpha(0), fade == "none" ? 1 : 0);
        return near(alpha(0), fade == "in" ? 0 : 1) && near(alpha(n - 1), fade == "out" ? 0 : 1);
    }
    if (smooth) {
        double step = (double)std::max<std::size_t>(1, n - 1);
        for (std::size_t i = 0; i < n; ++i) {
            double want = fade == "out" ? 1 - 0.8 * (double)i / step
                        : fade == "in"  ? 1 - 0.8 * (step - (double)i) / step
                                        : 1.0;
            if (!near(alpha(i), want)) return false;
        }
        return true;
    }
    return false;
}

NodePtr Loader::make_guide(const std::vector<const Entity*>& chain, long long kind, const GuideLook& look) {
    auto guide = std::make_shared<Guide>(*guide_color_name(kind), look.fade);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const Entity& e = *chain[i];
        NoteName n = *parse_note_name(e.archetype);
        bool last = i + 1 == chain.size();
        if (n.base != NoteBase::Anchor) unsupported("guide point archetype \"" + e.archetype + "\"");
        if (!last && int_of(e, "segmentKind", kind) != kind) unsupported("guide changing color part way");
        if (int_of(e, "isAttached", 0) != 0) unsupported("attached guide point");
        long long layer = int_of(e, "segmentLayer", 0);
        if (layer != 0 && layer != 1) unsupported("guide segmentLayer " + std::to_string(layer));
        auto p = std::make_shared<GuidePoint>();
        p->beat = num(e, "#BEAT");
        p->lane = num(e, "lane");
        p->size = num(e, "size");
        p->timeScaleGroup = group_of(e);
        long long ease = int_of(e, "connectorEase", 1);
        if (auto name = ease_name(ease))
            p->ease = *name;
        else {
            if (!last) unsupported("guide connectorEase " + std::to_string(ease));
            p->ease = "linear";
        }
        if (look.keys && look.at_separators) {
            if (separator(chain, i, num_or(e, "isSeparator", 0))) {
                const Field* f = e.field("segmentAlpha");
                p->alpha = f && f->value && is_number(*f->value) ? json_to_num(*f->value) : Num::I(1);
            }
        } else if (look.keys) {
            p->alpha = Num::F(alpha_at(*look.keys, (double)p->beat));
        }
        guide->append(p);
    }
    return guide;
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
    for (const Entity& e : entities_) validate(e);

    auto metadata = std::make_shared<MetaData>();
    metadata->title = "";
    metadata->artist = "";
    metadata->designer = "";
    const Json* offset = root.find("bgmOffset");
    metadata->waveoffset = offset && is_number(*offset) ? json_to_num(*offset) : Num::I(0);
    metadata->requests = std::vector<std::string>{"ticks_per_beat 480"};

    std::vector<NodePtr> bpms, groups, events, singles, connected;

    // timescale groups, in order, and their changes
    std::vector<std::shared_ptr<TimeScaleGroup>> group_nodes;
    for (const Entity& e : entities_) {
        if (e.archetype != "#TIMESCALE_GROUP") continue;
        // the exporter names an entity only once something refers to it: an empty group is unnamed
        if (!e.name.empty()) group_index_[e.name] = static_cast<long long>(group_nodes.size());
        group_nodes.push_back(std::make_shared<TimeScaleGroup>());
    }
    for (const Entity& e : entities_) {
        if (e.archetype == "#BPM_CHANGE") {
            bpms.push_back(std::make_shared<Bpm>(num(e, "#BEAT"), num(e, "#BPM")));
        } else if (e.archetype == "#TIMESCALE_CHANGE") {
            group_nodes[static_cast<std::size_t>(group_of(e))]->append(
                std::make_shared<TimeScalePoint>(num(e, "#BEAT"), num(e, "#TIMESCALE")));
        } else if (e.archetype == "Skill") {
            events.push_back(std::make_shared<Skill>(num(e, "#BEAT")));
        } else if (e.archetype == "FeverChance") {
            events.push_back(std::make_shared<FeverChance>(num(e, "#BEAT")));
        } else if (e.archetype == "FeverStart") {
            events.push_back(std::make_shared<FeverStart>(num(e, "#BEAT")));
        }
    }
    for (auto& g : group_nodes) {
        std::stable_sort(g->changes.begin(), g->changes.end(),
                         [](auto& a, auto& b) { return (double)a->beat < (double)b->beat; });
        groups.push_back(g);
    }

    // notes: slides and guides are the chains of "next"; a note in none is a lone note
    std::vector<std::optional<NoteName>> names(entities_.size());
    std::vector<long long> next(entities_.size(), -1);
    std::vector<int> incoming(entities_.size(), 0);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        names[i] = parse_note_name(entities_[i].archetype);
        if (!names[i]) continue;
        if (const std::string* r = ref(entities_[i], "next")) {
            std::size_t j = by_name_.at(*r);
            if (!parse_note_name(entities_[j].archetype)) {
                unsupported("slide continuing to archetype \"" + entities_[j].archetype + "\"");
                continue;
            }
            next[i] = static_cast<long long>(j);
            if (++incoming[j] > 1) malformed("two notes continue to note \"" + *r + "\"");
        }
    }
    std::vector<bool> placed(entities_.size(), false);
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (!names[i] || incoming[i] > 0) continue;
        const NoteName& n = *names[i];
        const Entity& e = entities_[i];
        if (next[i] < 0) {
            placed[i] = true;
            if (n.base == NoteBase::HiddenTick && !n.fake) continue;  // generated by the exporter
            if (n.base == NoteBase::Damage || (n.base == NoteBase::Judged && n.pos == NotePos::Plain)) {
                singles.push_back(make_single(e, n));
                continue;
            }
            if (n.base == NoteBase::HiddenTick || n.base == NoteBase::HiddenDamageTick) {
                unsupported("archetype \"" + e.archetype + "\"");
                continue;
            }
        }
        std::vector<const Entity*> chain;
        for (long long k = static_cast<long long>(i); k >= 0; k = next[static_cast<std::size_t>(k)]) {
            if (placed[static_cast<std::size_t>(k)] && k != static_cast<long long>(i))
                malformed("a slide loops back on itself");
            placed[static_cast<std::size_t>(k)] = true;
            chain.push_back(&entities_[static_cast<std::size_t>(k)]);
        }
        long long kind = int_of(e, "segmentKind", 0);
        bool active = kind == 1 || kind == 2 || kind == 51 || kind == 52;
        if (!active && !guide_color_name(kind)) {
            unsupported("connector segmentKind " + std::to_string(kind));
            long long base = kind % 10;  // colored active kinds 11-18, 21-28, 61-68, 71-78
            if (kind >= 11 && kind <= 78 && (kind / 10 == 1 || kind / 10 == 2 || kind / 10 == 6 || kind / 10 == 7) && base >= 1 && base <= 8) {
                kind = kind / 10 == 1 ? 1 : kind / 10 == 2 ? 2 : kind / 10 == 6 ? 51 : 52;
                active = true;
            } else {
                continue;
            }
        }
        // A Score line has one kind (color / critical / fake), so where the kind changes part way the
        // line ends and a new one starts at that point. An attached anchor (lane and size taken from
        // the curve) only marks such a place: the line passes through it, so otherwise it is dropped.
        // A judged note riding a line (attached or not) has no place in one, so with
        // attempt_attached_note_convert it becomes a note of its own: the line stops a 480th of a
        // beat before it and starts again a 480th after, leaving the moment clear. Not a round trip.
        struct Part {
            long long kind;
            std::vector<const Entity*> points;
        };
        std::vector<Part> parts{{kind, {}}};
        std::vector<Keyframe> keys;
        GuideLook look;
        if (!active) {
            keys = keyframes(chain);
            look.fade = fade_of(keys);
            if (!exporter_alphas(chain, look.fade)) look.keys = &keys;
        }
        auto known_kind = [](long long k) { return k == 1 || k == 2 || k == 51 || k == 52 || (k >= 101 && k <= 108); };
        bool split = false;
        for (std::size_t k = 0; k < chain.size(); ++k) {
            const Entity* member = chain[k];
            NoteName m = *parse_note_name(member->archetype);
            bool inner = k > 0 && k + 1 < chain.size();
            bool judged = m.base == NoteBase::Judged && m.pos == NotePos::Plain;
            if (m.base == NoteBase::Damage || m.base == NoteBase::HiddenDamageTick ||
                (judged && !opts_.attempt_attached_note_convert))
                unsupported("archetype \"" + member->archetype + "\" in a slide");
            bool attached_anchor = inner && m.base == NoteBase::Anchor && int_of(*member, "isAttached", 0) != 0;
            if (judged && opts_.attempt_attached_note_convert) {
                split = true;
                singles.push_back(make_single(*member, m));
                if (!parts.back().points.empty()) parts.back().points.push_back(split_point(*member, -1));
                parts.push_back({parts.back().kind, {}});
                if (k + 1 < chain.size()) parts.back().points.push_back(split_point(*member, 1));
                continue;
            }
            long long next_kind = int_of(*member, "segmentKind", parts.back().kind);
            bool changes = inner && next_kind != parts.back().kind && known_kind(next_kind);
            if (attached_anchor && !changes) {
                // kept only as the place of an alpha keyframe the line's alphas need
                if (look.keys && num_or(*member, "isSeparator", 0) != 0) parts.back().points.push_back(split_point(*member, 0));
                split = true;
                continue;
            }
            if (!changes) {
                parts.back().points.push_back(member);
                continue;
            }
            // a combo tick stays on the line it ends, so the new line starts a 480th after it
            bool tick = m.base == NoteBase::Tick || m.base == NoteBase::HiddenTick;
            if (tick) parts.back().points.push_back(with_kind(*member, parts.back().kind));
            split = true;
            const Entity* at = split_point(*member, tick ? 1 : 0);
            parts.back().points.push_back(at);
            parts.push_back({next_kind, {at}});
        }
        if (!split) {
            look.at_separators = true;
            connected.push_back(active ? make_slide(chain, kind) : make_guide(chain, kind, look));
            continue;
        }
        for (const Part& part : parts) {
            if (part.points.size() < 2) continue;
            bool slide = part.kind == 1 || part.kind == 2 || part.kind == 51 || part.kind == 52;
            if (slide) {
                connected.push_back(make_slide(part.points, part.kind));
            } else if (active) {
                std::vector<Keyframe> own = keyframes(part.points);
                GuideLook part_look{fade_of(own), nullptr, false};
                connected.push_back(make_guide(part.points, part.kind, part_look));
            } else {
                connected.push_back(make_guide(part.points, part.kind, look));
            }
        }
    }
    for (std::size_t i = 0; i < entities_.size(); ++i)
        if (names[i] && !placed[i]) malformed("a slide loops back on itself");

    if (!issue_order_.empty() && opts_.error_on_unsupported_feature) {
        std::string message =
            "Unsupported Next SEKAI level data (pass error_on_unsupported_feature=False to skip these):";
        for (const std::string& what : issue_order_) {
            message += "\n  " + what;
            long long count = issues_[what];
            if (count > 1) message += " (x" + std::to_string(count) + ")";
        }
        throw UnsupportedFeatureError(message);
    }

    if (bpms.empty()) bpms.push_back(std::make_shared<Bpm>(Num::I(0), Num::F(160.0)));
    std::vector<NodePtr> notes;
    for (auto* part : {&bpms, &groups, &events, &singles, &connected})
        notes.insert(notes.end(), part->begin(), part->end());
    return core::Score(metadata, notes);
}

}  // namespace

core::Score load_text(const std::string& data, const LoadOptions& opts) {
    return Loader(opts).load(data);
}

}  // namespace rg::converters::leveldata::next_sekai
