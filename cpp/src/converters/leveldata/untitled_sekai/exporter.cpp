#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../level_data_io.hpp"
#include "rg/converters/leveldata/untitled_sekai.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/base36.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::leveldata::untitled_sekai {

using namespace rg::core;
using rg::util::Json;

namespace {

constexpr double EPSILON = 1e-6;
const std::string BEAT = "#BEAT";
const std::string BPM = "#BPM";
const std::string BPM_CHANGE = "#BPM_CHANGE";

struct Inter;
struct DV {
    int kind;  // 0=num, 1=refstr, 2=inter
    Json num;
    std::string s;
    Inter* inter;
};
struct Inter {
    std::string archetype;
    std::vector<std::pair<std::string, DV>> data;
    bool sim = false;
    std::optional<long long> timeScaleGroup;
    std::optional<std::string> ref;
    std::optional<std::string> ease;  // ConnectionIntermediate

    DV* find(const std::string& k) {
        for (auto& kv : data)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    void set(const std::string& k, DV v) {
        for (auto& kv : data)
            if (kv.first == k) {
                kv.second = std::move(v);
                return;
            }
        data.push_back({k, std::move(v)});
    }
};

DV dv_num(Json j) { return DV{0, std::move(j), "", nullptr}; }
DV dv_str(std::string s) { return DV{1, Json(), std::move(s), nullptr}; }
DV dv_inter(Inter* i) { return DV{2, Json(), "", i}; }

long long dir_num(const std::string& d) {
    if (d == "left") return -1;
    if (d == "up") return 0;
    if (d == "right") return 1;
    return 0;
}
long long ease_num(const std::string& e) {
    if (e == "outin") return -2;
    if (e == "out") return -1;
    if (e == "linear") return 0;
    if (e == "in") return 1;
    if (e == "inout") return 2;
    return 0;
}
long long slide_start_num(const std::string& s) {
    if (s == "tap") return 0;
    if (s == "trace") return 1;
    if (s == "none") return 2;
    return 0;
}
long long color_num(const std::string& c) {
    if (c == "neutral") return 0;
    if (c == "red") return 1;
    if (c == "green") return 2;
    if (c == "blue") return 3;
    if (c == "yellow") return 4;
    if (c == "purple") return 5;
    if (c == "cyan") return 6;
    if (c == "black") return 7;
    return 0;
}
long long fade_num(const std::string& f) {
    if (f == "in") return 2;
    if (f == "out") return 0;
    if (f == "none") return 1;
    return 1;
}

// connection accessors over slide points (SlideStartPoint/RelayPoint/EndPoint)
std::string conn_type(const Node* n) {
    if (n->kind == Kind::SlideStartPoint) return "start";
    if (n->kind == Kind::SlideEndPoint) return "end";
    return static_cast<const SlideRelayPoint*>(n)->type;  // tick/attach
}
std::string conn_ease(const Node* n) {
    if (n->kind == Kind::SlideStartPoint) return static_cast<const SlideStartPoint*>(n)->ease;
    if (n->kind == Kind::SlideRelayPoint) return static_cast<const SlideRelayPoint*>(n)->ease;
    return "linear";
}
long long conn_tsg(const Node* n) {
    if (n->kind == Kind::SlideStartPoint) return static_cast<const SlideStartPoint*>(n)->timeScaleGroup;
    if (n->kind == Kind::SlideRelayPoint) return static_cast<const SlideRelayPoint*>(n)->timeScaleGroup;
    if (n->kind == Kind::SlideEndPoint) return static_cast<const SlideEndPoint*>(n)->timeScaleGroup;
    return 0;
}

}  // namespace

core::Score load_text(const std::string&) {
    throw std::runtime_error("UntitledSekai support loading not implemented...");
}

core::ExportResult export_bytes(const core::Score& score_in, const ExportOptions& opts) {
    core::Score score = core::deep_copy(score_in);
    score.close_open_slides();
    // Automatically replaces extended eases.
    score.replace_extended_ease();  // XXX: they don't support inout or outin? According to their commits on usctool-custom

    // XXX: support isdummy/fake notes on export
    score.delete_fake_notes();
    score.delete_damage_notes();
    score.flatten_speed_ratios_to_layers();

    bool any_bpm = false;
    for (auto& n : score.notes)
        if (n->kind == Kind::Bpm) { any_bpm = true; break; }
    if (!any_bpm)
        score.notes.insert(score.notes.begin(), std::make_shared<Bpm>(Num::I(0), Num::F(160.0)));
    score.sort_by_beat();

    std::deque<std::unique_ptr<Inter>> pool;
    auto new_inter = [&]() -> Inter* {
        pool.push_back(std::make_unique<Inter>());
        return pool.back().get();
    };
    // owns generated slide relay points
    std::vector<std::shared_ptr<Node>> gen_pool;

    std::vector<Json> entities;
    std::map<double, std::vector<Inter*>> time_to_intermediates;  // keyed by beat
    std::vector<double> time_order;  // insertion order of beats
    long long ref_counter = 0;

    auto get_ref = [&](Inter* inter) -> std::string {
        if (inter->ref.has_value()) return *inter->ref;
        inter->ref = util::base36_dumps(ref_counter);
        ref_counter += 1;
        return *inter->ref;
    };

    auto append = [&](Inter* inter) {
        std::string ref = get_ref(inter);
        Json entity = Json::object();
        entity["archetype"] = inter->archetype;
        Json data = Json::array();
        // record for simline
        if (inter->sim) {
            DV* beatdv = inter->find(BEAT);
            double beat = beatdv->num.as_float();
            if (!time_to_intermediates.count(beat)) time_order.push_back(beat);
            time_to_intermediates[beat].push_back(inter);
        }
        for (auto& kv : inter->data) {
            Json d = Json::object();
            d["name"] = kv.first;
            if (kv.second.kind == 0)
                d["value"] = kv.second.num;
            else if (kv.second.kind == 1)
                d["ref"] = kv.second.s;
            else
                d["ref"] = get_ref(kv.second.inter);
            data.push_back(d);
        }
        if (inter->timeScaleGroup.has_value()) {
            Json d = Json::object();
            d["name"] = "timeScaleGroup";
            d["ref"] = "tsg:" + std::to_string(*inter->timeScaleGroup);
            data.push_back(d);
        }
        entity["data"] = data;
        entity["name"] = ref;
        entities.push_back(entity);
    };

    // Initialization entities
    {
        Inter* a = new_inter(); a->archetype = "Initialization"; append(a);
        Inter* b = new_inter(); b->archetype = "InputManager"; append(b);
        Inter* c = new_inter(); c->archetype = "Stage"; append(c);
    }

    // TimeScaleGroups
    long long ts_group_index = -1;
    std::vector<Json> ts_group_entities;
    std::vector<Json> ts_change_entities;

    std::vector<TimeScaleGroup*> ts_groups;
    for (auto& n : score.notes)
        if (n->kind == Kind::TimeScaleGroup) ts_groups.push_back(static_cast<TimeScaleGroup*>(n.get()));
    for (TimeScaleGroup* ts_group : ts_groups) {
        ts_group_index += 1;
        std::vector<std::shared_ptr<TimeScalePoint>> changes = ts_group->changes;
        std::stable_sort(changes.begin(), changes.end(),
                         [](auto& a, auto& b) { return (double)a->beat < (double)b->beat; });
        for (std::size_t idx = 0; idx < changes.size(); ++idx) {
            auto& change = changes[idx];
            Json next_ref = Json::object();
            next_ref["name"] = "next";
            if (idx + 1 < changes.size())
                next_ref["ref"] = "tsc:" + std::to_string(ts_group_index) + ":" + std::to_string(idx + 1);
            else
                next_ref["value"] = Json::integer(-1);
            Json e = Json::object();
            e["archetype"] = "TimeScaleChange";
            Json data = Json::array();
            Json d0 = Json::object(); d0["name"] = BEAT; d0["value"] = num_to_json(change->beat); data.push_back(d0);
            Json d1 = Json::object(); d1["name"] = "timeScale";
            d1["value"] = Json((double)change->timeScale == 0 ? 0.000001 : (double)change->timeScale);
            data.push_back(d1);
            data.push_back(next_ref);
            e["data"] = data;
            e["name"] = "tsc:" + std::to_string(ts_group_index) + ":" + std::to_string(idx);
            ts_change_entities.push_back(e);
        }
        Json next_group = Json::object();
        next_group["name"] = "next";
        if (ts_group_index == static_cast<long long>(ts_groups.size()) - 1)
            next_group["value"] = Json::integer(-1);
        else
            next_group["ref"] = "tsg:" + std::to_string(ts_group_index + 1);
        Json e = Json::object();
        e["archetype"] = "TimeScaleGroup";
        Json data = Json::array();
        Json d0 = Json::object(); d0["name"] = "first"; d0["ref"] = "tsc:" + std::to_string(ts_group_index) + ":0"; data.push_back(d0);
        Json d1 = Json::object(); d1["name"] = "length"; d1["value"] = Json::integer(static_cast<long long>(changes.size())); data.push_back(d1);
        data.push_back(next_group);
        e["data"] = data;
        e["name"] = "tsg:" + std::to_string(ts_group_index);
        ts_group_entities.push_back(e);
    }

    if (ts_group_index == -1) {
        Json g = Json::object();
        g["archetype"] = "TimeScaleGroup";
        Json gd = Json::array();
        Json gd0 = Json::object(); gd0["name"] = "first"; gd0["ref"] = "tsc:0:0"; gd.push_back(gd0);
        Json gd1 = Json::object(); gd1["name"] = "length"; gd1["value"] = Json::integer(0); gd.push_back(gd1);
        g["data"] = gd;
        g["name"] = "tsg:0";
        entities.push_back(g);
        Json c = Json::object();
        c["archetype"] = "TimeScaleChange";
        Json cd = Json::array();
        Json cd0 = Json::object(); cd0["name"] = BEAT; cd0["value"] = Json(0.0); cd.push_back(cd0);
        Json cd1 = Json::object(); cd1["name"] = "timeScale"; cd1["value"] = Json(1.0); cd.push_back(cd1);
        Json cd2 = Json::object(); cd2["name"] = "timeScaleGroup"; cd2["ref"] = "tsg:0"; cd.push_back(cd2);
        c["data"] = cd;
        c["name"] = "tsc:0:0";
        entities.push_back(c);
    } else {
        for (auto& e : ts_group_entities) entities.push_back(e);
        for (auto& e : ts_change_entities) entities.push_back(e);
    }

    auto handle_bpm = [&](Bpm* obj) -> Inter* {
        Inter* i = new_inter();
        i->archetype = BPM_CHANGE;
        i->set(BEAT, dv_num(num_to_json(obj->beat)));
        i->set(BPM, dv_num(num_to_json(obj->bpm)));
        i->sim = false;
        return i;
    };

    auto handle_single = [&](Single* obj) -> Inter* {
        Inter* inter;
        if (obj->type == "damage") {
            inter = new_inter();
            inter->archetype = "DamageNote";
            inter->set(BEAT, dv_num(num_to_json(obj->beat)));
            inter->set("lane", dv_num(num_to_json(obj->lane)));
            inter->set("size", dv_num(num_to_json(obj->size)));
            inter->sim = false;
            inter->timeScaleGroup = obj->timeScaleGroup;
        } else {
            bool crit = obj->critical.has_value() && obj->critical.value();
            inter = new_inter();
            inter->archetype = crit ? "CriticalTapNote" : "NormalTapNote";
            inter->set(BEAT, dv_num(num_to_json(obj->beat)));
            inter->set("lane", dv_num(num_to_json(obj->lane)));
            inter->set("size", dv_num(num_to_json(obj->size)));
            inter->sim = true;
            inter->timeScaleGroup = obj->timeScaleGroup;
            bool trace = obj->trace.has_value() && obj->trace.value();
            bool has_dir = obj->direction.has_value();
            if (trace) {
                inter->archetype = crit ? "CriticalTraceNote" : "NormalTraceNote";
                if (has_dir) {
                    if (*obj->direction == "none") {
                        inter->archetype = "NonDirectionalTraceFlickNote";
                    } else {
                        inter->archetype = crit ? "CriticalTraceFlickNote" : "NormalTraceFlickNote";
                        inter->set("direction", dv_num(Json::integer(dir_num(*obj->direction))));
                    }
                }
            } else {
                if (has_dir && *obj->direction != "none") {
                    inter->archetype = crit ? "CriticalFlickNote" : "NormalFlickNote";
                    inter->set("direction", dv_num(Json::integer(dir_num(*obj->direction))));
                } else if (has_dir && *obj->direction == "none") {
                    return nullptr;
                }
            }
        }
        return inter;
    };

    auto get_slide_connections = [&](Slide* obj) -> std::vector<Node*> {
        std::vector<Node*> connections;
        for (auto& c : obj->connections) connections.push_back(c.get());
        std::vector<double> beats;
        for (Node* c : connections) beats.push_back(node_beat(c));
        std::sort(beats.begin(), beats.end());
        double min_beat = beats.front(), max_beat = beats.back();
        double start = std::max(std::ceil((min_beat - EPSILON) / 0.5) * 0.5,
                                std::floor((min_beat + EPSILON) / 0.5 + 1) * 0.5);
        long long num_steps = static_cast<long long>(std::floor((max_beat - start) / 0.5 + EPSILON));
        for (long long i = 0; i < num_steps; ++i) {
            double beat = util::py_round(start + i * 0.5, 9);
            if (beat + EPSILON >= max_beat) break;
            auto rp = std::make_shared<SlideRelayPoint>();
            rp->beat = Num::F(beat);
            rp->type = "attach";
            rp->lane = Num::I(0);
            rp->size = Num::I(0);
            rp->timeScaleGroup = 0;
            rp->ease = "linear";
            gen_pool.push_back(rp);
            connections.push_back(rp.get());
        }
        Node* start_step = nullptr;
        for (Node* c : connections)
            if (conn_type(c) == "start") { start_step = c; break; }
        Node* end_step = nullptr;
        for (Node* c : connections)
            if (conn_type(c) == "end") { end_step = c; break; }
        std::vector<Node*> steps;
        for (Node* c : connections) {
            std::string t = conn_type(c);
            if (t == "tick" || t == "attach") steps.push_back(c);
        }
        std::stable_sort(steps.begin(), steps.end(),
                         [](Node* a, Node* b) { return node_beat(a) < node_beat(b); });
        std::vector<Node*> out;
        out.push_back(start_step);
        for (Node* s : steps) out.push_back(s);
        out.push_back(end_step);
        return out;
    };

    auto handle_slide = [&](Slide* obj) -> std::vector<Inter*> {
        std::vector<Inter*> cis;
        std::vector<Inter*> joints;
        std::vector<Inter*> attaches;
        std::vector<Inter*> ends;

        std::vector<Node*> connections = get_slide_connections(obj);
        std::string start_type = "tap";

        for (std::size_t i = 0; i < connections.size(); ++i) {
            Node* connection = connections[i];
            std::string ctype = conn_type(connection);
            if (i == 0 && ctype == "start") {
                auto* sp = static_cast<SlideStartPoint*>(connection);
                std::string archetype;
                bool sim;
                if (sp->judgeType == "none") {
                    archetype = "HiddenSlideStartNote"; sim = false; start_type = "none";
                } else if (sp->judgeType == "trace") {
                    archetype = sp->critical ? "CriticalTraceSlideStartNote" : "NormalTraceSlideStartNote";
                    sim = true; start_type = "trace";
                } else {
                    archetype = sp->critical ? "CriticalSlideStartNote" : "NormalSlideStartNote";
                    sim = true; start_type = "tap";
                }
                Inter* ci = new_inter();
                ci->archetype = archetype;
                ci->set(BEAT, dv_num(num_to_json(sp->beat)));
                ci->set("lane", dv_num(num_to_json(sp->lane)));
                ci->set("size", dv_num(num_to_json(sp->size)));
                ci->sim = sim;
                ci->timeScaleGroup = sp->timeScaleGroup;
                ci->ease = sp->ease;
                cis.push_back(ci);
                joints.push_back(ci);
                continue;
            } else if (i == connections.size() - 1 && ctype == "end") {
                auto* ep = static_cast<SlideEndPoint*>(connection);
                Inter* ci = new_inter();
                if (ep->judgeType == "none") {
                    ci->archetype = "HiddenSlideTickNote";
                    ci->set(BEAT, dv_num(num_to_json(ep->beat)));
                    ci->set("lane", dv_num(num_to_json(ep->lane)));
                    ci->set("size", dv_num(num_to_json(ep->size)));
                    ci->sim = false;
                    ci->timeScaleGroup = ep->timeScaleGroup;
                } else {
                    std::string archetype;
                    if (ep->critical && ep->judgeType == "trace") archetype = "CriticalTraceSlideEndNote";
                    else if (ep->judgeType == "trace") archetype = "NormalTraceSlideEndNote";
                    else if (ep->critical) archetype = "CriticalSlideEndNote";
                    else archetype = "NormalSlideEndNote";
                    ci->archetype = archetype;
                    ci->set(BEAT, dv_num(num_to_json(ep->beat)));
                    ci->set("lane", dv_num(num_to_json(ep->lane)));
                    ci->set("size", dv_num(num_to_json(ep->size)));
                    ci->sim = true;
                    ci->timeScaleGroup = ep->timeScaleGroup;
                    if (ep->direction.has_value()) {
                        ci->archetype = ep->critical ? "CriticalSlideEndFlickNote" : "NormalSlideEndFlickNote";
                        ci->set("direction", dv_num(Json::integer(dir_num(*ep->direction))));
                    }
                }
                cis.push_back(ci);
                joints.push_back(ci);
                ends.push_back(ci);
                continue;
            } else if (ctype == "tick") {
                auto* rp = static_cast<SlideRelayPoint*>(connection);
                std::string archetype = "HiddenSlideTickNote";
                if (rp->critical.has_value())
                    archetype = rp->critical.value() ? "CriticalSlideTickNote" : "NormalSlideTickNote";
                Inter* ci = new_inter();
                ci->archetype = archetype;
                ci->set(BEAT, dv_num(num_to_json(rp->beat)));
                ci->set("lane", dv_num(num_to_json(rp->lane)));
                ci->set("size", dv_num(num_to_json(rp->size)));
                ci->sim = false;
                ci->ease = rp->ease;
                ci->timeScaleGroup = rp->timeScaleGroup;
                cis.push_back(ci);
                joints.push_back(ci);
            } else if (ctype == "attach") {
                auto* rp = static_cast<SlideRelayPoint*>(connection);
                std::string archetype = "IgnoredSlideTickNote";
                if (rp->critical.has_value())
                    archetype = rp->critical.value() ? "CriticalAttachedSlideTickNote" : "NormalAttachedSlideTickNote";
                Inter* ci = new_inter();
                ci->archetype = archetype;
                ci->set(BEAT, dv_num(num_to_json(rp->beat)));
                ci->sim = false;
                ci->ease = rp->ease;
                if (archetype != "IgnoredSlideTickNote") ci->timeScaleGroup = rp->timeScaleGroup;
                cis.push_back(ci);
                attaches.push_back(ci);
            } else {
                throw std::runtime_error("Unexpected slide type " + ctype);
            }
        }

        std::vector<Inter*> connectors;
        Inter* start = cis[0];
        for (std::size_t i = 0; i < joints.size(); ++i) {
            if (i == 0) continue;
            Inter* head = joints[i - 1];
            if (!head->ease.has_value()) throw std::runtime_error("Unexpected missing ease");
            std::string archetype = obj->critical ? "CriticalSlideConnector" : "NormalSlideConnector";
            Inter* c = new_inter();
            c->archetype = archetype;
            c->set("start", dv_inter(start));
            c->set("end", dv_inter(ends[0]));
            c->set("head", dv_inter(head));
            c->set("tail", dv_inter(joints[i]));
            c->set("ease", dv_num(Json::integer(ease_num(*head->ease))));
            c->set("startType", dv_num(Json::integer(slide_start_num(start_type))));
            c->sim = false;
            connectors.push_back(c);
        }

        auto cis_index = [&](Inter* x) -> long long {
            for (std::size_t k = 0; k < cis.size(); ++k)
                if (cis[k] == x) return static_cast<long long>(k);
            return -1;
        };
        for (Inter* attach : attaches) {
            long long index = cis_index(attach);
            long long tail_index = -1;
            for (std::size_t k = 0; k < joints.size(); ++k) {
                if (cis_index(joints[k]) > index) { tail_index = static_cast<long long>(k); break; }
            }
            if (tail_index < 0 || tail_index - 1 < 0) continue;
            attach->set("attach", dv_inter(connectors[static_cast<std::size_t>(tail_index - 1)]));
        }
        for (Inter* end : ends) end->set("slide", dv_inter(connectors.back()));

        std::stable_sort(cis.begin(), cis.end(), [&](Inter* a, Inter* b) {
            return a->find(BEAT)->num.as_float() < b->find(BEAT)->num.as_float();
        });
        std::vector<Inter*> result;
        for (Inter* c : cis) result.push_back(c);
        for (Inter* c : connectors) result.push_back(c);
        return result;
    };

    auto handle_guide = [&](Guide* obj) -> std::vector<Inter*> {
        std::vector<Inter*> intermediates;
        GuidePoint* start = obj->midpoints.front().get();
        GuidePoint* end = obj->midpoints.back().get();
        for (std::size_t i = 1; i < obj->midpoints.size(); ++i) {
            GuidePoint* joint = obj->midpoints[i].get();
            GuidePoint* head = obj->midpoints[i - 1].get();
            Inter* g = new_inter();
            g->archetype = "Guide";
            g->set("color", dv_num(Json::integer(color_num(obj->color))));
            g->set("fade", dv_num(Json::integer(fade_num(obj->fade))));
            g->set("ease", dv_num(Json::integer(ease_num(head->ease))));
            g->set("startLane", dv_num(num_to_json(start->lane)));
            g->set("startSize", dv_num(num_to_json(start->size)));
            g->set("startBeat", dv_num(num_to_json(start->beat)));
            g->set("startTimeScaleGroup", dv_str("tsg:" + std::to_string(start->timeScaleGroup)));
            g->set("headLane", dv_num(num_to_json(head->lane)));
            g->set("headSize", dv_num(num_to_json(head->size)));
            g->set("headBeat", dv_num(num_to_json(head->beat)));
            g->set("headTimeScaleGroup", dv_str("tsg:" + std::to_string(head->timeScaleGroup)));
            g->set("tailLane", dv_num(num_to_json(joint->lane)));
            g->set("tailSize", dv_num(num_to_json(joint->size)));
            g->set("tailBeat", dv_num(num_to_json(joint->beat)));
            g->set("tailTimeScaleGroup", dv_str("tsg:" + std::to_string(joint->timeScaleGroup)));
            g->set("endLane", dv_num(num_to_json(end->lane)));
            g->set("endSize", dv_num(num_to_json(end->size)));
            g->set("endBeat", dv_num(num_to_json(end->beat)));
            g->set("endTimeScaleGroup", dv_str("tsg:" + std::to_string(end->timeScaleGroup)));
            g->sim = false;
            intermediates.push_back(g);
        }
        return intermediates;
    };

    for (auto& note_sp : score.notes) {
        Node* note = note_sp.get();
        if (is_event(note->kind)) continue;  // the engine has no skills or fever
        std::string type;
        switch (note->kind) {
            case Kind::Bpm: type = "bpm"; break;
            case Kind::TimeScaleGroup: type = "timeScaleGroup"; break;
            case Kind::Single: type = static_cast<Single*>(note)->type; break;
            case Kind::Slide: type = "slide"; break;
            case Kind::Guide: type = "guide"; break;
            case Kind::Volume: type = "volume"; break;
            default: type = ""; break;
        }
        if (type == "bpm") {
            append(handle_bpm(static_cast<Bpm*>(note)));
        } else if (type == "timeScaleGroup" || type == "volume") {
            // handled separately / None
        } else if (type == "single") {
            Inter* r = handle_single(static_cast<Single*>(note));
            if (r) append(r);
        } else if (type == "slide") {
            for (Inter* item : handle_slide(static_cast<Slide*>(note))) if (item) append(item);
        } else if (type == "guide") {
            for (Inter* item : handle_guide(static_cast<Guide*>(note))) if (item) append(item);
        } else {
            throw std::runtime_error("Unknown note type: " + type);
        }
    }

    // SimLine connections
    for (double beat : time_order) {
        auto& inters = time_to_intermediates[beat];
        for (std::size_t j = 1; j < inters.size(); ++j) {
            Inter* s = new_inter();
            s->archetype = "SimLine";
            s->set("a", dv_inter(inters[j - 1]));
            s->set("b", dv_inter(inters[j]));
            s->sim = false;
            append(s);
        }
    }

    Json leveldata = Json::object();
    leveldata["bgmOffset"] = num_to_json(score.metadata->waveoffset);
    Json ents = Json::array();
    for (auto& e : entities) ents.push_back(e);
    leveldata["entities"] = ents;

    return {detail::dump_level_data(leveldata, opts.compressed), std::move(score)};
}

}  // namespace rg::converters::leveldata::untitled_sekai
