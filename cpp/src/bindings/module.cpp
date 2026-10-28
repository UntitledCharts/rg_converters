#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "num_caster.hpp"
#include "object_lock.hpp"

#include "rg/converters/bdon.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/core/transforms.hpp"
#include "rg/detect.hpp"
#include "rg/formats.hpp"

namespace py = pybind11;
using namespace rg::core;
using namespace rg::bindings;

namespace fm = rg::formats;

static py::list option_infos(const std::vector<fm::OptionInfo>& options) {
    py::list out;
    for (auto& o : options) {
        py::dict d;
        d["name"] = o.name;
        d["type"] = fm::option_type_name(o.type);
        d["required"] = !o.default_value.has_value();
        d["default"] = o.default_value ? py::cast(*o.default_value) : py::none();
        out.append(d);
    }
    return out;
}

static py::dict format_info(const fm::Format& f) {
    py::dict d;
    d["id"] = f.id;
    d["description"] = f.description;
    d["deprecated"] = f.deprecated;
    d["binary"] = f.binary;
    d["can_load"] = f.can_load();
    d["can_export"] = f.can_export();
    d["file_types"] = f.file_types;
    py::dict features;
    for (auto& info : fm::features()) features[py::str(info.id)] = fm::support_name(f.support(info.feature));
    d["features"] = features;
    d["import_options"] = option_infos(f.import_options);
    d["export_options"] = option_infos(f.export_options);
    return d;
}

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

    // ---- Score ----
    locked_class<Score, std::shared_ptr<Score>>(m, "Score")
        .def(py::init([](std::shared_ptr<MetaData> metadata, std::vector<NodePtr> notes) {
                 return std::make_shared<Score>(std::move(metadata), std::move(notes));
             }),
             py::arg("metadata"), py::arg("notes"))
        .def_field("metadata", &Score::metadata)
        .def_field("notes", &Score::notes)
        .def("check_skill_overlap", locked(&Score::check_skill_overlap))
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
        .def_property(
            "format_metadata",
            [](const Score& s) {
                std::map<std::string, Value> copy;
                {
                    object_lock lock(s);
                    copy = s.format_metadata;
                }
                py::dict d;
                for (auto& [key, value] : copy) d[py::str(key)] = py::cast(value);
                auto it = copy.find("format");
                if (it != copy.end() && std::holds_alternative<std::string>(it->second) &&
                    fm::get(std::get<std::string>(it->second)))
                    d["format"] = py::module_::import("rg_converters.format").attr("get")(it->second);
                return d;
            },
            [](Score& s, const py::dict& d) {
                std::map<std::string, Value> values;
                for (auto& [key, value] : d) {
                    std::string name = py::cast<std::string>(key);
                    values[name] = name == "format" && py::hasattr(value, "id") ? Value(py::cast<std::string>(value.attr("id")))
                                                                                : py::cast<Value>(value);
                }
                object_lock lock(s);
                s.format_metadata = std::move(values);
            });

    // ---- transforms: each returns a new Score and leaves the one passed in as it is ----
    namespace tf = rg::transforms;
    auto mtf = m.def_submodule("transforms");
    auto def_transform = [&](const char* name, Score (*f)(const Score&)) {
        mtf.def(name, [f](const Score& s) {
            object_lock lock(s);
            return f(s);
        }, py::arg("score"));
    };
    def_transform("delete_fake_notes", &tf::delete_fake_notes);
    def_transform("delete_damage_notes", &tf::delete_damage_notes);
    def_transform("replace_extended_guide_colors", &tf::replace_extended_guide_colors);
    def_transform("replace_extended_ease", &tf::replace_extended_ease);
    def_transform("sort_by_beat", &tf::sort_by_beat);
    def_transform("add_point_without_fade", &tf::add_point_without_fade);
    def_transform("strip_speed_ratios", &tf::strip_speed_ratios);
    def_transform("strip_layers", &tf::strip_layers);
    def_transform("close_open_slides", &tf::close_open_slides);
    def_transform("flatten_speed_ratios_to_layers", &tf::flatten_speed_ratios_to_layers);
    def_transform("shift", &tf::shift);
    def_transform("convert_holodori_events", &tf::convert_holodori_events);
    mtf.def("strip_extended_lanes",
            [](const Score& s, bool resize_if_possible) {
                object_lock lock(s);
                return tf::strip_extended_lanes(s, resize_if_possible);
            },
            py::arg("score"), py::arg("resize_if_possible") = false);
    mtf.def("export_overlaps_score",
            [](const Score& s) {
                object_lock lock(s);
                auto [sc, n] = tf::export_overlaps_score(s);
                return py::make_tuple(std::make_shared<Score>(std::move(sc)), n);
            },
            py::arg("score"));
    mtf.def("cut",
            [](const Score& s, std::optional<double> start_at, std::optional<double> end_at,
               bool keep_position) {
                object_lock lock(s);
                auto r = tf::cut(s, start_at, end_at, keep_position);
                return py::make_tuple(std::make_shared<Score>(std::move(r.score)), r.combo_before,
                                      py::make_tuple(r.ticks.first, r.ticks.second));
            },
            py::arg("score"), py::arg("start_at") = std::optional<double>(std::nullopt),
            py::arg("end_at") = std::optional<double>(std::nullopt),
            py::arg("keep_position") = false);

    // ---- formats: the registry every format API is built from ----
    py::register_exception<UnsupportedFeatureError>(m, "UnsupportedFeatureError", PyExc_ValueError);
    py::register_exception<rg::converters::bdon::BdonFormatError>(m, "BdonFormatError", PyExc_ValueError);

    auto mfmt = m.def_submodule("formats");
    mfmt.def("features", [] {
        py::list out;
        for (auto& f : fm::features()) out.append(py::make_tuple(f.id, f.name));
        return out;
    });
    mfmt.def("all", [] {
        py::list out;
        for (auto& f : fm::all()) out.append(format_info(f));
        return out;
    });
    mfmt.def("load",
             [](const std::string& id, const py::bytes& data, const fm::Options& options) {
                 return fm::require(id).load(std::string(data), options);
             },
             py::arg("format"), py::arg("data"), py::arg("options") = fm::Options{});
    mfmt.def("dumps",
             [](const std::string& id, const Score& score, const fm::Options& options) {
                 const fm::Format& f = fm::require(id);
                 ExportResult r = [&] {
                     object_lock lock(score);
                     return f.dumps(score, options);
                 }();
                 auto written = std::make_shared<Score>(std::move(r.score));
                 py::object data = f.binary ? py::object(py::bytes(r.data)) : py::object(py::str(r.data));
                 return py::make_tuple(data, written);
             },
             py::arg("format"), py::arg("score"), py::arg("options") = fm::Options{});
    mfmt.def("features_used",
             [](const Score& score) {
                 std::vector<fm::Feature> used;
                 {
                     object_lock lock(score);
                     used = fm::features_used(score);
                 }
                 std::vector<std::string> ids;
                 for (auto f : used) ids.push_back(fm::feature_info(f).id);
                 return ids;
             },
             py::arg("score"));

    m.def("detect", [](const py::bytes& b) { return rg::detect(std::string(b)); },
          py::arg("data"));
}
