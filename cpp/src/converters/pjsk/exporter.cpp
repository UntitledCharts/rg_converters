#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rg/converters/pjsk.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/base64.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::pjsk {

using namespace rg::core;
using rg::util::Json;

static const long long TICKS_PER_BEAT = 480;

static long long beat_to_ticks(double beat) {
    return util::py_round_int(beat * TICKS_PER_BEAT);
}

static Num sanitize_speed_ratio(Num speed_ratio) {
    if (!std::isfinite(speed_ratio.v) || speed_ratio.v <= 0.0) return Num::F(1.0);
    return speed_ratio;
}

static std::pair<long long, long long> unconvert_lane(double lane, double size) {
    long long lane_start = static_cast<long long>(std::floor(lane - size + 5.5 + 0.5));
    long long lane_end = static_cast<long long>(std::floor(lane + size + 5.5 - 1 + 0.5));
    lane_start = std::max(0LL, std::min(11LL, lane_start));
    lane_end = std::max(0LL, std::min(11LL, lane_end));
    return {lane_start, lane_end};
}

static long long dir_rev(const std::optional<std::string>& d) {
    if (!d.has_value()) return 0;
    if (*d == "left") return 1;
    if (*d == "up") return 0;
    if (*d == "right") return 2;
    return 0;
}

static long long ease_rev(const std::string& e) {
    if (e == "out") return 1;
    if (e == "in") return 2;
    return 0;
}

static bool has_ease(const Node* n) {
    return n->kind == Kind::SlideStartPoint || n->kind == Kind::SlideRelayPoint ||
           n->kind == Kind::GuidePoint;
}
static std::string point_ease(const Node* n) {
    switch (n->kind) {
        case Kind::SlideStartPoint: return static_cast<const SlideStartPoint*>(n)->ease;
        case Kind::SlideRelayPoint: return static_cast<const SlideRelayPoint*>(n)->ease;
        case Kind::GuidePoint: return static_cast<const GuidePoint*>(n)->ease;
        default: return "linear";
    }
}

static long long get_note_base_type(long long category, bool is_start, bool is_end,
                                    bool is_single) {
    // NoteBaseType: Base=0, Normal=1, Long=2, Flick=3, FrictionFlick=4,
    // Connection=5, HiddenConnection=6, LongHoldCombo=7, FrictionLong=8,
    // FrictionHideLong=9, Guide=10, Friction=11, FrictionHide=12,
    // GuideEnd=13, GuideHiddenConnection=14
    if (is_single) {
        switch (category) {
            case 0: return 1;   // Normal
            case 3: return 3;   // Flick
            case 4: return 11;  // Friction
            case 5: return 12;  // FrictionHide
            case 8: return 4;   // FrictionFlick
        }
        return 1;
    }
    if (is_start) {
        switch (category) {
            case 1: return 2;   // Long
            case 6: return 8;   // FrictionLong
            case 7: return 9;   // FrictionHideLong
            case 9: return 10;  // Guide
        }
        return 2;
    }
    if (is_end) {
        switch (category) {
            case 0:
            case 1: return 1;   // Normal
            case 3: return 3;   // Flick
            case 4: return 11;  // Friction
            case 5: return 12;  // FrictionHide
            case 8: return 4;   // FrictionFlick
            case 10: return 13; // GuideEnd
        }
        return 1;
    }
    // mid
    switch (category) {
        case 2: return 5;    // Connection
        case 13: return 6;   // HiddenConnection
        case 11: return 14;  // GuideHiddenConnection
    }
    return 5;
}

