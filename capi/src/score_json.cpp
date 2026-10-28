#include "score_json.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "rg/core/jsonutil.hpp"

namespace rg::capi {

using core::Kind;
using core::Node;
using core::NodePtr;
using core::Num;
using util::Json;

namespace {

constexpr std::pair<Kind, std::string_view> kKindNames[] = {
    {Kind::Bpm, "Bpm"},
    {Kind::TimeScalePoint, "TimeScalePoint"},
    {Kind::TimeScaleGroup, "TimeScaleGroup"},
    {Kind::Volume, "Volume"},
    {Kind::Single, "Single"},
    {Kind::Skill, "Skill"},
    {Kind::FeverChance, "FeverChance"},
    {Kind::FeverStart, "FeverStart"},
    {Kind::HolodoriSkill, "HolodoriSkill"},
    {Kind::HolodoriChargeStart, "HolodoriChargeStart"},
    {Kind::HolodoriChargeEnd, "HolodoriChargeEnd"},
    {Kind::HolodoriFeverStart, "HolodoriFeverStart"},
    {Kind::HolodoriFeverEnd, "HolodoriFeverEnd"},
    {Kind::SlideStartPoint, "SlideStartPoint"},
    {Kind::SlideRelayPoint, "SlideRelayPoint"},
    {Kind::SlideEndPoint, "SlideEndPoint"},
    {Kind::Slide, "Slide"},
    {Kind::GuidePoint, "GuidePoint"},
    {Kind::Guide, "Guide"},
};

std::string kind_name(Kind kind) {
    for (const auto& [k, name] : kKindNames)
        if (k == kind) return std::string(name);
    return "?";
}

std::optional<Kind> kind_from_name(std::string_view name) {
    for (const auto& [k, n] : kKindNames)
        if (n == name) return k;
    return std::nullopt;
}

bool is_slide_point(Kind k) {
    return k == Kind::SlideStartPoint || k == Kind::SlideRelayPoint || k == Kind::SlideEndPoint;
}

// ---- Score -> JSON ----

void put(Json& o, const char* key, Json value) { o.items().emplace_back(key, std::move(value)); }

Json num(const Num& n) { return core::num_to_json(n); }
Json opt(const std::optional<bool>& v) { return v ? Json(*v) : Json(); }
Json opt(const std::optional<std::string>& v) { return v ? Json(*v) : Json(); }
Json opt(const std::optional<Num>& v) { return v ? num(*v) : Json(); }

template <class T>
void put_event(Json& o, const Node* n) {
    auto* p = static_cast<const T*>(n);
    put(o, "beat", num(p->beat));
    put(o, "type", p->type);
}

Json node_to_json(const Node* n) {
    Json o = Json::object();
    put(o, "kind", kind_name(n->kind));
    switch (n->kind) {
        case Kind::Bpm: {
            auto* p = static_cast<const core::Bpm*>(n);
            put(o, "beat", num(p->beat));
            put(o, "bpm", num(p->bpm));
            put(o, "type", p->type);
            break;
        }
        case Kind::TimeScalePoint: {
            auto* p = static_cast<const core::TimeScalePoint*>(n);
            put(o, "beat", num(p->beat));
            put(o, "timeScale", num(p->timeScale));
            break;
        }
        case Kind::TimeScaleGroup: {
            auto* p = static_cast<const core::TimeScaleGroup*>(n);
            Json changes = Json::array();
            for (const auto& c : p->changes) changes.push_back(node_to_json(c.get()));
            put(o, "changes", std::move(changes));
            put(o, "type", p->type);
            break;
        }
        case Kind::Volume: {
            auto* p = static_cast<const core::Volume*>(n);
            put(o, "beat", num(p->beat));
            put(o, "volume", num(p->volume));
            put(o, "type", p->type);
            break;
        }
        case Kind::Skill: put_event<core::Skill>(o, n); break;
        case Kind::FeverChance: put_event<core::FeverChance>(o, n); break;
        case Kind::FeverStart: put_event<core::FeverStart>(o, n); break;
        case Kind::HolodoriChargeStart: put_event<core::HolodoriChargeStart>(o, n); break;
        case Kind::HolodoriChargeEnd: put_event<core::HolodoriChargeEnd>(o, n); break;
        case Kind::HolodoriFeverStart: put_event<core::HolodoriFeverStart>(o, n); break;
        case Kind::HolodoriFeverEnd: put_event<core::HolodoriFeverEnd>(o, n); break;
        case Kind::HolodoriSkill: {
            auto* p = static_cast<const core::HolodoriSkill*>(n);
            put(o, "beat", num(p->beat));
            put(o, "slot", Json::integer(p->slot));
            put(o, "type", p->type);
            break;
        }
        case Kind::Single: {
            auto* p = static_cast<const core::Single*>(n);
            put(o, "beat", num(p->beat));
            put(o, "critical", opt(p->critical));
            put(o, "lane", num(p->lane));
            put(o, "size", num(p->size));
            put(o, "fake", p->fake);
            put(o, "timeScaleGroup", Json::integer(p->timeScaleGroup));
            put(o, "speedRatio", num(p->speedRatio));
            put(o, "trace", opt(p->trace));
            put(o, "direction", opt(p->direction));
            put(o, "type", p->type);
            break;
        }
        case Kind::SlideStartPoint: {
            auto* p = static_cast<const core::SlideStartPoint*>(n);
            put(o, "beat", num(p->beat));
            put(o, "critical", p->critical);
            put(o, "ease", p->ease);
            put(o, "judgeType", p->judgeType);
            put(o, "lane", num(p->lane));
            put(o, "size", num(p->size));
            put(o, "timeScaleGroup", Json::integer(p->timeScaleGroup));
            put(o, "speedRatio", num(p->speedRatio));
            put(o, "type", p->type);
            break;
        }
        case Kind::SlideRelayPoint: {
            auto* p = static_cast<const core::SlideRelayPoint*>(n);
            put(o, "beat", num(p->beat));
            put(o, "ease", p->ease);
            put(o, "lane", num(p->lane));
            put(o, "size", num(p->size));
            put(o, "timeScaleGroup", Json::integer(p->timeScaleGroup));
            put(o, "type", p->type);
            put(o, "critical", opt(p->critical));
            put(o, "fake", p->fake);
            put(o, "speedRatio", num(p->speedRatio));
            break;
        }
        case Kind::SlideEndPoint: {
            auto* p = static_cast<const core::SlideEndPoint*>(n);
            put(o, "beat", num(p->beat));
            put(o, "critical", p->critical);
            put(o, "judgeType", p->judgeType);
            put(o, "lane", num(p->lane));
            put(o, "size", num(p->size));
            put(o, "timeScaleGroup", Json::integer(p->timeScaleGroup));
            put(o, "speedRatio", num(p->speedRatio));
            put(o, "direction", opt(p->direction));
            put(o, "type", p->type);
            break;
        }
        case Kind::Slide: {
            auto* p = static_cast<const core::Slide*>(n);
            put(o, "critical", p->critical);
            put(o, "fake", p->fake);
            Json connections = Json::array();
            for (const auto& c : p->connections) connections.push_back(node_to_json(c.get()));
            put(o, "connections", std::move(connections));
            put(o, "type", p->type);
            break;
        }
        case Kind::GuidePoint: {
            auto* p = static_cast<const core::GuidePoint*>(n);
            put(o, "beat", num(p->beat));
            put(o, "ease", p->ease);
            put(o, "lane", num(p->lane));
            put(o, "size", num(p->size));
            put(o, "timeScaleGroup", Json::integer(p->timeScaleGroup));
            put(o, "speedRatio", num(p->speedRatio));
            put(o, "alpha", opt(p->alpha));
            break;
        }
        case Kind::Guide: {
            auto* p = static_cast<const core::Guide*>(n);
            put(o, "color", p->color);
            put(o, "fade", p->fade);
            Json midpoints = Json::array();
            for (const auto& m : p->midpoints) midpoints.push_back(node_to_json(m.get()));
            put(o, "midpoints", std::move(midpoints));
            put(o, "type", p->type);
            put(o, "color_code", opt(p->color_code));
            break;
        }
    }
    return o;
}

// ---- JSON -> Score ----

std::string quoted(std::string_view key) { return "\"" + std::string(key) + "\""; }

class Reader {
public:
    Reader(const Json& json, std::string path) : json_(json), path_(std::move(path)) {
        if (!json.is_object()) fail("must be an object");
    }

