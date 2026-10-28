#include <cmath>
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
            eo.compressed = false;
            eo.smooth_guide_fade = smooth;
            eo.use_guide_layer = layer;
            std::string first = ns::export_bytes(score, eo).data;
            core::Score loaded = ns::load_text(first);
            std::string second = ns::export_bytes(loaded, eo).data;
            check(first == second, name + " round trip smooth=" + std::to_string(smooth) +
                                       " layer=" + std::to_string(layer));
        }
    ns::ExportOptions gz;
    std::string compressed = ns::export_bytes(score, gz).data;
    check(ns::export_bytes(ns::load_text(compressed), gz).data == compressed, name + " gzip round trip");
}

// The USC objects of a score, order aside.
static std::vector<std::string> usc_objects(core::Score score) {
    util::Json usc = util::json_parse(converters::usc::dumps(score).data).at("usc");
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
    eo.compressed = false;
    core::Score back = ns::load_text(ns::export_bytes(score, eo).data);
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
    ns::ImportOptions lenient;
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
    {
        // an empty group is never referred to, so the exporter leaves it unnamed
        core::Score s = ns::load_text(level(flick("0") + ",{\"archetype\":\"#TIMESCALE_GROUP\",\"data\":"
                                                         "[{\"name\":\"editorName\",\"ref\":\"#1\"}]}"));
        auto groups = all_of<core::TimeScaleGroup>(s, core::Kind::TimeScaleGroup);
        check(groups.size() == 2 && groups[1]->changes.empty(), "empty unnamed timescale group");
        round_trip("empty timescale group", s);
    }
    check(all_of<core::Single>(ns::load_text("\xEF\xBB\xBF" + level(flick("0"))), core::Kind::Single).size() == 1,
          "UTF-8 BOM");

    // judged notes riding a line: notes of their own, the line stopping a 480th either side
    {
        auto point = [](const std::string& name, const std::string& archetype, const std::string& beat,
                        const std::string& lane, const std::string& kind, const std::string& alpha,
                        const std::string& next, const std::string& extra = "") {
            return note(name, archetype,
                        num("#BEAT", beat) + num("lane", lane) + num("size", "1.5") + num("connectorEase", "1") +
                            num("segmentKind", kind) + num("segmentAlpha", alpha) +
                            (next.empty() ? "" : ref("next", next)) + extra);
        };
        std::string guide = level(point("30", "NormalTraceNote", "1", "0", "103", "1", "31") +
                                  point("31", "AnchorNote", "2", "1", "103", "1", "32") +
                                  point("32", "NormalTraceNote", "3", "2", "103", "1", "33") +
                                  point("33", "AnchorNote", "4", "3", "103", "0", ""));
        core::Score s = ns::load_text(guide);
        auto traces = all_of<core::Single>(s, core::Kind::Single);
        auto guides = all_of<core::Guide>(s, core::Kind::Guide);
        const double tick = 1.0 / 480;
        check(traces.size() == 2 && traces[0]->trace.value_or(false) && (double)traces[1]->beat == 3,
              "notes on a guide become their own notes");
        check(guides.size() == 2 && guides[0]->midpoints.size() == 3 && guides[1]->midpoints.size() == 2 &&
                  std::abs(guides[0]->midpoints[0]->beat - (1 + tick)) < 1e-9 &&
                  std::abs(guides[0]->midpoints[2]->beat - (3 - tick)) < 1e-9 &&
                  std::abs(guides[1]->midpoints[0]->beat - (3 + tick)) < 1e-9 && guides[0]->fade == "out" &&
                  guides[1]->fade == "out" && guides[0]->color == "green",
              "the guide stops a 480th around them, keeping its fade");

        std::string slide = level(point("40", "NormalHeadTapNote", "1", "0", "1", "1", "41") +
                                  point("41", "NormalTraceNote", "2", "1", "1", "1", "42", num("isAttached", "1")) +
                                  point("42", "NormalTailReleaseNote", "3", "2", "1", "1", ""));
        core::Score t = ns::load_text(slide);
        auto slides = all_of<core::Slide>(t, core::Kind::Slide);
        check(all_of<core::Single>(t, core::Kind::Single).size() == 1 && slides.size() == 2 &&
                  slides[0]->connections.size() == 2 && slides[1]->connections.size() == 2 &&
                  static_cast<core::SlideEndPoint*>(slides[0]->connections[1].get())->judgeType == "none" &&
                  static_cast<core::SlideStartPoint*>(slides[1]->connections[0].get())->judgeType == "none" &&
                  std::abs(node_beat(slides[1]->connections[0].get()) - (2 + tick)) < 1e-9,
              "an attached note on a slide splits it into a hidden end and a hidden start");

        ns::ImportOptions keep;
        keep.attempt_attached_note_convert = false;
        bool threw = false;
        try {
            ns::load_text(guide, keep);
        } catch (const ns::UnsupportedFeatureError&) {
            threw = true;
        }
        check(threw, "without attempt_attached_note_convert they are unsupported");

        // attached anchors: dropped, or where the line's kind changes, the start of a new line
        std::string attached = num("isAttached", "1");
        core::Score plain = ns::load_text(level(point("50", "AnchorNote", "1", "0", "103", "1", "51") +
                                                point("51", "AnchorNote", "2", "1", "103", "1", "52", attached) +
                                                point("52", "AnchorNote", "3", "2", "103", "0", "")));
        auto one = all_of<core::Guide>(plain, core::Kind::Guide);
        check(one.size() == 1 && one[0]->midpoints.size() == 2 && one[0]->fade == "out",
              "an attached anchor that changes nothing is dropped");
        core::Score recolor = ns::load_text(level(point("60", "AnchorNote", "1", "0", "103", "1", "61") +
                                                  point("61", "AnchorNote", "2", "1", "105", "1", "62", attached) +
                                                  point("62", "AnchorNote", "3", "2", "105", "0", "")));
        auto two = all_of<core::Guide>(recolor, core::Kind::Guide);
        check(two.size() == 2 && two[0]->color == "green" && two[1]->color == "yellow" &&
                  (double)two[0]->midpoints.back()->beat == 2 && (double)two[1]->midpoints.front()->beat == 2 &&
                  (double)two[1]->midpoints.front()->lane == 1,
              "a guide changing color at an attached anchor becomes two guides");
        core::Score crit = ns::load_text(level(point("70", "NormalHeadTapNote", "1", "0", "1", "1", "71") +
                                               point("71", "AnchorNote", "2", "1", "2", "1", "72", attached) +
                                               point("72", "CriticalTailReleaseNote", "3", "2", "2", "1", "")));
        auto pieces = all_of<core::Slide>(crit, core::Kind::Slide);
        check(pieces.size() == 2 && !pieces[0]->critical && pieces[1]->critical &&
                  static_cast<core::SlideEndPoint*>(pieces[0]->connections.back().get())->judgeType == "none" &&
                  static_cast<core::SlideStartPoint*>(pieces[1]->connections.front().get())->judgeType == "none",
              "a slide turning critical part way: a hidden end, then a slide with a hidden head");
        core::Score ticked = ns::load_text(level(point("90", "NormalHeadTapNote", "1", "0", "1", "1", "91") +
                                                 point("91", "NormalTickNote", "2", "1", "2", "1", "92") +
                                                 point("92", "CriticalTailReleaseNote", "3", "2", "2", "1", "")));
        auto around_tick = all_of<core::Slide>(ticked, core::Kind::Slide);
        auto* tick_point = around_tick.size() == 2 && around_tick[0]->connections.size() == 3
                               ? static_cast<core::SlideRelayPoint*>(around_tick[0]->connections[1].get())
                               : nullptr;
        check(tick_point && tick_point->critical.has_value() && (double)tick_point->beat == 2 &&
                  std::abs(node_beat(around_tick[0]->connections[2].get()) - (2 + tick)) < 1e-9 &&
                  std::abs(node_beat(around_tick[1]->connections[0].get()) - (2 + tick)) < 1e-9 &&
                  around_tick[1]->critical,
              "a kind change on a combo tick keeps the tick, the new slide starting a 480th after");
        core::Score shape = ns::load_text(level(point("80", "AnchorNote", "1", "0", "103", "1", "81") +
                                                point("81", "AnchorNote", "2", "1", "108", "1", "82") +
                                                point("82", "AnchorNote", "3", "2", "108", "0", "")));
        auto split_shape = all_of<core::Guide>(shape, core::Kind::Guide);
        check(split_shape.size() == 2 && split_shape[1]->color == "black",
              "a color change at a shape point also starts a new guide");

        // guide alpha: a profile no fade gives keeps its keyframes (and round-trips); fades stay fades
        auto alphas = [](const core::Score& sc) {
            std::vector<double> out;
            for (auto* g : all_of<core::Guide>(sc, core::Kind::Guide))
                for (auto& p : g->midpoints) out.push_back(p->alpha ? (double)*p->alpha : -1);
            return out;
        };
        core::Score half = ns::load_text(level(::guide(103, "1", "0.5", "0.5")));
        check(alphas(half) == std::vector<double>{0.5, 0.5} &&
                  all_of<core::Guide>(half, core::Kind::Guide)[0]->fade == "none",
              "a guide at half alpha keeps it");
        std::string sep = num("isSeparator", "1");
        core::Score dip = ns::load_text(level(point("100", "AnchorNote", "1", "0", "103", "1", "101") +
                                              point("101", "AnchorNote", "2", "1", "103", "0.25", "102", sep) +
                                              point("102", "AnchorNote", "3", "2", "103", "1", "")));
        check(alphas(dip) == std::vector<double>{1, 0.25, 1}, "a guide dipping in alpha keeps its keyframes");
        round_trip("alpha keyframes", dip);
        core::Score keyed = ns::load_text(level(point("110", "AnchorNote", "1", "0", "103", "1", "111") +
                                                point("111", "AnchorNote", "2", "1", "103", "0.5", "112", attached + sep) +
                                                point("112", "AnchorNote", "3", "2", "103", "1", "")));
        auto kg = all_of<core::Guide>(keyed, core::Kind::Guide);
        check(kg.size() == 1 && kg[0]->midpoints.size() == 3 && alphas(keyed) == std::vector<double>{1, 0.5, 1},
              "an attached anchor holding an alpha keyframe stays, as a point");
        check(alphas(plain) == std::vector<double>{-1, -1}, "a plain fade has no keyframes");
    }

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

    // an unnamed group with no changes (newer editors leave them) loads, and its score still has a
    // duration and sorts
    {
        core::Score s = ns::load_text(level(flick("1") + ",{\"archetype\":\"#TIMESCALE_GROUP\",\"data\":[]}"));
        check(std::abs(s.duration() - 0.5) < 1e-9, "duration with an empty timescale group");
        s.sort_by_beat();
        check(s.notes.size() >= 2, "sorting with an empty timescale group");
    }

    // engine revisions that leave the notes alone load as they are
    auto with_revision = [](const std::string& value) {
        std::string data = level(flick("1"));
        std::string empty = "\"archetype\":\"Initialization\",\"data\":[]";
        return data.replace(data.find(empty), empty.size(),
                            "\"archetype\":\"Initialization\",\"data\":[{\"name\":\"revision\",\"value\":" + value +
                                "}]");
    };
    for (const char* revision : {"0", "1", "2"})
        check(all_of<core::Single>(ns::load_text(with_revision(revision)), core::Kind::Single).size() == 1,
              std::string("engine revision ") + revision);
    unsupported("a newer engine revision", with_revision("3"));

    if (fails) {
        std::printf("%d failure(s)\n", fails);
        return 1;
    }
    std::printf("next_sekai: all ok\n");
    return 0;
}
