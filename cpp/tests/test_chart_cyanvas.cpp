#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rg/converters/leveldata/chart_cyanvas.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/core/model.hpp"
#include "rg/util/json.hpp"

using namespace rg;
namespace cc = rg::converters::leveldata::chart_cyanvas;
using util::Json;

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

static std::string to_usc(core::Score score) { return converters::usc::dumps(score).data; }

static std::string to_cc(const std::string& usc, bool compressed = false) {
    core::Score s = converters::usc::load_text(usc);
    return cc::export_bytes(s, {compressed}).data;
}

static std::string cc_to_usc(const std::string& level) { return to_usc(cc::load_text(level)); }

// A USC object as Chart Cyanvas level data can hold it: what the format has no room for is left out
// or set to what loading gives back. Skills and fever, fake notes, speed ratios, a single flick of no
// direction, a guide of one point are not there; a hidden slide head or tail and a trace flick of no
// direction have no critical of their own (the slide's / none); a slide end flick is never a trace;
// an attached tick lies on its connector (no lane, size or ease), one of no combo in no group; a
// guide's last point starts no segment, so it has no ease.
static std::optional<Json> as_cc_holds(Json o) {
    std::string type = o.at("type").as_str();
    auto drop_speed = [](Json& j) {
        if (j.find("speedRatio")) j.erase("speedRatio");
    };
    auto fake = [](const Json& j) {
        const Json* f = j.find("fake");
        return f && f->type() == Json::Type::Bool && f->as_bool();
    };
    if (type == "skill" || type == "feverChance" || type == "feverStart") return std::nullopt;
    if (type == "single" || type == "damage") {
        if (fake(o)) return std::nullopt;
        drop_speed(o);
        if (o.find("fake")) o.erase("fake");
        const Json* dir = o.find("direction");
        bool trace = o.find("trace") && o.at("trace").as_bool();
        if (dir && dir->as_str() == "none") {
            if (!trace) return std::nullopt;
            o["critical"] = Json(false);
        }
        return o;
    }
    if (type == "slide") {
        if (fake(o)) return std::nullopt;
        if (o.find("fake")) o.erase("fake");
        bool critical = o.at("critical").as_bool();
        std::vector<std::string> conns;
        for (Json c : o.at("connections").arr()) {
            drop_speed(c);
            if (c.find("fake")) c.erase("fake");
            std::string ct = c.at("type").as_str();
            if ((ct == "start" || ct == "end") && c.at("judgeType").as_str() == "none") c["critical"] = Json(critical);
            if (ct == "end" && c.find("direction") && c.at("judgeType").as_str() == "trace") c["judgeType"] = Json("normal");
            if (ct == "attach") {
                c.erase("lane");
                c.erase("size");
                c["ease"] = Json("linear");
                if (!c.find("critical")) c["timeScaleGroup"] = Json::integer(0);
            }
            conns.push_back(c.dump());
        }
        std::sort(conns.begin(), conns.end());
        Json list = Json::array();
        for (auto& c : conns) list.push_back(util::json_parse(c));
        o["connections"] = list;
        return o;
    }
    if (type == "guide") {
        Json mids = Json::array();
        for (Json m : o.at("midpoints").arr()) {
            drop_speed(m);
            mids.push_back(m);
        }
        if (mids.arr().size() < 2) return std::nullopt;
        mids.arr().back()["ease"] = Json("linear");
        o["midpoints"] = mids;
        return o;
    }
    if (type == "timeScaleGroup") {
        // the converters write every scale as a float, one of 0 as 0.000001
        auto number = [](const Json& j) { return j.type() == Json::Type::Int ? (double)j.as_int() : j.as_float(); };
        std::vector<std::pair<double, std::string>> changes;
        for (Json c : o.at("changes").arr()) {
            double scale = number(c.at("timeScale"));
            c["timeScale"] = Json(scale == 0.000001 ? 0.0 : scale);
            changes.push_back({number(c.at("beat")), c.dump()});
        }
        std::stable_sort(changes.begin(), changes.end(), [](auto& a, auto& b) { return a.first < b.first; });
        Json list = Json::array();
        for (auto& [beat, c] : changes) list.push_back(util::json_parse(c));
        o["changes"] = list;
    }
    return o;
}

static std::vector<std::string> usc_objects(const std::string& usc_text, bool normalize) {
    Json usc = util::json_parse(usc_text).at("usc");
    std::vector<std::string> out;
    for (auto& o : usc.at("objects").arr()) {
        if (!normalize) {
            out.push_back(o.dump());
            continue;
        }
        if (auto kept = as_cc_holds(o)) out.push_back(kept->dump());
    }
    std::sort(out.begin(), out.end());
    out.push_back("offset " + usc.at("offset").dump());
    return out;
}