    [[noreturn]] void fail(const std::string& what) const {
        throw SchemaError("invalid score JSON" + (path_.empty() ? "" : " at " + path_) + ": " + what);
    }

    std::string child(const char* key, std::size_t index) const {
        return (path_.empty() ? "" : path_ + ".") + key + "[" + std::to_string(index) + "]";
    }
    std::string child(const char* key) const { return (path_.empty() ? "" : path_ + ".") + key; }

    const Json* get(const char* key) {
        seen_.emplace_back(key);
        return json_.find(key);
    }
    const Json* get_non_null(const char* key) {
        const Json* p = get(key);
        return p && !p->is_null() ? p : nullptr;
    }
    const Json& need(const char* key) {
        const Json* p = get(key);
        if (!p) fail(quoted(key) + " is required");
        return *p;
    }

    Num num(const char* key) { return to_num(key, need(key)); }
    Num num_or(const char* key, Num dflt) {
        const Json* p = get(key);
        return p ? to_num(key, *p) : dflt;
    }
    std::optional<Num> opt_num(const char* key) {
        const Json* p = get_non_null(key);
        return p ? std::optional<Num>(to_num(key, *p)) : std::nullopt;
    }

    long long integer(const char* key) {
        const Json& j = need(key);
        if (j.type() == Json::Type::Int) return j.as_int();
        if (j.type() == Json::Type::Float) {
            double v = j.as_float();
            if (std::isfinite(v) && v == std::trunc(v) && std::fabs(v) < 9.2e18)
                return static_cast<long long>(v);
        }
        fail(quoted(key) + " must be an integer");
    }

