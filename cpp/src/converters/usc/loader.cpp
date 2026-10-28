#include <string>

#include "rg/converters/usc.hpp"
#include "rg/core/jsonutil.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/json.hpp"

namespace rg::converters::usc {

using namespace rg::core;
using rg::util::Json;
using rg::util::json_parse;

// No group, or null, is the first, as uscToLevelData has it (`tsg:${timeScaleGroup ?? 0}`).
static long long group_of(const Json& obj) {
    const Json* group = obj.find("timeScaleGroup");
    return group && !group->is_null() ? group->as_int() : 0;
}

core::Score load_text(const std::string& json_text) {
    Json usc = json_parse(json_text);
    const Json& uscobj = usc.at("usc");

    auto metadata = std::make_shared<MetaData>();
    metadata->title = "";
    metadata->artist = "";
    metadata->designer = "";
    metadata->waveoffset = json_to_num(uscobj.at("offset"));
    metadata->requests = std::vector<std::string>{"ticks_per_beat 480"};

    std::vector<NodePtr> notelist;
    bool has_bpm = false;
    for (auto& obj : uscobj.at("objects").arr()) {
        std::string type = obj.at("type").as_str();
        if (type == "feverChance" || type == "feverStart" || type == "skill") {
            Num beat = round6(json_to_num(obj.at("beat")));
            if (type == "skill")
                notelist.push_back(std::make_shared<Skill>(beat));
            else if (type == "feverChance")
                notelist.push_back(std::make_shared<FeverChance>(beat));
            else
                notelist.push_back(std::make_shared<FeverStart>(beat));
        } else if (type == "bpm") {
            notelist.push_back(std::make_shared<Bpm>(round6(json_to_num(obj.at("beat"))),
                                                     json_to_num(obj.at("bpm"))));
            has_bpm = true;

        } else if (type == "timeScaleGroup") {
            auto group = std::make_shared<TimeScaleGroup>();
            for (auto& timescale : obj.at("changes").arr()) {
                group->append(std::make_shared<TimeScalePoint>(
                    round6(json_to_num(timescale.at("beat"))),
                    json_to_num(timescale.at("timeScale"))));
            }
            notelist.push_back(group);

        } else if (type == "single" || type == "damage") {
            if (obj.contains("direction") && type == "single") {
                auto s = std::make_shared<Single>();
                s->beat = round6(json_to_num(obj.at("beat")));
                s->critical = obj.at("critical").as_bool();
                s->lane = json_to_num(obj.at("lane"));
                s->size = json_to_num(obj.at("size"));
                s->timeScaleGroup = group_of(obj);
                s->speedRatio = get_num_or(obj, "speedRatio", 1.0);
                s->trace = obj.at("trace").as_bool();
                s->direction = obj.at("direction").as_str();
                s->type = "single";
                notelist.push_back(s);
            } else if (type == "damage") {
                auto s = std::make_shared<Single>();
                s->beat = round6(json_to_num(obj.at("beat")));
                s->lane = json_to_num(obj.at("lane"));
                s->size = json_to_num(obj.at("size"));
                s->timeScaleGroup = group_of(obj);
                s->speedRatio = get_num_or(obj, "speedRatio", 1.0);
                s->type = type;
                notelist.push_back(s);
            } else {
                auto s = std::make_shared<Single>();
                s->beat = round6(json_to_num(obj.at("beat")));
                s->critical = obj.at("critical").as_bool();
                s->lane = json_to_num(obj.at("lane"));
                s->size = json_to_num(obj.at("size"));
                s->timeScaleGroup = group_of(obj);
                s->speedRatio = get_num_or(obj, "speedRatio", 1.0);
                s->trace = obj.at("trace").as_bool();
                notelist.push_back(s);
            }

        } else if (type == "slide") {
            auto slide = std::make_shared<Slide>(obj.at("critical").as_bool());
            for (auto& point : obj.at("connections").arr()) {
                std::string ptype = point.at("type").as_str();
                if (ptype == "start") {
                    auto p = std::make_shared<SlideStartPoint>();
                    p->beat = round6(json_to_num(point.at("beat")));
                    p->critical = point.at("critical").as_bool();
                    p->ease = point.at("ease").as_str();
                    p->judgeType = point.at("judgeType").as_str();
                    p->lane = json_to_num(point.at("lane"));
                    p->size = json_to_num(point.at("size"));
                    p->timeScaleGroup = group_of(point);
                    p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                    slide->append(p);
                } else if (ptype == "tick" || ptype == "attach") {
                    if (point.contains("critical")) {
                        auto p = std::make_shared<SlideRelayPoint>();
                        p->beat = round6(json_to_num(point.at("beat")));
                        p->ease = point.at("ease").as_str();
                        p->lane = json_to_num(point.at("lane"));
                        p->size = json_to_num(point.at("size"));
                        p->timeScaleGroup = group_of(point);
                        p->type = point.at("type").as_str();
                        p->critical = point.at("critical").as_bool();
                        p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                        slide->append(p);
                    } else {
                        auto p = std::make_shared<SlideRelayPoint>();
                        p->beat = round6(json_to_num(point.at("beat")));
                        p->ease = point.at("ease").as_str();
                        p->lane = json_to_num(point.at("lane"));
                        p->size = json_to_num(point.at("size"));
                        p->timeScaleGroup = group_of(point);
                        p->type = point.at("type").as_str();
                        p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                        slide->append(p);
                    }
                } else if (ptype == "end") {
                    if (point.contains("direction")) {
                        auto p = std::make_shared<SlideEndPoint>();
                        p->beat = round6(json_to_num(point.at("beat")));
                        p->critical = point.at("critical").as_bool();
                        p->judgeType = point.at("judgeType").as_str();
                        p->lane = json_to_num(point.at("lane"));
                        p->size = json_to_num(point.at("size"));
                        p->timeScaleGroup = group_of(point);
                        p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                        p->direction = point.at("direction").as_str();
                        slide->append(p);
                    } else {
                        auto p = std::make_shared<SlideEndPoint>();
                        p->beat = round6(json_to_num(point.at("beat")));
                        p->critical = point.at("critical").as_bool();
                        p->judgeType = point.at("judgeType").as_str();
                        p->lane = json_to_num(point.at("lane"));
                        p->size = json_to_num(point.at("size"));
                        p->timeScaleGroup = group_of(point);
                        p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                        slide->append(p);
                    }
                }
            }
            notelist.push_back(slide);

        } else if (type == "guide") {
            auto guide = std::make_shared<Guide>(obj.at("color").as_str(),
                                                 obj.at("fade").as_str());
            for (auto& point : obj.at("midpoints").arr()) {
                auto p = std::make_shared<GuidePoint>();
                p->beat = round6(json_to_num(point.at("beat")));
                p->ease = point.at("ease").as_str();
                p->lane = json_to_num(point.at("lane"));
                p->size = json_to_num(point.at("size"));
                p->timeScaleGroup = group_of(point);
                p->speedRatio = get_num_or(point, "speedRatio", 1.0);
                guide->append(p);
            }
            notelist.push_back(guide);
        }
    }

    if (!has_bpm)
        notelist.insert(notelist.begin(),
                        std::make_shared<Bpm>(Num::I(0), Num::F(160.0)));

    return core::Score(metadata, notelist);
}

}  // namespace rg::converters::usc
