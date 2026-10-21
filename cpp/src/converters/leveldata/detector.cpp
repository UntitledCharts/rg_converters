#include <set>
#include <string>

#include "rg/converters/leveldata/detect.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pygzip.hpp"

namespace rg::converters::leveldata {

using rg::util::Json;

namespace {

const std::set<std::string>& chcy_archetypes() {
    static const std::set<std::string> s = {
        "Initialization", "Stage", "NormalTapNote", "CriticalTapNote", "NormalFlickNote",
        "CriticalFlickNote", "NormalTraceNote", "CriticalTraceNote", "NormalTraceFlickNote",
        "CriticalTraceFlickNote", "NormalSlideStartNote", "CriticalSlideStartNote",
        "NormalSlideEndNote", "CriticalSlideEndNote", "NormalSlideEndFlickNote",
        "CriticalSlideEndFlickNote", "IgnoredSlideTickNote", "NormalSlideTickNote",
        "CriticalSlideTickNote", "HiddenSlideTickNote", "NormalAttachedSlideTickNote",
        "CriticalAttachedSlideTickNote", "NormalSlideConnector", "CriticalSlideConnector",
        "SimLine",
        // Chcy trace implementation
        "NormalTraceSlideStartNote", "CriticalTraceSlideStartNote", "NormalTraceSlideEndNote",
        "CriticalTraceSlideEndNote", "NonDirectionalTraceFlickNote",
        // Chcy extend
        "DamageNote", "HiddenSlideStartNote", "TimeScaleGroup", "TimeScaleChange", "Guide"};
    return s;
}

const std::set<std::string>& pysekai_archetypes() {
    static const std::set<std::string> s = {
        "#TIMESCALE_CHANGE", "#TIMESCALE_GROUP", "Initialization", "Stage", "SimLine",
        "Connector", "NormalTapNote", "CriticalTapNote", "NormalFlickNote",
        "CriticalFlickNote", "NormalTraceNote", "CriticalTraceNote", "NormalTraceFlickNote",
        "CriticalTraceFlickNote", "NormalReleaseNote", "CriticalReleaseNote",
        "NormalHeadTapNote", "CriticalHeadTapNote", "NormalHeadFlickNote",
        "CriticalHeadFlickNote", "NormalHeadTraceNote", "CriticalHeadTraceNote",
        "NormalHeadTraceFlickNote", "CriticalHeadTraceFlickNote", "NormalHeadReleaseNote",
        "CriticalHeadReleaseNote", "NormalTailTapNote", "CriticalTailTapNote",
        "NormalTailFlickNote", "CriticalTailFlickNote", "NormalTailTraceNote",
        "CriticalTailTraceNote", "NormalTailTraceFlickNote", "CriticalTailTraceFlickNote",
        "NormalTailReleaseNote", "CriticalTailReleaseNote", "NormalTickNote",
        "CriticalTickNote", "DamageNote", "AnchorNote", "TransientHiddenTickNote",
        "FakeNormalTapNote", "FakeCriticalTapNote", "FakeNormalFlickNote",
        "FakeCriticalFlickNote", "FakeNormalTraceNote", "FakeCriticalTraceNote",
        "FakeNormalTraceFlickNote", "FakeCriticalTraceFlickNote", "FakeNormalReleaseNote",
        "FakeCriticalReleaseNote", "FakeNormalHeadTapNote", "FakeCriticalHeadTapNote",
        "FakeNormalHeadFlickNote", "FakeCriticalHeadFlickNote", "FakeNormalHeadTraceNote",
        "FakeCriticalHeadTraceNote", "FakeNormalHeadTraceFlickNote",
        "FakeCriticalHeadTraceFlickNote", "FakeNormalHeadReleaseNote",
        "FakeCriticalHeadReleaseNote", "FakeNormalTailTapNote", "FakeCriticalTailTapNote",
        "FakeNormalTailFlickNote", "FakeCriticalTailFlickNote", "FakeNormalTailTraceNote",
        "FakeCriticalTailTraceNote", "FakeNormalTailTraceFlickNote",
        "FakeCriticalTailTraceFlickNote", "FakeNormalTailReleaseNote",
        "FakeCriticalTailReleaseNote", "FakeNormalTickNote", "FakeCriticalTickNote",
        "FakeDamageNote", "FakeAnchorNote", "FakeTransientHiddenTickNote"};
    return s;
}

}  // namespace

std::optional<std::string> detect_parsed(const Json& level_data) {
    try {
        if (!level_data.contains("bgmOffset")) return std::nullopt;
        const Json* ents = level_data.find("entities");
        if (!ents || !ents->is_array()) return std::nullopt;
        std::set<std::string> archetypes;
        for (auto& ent : ents->arr()) {
            const Json& a = ent.at("archetype");
            archetypes.insert(a.type() == Json::Type::Str ? a.as_str() : std::string());
        }
        const auto& chcy = chcy_archetypes();
        const auto& pysk = pysekai_archetypes();

        for (auto& a : archetypes)
            if (chcy.count(a) && !pysk.count(a)) return std::string("chcy");
        for (auto& a : archetypes)
            if (pysk.count(a) && !chcy.count(a)) return std::string("pysekai");
        bool all_not_shared = true;
        for (auto& a : archetypes)
            if (chcy.count(a) && pysk.count(a)) {
                all_not_shared = false;
                break;
            }
        if (all_not_shared) return std::nullopt;
        return std::string("pysekai");
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> detect(const std::string& data, bool skip_gzip, bool skip_json) {
    std::string buf = data;
    if (!skip_gzip) {
        try {
            buf = rg::util::gzip_decompress(data);
        } catch (...) {
            return std::nullopt;
        }
    }
    Json level_data;
    if (!skip_json) {
        try {
            level_data = rg::util::json_parse(buf);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        // skip_json used when data is not parsed -> Python raises ValueError
        throw std::runtime_error("skip_json used when data is not parsed");
    }
    return detect_parsed(level_data);
}

}  // namespace rg::converters::leveldata