    bool boolean(const char* key) { return to_bool(key, need(key)); }
    bool boolean_or(const char* key, bool dflt) {
        const Json* p = get(key);
        return p ? to_bool(key, *p) : dflt;
    }
    std::optional<bool> opt_bool(const char* key) {
        const Json* p = get_non_null(key);
        return p ? std::optional<bool>(to_bool(key, *p)) : std::nullopt;
    }

    std::string str(const char* key) { return to_str(key, need(key)); }
    std::string str_or(const char* key, const char* dflt) {
        const Json* p = get(key);
        return p ? to_str(key, *p) : std::string(dflt);
    }
    std::optional<std::string> opt_str(const char* key) {
        const Json* p = get_non_null(key);
        return p ? std::optional<std::string>(to_str(key, *p)) : std::nullopt;
    }

    const Json::Array* array(const char* key, bool required) {
        const Json* p = required ? &need(key) : get(key);
        if (!p) return nullptr;
        if (!p->is_array()) fail(quoted(key) + " must be an array");
        return &p->arr();
    }

    void finish() const {
        for (const auto& kv : json_.items())
            if (std::find(seen_.begin(), seen_.end(), kv.first) == seen_.end())
                fail("unknown key " + quoted(kv.first));
    }

    const std::string& path() const { return path_; }

private:
    const Json& json_;
    std::string path_;
    std::vector<std::string> seen_;

