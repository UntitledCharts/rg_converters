#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>

#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/core/model.hpp"
#include "rg/util/json.hpp"

using namespace rg;
namespace ns = rg::converters::leveldata::next_sekai;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int fails = 0;

static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++fails;
    }
}

// export -> load -> export must give back the same bytes: the loader undoes everything the
// exporter wrote.
static void round_trip(const std::string& name, const core::Score& score) {
    for (int smooth = 0; smooth < 2; ++smooth)
        for (int layer = 0; layer < 2; ++layer) {
            ns::ExportOptions eo;
            eo.as_compressed = false;
            eo.smooth_guide_fade = smooth;
            eo.use_guide_layer = layer;
            std::string first = ns::export_bytes(score, eo);
            core::Score loaded = ns::load_text(first);
            std::string second = ns::export_bytes(loaded, eo);
            check(first == second, name + " round trip smooth=" + std::to_string(smooth) +
                                       " layer=" + std::to_string(layer));
        }
    ns::ExportOptions gz;
    std::string compressed = ns::export_bytes(score, gz);
    check(ns::export_bytes(ns::load_text(compressed), gz) == compressed, name + " gzip round trip");
}

// The USC objects of a score, order aside.
static std::vector<std::string> usc_objects(core::Score score) {
    util::Json usc = util::json_parse(converters::usc::dumps(score)).at("usc");
    std::vector<std::string> out;
    for (auto& o : usc.at("objects").arr()) out.push_back(o.dump());
    std::sort(out.begin(), out.end());
    out.push_back("offset " + usc.at("offset").dump());
    return out;
}

// score -> level data -> score keeps every note as it was, but for what the format has no room
// for: a hidden (judgeType none) slide head or tail is an AnchorNote, which takes the slide's
// critical.
static void semantic_round_trip(const std::string& name, core::Score score) {
    ns::ExportOptions eo;
    eo.as_compressed = false;
    core::Score back = ns::load_text(ns::export_bytes(score, eo));
    for (auto& node : score.notes) {
        if (node->kind != core::Kind::Slide) continue;
        auto* slide = static_cast<core::Slide*>(node.get());
        for (auto& c : slide->connections) {
            if (c->kind == core::Kind::SlideStartPoint) {
                auto* p = static_cast<core::SlideStartPoint*>(c.get());
                if (p->judgeType == "none") p->critical = slide->critical;
            } else if (c->kind == core::Kind::SlideEndPoint) {
                auto* p = static_cast<core::SlideEndPoint*>(c.get());
                if (p->judgeType == "none") p->critical = slide->critical;
            }
        }
    }
    std::vector<std::string> want = usc_objects(score), got = usc_objects(back);
    std::size_t differing = 0;
    for (std::size_t i = 0; i < std::max(want.size(), got.size()); ++i)
        if (i >= want.size() || i >= got.size() || want[i] != got[i]) ++differing;
    check(differing == 0, name + " semantic round trip (" + std::to_string(differing) + " objects differ)");
}

static std::string level(const std::string& notes, const std::string& extra_top = "",
                         const std::string& ease = "0") {
    return "{\"bgmOffset\":0.25" + extra_top +
           ",\"entities\":["
           "{\"name\":\"0\",\"archetype\":\"Initialization\",\"data\":[]},"
           "{\"name\":\"1\",\"archetype\":\"#BPM_CHANGE\",\"data\":[{\"name\":\"#BEAT\",\"value\":0},{\"name\":\"#BPM\",\"value\":120}]},"
           "{\"name\":\"2\",\"archetype\":\"#TIMESCALE_GROUP\",\"data\":[{\"name\":\"first\",\"ref\":\"3\"}]},"
           "{\"name\":\"3\",\"archetype\":\"#TIMESCALE_CHANGE\",\"data\":[{\"name\":\"#BEAT\",\"value\":0},{\"name\":\"#TIMESCALE\",\"value\":1},"
           "{\"name\":\"#TIMESCALE_SKIP\",\"value\":0},{\"name\":\"#TIMESCALE_GROUP\",\"ref\":\"2\"},{\"name\":\"#TIMESCALE_EASE\",\"value\":" +
           ease + "}]}" + notes + "]}";
}

static std::string note(const std::string& name, const std::string& archetype, const std::string& fields) {
    return ",{\"name\":\"" + name + "\",\"archetype\":\"" + archetype +
           "\",\"data\":[{\"name\":\"#TIMESCALE_GROUP\",\"ref\":\"2\"}" + fields + "]}";
}

static std::string num(const std::string& key, const std::string& value) {
    return ",{\"name\":\"" + key + "\",\"value\":" + value + "}";
}

static std::string ref(const std::string& key, const std::string& value) {
    return ",{\"name\":\"" + key + "\",\"ref\":\"" + value + "\"}";
}

static std::string flick(const std::string& direction, const std::string& extra = "") {
    return note("4", "NormalFlickNote",
                num("#BEAT", "1") + num("lane", "8.5") + num("size", "1") + num("direction", direction) + extra);
}

static std::string guide(long long kind, const std::string& ease, const std::string& head_alpha,
                         const std::string& tail_alpha) {
    std::string k = std::to_string(kind);
    return note("10", "AnchorNote",
                num("#BEAT", "2") + num("lane", "-3") + num("size", "1.5") + num("connectorEase", ease) +
                    num("segmentKind", k) + num("segmentAlpha", head_alpha) + ref("next", "11")) +
           note("11", "AnchorNote",
                num("#BEAT", "4") + num("lane", "3") + num("size", "1.5") + num("connectorEase", "1") +
                    num("segmentKind", k) + num("segmentAlpha", tail_alpha));
}