core::ExportResult export_bytes(const core::Score& score_in, const ExportOptions& opts) {
    if (!opts.music_id.has_value()) throw std::invalid_argument("pjsk export needs a music_id");
    core::Score score = core::deep_copy(score_in);
    score.close_open_slides();
    score.delete_fake_notes();
    score.delete_damage_notes();
    score.replace_extended_ease();
    score.replace_extended_guide_colors();
    limit_to_one_layer(score, "pjsk", opts.strip_layers, "Set the strip_layers export option to strip them",
                       false);
    long long id_counter = 0;
    long long ref_counter = 1;
    auto next_id = [&]() -> long long { return ++id_counter; };
    auto next_ref = [&]() -> std::string { return std::to_string(++ref_counter); };

    // -- EVENTS --
    Json event_list = Json::array();

    for (auto& note_sp : score.notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Bpm) {
            Json e = Json::object();
            e["$id"] = next_ref();
            e["id"] = Json::integer(next_id());
            e["eventType"] = Json::integer(0);
            e["ticks"] = Json::integer(beat_to_ticks(static_cast<Bpm*>(note)->beat));
            e["changeValue"] = num_to_json(static_cast<Bpm*>(note)->bpm);
            event_list.push_back(e);
        } else if (note->kind == Kind::TimeScaleGroup) {
            for (auto& point : static_cast<TimeScaleGroup*>(note)->changes) {
                Json e = Json::object();
                e["$id"] = next_ref();
                e["id"] = Json::integer(next_id());
                e["eventType"] = Json::integer(1);
                e["ticks"] = Json::integer(beat_to_ticks(point->beat));
                e["changeValue"] = num_to_json(point->timeScale);
                event_list.push_back(e);
            }
        } else if (note->kind == Kind::Volume) {
            Json e = Json::object();
            e["$id"] = next_ref();
            e["id"] = Json::integer(next_id());
            e["eventType"] = Json::integer(2);
            e["ticks"] = Json::integer(beat_to_ticks(static_cast<Volume*>(note)->beat));
            e["changeValue"] = num_to_json(static_cast<Volume*>(note)->volume);
            event_list.push_back(e);
        }
    }

    bool has_time_sig = false;
    for (auto& e : event_list.arr())
        if (e.at("eventType").as_int() == 3) {
            has_time_sig = true;
            break;
        }
    if (!has_time_sig) {
        Json e = Json::object();
        e["$id"] = next_ref();
        e["id"] = Json::integer(next_id());
        e["eventType"] = Json::integer(3);
        e["ticks"] = Json::integer(0);
        e["changeValue"] = std::string("4/4");
        event_list.push_back(e);
    }

    bool has_se_vol = false;
    for (auto& e : event_list.arr())
        if (e.at("eventType").as_int() == 2) {
            has_se_vol = true;
            break;
        }
    if (!has_se_vol) {
        Json e = Json::object();
        e["$id"] = next_ref();
        e["id"] = Json::integer(next_id());
        e["eventType"] = Json::integer(2);
        e["ticks"] = Json::integer(0);
        e["changeValue"] = Json(1.0);
        event_list.push_back(e);
    }

    // -- NOTES --
    Json note_dicts = Json::array();
    long long max_ticks = 0;

    auto make_note = [&](long long ticks, long long lane_start, long long lane_end,
                         long long category, long long note_type, long long note_line_type,
                         long long note_base_type, long long direction, Num speed_ratio,
                         bool is_skip, long long prev_id, long long next_id_val) -> Json {
        long long nid = next_id();
        if (ticks > max_ticks) max_ticks = ticks;
        Json o = Json::object();
        o["$id"] = next_ref();
        o["id"] = Json::integer(nid);
        o["ticks"] = Json::integer(ticks);
        o["laneStart"] = Json::integer(lane_start);
        o["laneEnd"] = Json::integer(lane_end);
        o["category"] = Json::integer(category);
        o["type"] = Json::integer(note_type);
        o["speedRatio"] = num_to_json(speed_ratio);
        o["noteLineType"] = Json::integer(note_line_type);
        o["noteBaseType"] = Json::integer(note_base_type);
        o["previousConnectionId"] = Json::integer(prev_id);
        o["nextConnectionId"] = Json::integer(next_id_val);
        o["direction"] = Json::integer(direction);
        o["isSkip"] = Json(is_skip);
        o["IsSingle"] = Json(prev_id == -1 && next_id_val == -1);
        o["IsConnectedFirst"] = Json(prev_id == -1 && next_id_val != -1);
        o["IsConnectedLast"] = Json(prev_id != -1 && next_id_val == -1);
        return o;
    };

    for (auto& note_sp : score.notes) {
        Node* note = note_sp.get();
        if (note->kind == Kind::Bpm || note->kind == Kind::TimeScaleGroup ||
            note->kind == Kind::Volume || note->kind == Kind::Skill ||
            note->kind == Kind::FeverChance || note->kind == Kind::FeverStart)
            continue;

        if (note->kind == Kind::Single) {
            auto* s = static_cast<Single*>(note);
            long long ticks = beat_to_ticks(s->beat);
            auto [lane_start, lane_end] = unconvert_lane(s->lane, s->size);
            long long critical = (s->critical.has_value() && s->critical.value()) ? 1 : 0;
            long long direction = dir_rev(s->direction);

            long long category;
            bool trace = s->trace.has_value() && s->trace.value();
            bool dir = s->direction.has_value();
            if (trace && dir)
                category = 8;  // FrictionFlick
            else if (trace)
                category = 4;  // Friction
            else if (dir)
                category = 3;  // Flick
            else
                category = 0;  // Normal

            long long note_base_type = get_note_base_type(category, false, false, true);
            note_dicts.push_back(make_note(ticks, lane_start, lane_end, category, critical,
                                           0, note_base_type, direction,
                                           sanitize_speed_ratio(s->speedRatio), false, -1,
                                           -1));

        } else if (note->kind == Kind::Slide) {
            auto* sl = static_cast<Slide*>(note);
            std::vector<Json> chain_notes;
            long long n = static_cast<long long>(sl->connections.size());
            std::vector<long long> reserved_ids;
            for (long long k = 0; k < n; ++k) reserved_ids.push_back(id_counter + 1 + k);
            Num hold_speed_ratio = sanitize_speed_ratio(
                sl->connections.empty() ? Num::F(1.0) : [&]() {
                    Node* c0 = sl->connections[0].get();
                    switch (c0->kind) {
                        case Kind::SlideStartPoint: return static_cast<SlideStartPoint*>(c0)->speedRatio;
                        case Kind::SlideRelayPoint: return static_cast<SlideRelayPoint*>(c0)->speedRatio;
                        case Kind::SlideEndPoint: return static_cast<SlideEndPoint*>(c0)->speedRatio;
                        default: return Num::F(1.0);
                    }
                }());

            for (long long i = 0; i < n; ++i) {
                Node* conn = sl->connections[static_cast<std::size_t>(i)].get();
                long long ticks = beat_to_ticks(node_beat(conn));
                auto [lane_start, lane_end] = unconvert_lane(node_lane(conn), node_size(conn));
                long long note_line_type =
                    ease_rev(has_ease(conn) ? point_ease(conn) : "linear");

                long long category, note_base_type, direction, critical;
                bool is_skip;
                long long prev_conn, next_conn;

                if (i == 0) {
                    auto* p = static_cast<SlideStartPoint*>(conn);
                    critical = p->critical ? 1 : 0;
                    if (p->judgeType == "none")
                        category = 7;  // FrictionHideLong
                    else if (p->judgeType == "trace")
                        category = 6;  // FrictionLong
                    else
                        category = 1;  // Long
                    note_base_type = get_note_base_type(category, true, false, false);
                    direction = 0;
                    is_skip = false;
                    prev_conn = -1;
                    next_conn = (i + 1 < n) ? reserved_ids[static_cast<std::size_t>(i + 1)] : -1;
                } else if (i == n - 1) {
                    auto* p = static_cast<SlideEndPoint*>(conn);
                    critical = p->critical ? 1 : 0;
                    direction = dir_rev(p->direction);

                    if (p->direction.has_value() && p->judgeType == "trace")
                        category = 8;  // FrictionFlick
                    else if (p->judgeType == "trace")
                        category = 4;  // Friction
                    else if (p->judgeType == "none")
                        category = 5;  // FrictionHide
                    else if (p->direction.has_value())
                        category = 3;  // Flick
                    else if (!p->direction.has_value() && p->judgeType == "normal")
                        category = 1;  // Long (normal end, no flick/friction)
                    else
                        category = 1;  // Long

                    note_base_type = get_note_base_type(category, false, true, false);
                    is_skip = false;
                    prev_conn = reserved_ids[static_cast<std::size_t>(i - 1)];
                    next_conn = -1;
                } else {
                    auto* p = static_cast<SlideRelayPoint*>(conn);
                    critical = (p->critical.has_value() && p->critical.value()) ? 1 : 0;

                    if (!p->critical.has_value()) {
                        category = 13;  // Hidden
                        critical = sl->critical ? 1 : 0;
                    } else {
                        category = 2;  // Connection
                    }

                    note_base_type = get_note_base_type(category, false, false, false);
                    direction = 0;
                    // Skip (attach): isSkip=true, no shape change but adds combo
                    // Normal/Hidden (tick): isSkip=false, changes shape
                    is_skip = p->type == "attach";
                    prev_conn = reserved_ids[static_cast<std::size_t>(i - 1)];
                    next_conn = (i + 1 < n) ? reserved_ids[static_cast<std::size_t>(i + 1)] : -1;
                }

                long long nid = reserved_ids[static_cast<std::size_t>(i)];
                if (ticks > max_ticks) max_ticks = ticks;

                Json o = Json::object();
                o["$id"] = next_ref();
                o["id"] = Json::integer(nid);
                o["ticks"] = Json::integer(ticks);
                o["laneStart"] = Json::integer(lane_start);
                o["laneEnd"] = Json::integer(lane_end);
                o["category"] = Json::integer(category);
                o["type"] = Json::integer(critical);
                o["speedRatio"] = num_to_json(hold_speed_ratio);
                o["noteLineType"] = Json::integer(note_line_type);
                o["noteBaseType"] = Json::integer(note_base_type);
                o["previousConnectionId"] = Json::integer(prev_conn);
                o["nextConnectionId"] = Json::integer(next_conn);
                o["direction"] = Json::integer(i == n - 1 ? direction : 0);
                o["isSkip"] = Json(is_skip);
                o["IsSingle"] = Json(false);
                o["IsConnectedFirst"] = Json(i == 0);
                o["IsConnectedLast"] = Json(i == n - 1);
                chain_notes.push_back(o);
            }

            id_counter = reserved_ids.back();
            for (auto& o : chain_notes) note_dicts.push_back(o);

        } else if (note->kind == Kind::Guide) {
            auto* gd = static_cast<Guide*>(note);
            long long n = static_cast<long long>(gd->midpoints.size());
            std::vector<long long> reserved_ids;
            for (long long k = 0; k < n; ++k) reserved_ids.push_back(id_counter + 1 + k);
            Num hold_speed_ratio = sanitize_speed_ratio(
                gd->midpoints.empty() ? Num::F(1.0) : gd->midpoints[0]->speedRatio);

            for (long long i = 0; i < n; ++i) {
                GuidePoint* mp = gd->midpoints[static_cast<std::size_t>(i)].get();
                long long ticks = beat_to_ticks(mp->beat);
                auto [lane_start, lane_end] = unconvert_lane(mp->lane, mp->size);
                long long note_line_type = ease_rev(mp->ease);
                long long critical = gd->color == "yellow" ? 1 : 0;

                long long category, note_base_type;
                if (i == 0) {
                    category = 9;        // Guide
                    note_base_type = 10;  // Guide
                } else if (i == n - 1) {
                    category = 9;         // Guide (end uses same category as start)
                    note_base_type = 13;  // GuideEnd
                    note_line_type = 0;
                } else {
                    category = 11;        // GuideHidden
                    note_base_type = 14;  // GuideHiddenConnection
                }

                long long prev_conn = i > 0 ? reserved_ids[static_cast<std::size_t>(i - 1)] : -1;
                long long next_conn = (i + 1 < n) ? reserved_ids[static_cast<std::size_t>(i + 1)] : -1;

                long long nid = reserved_ids[static_cast<std::size_t>(i)];
                if (ticks > max_ticks) max_ticks = ticks;

                Json o = Json::object();
                o["$id"] = next_ref();
                o["id"] = Json::integer(nid);
                o["ticks"] = Json::integer(ticks);
                o["laneStart"] = Json::integer(lane_start);
                o["laneEnd"] = Json::integer(lane_end);
                o["category"] = Json::integer(category);
                o["type"] = Json::integer(critical);
                o["speedRatio"] = num_to_json(hold_speed_ratio);
                o["noteLineType"] = Json::integer(note_line_type);
                o["noteBaseType"] = Json::integer(note_base_type);
                o["previousConnectionId"] = Json::integer(prev_conn);
                o["nextConnectionId"] = Json::integer(next_conn);
                o["direction"] = Json::integer(0);
                o["isSkip"] = Json(false);
                o["IsSingle"] = Json(false);
                o["IsConnectedFirst"] = Json(i == 0);
                o["IsConnectedLast"] = Json(i == n - 1);
                note_dicts.push_back(o);
            }

            id_counter = reserved_ids.back();
        }
    }

    std::stable_sort(note_dicts.arr().begin(), note_dicts.arr().end(),
                     [](const Json& a, const Json& b) {
                         long long at = a.at("ticks").as_int(), bt = b.at("ticks").as_int();
                         if (at != bt) return at < bt;
                         long long al = a.at("laneStart").as_int(), bl = b.at("laneStart").as_int();
                         if (al != bl) return al < bl;
                         return a.at("id").as_int() < b.at("id").as_int();
                     });

    Json pjsk_data = Json::object();
    pjsk_data["$id"] = std::string("1");
    pjsk_data["VersionCode"] = Json::integer(10000);
    pjsk_data["MusicScoreEventDataList"] = event_list;
    pjsk_data["EventArray"] = Json::array();
    pjsk_data["NoteList"] = note_dicts;
    pjsk_data["MusicScoreTicksMax"] = Json::integer(max_ticks);
    pjsk_data["MusicId"] = Json::integer(*opts.music_id);
    pjsk_data["FullComboDataHash"] = Json(nullptr);

    std::string json_bytes = pjsk_data.dump(-1, true);
    if (!opts.compressed) return {std::move(json_bytes), std::move(score)};
    std::string gz = util::gzip_compress(json_bytes, 0, 9);
    return {util::b64encode(gz), std::move(score)};
}

}  // namespace rg::converters::pjsk