    Num to_num(const char* key, const Json& j) const {
        if (j.type() == Json::Type::Int) return Num::I(j.as_int());
        if (j.type() == Json::Type::Float) return Num::F(j.as_float());
        fail(quoted(key) + " must be a number");
    }
    bool to_bool(const char* key, const Json& j) const {
        if (j.type() != Json::Type::Bool) fail(quoted(key) + " must be a boolean");
        return j.as_bool();
    }
    std::string to_str(const char* key, const Json& j) const {
        if (j.type() != Json::Type::Str) fail(quoted(key) + " must be a string");
        return j.as_str();
    }
};

NodePtr parse_node(const Json& json, const std::string& path);

void expect_kind(Reader& r, Kind kind) {
    if (auto k = r.opt_str("kind"); k && *k != kind_name(kind))
        r.fail("expected kind " + quoted(kind_name(kind)) + ", got " + quoted(*k));
}

std::shared_ptr<core::TimeScalePoint> build_time_scale_point(Reader& r) {
    Num beat = r.num("beat");
    Num time_scale = r.num("timeScale");
    return std::make_shared<core::TimeScalePoint>(beat, time_scale);
}

std::shared_ptr<core::GuidePoint> build_guide_point(Reader& r) {
    auto p = std::make_shared<core::GuidePoint>();
    p->beat = r.num("beat");
    p->ease = r.str("ease");
    p->lane = r.num("lane");
    p->size = r.num("size");
    p->timeScaleGroup = r.integer("timeScaleGroup");
    p->speedRatio = r.num_or("speedRatio", Num::F(1.0));
    p->alpha = r.opt_num("alpha");
    return p;
}

template <class T>
std::shared_ptr<T> nested(const Json& json, const std::string& path, Kind kind,
                          std::shared_ptr<T> (*build)(Reader&)) {
    Reader r(json, path);
    expect_kind(r, kind);
    auto node = build(r);
    r.finish();
    return node;
}

template <class T>
NodePtr build_event(Reader& r) {
    auto p = std::make_shared<T>(r.num("beat"));
    if (const Json* t = r.get("type")) {
        if (t->type() != Json::Type::Str) r.fail("\"type\" must be a string");
        p->type = t->as_str();
    }
    return p;
}

NodePtr build_node(Kind kind, Reader& r) {
    switch (kind) {
        case Kind::Bpm: {
            Num beat = r.num("beat");
            Num bpm = r.num("bpm");
            return std::make_shared<core::Bpm>(beat, bpm, r.str_or("type", "bpm"));
        }
        case Kind::TimeScalePoint: return build_time_scale_point(r);
        case Kind::TimeScaleGroup: {
            std::vector<std::shared_ptr<core::TimeScalePoint>> changes;
            if (const auto* arr = r.array("changes", false))
                for (std::size_t i = 0; i < arr->size(); ++i)
                    changes.push_back(nested((*arr)[i], r.child("changes", i), Kind::TimeScalePoint,
                                             &build_time_scale_point));
            return std::make_shared<core::TimeScaleGroup>(std::move(changes),
                                                          r.str_or("type", "timeScaleGroup"));
        }
        case Kind::Volume: {
            Num beat = r.num("beat");
            Num volume = r.num("volume");
            return std::make_shared<core::Volume>(beat, volume, r.str_or("type", "volume"));
        }
        case Kind::Skill: return build_event<core::Skill>(r);
        case Kind::FeverChance: return build_event<core::FeverChance>(r);
        case Kind::FeverStart: return build_event<core::FeverStart>(r);
        case Kind::HolodoriChargeStart: return build_event<core::HolodoriChargeStart>(r);
        case Kind::HolodoriChargeEnd: return build_event<core::HolodoriChargeEnd>(r);
        case Kind::HolodoriFeverStart: return build_event<core::HolodoriFeverStart>(r);
        case Kind::HolodoriFeverEnd: return build_event<core::HolodoriFeverEnd>(r);
        case Kind::HolodoriSkill: {
            Num beat = r.num("beat");
            auto p = std::make_shared<core::HolodoriSkill>(beat, r.integer("slot"));
            p->type = r.str_or("type", "holodoriSkill");
            return p;
        }
        case Kind::Single: {
            auto p = std::make_shared<core::Single>();
            p->beat = r.num("beat");
            p->critical = r.opt_bool("critical");
            p->lane = r.num("lane");
            p->size = r.num("size");
            p->fake = r.boolean_or("fake", false);
            p->timeScaleGroup = r.integer("timeScaleGroup");
            p->speedRatio = r.num_or("speedRatio", Num::F(1.0));
            p->trace = r.opt_bool("trace");
            p->direction = r.opt_str("direction");
            p->type = r.str_or("type", "single");
            return p;
        }
        case Kind::SlideStartPoint: {
            auto p = std::make_shared<core::SlideStartPoint>();
            p->beat = r.num("beat");
            p->critical = r.boolean("critical");
            p->ease = r.str("ease");
            p->judgeType = r.str("judgeType");
            p->lane = r.num("lane");
            p->size = r.num("size");
            p->timeScaleGroup = r.integer("timeScaleGroup");
            p->speedRatio = r.num_or("speedRatio", Num::F(1.0));
            p->type = r.str_or("type", "start");
            return p;
        }
        case Kind::SlideRelayPoint: {
            auto p = std::make_shared<core::SlideRelayPoint>();
            p->beat = r.num("beat");
            p->ease = r.str("ease");
            p->lane = r.num("lane");
            p->size = r.num("size");
            p->timeScaleGroup = r.integer("timeScaleGroup");
            p->type = r.str("type");
            p->critical = r.opt_bool("critical");
            p->fake = r.boolean_or("fake", false);
            p->speedRatio = r.num_or("speedRatio", Num::F(1.0));
            return p;
        }
        case Kind::SlideEndPoint: {
            auto p = std::make_shared<core::SlideEndPoint>();
            p->beat = r.num("beat");
            p->critical = r.boolean("critical");
            p->judgeType = r.str("judgeType");
            p->lane = r.num("lane");
            p->size = r.num("size");
            p->timeScaleGroup = r.integer("timeScaleGroup");
            p->speedRatio = r.num_or("speedRatio", Num::F(1.0));
            p->direction = r.opt_str("direction");
            p->type = r.str_or("type", "end");
            return p;
        }
        case Kind::Slide: {
            bool critical = r.boolean("critical");
            bool fake = r.boolean_or("fake", false);
            const auto* arr = r.array("connections", true);
            if (arr->empty()) r.fail("\"connections\" must not be empty");
            std::vector<NodePtr> connections;
            for (std::size_t i = 0; i < arr->size(); ++i) {
                std::string path = r.child("connections", i);
                NodePtr c = parse_node((*arr)[i], path);
                if (!is_slide_point(c->kind))
                    throw SchemaError("invalid score JSON at " + path +
                                      ": a Slide connects SlideStartPoint, SlideRelayPoint and "
                                      "SlideEndPoint nodes only");
                connections.push_back(std::move(c));
            }
            return std::make_shared<core::Slide>(critical, fake, std::move(connections),
                                                 r.str_or("type", "slide"));
        }
        case Kind::GuidePoint: return build_guide_point(r);
        case Kind::Guide: {
            std::string color = r.str("color");
            std::string fade = r.str("fade");
            const auto* arr = r.array("midpoints", true);
            if (arr->empty()) r.fail("\"midpoints\" must not be empty");
            std::vector<std::shared_ptr<core::GuidePoint>> midpoints;
            for (std::size_t i = 0; i < arr->size(); ++i)
                midpoints.push_back(nested((*arr)[i], r.child("midpoints", i), Kind::GuidePoint,
                                           &build_guide_point));
            std::string type = r.str_or("type", "guide");
            return std::make_shared<core::Guide>(std::move(color), std::move(fade),
                                                 std::move(midpoints), std::move(type),
                                                 r.opt_str("color_code"));
        }
    }
    r.fail("unhandled kind");
}

NodePtr parse_node(const Json& json, const std::string& path) {
    Reader r(json, path);
    std::string name = r.str("kind");
    std::optional<Kind> kind = kind_from_name(name);
    if (!kind) r.fail("unknown kind " + quoted(name));
    NodePtr node = build_node(*kind, r);
    r.finish();
    return node;
}

std::shared_ptr<core::MetaData> parse_metadata(const Json* json) {
    auto md = std::make_shared<core::MetaData>();
    if (!json || json->is_null()) return md;
    Reader r(*json, "metadata");
    md->title = r.str_or("title", "");
    md->artist = r.str_or("artist", "");
    md->designer = r.str_or("designer", "");
    md->waveoffset = r.num_or("waveoffset", Num::F(0.0));
    if (const Json* req = r.get_non_null("requests")) {
        if (!req->is_array()) r.fail("\"requests\" must be an array of strings or null");
        std::vector<std::string> requests;
        for (const Json& item : req->arr()) {
            if (item.type() != Json::Type::Str) r.fail("\"requests\" must be an array of strings");
            requests.push_back(item.as_str());
        }
        md->requests = std::move(requests);
    }
    r.finish();
    return md;
}

std::map<std::string, core::Value> parse_format_metadata(const Json* json) {
    std::map<std::string, core::Value> out;
    if (!json || json->is_null()) return out;
    Reader r(*json, "format_metadata");
    for (const auto& [key, value] : json->items()) {
        switch (value.type()) {
            case Json::Type::Bool: out[key] = value.as_bool(); break;
            case Json::Type::Int: out[key] = value.as_int(); break;
            case Json::Type::Float: out[key] = value.as_float(); break;
            case Json::Type::Str: out[key] = value.as_str(); break;
            default: r.fail(quoted(key) + " must be a boolean, a number or a string");
        }
    }
    return out;
}

}  // namespace

Json value_to_json(const core::Value& value) {
    return std::visit([](const auto& v) { return Json(v); }, value);
}

Json format_metadata_to_json(const core::Score& score) {
    Json out = Json::object();
    for (const auto& [key, value] : score.format_metadata) put(out, key.c_str(), value_to_json(value));
    return out;
}

Json score_to_json(const core::Score& score) {
    Json root = Json::object();
    Json metadata = Json::object();
    if (const auto& md = score.metadata) {
        put(metadata, "title", md->title);
        put(metadata, "artist", md->artist);
        put(metadata, "designer", md->designer);
        put(metadata, "waveoffset", num(md->waveoffset));
        if (md->requests) {
            Json requests = Json::array();
            for (const auto& req : *md->requests) requests.push_back(req);
            put(metadata, "requests", std::move(requests));
        } else {
            put(metadata, "requests", Json());
        }
    }
    put(root, "metadata", std::move(metadata));
    put(root, "format_metadata", format_metadata_to_json(score));
    Json notes = Json::array();
    for (const auto& n : score.notes) notes.push_back(node_to_json(n.get()));
    put(root, "notes", std::move(notes));
    return root;
}

core::Score score_from_json(const Json& json) {
    Reader r(json, "");
    auto metadata = parse_metadata(r.get("metadata"));
    auto format_metadata = parse_format_metadata(r.get("format_metadata"));
    std::vector<NodePtr> notes;
    if (const auto* arr = r.array("notes", false)) {
        notes.reserve(arr->size());
        for (std::size_t i = 0; i < arr->size(); ++i)
            notes.push_back(parse_node((*arr)[i], r.child("notes", i)));
    }
    r.finish();
    core::Score score(std::move(metadata), std::move(notes));
    score.format_metadata = std::move(format_metadata);
    return score;
}

}  // namespace rg::capi
