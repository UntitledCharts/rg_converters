#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "num_caster.hpp"
#include "object_lock.hpp"

#include "rg/converters/bdon.hpp"
#include "rg/converters/holodori_sus.hpp"
#include "rg/converters/leveldata/chart_cyanvas.hpp"
#include "rg/converters/leveldata/detect.hpp"
#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/converters/leveldata/untitled_sekai.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/detect.hpp"

namespace py = pybind11;
using namespace rg::core;
using namespace rg::bindings;

PYBIND11_MODULE(_rg_converters, m, py::mod_gil_not_used()) {
    using OB = std::optional<bool>;
    using OS = std::optional<std::string>;

    // ---- base ----
    py::class_<Node, std::shared_ptr<Node>>(m, "_Node");

    locked_class<MetaData, std::shared_ptr<MetaData>>(m, "MetaData")
        .def(py::init([](std::string title, std::string artist, std::string designer,
                         Num waveoffset, std::optional<std::vector<std::string>> requests) {
                 auto mm = std::make_shared<MetaData>();
                 mm->title = std::move(title);
                 mm->artist = std::move(artist);
                 mm->designer = std::move(designer);
                 mm->waveoffset = waveoffset;
                 mm->requests = std::move(requests);
                 return mm;
             }),
             py::arg("title"), py::arg("artist"), py::arg("designer"),
             py::arg("waveoffset"), py::arg("requests"))
        .def_field("title", &MetaData::title)
        .def_field("artist", &MetaData::artist)
        .def_field("designer", &MetaData::designer)
        .def_field("waveoffset", &MetaData::waveoffset)
        .def_field("requests", &MetaData::requests);

    locked_class<Bpm, Node, std::shared_ptr<Bpm>>(m, "Bpm")
        .def(py::init([](Num beat, Num bpm, std::string type) {
                 return std::make_shared<Bpm>(beat, bpm, type);
             }),
             py::arg("beat"), py::arg("bpm"), py::arg("type") = "bpm")
        .def_field("beat", &Bpm::beat)
        .def_field("bpm", &Bpm::bpm)
        .def_field("type", &Bpm::type)
        .def("get_sus_sort_number", locked(&Bpm::get_sus_sort_number));

    locked_class<TimeScalePoint, Node, std::shared_ptr<TimeScalePoint>>(m, "TimeScalePoint")
        .def(py::init([](Num beat, Num timeScale) {
                 return std::make_shared<TimeScalePoint>(beat, timeScale);
             }),
             py::arg("beat"), py::arg("timeScale"))
        .def_field("beat", &TimeScalePoint::beat)
        .def_field("timeScale", &TimeScalePoint::timeScale);

    locked_class<TimeScaleGroup, Node, std::shared_ptr<TimeScaleGroup>>(m, "TimeScaleGroup")
        .def(py::init([](std::vector<std::shared_ptr<TimeScalePoint>> changes, std::string type) {
                 return std::make_shared<TimeScaleGroup>(std::move(changes), std::move(type));
             }),
             py::arg("changes") = std::vector<std::shared_ptr<TimeScalePoint>>{},
             py::arg("type") = "timeScaleGroup")
        .def_field("changes", &TimeScaleGroup::changes)
        .def_field("type", &TimeScaleGroup::type)
        .def("append", locked(&TimeScaleGroup::append), py::arg("time_scale"))
        .def("insert", locked(&TimeScaleGroup::insert), py::arg("index"), py::arg("time_scale"))
        .def("get_sus_sort_number", locked(&TimeScaleGroup::get_sus_sort_number));

    locked_class<Volume, Node, std::shared_ptr<Volume>>(m, "Volume")
        .def(py::init([](Num beat, Num volume, std::string type) {
                 return std::make_shared<Volume>(beat, volume, type);
             }),
             py::arg("beat"), py::arg("volume"), py::arg("type") = "volume")
        .def_field("beat", &Volume::beat)
        .def_field("volume", &Volume::volume)
        .def_field("type", &Volume::type)
        .def("get_sus_sort_number", locked(&Volume::get_sus_sort_number));

    locked_class<Skill, Node, std::shared_ptr<Skill>>(m, "Skill")
        .def(py::init([](Num beat) { return std::make_shared<Skill>(beat); }), py::kw_only(),
             py::arg("beat"))
        .def_field("beat", &Skill::beat)
        .def_field("type", &Skill::type)
        .def("get_sus_sort_number", locked(&Skill::get_sus_sort_number));

    locked_class<FeverChance, Node, std::shared_ptr<FeverChance>>(m, "FeverChance")
        .def(py::init([](Num beat) { return std::make_shared<FeverChance>(beat); }),
             py::kw_only(), py::arg("beat"))
        .def_field("beat", &FeverChance::beat)
        .def_field("type", &FeverChance::type)
        .def("get_sus_sort_number", locked(&FeverChance::get_sus_sort_number));

    locked_class<FeverStart, Node, std::shared_ptr<FeverStart>>(m, "FeverStart")
        .def(py::init([](Num beat) { return std::make_shared<FeverStart>(beat); }),
             py::kw_only(), py::arg("beat"))
        .def_field("beat", &FeverStart::beat)
        .def_field("type", &FeverStart::type)
        .def("get_sus_sort_number", locked(&FeverStart::get_sus_sort_number));

    locked_class<Single, Node, std::shared_ptr<Single>>(m, "Single")
        .def(py::init([](Num beat, OB critical, Num lane, Num size, bool fake,
                         long long timeScaleGroup, Num speedRatio, OB trace, OS direction,
                         std::string type) {
                 auto s = std::make_shared<Single>();
                 s->beat = beat;
                 s->critical = critical;
                 s->lane = lane;
                 s->size = size;
                 s->fake = fake;
                 s->timeScaleGroup = timeScaleGroup;
                 s->speedRatio = speedRatio;
                 s->trace = trace;
                 s->direction = direction;
                 s->type = std::move(type);
                 return s;
             }),
             py::kw_only(), py::arg("beat"), py::arg("critical") = OB(std::nullopt),
             py::arg("lane"), py::arg("size"), py::arg("fake") = false,
             py::arg("timeScaleGroup"), py::arg("speedRatio") = Num::F(1.0),
             py::arg("trace") = OB(std::nullopt), py::arg("direction") = OS(std::nullopt),
             py::arg("type") = "single")
        .def_field("beat", &Single::beat)
        .def_field("critical", &Single::critical)
        .def_field("lane", &Single::lane)
        .def_field("size", &Single::size)
        .def_field("fake", &Single::fake)
        .def_field("timeScaleGroup", &Single::timeScaleGroup)
        .def_field("speedRatio", &Single::speedRatio)
        .def_field("trace", &Single::trace)
        .def_field("direction", &Single::direction)
        .def_field("type", &Single::type)
        .def("get_sus_sort_number", locked(&Single::get_sus_sort_number));

    locked_class<SlideStartPoint, Node, std::shared_ptr<SlideStartPoint>>(m, "SlideStartPoint")
        .def(py::init([](Num beat, bool critical, std::string ease, std::string judgeType,
                         Num lane, Num size, long long timeScaleGroup, Num speedRatio,
                         std::string type) {
                 auto p = std::make_shared<SlideStartPoint>();
                 p->beat = beat;
                 p->critical = critical;
                 p->ease = std::move(ease);
                 p->judgeType = std::move(judgeType);
                 p->lane = lane;
                 p->size = size;
                 p->timeScaleGroup = timeScaleGroup;
                 p->speedRatio = speedRatio;
                 p->type = std::move(type);
                 return p;
             }),
             py::arg("beat"), py::arg("critical"), py::arg("ease"), py::arg("judgeType"),
             py::arg("lane"), py::arg("size"), py::arg("timeScaleGroup"),
             py::arg("speedRatio") = Num::F(1.0), py::arg("type") = "start")
        .def_field("beat", &SlideStartPoint::beat)
        .def_field("critical", &SlideStartPoint::critical)
        .def_field("ease", &SlideStartPoint::ease)
        .def_field("judgeType", &SlideStartPoint::judgeType)
        .def_field("lane", &SlideStartPoint::lane)
        .def_field("size", &SlideStartPoint::size)
        .def_field("timeScaleGroup", &SlideStartPoint::timeScaleGroup)
        .def_field("speedRatio", &SlideStartPoint::speedRatio)
        .def_field("type", &SlideStartPoint::type);

    locked_class<SlideRelayPoint, Node, std::shared_ptr<SlideRelayPoint>>(m, "SlideRelayPoint")
        .def(py::init([](Num beat, std::string ease, Num lane, Num size,
                         long long timeScaleGroup, std::string type, OB critical, bool fake,
                         Num speedRatio) {
                 auto p = std::make_shared<SlideRelayPoint>();
                 p->beat = beat;
                 p->ease = std::move(ease);
                 p->lane = lane;
                 p->size = size;
                 p->timeScaleGroup = timeScaleGroup;
                 p->type = std::move(type);
                 p->critical = critical;
                 p->fake = fake;
                 p->speedRatio = speedRatio;
                 return p;
             }),
             py::arg("beat"), py::arg("ease"), py::arg("lane"), py::arg("size"),
             py::arg("timeScaleGroup"), py::arg("type"), py::arg("critical") = OB(std::nullopt),
             py::arg("fake") = false, py::arg("speedRatio") = Num::F(1.0))
        .def_field("beat", &SlideRelayPoint::beat)
        .def_field("ease", &SlideRelayPoint::ease)
        .def_field("lane", &SlideRelayPoint::lane)
        .def_field("size", &SlideRelayPoint::size)
        .def_field("timeScaleGroup", &SlideRelayPoint::timeScaleGroup)
        .def_field("type", &SlideRelayPoint::type)
        .def_field("critical", &SlideRelayPoint::critical)
        .def_field("fake", &SlideRelayPoint::fake)
        .def_field("speedRatio", &SlideRelayPoint::speedRatio);

    locked_class<SlideEndPoint, Node, std::shared_ptr<SlideEndPoint>>(m, "SlideEndPoint")
        .def(py::init([](Num beat, bool critical, std::string judgeType, Num lane, Num size,
                         long long timeScaleGroup, Num speedRatio, OS direction,
                         std::string type) {
                 auto p = std::make_shared<SlideEndPoint>();
                 p->beat = beat;
                 p->critical = critical;
                 p->judgeType = std::move(judgeType);
                 p->lane = lane;
                 p->size = size;
                 p->timeScaleGroup = timeScaleGroup;
                 p->speedRatio = speedRatio;
                 p->direction = direction;
                 p->type = std::move(type);
                 return p;
             }),
             py::arg("beat"), py::arg("critical"), py::arg("judgeType"), py::arg("lane"),
             py::arg("size"), py::arg("timeScaleGroup"), py::arg("speedRatio") = Num::F(1.0),
             py::arg("direction") = OS(std::nullopt), py::arg("type") = "end")
        .def_field("beat", &SlideEndPoint::beat)
        .def_field("critical", &SlideEndPoint::critical)
        .def_field("judgeType", &SlideEndPoint::judgeType)
        .def_field("lane", &SlideEndPoint::lane)
        .def_field("size", &SlideEndPoint::size)
        .def_field("timeScaleGroup", &SlideEndPoint::timeScaleGroup)
        .def_field("speedRatio", &SlideEndPoint::speedRatio)
        .def_field("direction", &SlideEndPoint::direction)
        .def_field("type", &SlideEndPoint::type);

    locked_class<Slide, Node, std::shared_ptr<Slide>>(m, "Slide")
        .def(py::init([](bool critical, bool fake, std::vector<NodePtr> connections,
                         std::string type) {
                 return std::make_shared<Slide>(critical, fake, std::move(connections),
                                                std::move(type));
             }),
             py::arg("critical"), py::arg("fake") = false,
             py::arg("connections") = std::vector<NodePtr>{}, py::arg("type") = "slide")
        .def_field("critical", &Slide::critical)
        .def_field("fake", &Slide::fake)
        .def_field("connections", &Slide::connections)
        .def_field("type", &Slide::type)
        .def("append", locked(&Slide::append), py::arg("slidepoint"))
        .def("sort", locked(&Slide::sort))
        .def("get_sus_sort_number", locked(&Slide::get_sus_sort_number));

    locked_class<GuidePoint, Node, std::shared_ptr<GuidePoint>>(m, "GuidePoint")
        .def(py::init([](Num beat, OS ease, Num lane, Num size,
                         long long timeScaleGroup, Num speedRatio, std::optional<Num> alpha) {
                 auto p = std::make_shared<GuidePoint>();
                 p->beat = beat;
                 p->ease = ease.value_or("");  // legacy chcy loader passes ease=None transiently
                 p->lane = lane;
                 p->size = size;
                 p->timeScaleGroup = timeScaleGroup;
                 p->speedRatio = speedRatio;
                 p->alpha = alpha;
                 return p;
             }),
             py::arg("beat"), py::arg("ease"), py::arg("lane"), py::arg("size"),
             py::arg("timeScaleGroup"), py::arg("speedRatio") = Num::F(1.0),
             py::arg("alpha") = std::optional<Num>(std::nullopt))
        .def_field("beat", &GuidePoint::beat)
        .def_field("ease", &GuidePoint::ease)
        .def_field("lane", &GuidePoint::lane)
        .def_field("size", &GuidePoint::size)
        .def_field("timeScaleGroup", &GuidePoint::timeScaleGroup)
        .def_field("speedRatio", &GuidePoint::speedRatio)
        .def_field("alpha", &GuidePoint::alpha);

    locked_class<Guide, Node, std::shared_ptr<Guide>>(m, "Guide")
        .def(py::init([](std::string color, std::string fade,
                         std::vector<std::shared_ptr<GuidePoint>> midpoints, std::string type,
                         OS color_code) {
                 return std::make_shared<Guide>(std::move(color), std::move(fade),
                                                std::move(midpoints), std::move(type),
                                                color_code);
             }),
             py::arg("color"), py::arg("fade"),
             py::arg("midpoints") = std::vector<std::shared_ptr<GuidePoint>>{},
             py::arg("type") = "guide", py::arg("color_code") = OS(std::nullopt))
        .def_field("color", &Guide::color)
        .def_field("fade", &Guide::fade)
        .def_field("midpoints", &Guide::midpoints)
        .def_field("type", &Guide::type)
        .def_field("color_code", &Guide::color_code)
        .def("append", locked(&Guide::append), py::arg("guidepoint"))
        .def("sort", locked(&Guide::sort))
        .def("get_sus_sort_number", locked(&Guide::get_sus_sort_number));

    // Holodori events
    locked_class<HolodoriSkill, Node, std::shared_ptr<HolodoriSkill>>(m, "HolodoriSkill")
        .def(py::init([](Num beat, long long slot) {
                 return std::make_shared<HolodoriSkill>(beat, slot);
             }),
             py::kw_only(), py::arg("beat"), py::arg("slot"))
        .def_field("beat", &HolodoriSkill::beat)
        .def_field("slot", &HolodoriSkill::slot)
        .def_field("type", &HolodoriSkill::type)
        .def("get_sus_sort_number", locked(&HolodoriSkill::get_sus_sort_number));

    auto bind_holo = [&](auto* tag, const char* name) {
        using T = std::remove_pointer_t<decltype(tag)>;
        locked_class<T, Node, std::shared_ptr<T>>(m, name)
            .def(py::init([](Num beat) { return std::make_shared<T>(beat); }), py::kw_only(),
                 py::arg("beat"))
            .def_field("beat", &T::beat)
            .def_field("type", &T::type)
            .def("get_sus_sort_number", locked(&T::get_sus_sort_number));
    };
    bind_holo((HolodoriChargeStart*)nullptr, "HolodoriChargeStart");
    bind_holo((HolodoriChargeEnd*)nullptr, "HolodoriChargeEnd");
    bind_holo((HolodoriFeverStart*)nullptr, "HolodoriFeverStart");
    bind_holo((HolodoriFeverEnd*)nullptr, "HolodoriFeverEnd");

    m.def("convert_holodori_events", &convert_holodori_events, py::arg("notes"));

    // ---- Score ----
    locked_class<Score, std::shared_ptr<Score>>(m, "Score")
        .def(py::init([](std::shared_ptr<MetaData> metadata, std::vector<NodePtr> notes) {
                 return std::make_shared<Score>(std::move(metadata), std::move(notes));
             }),
             py::arg("metadata"), py::arg("notes"))
        .def_field("metadata", &Score::metadata)
        .def_field("notes", &Score::notes)
        .def("delete_fake_notes", locked(&Score::delete_fake_notes))
        .def("delete_damage_notes", locked(&Score::delete_damage_notes))
        .def("replace_extended_guide_colors", locked(&Score::replace_extended_guide_colors))
        .def("replace_extended_ease", locked(&Score::replace_extended_ease))
        .def("sort_by_beat", locked(&Score::sort_by_beat))
        .def("add_point_without_fade", locked(&Score::add_point_without_fade))
        .def("strip_extended_lanes", locked(&Score::strip_extended_lanes),
             py::arg("resize_if_possible") = false)
        .def("strip_speed_ratios", locked(&Score::strip_speed_ratios))
        .def("flatten_speed_ratios_to_layers", locked(&Score::flatten_speed_ratios_to_layers))
        .def("check_skill_overlap", locked(&Score::check_skill_overlap))
        .def("export_overlaps_score",
             [](const Score& s) {
                 object_lock lock(s);
                 auto [sc, n] = s.export_overlaps_score();
                 return py::make_tuple(std::make_shared<Score>(sc), n);
             })
        .def("shift", locked(&Score::shift))
        .def("time_at_beat", locked(&Score::time_at_beat), py::arg("target_beat"))
        .def("beat_at_time", locked(&Score::beat_at_time), py::arg("target_time"))
        .def_property_readonly("duration", locked(&Score::duration))
        .def_property_readonly("combo_count", locked(&Score::combo_count))
        .def_property_readonly("note_count", locked(&Score::note_count))
        .def("combo_events",
             [](const Score& s) {
                 object_lock lock(s);
                 py::list out;
                 for (auto& e : s.combo_events())
                     out.append(py::make_tuple(e.category, e.critical, e.beat));
                 return out;
             })
        .def("cut",
             [](Score& s, std::optional<double> start_at, std::optional<double> end_at,
                bool keep_position) {
                 object_lock lock(s);
                 auto [combo_before, ticks] = s.cut(start_at, end_at, keep_position);
                 return py::make_tuple(combo_before,
                                       py::make_tuple(ticks.first, ticks.second));
             },
             py::arg("start_at") = std::optional<double>(std::nullopt),
             py::arg("end_at") = std::optional<double>(std::nullopt),
             py::arg("keep_position") = false);

    // ---- converter submodules (string-level funcs; file I/O handled in Python shim) ----
    namespace cv = rg::converters;

    auto msus = m.def_submodule("sus");
    msus.def("loads", &cv::sus::loads, py::arg("data"));
    msus.def("dumps",
             [](const Score& score, bool allow_layers, bool allow_extended_lanes,
                bool delete_damage, bool keep_note_speed_ratios, bool measure_extensions,
                bool skip_shift) {
                 object_lock lock(score);
                 cv::sus::ExportOptions o;
                 o.allow_layers = allow_layers;
                 o.allow_extended_lanes = allow_extended_lanes;
                 o.delete_damage = delete_damage;
                 o.keep_note_speed_ratios = keep_note_speed_ratios;
                 o.measure_extensions = measure_extensions;
                 o.skip_shift = skip_shift;
                 return cv::sus::dumps(score, o);
             },
             py::arg("score"), py::arg("allow_layers") = false,
             py::arg("allow_extended_lanes") = false, py::arg("delete_damage") = true,
             py::arg("keep_note_speed_ratios") = false, py::arg("measure_extensions") = false,
             py::arg("skip_shift") = false);
    msus.def("detect", &cv::sus::detect, py::arg("data"));

    auto musc = m.def_submodule("usc");
    musc.def("load_text", &cv::usc::load_text, py::arg("json_text"));
    musc.def("dumps",
             [](Score& score, bool minified) {
                 object_lock lock(score);
                 return cv::usc::dumps(score, minified);
             },
             py::arg("score"), py::arg("minified") = true);
    musc.def("detect", &cv::usc::detect, py::arg("data"));

    auto mmmws = m.def_submodule("mmws");
    mmmws.def("load_bytes",
              [](const py::bytes& b) { return cv::mmws::load_bytes(std::string(b)); },
              py::arg("data"));
    mmmws.def("export_bytes",
              [](Score& s, const std::string& fmt) {
                  object_lock lock(s);
                  return py::bytes(cv::mmws::export_bytes(s, fmt));
              },
              py::arg("score"), py::arg("format"));
    mmmws.def("detect", [](const py::bytes& b) { return cv::mmws::detect(std::string(b)); },
              py::arg("data"));

    auto mpjsk = m.def_submodule("pjsk");
    mpjsk.def("load_bytes",
              [](const py::bytes& b) { return cv::pjsk::load_bytes(std::string(b)); },
              py::arg("data"));
    mpjsk.def("export_bytes",
              [](Score& s, long long music_id) {
                  object_lock lock(s);
                  return py::bytes(cv::pjsk::export_bytes(s, music_id));
              },
              py::arg("score"), py::arg("music_id"));
    mpjsk.def("detect", [](const py::bytes& b) { return cv::pjsk::detect(std::string(b)); },
              py::arg("data"));

    auto mbdon = m.def_submodule("bdon");
    py::register_exception<cv::bdon::BdonFormatError>(mbdon, "BdonFormatError",
                                                      PyExc_ValueError);
    mbdon.def("loads",
              [](std::string data, bool guide_traces, bool trace_ends,
                 bool flick_body_collide) {
                  cv::bdon::Options o;
                  o.guide_traces = guide_traces;
                  o.trace_ends = trace_ends;
                  o.flick_body_collide = flick_body_collide;
                  return cv::bdon::loads(data, o);
              },
              py::arg("data"), py::arg("guide_traces") = true, py::arg("trace_ends") = true,
              py::arg("flick_body_collide") = true);

    auto mholo = m.def_submodule("holodori_sus");
    mholo.def("loads",
              [](const std::string& data) {
                  auto [sc, bez] = cv::holodori_sus::loads(data);
                  return py::make_tuple(std::make_shared<Score>(sc), bez);
              },
              py::arg("data"));

    auto mlvd = m.def_submodule("leveldata");
    mlvd.def("detect",
             [](const py::object& data, bool skip_gzip, bool skip_json) -> std::optional<std::string> {
                 std::string s = py::isinstance<py::bytes>(data)
                                     ? std::string(data.cast<py::bytes>())
                                     : data.cast<std::string>();
                 return cv::leveldata::detect(s, skip_gzip, skip_json);
             },
             py::arg("data"), py::arg("skip_gzip") = false, py::arg("skip_json") = false);

    auto mcc = mlvd.def_submodule("chart_cyanvas");
    mcc.def("export_bytes",
            [](Score& s, bool as_compressed) {
                object_lock lock(s);
                return py::bytes(cv::leveldata::chart_cyanvas::export_bytes(s, as_compressed));
            },
            py::arg("score"), py::arg("as_compressed") = true);
    py::register_exception<cv::leveldata::chart_cyanvas::UnsupportedFeatureError>(
        mcc, "UnsupportedFeatureError", PyExc_ValueError);
    mcc.def("load_text",
            [](const py::object& data, bool error_on_unsupported_feature) {
                std::string s = py::isinstance<py::bytes>(data)
                                    ? std::string(data.cast<py::bytes>())
                                    : data.cast<std::string>();
                cv::leveldata::chart_cyanvas::LoadOptions o;
                o.error_on_unsupported_feature = error_on_unsupported_feature;
                return cv::leveldata::chart_cyanvas::load_text(s, o);
            },
            py::arg("data"), py::arg("error_on_unsupported_feature") = true);

    auto mns = mlvd.def_submodule("next_sekai");
    mns.def("export_bytes",
            [](const Score& s, bool as_compressed, bool smooth_guide_fade, bool use_guide_layer) {
                object_lock lock(s);
                cv::leveldata::next_sekai::ExportOptions o;
                o.as_compressed = as_compressed;
                o.smooth_guide_fade = smooth_guide_fade;
                o.use_guide_layer = use_guide_layer;
                return py::bytes(cv::leveldata::next_sekai::export_bytes(s, o));
            },
            py::arg("score"), py::arg("as_compressed") = true,
            py::arg("smooth_guide_fade") = false, py::arg("use_guide_layer") = false);
    py::register_exception<cv::leveldata::next_sekai::UnsupportedFeatureError>(
        mns, "UnsupportedFeatureError", PyExc_ValueError);
    mns.def("load_text",
            [](const py::object& data, bool error_on_unsupported_feature, bool attempt_attached_note_convert) {
                std::string s = py::isinstance<py::bytes>(data)
                                    ? std::string(data.cast<py::bytes>())
                                    : data.cast<std::string>();
                cv::leveldata::next_sekai::LoadOptions o;
                o.error_on_unsupported_feature = error_on_unsupported_feature;
                o.attempt_attached_note_convert = attempt_attached_note_convert;
                return cv::leveldata::next_sekai::load_text(s, o);
            },
            py::arg("data"), py::arg("error_on_unsupported_feature") = true,
            py::arg("attempt_attached_note_convert") = true);

    auto mus = mlvd.def_submodule("untitled_sekai");
    mus.def("export_bytes",
            [](Score& s, bool as_compressed) {
                object_lock lock(s);
                return py::bytes(cv::leveldata::untitled_sekai::export_bytes(s, as_compressed));
            },
            py::arg("score"), py::arg("as_compressed") = true);
    mus.def("load_text", &cv::leveldata::untitled_sekai::load_text, py::arg("data"));

    m.def("detect",
          [](const py::bytes& b) -> std::optional<std::pair<std::string, std::string>> {
              return rg::detect(std::string(b));
          },
          py::arg("data"));
}
