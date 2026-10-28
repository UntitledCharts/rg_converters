#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../level_data_io.hpp"
#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pynum.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::leveldata::next_sekai {

using namespace rg::core;
using rg::util::Json;

namespace {

constexpr double EPSILON = 1e-6;

struct DataVal {
    bool is_ref;
    Json num;        // when !is_ref
    struct Entity* ref;  // when is_ref
};

struct Entity {
    std::string archetype;
    std::string name;
    std::vector<std::pair<std::string, DataVal>> data;

    DataVal& operator[](const std::string& key) {
        for (auto& kv : data)
            if (kv.first == key) return kv.second;
        data.push_back({key, DataVal{}});
        return data.back().second;
    }
    void set_num(const std::string& key, Json v) { (*this)[key] = DataVal{false, std::move(v), nullptr}; }
    void set_ref(const std::string& key, Entity* e) { (*this)[key] = DataVal{true, Json(), e}; }
    Json get_num(const std::string& key) {
        for (auto& kv : data)
            if (kv.first == key) return kv.second.num;
        return Json();
    }
};

// directions / eases / colors
long long direction_of(const std::string& d) {
    if (d == "left") return 1;
    if (d == "up") return 0;
    if (d == "right") return 2;
    return 0;
}
long long connector_ease(const std::string& e) {
    if (e == "outin") return 5;
    if (e == "out") return 3;
    if (e == "linear") return 1;
    if (e == "in") return 2;
    if (e == "inout") return 4;
    return 1;
}
// Sonolus' quadratic eases, as the engine draws a connector
double eased(const std::string& ease, double s) {
    if (ease == "in") return s * s;
    if (ease == "out") return 1 - (1 - s) * (1 - s);
    if (ease == "inout") return s < 0.5 ? 2 * s * s : 1 - 2 * (1 - s) * (1 - s);
    if (ease == "outin") return s < 0.5 ? 0.5 * (1 - (1 - 2 * s) * (1 - 2 * s)) : 0.5 + 0.5 * (2 * s - 1) * (2 * s - 1);
    return s;
}

long long guide_color_num(const std::string& c) {
    if (c == "neutral") return 101;
    if (c == "red") return 102;
    if (c == "green") return 103;
    if (c == "blue") return 104;
    if (c == "yellow") return 105;
    if (c == "purple") return 106;
    if (c == "cyan") return 107;
    if (c == "black") return 108;
    return 103;
}

}  // namespace

