#include <algorithm>
#include <cmath>
#include <cstring>
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
};

struct FieldSpec {
    const char* name;
    Rule rule;
    double dflt = 0;
};

using Specs = std::vector<FieldSpec>;

const Specs INITIALIZATION_FIELDS = {{"initialLife", Rule::Default, 1000}};
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
    NodePtr make_guide(const std::vector<const Entity*>& chain, long long kind);
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

NodePtr Loader::make_guide(const std::vector<const Entity*>& chain, long long kind) {
    auto guide = std::make_shared<Guide>(*guide_color_name(kind), "none");
    std::vector<double> alphas;
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
        guide->append(p);
        bool separator = i == 0 || last || int_of(e, "isSeparator", 0) != 0;
        if (separator) alphas.push_back(num_or(e, "segmentAlpha", 1));
    }

    // a fade is the alpha running from the guide's head to its tail (through any separators)
    double head = alphas.front(), tail = alphas.back();
    if (chain.size() == 1) {
        guide->fade = head < 1 - EPSILON ? "in" : "none";
    } else if (std::abs(head - tail) <= EPSILON) {
        if (std::abs(head - 1) > EPSILON) unsupported("guide alpha " + std::to_string(head));
    } else {
        guide->fade = head < tail ? "in" : "out";
        bool rising = head < tail;
        double top = 0;
        for (std::size_t i = 0; i < alphas.size(); ++i) {
            top = std::max(top, alphas[i]);
            if (i > 0 && (rising ? alphas[i] < alphas[i - 1] - EPSILON : alphas[i] > alphas[i - 1] + EPSILON)) {
                unsupported("guide alpha changing direction");
                break;
            }
        }
        if (std::abs(top - 1) > EPSILON) unsupported("guide alpha peaking at " + std::to_string(top));
    }
    return guide;
}

core::Score Loader::load(const std::string& data) {
    std::string text = data;
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0x1f && static_cast<unsigned char>(text[1]) == 0x8b)
        text = util::gzip_decompress(text);
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
        if (e.name.empty()) malformed("a #TIMESCALE_GROUP has no name");
        group_index_[e.name] = static_cast<long long>(group_nodes.size());
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
        for (const Entity* member : chain) {
            NoteName m = *parse_note_name(member->archetype);
            if (m.base == NoteBase::Damage || m.base == NoteBase::HiddenDamageTick ||
                (m.base == NoteBase::Judged && m.pos == NotePos::Plain))
                unsupported("archetype \"" + member->archetype + "\" in a slide");
        }
        connected.push_back(active ? make_slide(chain, kind) : make_guide(chain, kind));
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
