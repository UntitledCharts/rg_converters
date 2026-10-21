#include <map>
#include <optional>
#include <set>
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

static optional<std::string> direction_map(long long v) {
    if (v == 0) return std::nullopt;
    if (v == 1) return std::string("left");
    if (v == 2) return std::string("right");
    return std::nullopt;
}

static std::string ease_map(long long v) {
    if (v == 0) return "linear";
    if (v == 1) return "out";
    if (v == 2) return "in";
    return "linear";
}

static Num tick_to_beat(double ticks) {
    return Num::F(util::py_round(ticks / TICKS_PER_BEAT, 6));
}

static std::pair<Num, Num> convert_lane(double lane_start, double lane_end) {
    double lane = (lane_start + lane_end) / 2.0 - 5.5;
    double size = (lane_end - lane_start + 1) / 2.0;
    return {Num::F(lane), Num::F(size)};
}

static long long get_int(const Json& o, const std::string& k, long long dflt) {
    const Json* p = o.find(k);
    return p ? p->as_int() : dflt;
}
static bool get_bool(const Json& o, const std::string& k, bool dflt) {
    const Json* p = o.find(k);
    return p ? p->as_bool() : dflt;
}

Json load_raw(const std::string& raw) {
    // base64(gzip(json)) (server format), raw gzip(json), or plain json
    try {
        return util::json_parse(util::gzip_decompress(util::b64decode(raw)));
    } catch (...) {
    }
    try {
        return util::json_parse(util::gzip_decompress(raw));
    } catch (...) {
    }
    return util::json_parse(raw);
}

static void build_single(const Json& n, std::vector<NodePtr>& notes);
static void build_slide(const std::vector<const Json*>& chain, std::vector<NodePtr>& notes);
static void build_guide(const std::vector<const Json*>& chain, std::vector<NodePtr>& notes);

core::Score load_bytes(const std::string& raw) {
    Json pjsk = load_raw(raw);

    auto metadata = std::make_shared<MetaData>();
    metadata->title = "";
    metadata->artist = "";
    metadata->designer = "";
    metadata->waveoffset = Num::I(0);
    metadata->requests = std::vector<std::string>{"ticks_per_beat 480"};

    std::vector<NodePtr> notes;
    std::shared_ptr<TimeScaleGroup> tsg = nullptr;

    const Json* mel = pjsk.find("MusicScoreEventDataList");
    if (mel) {
        for (auto& event : mel->arr()) {
            long long event_type = event.at("eventType").as_int();
            double ticks = static_cast<double>(event.at("ticks").as_int());
            Num beat = tick_to_beat(ticks);
            const Json& value = event.at("changeValue");

            if (event_type == 0) {
                notes.push_back(std::make_shared<Bpm>(beat, Num::F(value.as_float())));
            } else if (event_type == 1) {
                if (tsg == nullptr) {
                    tsg = std::make_shared<TimeScaleGroup>();
                    notes.push_back(tsg);
                }
                tsg->append(std::make_shared<TimeScalePoint>(beat, Num::F(value.as_float())));
            } else if (event_type == 2) {
                notes.push_back(std::make_shared<Volume>(beat, Num::F(value.as_float())));
            }
        }
    }

    bool any_bpm = false;
    for (auto& n : notes)
        if (n->kind == Kind::Bpm) {
            any_bpm = true;
            break;
        }
    if (!any_bpm)
        notes.insert(notes.begin(), std::make_shared<Bpm>(Num::F(0.0), Num::F(120.0)));

    std::vector<const Json*> note_list;
    const Json* nl = pjsk.find("NoteList");
    if (nl)
        for (auto& n : nl->arr()) note_list.push_back(&n);

    std::map<long long, const Json*> notes_by_id;
    for (const Json* n : note_list) notes_by_id[n->at("id").as_int()] = n;
    std::set<long long> consumed;

    std::vector<const Json*> chain_heads;
    for (const Json* n : note_list) {
        if (get_bool(*n, "IsConnectedFirst", false) && n->at("nextConnectionId").as_int() != -1)
            chain_heads.push_back(n);
    }

    for (const Json* head : chain_heads) {
        std::vector<const Json*> chain{head};
        consumed.insert(head->at("id").as_int());
        const Json* cur = head;
        while (cur->at("nextConnectionId").as_int() != -1) {
            long long next_id = cur->at("nextConnectionId").as_int();
            auto it = notes_by_id.find(next_id);
            if (it == notes_by_id.end()) break;
            cur = it->second;
            chain.push_back(cur);
            consumed.insert(cur->at("id").as_int());
        }

        if (chain.size() < 2) continue;

        long long cat = head->at("category").as_int();
        bool is_guide = (cat == 9 || cat == 10 || cat == 11);

        if (is_guide)
            build_guide(chain, notes);
        else
            build_slide(chain, notes);
    }

    for (const Json* n : note_list) {
        if (consumed.count(n->at("id").as_int())) continue;
        if (!get_bool(*n, "IsSingle", true)) continue;
        build_single(*n, notes);
    }

    auto metadata_notes = core::Score(metadata, notes);
    metadata_notes.sort_by_beat();
    return metadata_notes;
}

