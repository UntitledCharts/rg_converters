#include <string>

#include "rg/converters/usc.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"

namespace rg::converters::usc {

using namespace rg::core;
using rg::util::Json;

static Json opt_bool(const std::optional<bool>& b) {
    if (b.has_value()) return Json(b.value());
    return Json(nullptr);
}
static Json opt_str(const std::optional<std::string>& s) {
    if (s.has_value()) return Json(*s);
    return Json(nullptr);
}

static Json slide_point_asdict(const Node* n) {
    Json o = Json::object();
    if (n->kind == Kind::SlideStartPoint) {
        auto* p = static_cast<const SlideStartPoint*>(n);
        o["beat"] = num_to_json(p->beat);
        o["critical"] = Json(p->critical);
        o["ease"] = p->ease;
        o["judgeType"] = p->judgeType;
        o["lane"] = num_to_json(p->lane);
        o["size"] = num_to_json(p->size);
        o["timeScaleGroup"] = Json::integer(p->timeScaleGroup);
        o["speedRatio"] = num_to_json(p->speedRatio);
        o["type"] = p->type;
    } else if (n->kind == Kind::SlideRelayPoint) {
        auto* p = static_cast<const SlideRelayPoint*>(n);
        o["beat"] = num_to_json(p->beat);
        o["ease"] = p->ease;
        o["lane"] = num_to_json(p->lane);
        o["size"] = num_to_json(p->size);
        o["timeScaleGroup"] = Json::integer(p->timeScaleGroup);
        o["type"] = p->type;
        o["critical"] = opt_bool(p->critical);
        o["fake"] = Json(p->fake);
        o["speedRatio"] = num_to_json(p->speedRatio);
    } else {
        auto* p = static_cast<const SlideEndPoint*>(n);
        o["beat"] = num_to_json(p->beat);
        o["critical"] = Json(p->critical);
        o["judgeType"] = p->judgeType;
        o["lane"] = num_to_json(p->lane);
        o["size"] = num_to_json(p->size);
        o["timeScaleGroup"] = Json::integer(p->timeScaleGroup);
        o["speedRatio"] = num_to_json(p->speedRatio);
        o["direction"] = opt_str(p->direction);
        o["type"] = p->type;
    }
    return o;
}

static Json asdict(const Node* n) {
    Json o = Json::object();
    switch (n->kind) {
        case Kind::Bpm: {
            auto* b = static_cast<const Bpm*>(n);
            o["beat"] = num_to_json(b->beat);
            o["bpm"] = num_to_json(b->bpm);
            o["type"] = b->type;
            break;
        }
        case Kind::TimeScaleGroup: {
            auto* g = static_cast<const TimeScaleGroup*>(n);
            Json changes = Json::array();
            for (auto& c : g->changes) {
                Json co = Json::object();
                co["beat"] = num_to_json(c->beat);
                co["timeScale"] = num_to_json(c->timeScale);
                changes.push_back(co);
            }
            o["changes"] = changes;
            o["type"] = g->type;
            break;
        }
        case Kind::Volume: {
            auto* v = static_cast<const Volume*>(n);
            o["beat"] = num_to_json(v->beat);
            o["volume"] = num_to_json(v->volume);
            o["type"] = v->type;
            break;
        }
        case Kind::Skill:
            o["beat"] = num_to_json(static_cast<const Skill*>(n)->beat);
            o["type"] = static_cast<const Skill*>(n)->type;
            break;
        case Kind::FeverChance:
            o["beat"] = num_to_json(static_cast<const FeverChance*>(n)->beat);
            o["type"] = static_cast<const FeverChance*>(n)->type;
            break;
        case Kind::FeverStart:
            o["beat"] = num_to_json(static_cast<const FeverStart*>(n)->beat);
            o["type"] = static_cast<const FeverStart*>(n)->type;
            break;
        case Kind::Single: {
            auto* s = static_cast<const Single*>(n);
            o["beat"] = num_to_json(s->beat);
            // the loader (and USC) requires both on a single
            bool single = s->type == "single";
            o["critical"] = single ? Json(s->critical.value_or(false)) : opt_bool(s->critical);
            o["lane"] = num_to_json(s->lane);
            o["size"] = num_to_json(s->size);
            o["fake"] = Json(s->fake);
            o["timeScaleGroup"] = Json::integer(s->timeScaleGroup);
            o["speedRatio"] = num_to_json(s->speedRatio);
            o["trace"] = single ? Json(s->trace.value_or(false)) : opt_bool(s->trace);
            o["direction"] = opt_str(s->direction);
            o["type"] = s->type;
            break;
        }
        case Kind::Slide: {
            auto* sl = static_cast<const Slide*>(n);
            o["critical"] = Json(sl->critical);
            o["fake"] = Json(sl->fake);
            Json conns = Json::array();
            for (auto& c : sl->connections) conns.push_back(slide_point_asdict(c.get()));
            o["connections"] = conns;
            o["type"] = sl->type;
            break;
        }
        case Kind::Guide: {
            auto* g = static_cast<const Guide*>(n);
            o["color"] = g->color;
            o["fade"] = g->fade;
            Json mps = Json::array();
            for (auto& m : g->midpoints) {
                Json mo = Json::object();
                mo["beat"] = num_to_json(m->beat);
                mo["ease"] = m->ease;
                mo["lane"] = num_to_json(m->lane);
                mo["size"] = num_to_json(m->size);
                mo["timeScaleGroup"] = Json::integer(m->timeScaleGroup);
                mo["speedRatio"] = num_to_json(m->speedRatio);
                mps.push_back(mo);
            }
            o["midpoints"] = mps;
            o["type"] = g->type;
            o["color_code"] = opt_str(g->color_code);
            break;
        }
        default:
            break;
    }
    return o;
}

static void remove_none(Json& data) {
    if (data.is_object()) {
        Json::Object kept;
        for (auto& kv : data.items()) {
            if (kv.second.is_null()) continue;
            kept.push_back(kv);
        }
        data.items() = kept;
        for (auto& kv : data.items()) remove_none(kv.second);
    } else if (data.is_array()) {
        for (auto& e : data.arr()) remove_none(e);
    }
}

static void usc_remove_fake_field(Json& notes) {
    for (auto& note : notes.arr()) {
        note.erase("fake");
        if (note.contains("connections")) {
            for (auto& c : note["connections"].arr()) c.erase("fake");
        }
        if (note.contains("midpoints")) {
            for (auto& m : note["midpoints"].arr()) m.erase("fake");
        }
    }
}

core::ExportResult dumps(const core::Score& score_in, const ExportOptions& opts) {
    core::Score score = core::deep_copy(score_in);
    score.close_open_slides();
    // USC has no fake notes.
    score.delete_fake_notes();
    bool any_bpm = false;
    for (auto& n : score.notes)
        if (n->kind == Kind::Bpm) {
            any_bpm = true;
            break;
        }
    if (!any_bpm)
        score.notes.insert(score.notes.begin(),
                           std::make_shared<Bpm>(Num::I(0), Num::F(160.0)));

    Json notes = Json::array();
    for (auto& i : convert_holodori_events(score.notes)) {
        if (i->kind == Kind::Volume) continue;
        notes.push_back(asdict(i.get()));
    }
    remove_none(notes);
    usc_remove_fake_field(notes);

    Json usc_inner = Json::object();
    usc_inner["objects"] = notes;
    usc_inner["offset"] = num_to_json(score.metadata->waveoffset);
    Json usc_data = Json::object();
    usc_data["usc"] = usc_inner;
    usc_data["version"] = Json::integer(2);

    return {usc_data.dump(opts.minified ? -1 : 4, false), std::move(score)};
}

}  // namespace rg::converters::usc