// strict: rejected; lenient: loads and `then` holds
static void unsupported(const std::string& what, const std::string& data,
                        const std::function<bool(const core::Score&)>& then = nullptr) {
    bool threw = false;
    try {
        ns::load_text(data);
    } catch (const ns::UnsupportedFeatureError&) {
        threw = true;
    }
    check(threw, what + " is rejected");
    ns::LoadOptions lenient;
    lenient.error_on_unsupported_feature = false;
    try {
        core::Score s = ns::load_text(data, lenient);
        if (then) check(then(s), what + " loads leniently");
    } catch (const std::exception& e) {
        check(false, what + " loads leniently: " + e.what());
    }
}

template <typename T>
static std::vector<T*> all_of(const core::Score& s, core::Kind kind) {
    std::vector<T*> out;
    for (auto& n : s.notes)
        if (n->kind == kind) out.push_back(static_cast<T*>(n.get()));
    return out;
}

int main(int argc, char** argv) {
    std::string test_files = argc > 1 ? argv[1] : "test_files";
    int charts = 0;
    for (auto& entry : std::filesystem::directory_iterator(test_files)) {
        std::string name = entry.path().filename().string();
        core::Score score;
        if (entry.path().extension() == ".sus")
            score = converters::sus::loads(read_file(entry.path().string()));
        else if (name.size() > 10 && name.compare(name.size() - 10, 10, ".pjsk.json") == 0)
            score = converters::pjsk::load_bytes(read_file(entry.path().string()));
        else
            continue;
        ++charts;
        round_trip(name, score);
        semantic_round_trip(name, score);
    }
    check(charts > 0, "test charts found");

    // supported: extended lanes, every flick direction the exporter writes, all guide colors
    {
        core::Score s = ns::load_text(level(flick("1")));
        auto singles = all_of<core::Single>(s, core::Kind::Single);
        check(singles.size() == 1 && (double)singles[0]->lane == 8.5 && singles[0]->direction == "left" &&
                  singles[0]->critical == false && singles[0]->trace == false,
              "extended-lane left flick");
        check((double)s.metadata->waveoffset == 0.25, "bgmOffset");
    }
    const char* colors[] = {"neutral", "red", "green", "blue", "yellow", "purple", "cyan", "black"};
    for (long long kind = 101; kind <= 108; ++kind) {
        core::Score s = ns::load_text(level(guide(kind, "3", "1", "0")));
        auto guides = all_of<core::Guide>(s, core::Kind::Guide);
        check(guides.size() == 1 && guides[0]->color == colors[kind - 101] && guides[0]->fade == "out" &&
                  guides[0]->midpoints.size() == 2 && guides[0]->midpoints[0]->ease == "out",
              std::string("guide ") + colors[kind - 101]);
    }
    check(all_of<core::Guide>(ns::load_text(level(guide(103, "1", "0", "1"))), core::Kind::Guide)[0]->fade == "in",
          "guide fade in");
    check(all_of<core::Guide>(ns::load_text(level(guide(103, "1", "1", "1"))), core::Kind::Guide)[0]->fade == "none",
          "guide no fade");
    ns::load_text(level(flick("0", num("style", "0") + num("effectKind", "0"))));  // defaults are fine

    // unsupported: anything the exporter never writes
    unsupported("down flick", level(flick("4")), [](const core::Score& s) {
        return all_of<core::Single>(s, core::Kind::Single)[0]->direction == "left";
    });
    unsupported("note style", level(flick("0", num("style", "2"))));
    unsupported("note on a dynamic stage", level(flick("0", ref("stage", "2"))));
    unsupported("unknown note data", level(flick("0", num("somethingNew", "1"))));
    unsupported("dynamic stage pivot",
                level(flick("0") + ",{\"name\":\"9\",\"archetype\":\"StagePivotChange\",\"data\":[]}"),
                [](const core::Score& s) { return all_of<core::Single>(s, core::Kind::Single).size() == 1; });
    unsupported("unknown archetype", level(",{\"archetype\":\"SomeFutureThing\",\"data\":[]}"));
    unsupported("guide ease", level(guide(103, "6", "1", "1")), [](const core::Score& s) {
        return all_of<core::Guide>(s, core::Kind::Guide)[0]->midpoints[0]->ease == "linear";
    });
    unsupported("damage connector", level(guide(3, "1", "1", "1")));
    unsupported("partial guide alpha", level(guide(103, "1", "0.5", "0.5")));
    unsupported("timescale ease", level(flick("0"), "", "1"));
    unsupported("unknown level data key", level(flick("0"), ",\"somethingNew\":1"));
    unsupported("head flick",
                level(note("20", "NormalHeadFlickNote",
                           num("#BEAT", "1") + num("lane", "0") + num("size", "1") + num("connectorEase", "1") +
                               num("segmentKind", "1") + ref("next", "21")) +
                      note("21", "NormalTailReleaseNote",
                           num("#BEAT", "2") + num("lane", "0") + num("size", "1") + num("segmentKind", "1"))),
                [](const core::Score& s) {
                    auto slides = all_of<core::Slide>(s, core::Kind::Slide);
                    return slides.size() == 1 && slides[0]->connections.size() == 2 &&
                           static_cast<core::SlideStartPoint*>(slides[0]->connections[0].get())->judgeType == "normal";
                });

    if (fails) {
        std::printf("%d failure(s)\n", fails);
        return 1;
    }
    std::printf("next_sekai: all ok\n");
    return 0;
}