static void build_single(const Json& n, std::vector<NodePtr>& notes) {
    long long category = n.at("category").as_int();
    long long note_type = n.at("type").as_int();
    double ticks = static_cast<double>(n.at("ticks").as_int());
    Num beat = tick_to_beat(ticks);
    auto [lane, size] = convert_lane(static_cast<double>(n.at("laneStart").as_int()),
                                     static_cast<double>(n.at("laneEnd").as_int()));
    bool critical = note_type == 1;
    long long direction_val = get_int(n, "direction", 0);
    Num speed_ratio = get_num_or(n, "speedRatio", 1.0);

    if (category == 14) return;

    auto mk = [&](bool trace, optional<std::string> direction) {
        auto s = std::make_shared<Single>();
        s->beat = beat;
        s->critical = critical;
        s->lane = lane;
        s->size = size;
        s->timeScaleGroup = 0;
        s->speedRatio = speed_ratio;
        s->trace = trace;
        s->direction = direction;
        return s;
    };

    if (category == 0) {
        notes.push_back(mk(false, std::nullopt));
    } else if (category == 3) {
        auto direction = direction_map(direction_val);
        if (!direction.has_value()) direction = std::string("up");
        notes.push_back(mk(false, direction));
    } else if (category == 4 || category == 5) {
        notes.push_back(mk(true, std::nullopt));
    } else if (category == 8) {
        auto direction = direction_map(direction_val);
        if (!direction.has_value()) direction = std::string("up");
        notes.push_back(mk(true, direction));
    }
}

static void build_slide(const std::vector<const Json*>& chain, std::vector<NodePtr>& notes) {
    const Json* head = chain[0];
    bool critical = head->at("type").as_int() == 1;
    std::vector<NodePtr> connections;

    for (std::size_t i = 0; i < chain.size(); ++i) {
        const Json& n = *chain[i];
        Num beat = tick_to_beat(static_cast<double>(n.at("ticks").as_int()));
        auto [lane, size] = convert_lane(static_cast<double>(n.at("laneStart").as_int()),
                                         static_cast<double>(n.at("laneEnd").as_int()));
        std::string ease = ease_map(get_int(n, "noteLineType", 0));
        long long category = n.at("category").as_int();
        long long note_type = n.at("type").as_int();
        Num speed_ratio = get_num_or(n, "speedRatio", 1.0);

        if (i == 0) {
            std::string judge_type;
            if (category == 5 || category == 7)
                judge_type = "none";
            else if (category == 4 || category == 6)
                judge_type = "trace";
            else
                judge_type = "normal";

            auto p = std::make_shared<SlideStartPoint>();
            p->beat = beat;
            p->critical = note_type == 1;
            p->ease = ease;
            p->judgeType = judge_type;
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            p->speedRatio = speed_ratio;
            connections.push_back(p);
        } else if (i == chain.size() - 1) {
            long long direction_val = get_int(n, "direction", 0);
            auto direction = direction_map(direction_val);

            std::string judge_type;
            if (category == 5)
                judge_type = "none";
            else if (category == 4 || category == 8)
                judge_type = "trace";
            else
                judge_type = "normal";

            if ((category == 3 || category == 8) && !direction.has_value())
                direction = std::string("up");

            auto p = std::make_shared<SlideEndPoint>();
            p->beat = beat;
            p->critical = note_type == 1;
            p->judgeType = judge_type;
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            p->speedRatio = speed_ratio;
            p->direction = direction;
            connections.push_back(p);
        } else {
            optional<bool> relay_critical;
            if (category == 13)
                relay_critical = std::nullopt;
            else
                relay_critical = note_type == 1;

            // Normal (category != 13, not skip): changes shape + adds combo (tick + critical)
            // Hidden (category 13): changes shape, no combo (tick + critical=None)
            // Skip (isSkip=true): no shape change, adds combo (attach + critical)
            bool is_skip = get_bool(n, "isSkip", false);

            auto p = std::make_shared<SlideRelayPoint>();
            p->beat = beat;
            p->ease = ease;
            p->lane = lane;
            p->size = size;
            p->timeScaleGroup = 0;
            p->type = is_skip ? "attach" : "tick";
            p->critical = relay_critical;
            p->speedRatio = speed_ratio;
            connections.push_back(p);
        }
    }

    auto slide = std::make_shared<Slide>(critical, false, connections);
    slide->sort();
    notes.push_back(slide);
}

static void build_guide(const std::vector<const Json*>& chain, std::vector<NodePtr>& notes) {
    const Json* head = chain[0];
    bool critical = head->at("type").as_int() == 1;
    std::string color = critical ? "yellow" : "green";

    std::vector<shared_ptr<GuidePoint>> midpoints;
    for (const Json* np : chain) {
        const Json& n = *np;
        Num beat = tick_to_beat(static_cast<double>(n.at("ticks").as_int()));
        auto [lane, size] = convert_lane(static_cast<double>(n.at("laneStart").as_int()),
                                         static_cast<double>(n.at("laneEnd").as_int()));
        std::string ease = ease_map(get_int(n, "noteLineType", 0));

        auto p = std::make_shared<GuidePoint>();
        p->beat = beat;
        p->ease = ease;
        p->lane = lane;
        p->size = size;
        p->timeScaleGroup = 0;
        p->speedRatio = get_num_or(n, "speedRatio", 1.0);
        midpoints.push_back(p);
    }

    auto guide = std::make_shared<Guide>(color, "none", midpoints);
    notes.push_back(guide);
}

}  // namespace rg::converters::pjsk