static std::size_t differing(const std::vector<std::string>& a, const std::vector<std::string>& b, const std::string& name) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
        if (i >= a.size() || i >= b.size() || a[i] != b[i]) {
            if (n++ < 2)
                std::printf("  %s: want %s\n  %s:  got %s\n", name.c_str(), i < a.size() ? a[i].substr(0, 300).c_str() : "-",
                            name.c_str(), i < b.size() ? b[i].substr(0, 300).c_str() : "-");
        }
    return n;
}

// usc -> cc -> usc, three times: the first trip keeps everything the format holds, the next ones
// change nothing (level data and USC alike), and export -> load -> export gives back the same bytes.
// The USC is compared as loaded (the USC loader rounds beats to 6 places).
static void round_trips(const std::string& name, const std::string& usc_text) {
    std::string usc0 = to_usc(converters::usc::load_text(usc_text));
    std::string level1 = to_cc(usc0);
    std::string usc1 = cc_to_usc(level1);
    std::string level2 = to_cc(usc1);
    std::string usc2 = cc_to_usc(level2);
    std::string level3 = to_cc(usc2);
    std::string usc3 = cc_to_usc(level3);
    check(differing(usc_objects(usc0, true), usc_objects(usc1, true), name) == 0,
          name + ": usc -> cc -> usc keeps what level data holds");
    check(level1 == level2 && level2 == level3, name + ": level data the same every trip");
    check(usc1 == usc2 && usc2 == usc3, name + ": usc the same every trip");
    check(cc::export_bytes(*std::make_unique<core::Score>(cc::load_text(level1)), {false}).data == level1,
          name + ": export -> load -> export");
    std::string gz = to_cc(usc0, true);
    core::Score from_gz = cc::load_text(gz);
    check(cc::export_bytes(from_gz, {true}).data == gz, name + ": gzip round trip");
}

// ---- guides -------------------------------------------------------------------------------------

struct P {
    double beat, lane, size;
    const char* ease = "linear";
};

static std::shared_ptr<core::Guide> guide(const std::vector<P>& points, const char* color = "green",
                                          const char* fade = "out") {
    auto g = std::make_shared<core::Guide>(color, fade);
    for (auto& p : points) {
        auto m = std::make_shared<core::GuidePoint>();
        m->beat = core::Num::F(p.beat);
        m->lane = core::Num::F(p.lane);
        m->size = core::Num::F(p.size);
        m->ease = p.ease;
        m->timeScaleGroup = 0;
        g->append(m);
    }
    return g;
}

static std::string guide_text(const core::Guide& g) {
    std::string out = g.color + "/" + g.fade + ":";
    for (auto& p : g.midpoints)
        out += " (" + std::to_string((double)p->beat) + "," + std::to_string((double)p->lane) + "," +
               std::to_string((double)p->size) + "," + p->ease + ")";
    return out;
}

template <typename T>
static std::vector<T*> all_of(const core::Score& s, core::Kind kind) {
    std::vector<T*> out;
    for (auto& n : s.notes)
        if (n->kind == kind) out.push_back(static_cast<T*>(n.get()));
    return out;
}

// every guide comes back as it went in: the same points, in its order, on the same guide
static void guides_back(const std::string& what, const std::vector<std::shared_ptr<core::Guide>>& guides) {
    std::vector<core::NodePtr> notes{std::make_shared<core::Bpm>(core::Num::I(0), core::Num::F(120.0))};
    std::vector<std::string> want;
    for (auto& g : guides) {
        notes.push_back(g);
        core::Guide copy = *g;
        copy.midpoints.back()->ease = "linear";
        want.push_back(guide_text(copy));
    }
    auto meta = std::make_shared<core::MetaData>();
    meta->waveoffset = core::Num::I(0);
    core::Score score(meta, notes);
    core::Score back = cc::load_text(cc::export_bytes(score, {false}).data);
    std::vector<std::string> got;
    for (auto* g : all_of<core::Guide>(back, core::Kind::Guide)) got.push_back(guide_text(*g));
    std::sort(want.begin(), want.end());
    std::sort(got.begin(), got.end());
    check(want == got, what);
    if (want != got)
        for (auto& g : got) std::printf("    got %s\n", g.c_str());
}

// ---- hand-made level data ------------------------------------------------------------------------