core::ExportResult export_bytes(const core::Score& score_in, const ExportOptions& opts) {
    core::Score score = core::deep_copy(score_in);
    score.close_open_slides();
    score.flatten_speed_ratios_to_layers();
    std::deque<std::unique_ptr<Entity>> pool;  // owns entities
    std::vector<Entity*> entities;
    long long name_counter = 0;
    auto new_entity = [&](const std::string& archetype) -> Entity* {
        pool.push_back(std::make_unique<Entity>());
        Entity* e = pool.back().get();
        e->archetype = archetype;
        e->name = std::to_string(name_counter++);
        return e;
    };

    entities.push_back(new_entity("Initialization"));

    std::vector<Bpm*> bpm_changes;
    std::vector<TimeScaleGroup*> timescale_groups;
    std::vector<Single*> single_notes;
    std::vector<Node*> events;
    std::vector<Slide*> slide_notes;
    std::vector<Guide*> guide_notes;

    std::vector<Entity*> timescale_group_entities;
    std::vector<Entity*> sim_line_eligible_notes;

    // keep converted-holodori notes alive
    std::vector<NodePtr> conv = convert_holodori_events(score.notes);
    for (auto& entry_sp : conv) {
        Node* entry = entry_sp.get();
        switch (entry->kind) {
            case Kind::Bpm: bpm_changes.push_back(static_cast<Bpm*>(entry)); break;
            case Kind::TimeScaleGroup: timescale_groups.push_back(static_cast<TimeScaleGroup*>(entry)); break;
            case Kind::Single: single_notes.push_back(static_cast<Single*>(entry)); break;
            case Kind::Slide: slide_notes.push_back(static_cast<Slide*>(entry)); break;
            case Kind::Guide: guide_notes.push_back(static_cast<Guide*>(entry)); break;
            case Kind::Skill:
            case Kind::FeverChance:
            case Kind::FeverStart: events.push_back(entry); break;
            default: break;
        }
    }

    // owned fallback notes
    std::vector<NodePtr> fallback;
    if (timescale_groups.empty()) {
        auto tsg = std::make_shared<TimeScaleGroup>();
        tsg->changes.push_back(std::make_shared<TimeScalePoint>(Num::I(0), Num::I(1)));
        fallback.push_back(tsg);
        timescale_groups.push_back(static_cast<TimeScaleGroup*>(tsg.get()));
    }
    if (bpm_changes.empty()) {
        auto b = std::make_shared<Bpm>(Num::I(0), Num::I(160));
        fallback.push_back(b);
        bpm_changes.push_back(static_cast<Bpm*>(b.get()));
    }

    auto group_entity = [&](long long group, double beat) {
        if (group < 0 || group >= static_cast<long long>(timescale_group_entities.size()))
            throw std::invalid_argument("The note at beat " + util::py_float_repr(beat) + " is in timescale group " +
                                        std::to_string(group) + ", but the score has " +
                                        std::to_string(timescale_group_entities.size()));
        return timescale_group_entities[static_cast<std::size_t>(group)];
    };

    for (Bpm* bpm : bpm_changes) {
        Entity* e = new_entity("#BPM_CHANGE");
        e->set_num("#BEAT", num_to_json(bpm->beat));
        e->set_num("#BPM", num_to_json(bpm->bpm));
        entities.push_back(e);
    }

    for (TimeScaleGroup* group : timescale_groups) {
        Entity* group_entity = new_entity("#TIMESCALE_GROUP");
        entities.push_back(group_entity);
        timescale_group_entities.push_back(group_entity);
        Entity* last_entity = nullptr;
        std::vector<std::shared_ptr<TimeScalePoint>> changes = group->changes;
        std::stable_sort(changes.begin(), changes.end(),
                         [](auto& a, auto& b) { return (double)a->beat < (double)b->beat; });
        for (auto& change : changes) {
            Entity* new_e = new_entity("#TIMESCALE_CHANGE");
            new_e->set_num("#BEAT", num_to_json(change->beat));
            new_e->set_num("#TIMESCALE", num_to_json(change->timeScale));
            new_e->set_num("#TIMESCALE_SKIP", Json::integer(0));
            new_e->set_ref("#TIMESCALE_GROUP", group_entity);
            new_e->set_num("#TIMESCALE_EASE", Json::integer(0));
            if (last_entity == nullptr)
                group_entity->set_ref("first", new_e);
            else
                last_entity->set_ref("next", new_e);
            last_entity = new_e;
            entities.push_back(new_e);
        }
    }

    for (Node* event : events) {
        std::string name;
        double beat;
        if (event->kind == Kind::Skill) { name = "Skill"; beat = static_cast<Skill*>(event)->beat; }
        else if (event->kind == Kind::FeverStart) { name = "FeverStart"; beat = static_cast<FeverStart*>(event)->beat; }
        else { name = "FeverChance"; beat = static_cast<FeverChance*>(event)->beat; }
        Entity* e = new_entity(name);
        Num bnum = (event->kind == Kind::Skill) ? static_cast<Skill*>(event)->beat
                   : (event->kind == Kind::FeverStart) ? static_cast<FeverStart*>(event)->beat
                                                       : static_cast<FeverChance*>(event)->beat;
        (void)beat;
        e->set_num("#BEAT", num_to_json(bnum));
        entities.push_back(e);
    }

    for (Single* note : single_notes) {
        std::vector<std::string> name_parts;
        if (note->fake) name_parts.push_back("Fake");
        if (note->type == "damage") {
            name_parts.push_back("Damage");
        } else {
            if (note->critical.has_value() && note->critical.value())
                name_parts.push_back("Critical");
            else
                name_parts.push_back("Normal");
            if (!note->direction.has_value()) {
                if (note->trace.has_value() && note->trace.value()) name_parts.push_back("Trace");
                else name_parts.push_back("Tap");
            } else {
                if (note->trace.has_value() && note->trace.value()) name_parts.push_back("TraceFlick");
                else name_parts.push_back("Flick");
            }
        }
        name_parts.push_back("Note");
        std::string name;
        for (auto& p : name_parts) name += p;
        Entity* e = new_entity(name);
        e->set_num("#BEAT", num_to_json(note->beat));
        e->set_ref("#TIMESCALE_GROUP", group_entity(note->timeScaleGroup, note->beat));
        e->set_num("lane", num_to_json(note->lane));
        e->set_num("size", num_to_json(note->size));
        e->set_num("direction", Json::integer(direction_of(note->direction.value_or("up"))));
        e->set_num("isAttached", Json::integer(0));
        e->set_num("connectorEase", Json::integer(1));
        e->set_num("isSeparator", Json::integer(0));
        e->set_num("segmentKind", Json::integer((note->critical.has_value() && note->critical.value()) ? 2 : 1));
        e->set_num("segmentAlpha", Json::integer(1));
        entities.push_back(e);
        if (note->type != "damage") sim_line_eligible_notes.push_back(e);
    }

    for (Slide* slide : slide_notes) {
        Entity* prev_joint_entity = nullptr;
        std::string prev_joint_ease = "linear";
        Entity* prev_note_entity = nullptr;
        Entity* head_note_entity = nullptr;
        std::vector<Entity*> queued_attach_notes;
        std::vector<Entity*> connectors;
        std::vector<NodePtr> connections = slide->connections;
        std::stable_sort(connections.begin(), connections.end(),
                         [](const NodePtr& a, const NodePtr& b) { return node_beat(a.get()) < node_beat(b.get()); });
        double next_hidden_tick_beat = std::floor(node_beat(connections[0].get()) * 2 + 1) / 2;
        for (auto& note_sp : connections) {
            Node* note = note_sp.get();
            bool is_sim_line_eligible = false;
            bool is_attached = false;
            std::vector<std::string> name_parts;
            if (slide->fake) name_parts.push_back("Fake");
            if (note->kind == Kind::SlideStartPoint) {
                auto* p = static_cast<SlideStartPoint*>(note);
                if (p->judgeType == "none") {
                    name_parts.push_back("Anchor");
                } else {
                    name_parts.push_back(p->critical ? "Critical" : "Normal");
                    name_parts.push_back("Head");
                    if (p->judgeType == "trace") name_parts.push_back("Trace");
                    else name_parts.push_back("Tap");  // judgeType=="normal"
                    is_sim_line_eligible = true;
                }
            } else if (note->kind == Kind::SlideEndPoint) {
                auto* p = static_cast<SlideEndPoint*>(note);
                if (p->judgeType == "none") {
                    name_parts.push_back("Anchor");
                } else {
                    name_parts.push_back(p->critical ? "Critical" : "Normal");
                    name_parts.push_back("Tail");
                    if (!p->direction.has_value()) {
                        if (p->judgeType == "trace") name_parts.push_back("Trace");
                        else name_parts.push_back("Release");  // normal
                    } else {
                        if (p->judgeType == "trace") name_parts.push_back("TraceFlick");
                        else name_parts.push_back("Flick");  // normal
                    }
                    is_sim_line_eligible = true;
                }
            } else {  // SlideRelayPoint
                auto* p = static_cast<SlideRelayPoint*>(note);
                if (p->type == "tick") {
                    if (p->critical.has_value()) {
                        name_parts.push_back(p->critical.value() ? "Critical" : "Normal");
                        name_parts.push_back("Tick");
                    } else {
                        name_parts.push_back("Anchor");
                    }
                } else {  // attach
                    is_attached = true;
                    if (p->critical.has_value()) {
                        name_parts.push_back(p->critical.value() ? "Critical" : "Normal");
                        name_parts.push_back("Tick");
                    } else {
                        name_parts = {"TransientHiddenTick"};
                    }
                }
            }
            name_parts.push_back("Note");
            std::string name;
            for (auto& pt : name_parts) name += pt;

            // direction / ease accessors
            std::string dir = "up";
            if (note->kind == Kind::SlideEndPoint && static_cast<SlideEndPoint*>(note)->direction.has_value())
                dir = *static_cast<SlideEndPoint*>(note)->direction;
            std::string ease = "linear";
            if (note->kind == Kind::SlideStartPoint) ease = static_cast<SlideStartPoint*>(note)->ease;
            else if (note->kind == Kind::SlideRelayPoint) ease = static_cast<SlideRelayPoint*>(note)->ease;

            Entity* e = new_entity(name);
            e->set_num("#BEAT", num_to_json(node_beat(note)));
            long long tsg = 0;
            switch (note->kind) {
                case Kind::SlideStartPoint: tsg = static_cast<SlideStartPoint*>(note)->timeScaleGroup; break;
                case Kind::SlideRelayPoint: tsg = static_cast<SlideRelayPoint*>(note)->timeScaleGroup; break;
                case Kind::SlideEndPoint: tsg = static_cast<SlideEndPoint*>(note)->timeScaleGroup; break;
                default: break;
            }
            e->set_ref("#TIMESCALE_GROUP", group_entity(tsg, node_beat(note)));
            e->set_num("lane", num_to_json(Num::F(node_lane(note))));
            e->set_num("size", num_to_json(Num::F(node_size(note))));
            e->set_num("direction", Json::integer(direction_of(dir)));
            e->set_num("isAttached", Json::integer(is_attached ? 1 : 0));
            e->set_num("connectorEase", Json::integer(connector_ease(ease)));
            e->set_num("isSeparator", Json::integer(0));
            e->set_num("segmentKind", Json::integer((slide->critical ? 2 : 1) + (slide->fake ? 50 : 0)));
            e->set_num("segmentAlpha", Json::integer(1));
            e->set_num("segmentLayer", Json::integer(0));
            entities.push_back(e);
            if (is_sim_line_eligible) sim_line_eligible_notes.push_back(e);
            if (head_note_entity == nullptr) head_note_entity = e;
            e->set_ref("activeHead", head_note_entity);
            if (is_attached) {
                queued_attach_notes.push_back(e);
            } else {
                if (prev_joint_entity == nullptr) {
                    // assert not queued_attach_notes
                } else {
                    for (Entity* attach : queued_attach_notes) {
                        attach->set_ref("attachHead", prev_joint_entity);
                        attach->set_ref("attachTail", e);
                    }
                    queued_attach_notes.clear();
                    while (next_hidden_tick_beat + EPSILON < e->get_num("#BEAT").as_float()) {
                        Entity* hidden_tick = new_entity("TransientHiddenTickNote");
                        hidden_tick->set_num("#BEAT", Json(util::py_round(next_hidden_tick_beat, 9)));
                        // on the segment it lies on: the layer of the point it follows, and the lane and
                        // size of the curve there
                        hidden_tick->set_ref("#TIMESCALE_GROUP", (*prev_joint_entity)["#TIMESCALE_GROUP"].ref);
                        double head_beat = prev_joint_entity->get_num("#BEAT").as_float();
                        double tail_beat = e->get_num("#BEAT").as_float();
                        double head_lane = prev_joint_entity->get_num("lane").as_float();
                        double head_size = prev_joint_entity->get_num("size").as_float();
                        double tail_lane = e->get_num("lane").as_float(), tail_size = e->get_num("size").as_float();
                        if (head_lane == tail_lane && head_size == tail_size) {
                            hidden_tick->set_num("lane", e->get_num("lane"));
                            hidden_tick->set_num("size", e->get_num("size"));
                        } else {
                            double at = tail_beat > head_beat ? (next_hidden_tick_beat - head_beat) / (tail_beat - head_beat) : 0.0;
                            double k = eased(prev_joint_ease, std::clamp(at, 0.0, 1.0));
                            hidden_tick->set_num("lane", num_to_json(Num::F(util::py_round(head_lane + (tail_lane - head_lane) * k, 6))));
                            hidden_tick->set_num("size", num_to_json(Num::F(util::py_round(head_size + (tail_size - head_size) * k, 6))));
                        }
                        hidden_tick->set_num("direction", Json::integer(0));
                        hidden_tick->set_num("isAttached", Json::integer(1));
                        hidden_tick->set_num("connectorEase", Json::integer(1));
                        hidden_tick->set_num("isSeparator", Json::integer(0));
                        hidden_tick->set_num("segmentKind", Json::integer(1));
                        hidden_tick->set_num("segmentAlpha", Json::integer(0));
                        hidden_tick->set_ref("activeHead", head_note_entity);
                        hidden_tick->set_ref("attachHead", prev_joint_entity);
                        hidden_tick->set_ref("attachTail", e);
                        entities.push_back(hidden_tick);
                        next_hidden_tick_beat += 0.5;
                    }
                    Entity* connector_entity = new_entity("Connector");
                    connector_entity->set_ref("head", prev_joint_entity);
                    connector_entity->set_ref("tail", e);
                    entities.push_back(connector_entity);
                    connectors.push_back(connector_entity);
                }
                prev_joint_entity = e;
                prev_joint_ease = ease;
            }
            if (prev_note_entity != nullptr) prev_note_entity->set_ref("next", e);
            prev_note_entity = e;
        }
        for (Entity* connector_entity : connectors) {
            connector_entity->set_ref("segmentHead", head_note_entity);
            connector_entity->set_ref("segmentTail", prev_joint_entity);
            connector_entity->set_ref("activeHead", head_note_entity);
            connector_entity->set_ref("activeTail", prev_joint_entity);
        }
    }

    for (Guide* guide : guide_notes) {
        std::vector<std::shared_ptr<GuidePoint>> connections = guide->midpoints;
        std::stable_sort(connections.begin(), connections.end(),
                         [](auto& a, auto& b) { return (double)a->beat < (double)b->beat; });
        Entity* prev_note_entity = nullptr;
        Entity* head_note_entity = nullptr;
        std::vector<Entity*> connectors;

        long long step_size = std::max(1LL, static_cast<long long>(connections.size()) - 1);
        long long step_idx = 0;
        // alpha keyframes, when the guide has them, are its separators instead of its fade
        std::vector<std::pair<double, double>> keys;
        for (auto& p : connections)
            if (p->alpha) keys.push_back({(double)p->beat, (double)*p->alpha});
        auto alpha_at = [&keys](double beat) {
            if (beat <= keys.front().first) return keys.front().second;
            for (std::size_t i = 1; i < keys.size(); ++i)
                if (beat <= keys[i].first) {
                    double span = keys[i].first - keys[i - 1].first;
                    if (span <= 0) return keys[i].second;
                    return keys[i - 1].second +
                           (keys[i].second - keys[i - 1].second) * (beat - keys[i - 1].first) / span;
                }
            return keys.back().second;
        };

        for (auto& note : connections) {
            Json segment_alpha = Json::integer(1);
            long long is_separator = 0;
            if (!keys.empty()) {
                if (note->alpha) {
                    segment_alpha = num_to_json(*note->alpha);
                    is_separator = 1;
                } else if (step_idx == 0 || step_idx == step_size) {
                    segment_alpha = Json(alpha_at((double)note->beat));   // head and tail always separate
                }
            } else if (opts.smooth_guide_fade) {
                is_separator = 1;
                if (guide->fade == "out")
                    segment_alpha = Json(1 - 0.8 * (static_cast<double>(step_idx) / step_size));
                else if (guide->fade == "in")
                    segment_alpha = Json(1 - 0.8 * (static_cast<double>(step_size - step_idx) / step_size));
            }
            Entity* e = new_entity("AnchorNote");
            e->set_num("#BEAT", num_to_json(note->beat));
            e->set_ref("#TIMESCALE_GROUP", group_entity(note->timeScaleGroup, note->beat));
            e->set_num("lane", num_to_json(note->lane));
            e->set_num("size", num_to_json(note->size));
            e->set_num("direction", Json::integer(0));
            e->set_num("isAttached", Json::integer(0));
            e->set_num("connectorEase", Json::integer(connector_ease(note->ease)));
            e->set_num("isSeparator", Json::integer(is_separator));
            e->set_num("segmentKind", Json::integer(guide_color_num(guide->color)));
            e->set_num("segmentAlpha", segment_alpha);
            e->set_num("segmentLayer", Json::integer(opts.use_guide_layer ? 1 : 0));
            entities.push_back(e);
            if (head_note_entity == nullptr) head_note_entity = e;
            if (prev_note_entity != nullptr) {
                Entity* connector_entity = new_entity("Connector");
                connector_entity->set_ref("head", prev_note_entity);
                connector_entity->set_ref("tail", e);
                entities.push_back(connector_entity);
                connectors.push_back(connector_entity);
                prev_note_entity->set_ref("next", e);
            }
            prev_note_entity = e;
            step_idx += 1;
        }
        for (Entity* connector_entity : connectors) {
            connector_entity->set_ref("segmentHead", head_note_entity);
            connector_entity->set_ref("segmentTail", prev_note_entity);
            connector_entity->set_ref("activeHead", head_note_entity);
            connector_entity->set_ref("activeTail", prev_note_entity);
        }
        if (!opts.smooth_guide_fade && keys.empty()) {
            if (guide->fade == "in")
                head_note_entity->set_num("segmentAlpha", Json::integer(0));
            else if (guide->fade == "out")
                prev_note_entity->set_num("segmentAlpha", Json::integer(0));
            // "none": pass
        }
    }

    // SimLines
    std::vector<Entity*> sorted_notes = sim_line_eligible_notes;
    std::stable_sort(sorted_notes.begin(), sorted_notes.end(), [](Entity* a, Entity* b) {
        double ab = a->get_num("#BEAT").as_float(), bb = b->get_num("#BEAT").as_float();
        if (ab != bb) return ab < bb;
        return a->get_num("lane").as_float() < b->get_num("lane").as_float();
    });
    std::vector<std::vector<Entity*>> groups;
    std::vector<Entity*> last_group;
    for (Entity* note_entity : sorted_notes) {
        if (last_group.empty() ||
            std::abs(note_entity->get_num("#BEAT").as_float() - last_group[0]->get_num("#BEAT").as_float()) < 1e-2) {
            last_group.push_back(note_entity);
        } else {
            groups.push_back(last_group);
            last_group = {note_entity};
        }
    }
    if (!last_group.empty()) groups.push_back(last_group);
    for (auto& group : groups) {
        for (std::size_t i = 0; i + 1 < group.size(); ++i) {
            Entity* e = new_entity("SimLine");
            e->set_ref("left", group[i]);
            e->set_ref("right", group[i + 1]);
            entities.push_back(e);
        }
    }

    // build leveldata json
    Json ents = Json::array();
    for (Entity* e : entities) {
        Json eo = Json::object();
        eo["name"] = e->name;
        eo["archetype"] = e->archetype;
        Json darr = Json::array();
        for (auto& kv : e->data) {
            Json d = Json::object();
            d["name"] = kv.first;
            if (kv.second.is_ref)
                d["ref"] = kv.second.ref->name;
            else
                d["value"] = kv.second.num;
            darr.push_back(d);
        }
        eo["data"] = darr;
        ents.push_back(eo);
    }
    Json leveldata = Json::object();
    leveldata["bgmOffset"] = num_to_json(score.metadata->waveoffset);
    leveldata["entities"] = ents;

    return {detail::dump_level_data(leveldata, opts.compressed), std::move(score)};
}

}  // namespace rg::converters::leveldata::next_sekai