static std::string num(const std::string& key, const std::string& value) {
    return "{\"name\":\"" + key + "\",\"value\":" + value + "}";
}
static std::string ref(const std::string& key, const std::string& value) {
    return "{\"name\":\"" + key + "\",\"ref\":\"" + value + "\"}";
}
static std::string entity(const std::string& name, const std::string& archetype, const std::vector<std::string>& data) {
    std::string out = "{";
    if (!name.empty()) out += "\"name\":\"" + name + "\",";
    out += "\"archetype\":\"" + archetype + "\",\"data\":[";
    for (std::size_t i = 0; i < data.size(); ++i) out += (i ? "," : "") + data[i];
    return out + "]}";
}
// as uscToLevelData writes a chart of no timescale groups: unnamed entities, the stand-in group
static std::string level(const std::vector<std::string>& notes) {
    std::string out = "{\"bgmOffset\":0,\"entities\":[" + entity("", "Initialization", {}) + "," +
                      entity("", "InputManager", {}) + "," + entity("", "Stage", {}) + "," +
                      entity("tsg:0", "TimeScaleGroup", {ref("first", "tsc:0:0"), num("length", "0")}) + "," +
                      entity("tsc:0:0", "TimeScaleChange", {num("#BEAT", "0"), num("timeScale", "1"), ref("timeScaleGroup", "trg:0")}) +
                      "," + entity("", "#BPM_CHANGE", {num("#BEAT", "0"), num("#BPM", "120")});
    for (auto& n : notes) out += "," + n;
    return out + "]}";
}
static std::string tap(const std::string& name, const std::string& archetype, const std::string& beat,
                       std::vector<std::string> extra = {}) {
    std::vector<std::string> data{num("#BEAT", beat), num("lane", "0"), num("size", "1")};
    data.insert(data.end(), extra.begin(), extra.end());
    data.push_back(ref("timeScaleGroup", "tsg:0"));
    return entity(name, archetype, data);
}
static std::string segment(const std::string& color, const std::string& head_beat, const std::string& head_lane,
                           const std::string& tail_beat, const std::string& tail_lane, const std::string& end_beat,
                           const std::string& end_lane) {
    auto at = [](const std::string& prefix, const std::string& beat, const std::string& lane) {
        return num(prefix + "Lane", lane) + "," + num(prefix + "Size", "1") + "," + num(prefix + "Beat", beat) + "," +
               ref(prefix + "TimeScaleGroup", "tsg:0");
    };
    return "{\"archetype\":\"Guide\",\"data\":[" + num("color", color) + "," + num("fade", "0") + "," + num("ease", "0") + "," +
           at("start", "0", "0") + "," + at("head", head_beat, head_lane) + "," + at("tail", tail_beat, tail_lane) + "," +
           at("end", end_beat, end_lane) + "]}";
}

static void unsupported(const std::string& what, const std::string& data,
                        const std::function<bool(const core::Score&)>& then = nullptr) {
    bool threw = false;
    try {
        cc::load_text(data);
    } catch (const cc::UnsupportedFeatureError&) {
        threw = true;
    }
    check(threw, what + " is rejected");
    cc::ImportOptions lenient;
    lenient.error_on_unsupported_feature = false;
    try {
        core::Score s = cc::load_text(data, lenient);
        if (then) check(then(s), what + " loads leniently");
    } catch (const std::exception& e) {
        check(false, what + " loads leniently: " + e.what());
    }
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
        try {
            round_trips(name, to_usc(score));
        } catch (const std::exception& e) {
            check(false, name + ": " + e.what());
        }
    }
    check(charts > 0, "test charts found");

    // guides: each segment back on its own guide
    P s{0, 0, 1, "in"}, x{2, 0, 1, "out"}, e{6, 0, 1};
    guides_back("one guide", {guide({s, {3, -2, 1}, e})});
    guides_back("stacked copies of a guide", {guide({s, x, e}), guide({s, x, e}), guide({s, x, e})});
    guides_back("guides sharing a start, an end and a point between",
                {guide({s, x, {4, -2, 1, "inout"}, e}), guide({s, {1, 2, 1}, x, {4, 2, 1, "outin"}, e})});
    guides_back("a guide through its end point and on", {guide({s, e, e}), guide({s, e, e})});
    guides_back("dot guides (start = end)", {guide({{5, 1, 1}, {5, 1, 1}}), guide({{5, 1, 1}, {5, 1, 1}})});
    guides_back("same places, other colors and fades",
                {guide({s, x, e}, "green"), guide({s, x, e}, "red"), guide({s, x, e}, "green", "in"),
                 guide({s, x, e}, "green", "none")});
    {
        // segments of two copies of a guide, one after the other's: still two guides
        std::string interleaved = level({segment("2", "0", "0", "2", "0", "4", "0"), segment("2", "0", "0", "2", "0", "4", "0"),
                                         segment("2", "2", "0", "4", "0", "4", "0"), segment("2", "2", "0", "4", "0", "4", "0")});
        auto guides = all_of<core::Guide>(cc::load_text(interleaved), core::Kind::Guide);
        check(guides.size() == 2 && guides[0]->midpoints.size() == 3 && guides[1]->midpoints.size() == 3,
              "interleaved segments of stacked guides");
    }

    // uscToLevelData's own output: unnamed entities, the stand-in group naming "trg:0", an attached
    // tick of the chart's own keeping its group, the generated combo ticks
    {
        std::string data = level({
            tap("", "NormalTapNote", "1"),
            tap("a", "NormalSlideStartNote", "2"),
            entity("", "IgnoredSlideTickNote", {num("#BEAT", "2.25"), ref("attach", "c"), ref("timeScaleGroup", "tsg:0")}),
            entity("", "IgnoredSlideTickNote", {num("#BEAT", "2.5"), ref("attach", "c")}),
            tap("b", "CriticalSlideEndNote", "3", {ref("slide", "c")}),
            entity("c", "NormalSlideConnector", {ref("start", "a"), ref("end", "b"), ref("head", "a"), ref("tail", "b"),
                                                 num("ease", "0"), num("startType", "0")}),
        });
        core::Score s = cc::load_text(data);
        auto slides = all_of<core::Slide>(s, core::Kind::Slide);
        bool ok = all_of<core::Single>(s, core::Kind::Single).size() == 1 && slides.size() == 1 &&
                  slides[0]->connections.size() == 3 && all_of<core::TimeScaleGroup>(s, core::Kind::TimeScaleGroup).empty();
        if (ok) {
            auto* attach = static_cast<core::SlideRelayPoint*>(slides[0]->connections[1].get());
            ok = attach->type == "attach" && (double)attach->beat == 2.25 && !attach->critical.has_value();
        }
        check(ok, "uscToLevelData output: the chart's own attached tick stays, the generated one goes");
    }
    {
        // uscToLevelData's group next counts the group's changes: it can name a group that isn't there
        std::string data =
            "{\"bgmOffset\":0,\"entities\":[" +
            entity("tsg:0", "TimeScaleGroup", {ref("first", "tsc:0:0"), num("length", "2"), ref("next", "tsg:1")}) + "," +
            entity("tsg:1", "TimeScaleGroup", {ref("first", "tsc:1:0"), num("length", "1"), ref("next", "tsg:2")}) + "," +
            entity("tsc:0:0", "TimeScaleChange", {num("#BEAT", "0"), num("timeScale", "1"), ref("next", "tsc:0:1")}) + "," +
            entity("tsc:0:1", "TimeScaleChange", {num("#BEAT", "4"), num("timeScale", "0.000001"), num("next", "-1")}) + "," +
            entity("tsc:1:0", "TimeScaleChange", {num("#BEAT", "0"), num("timeScale", "2"), num("next", "-1")}) + "," +
            entity("", "#BPM_CHANGE", {num("#BEAT", "0"), num("#BPM", "120")}) + "," +
            entity("", "NormalTapNote", {num("#BEAT", "1"), num("lane", "0"), num("size", "1"), ref("timeScaleGroup", "tsg:1")}) +
            "]}";
        core::Score s = cc::load_text(data);
        auto groups = all_of<core::TimeScaleGroup>(s, core::Kind::TimeScaleGroup);
        auto singles = all_of<core::Single>(s, core::Kind::Single);
        check(groups.size() == 2 && groups[0]->changes.size() == 2 && (double)groups[0]->changes[1]->timeScale == 0 &&
                  groups[1]->changes.size() == 1 && singles.size() == 1 && singles[0]->timeScaleGroup == 1,
              "timescale groups (a scale of 0 written 0.000001, a group next naming nothing)");
    }

    // unsupported: anything neither converter writes
    unsupported("unknown archetype", level({entity("", "SomeFutureThing", {})}));
    unsupported("unknown note data", level({tap("", "NormalTapNote", "1", {num("somethingNew", "1")})}),
                [](const core::Score& s) { return all_of<core::Single>(s, core::Kind::Single).size() == 1; });
    unsupported("flick direction 2", level({tap("", "NormalFlickNote", "1", {num("direction", "2")})}),
                [](const core::Score& s) { return all_of<core::Single>(s, core::Kind::Single)[0]->direction == "up"; });
    unsupported("guide color 9", level({segment("9", "0", "0", "4", "0", "4", "0")}),
                [](const core::Score& s) { return all_of<core::Guide>(s, core::Kind::Guide).size() == 1; });
    unsupported("a guide segment joining nothing", level({segment("2", "1", "0", "4", "0", "4", "0")}),
                [](const core::Score& s) { return all_of<core::Guide>(s, core::Kind::Guide).size() == 1; });
    unsupported("a guide not reaching its end", level({segment("2", "0", "0", "2", "0", "4", "0")}));
    unsupported("unknown level data key", "{\"bgmOffset\":0,\"somethingNew\":1,\"entities\":[]}");

    if (fails) {
        std::printf("%d failure(s)\n", fails);
        return 1;
    }
    std::printf("chart_cyanvas: all ok\n");
    return 0;
}
