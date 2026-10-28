#include "rg/formats.hpp"

#include <algorithm>
#include <stdexcept>

#include "rg/converters/bdon.hpp"
#include "rg/converters/holodori_sus.hpp"
#include "rg/converters/leveldata/chart_cyanvas.hpp"
#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/converters/leveldata/untitled_sekai.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/converters/pjsk.hpp"
#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/converters/yume_sus.hpp"

namespace rg::formats {

using core::ExportResult;
using core::Kind;
using core::Node;
using core::Score;
namespace cv = rg::converters;

const std::vector<FeatureInfo>& features() {
    static const std::vector<FeatureInfo> list = {
        {Feature::ExtendedLanes, "extended_lanes", "Extended Lanes"},
        {Feature::Layers, "layers", "Layers"},
        {Feature::NoteSpeedRatios, "note_speed_ratios", "Note Speed Ratios"},
        {Feature::MeasureExtensions, "measure_extensions", "Measure Extensions"},
        {Feature::FakeNotes, "fake_notes", "Fake Notes"},
        {Feature::DamageNotes, "damage_notes", "Damage Notes"},
        {Feature::SkillFever, "skill_fever", "Skill & Fever"},
        {Feature::HolodoriSkillFever, "holodori_skill_fever", "hololive Dreams Skill & Fever"},
        {Feature::ExtendedEases, "extended_eases", "Extended Eases"},
        {Feature::ExtendedGuideColors, "extended_guide_colors", "Extended Guide Colors"},
        {Feature::GuideFades, "guide_fades", "Guide Fades"},
        {Feature::CriticalEndOnPlainSlide, "critical_end_on_plain_slide", "Critical End on Plain Slide"},
        {Feature::VolumeChanges, "volume_changes", "Volume Changes"},
    };
    return list;
}

const FeatureInfo& feature_info(Feature feature) {
    for (auto& info : features())
        if (info.feature == feature) return info;
    throw std::invalid_argument("Unknown feature");
}

std::string support_name(Support support) {
    switch (support) {
        case Support::Yes: return "yes";
        case Support::Option: return "option";
        case Support::Lossy: return "lossy";
        case Support::No: return "no";
    }
    return "no";
}

std::string option_type_name(OptionType type) {
    switch (type) {
        case OptionType::Bool: return "bool";
        case OptionType::Int: return "int";
        case OptionType::String: return "str";
    }
    return "str";
}

Support Format::support(Feature feature) const {
    auto it = features.find(feature);
    return it == features.end() ? Support::No : it->second;
}

namespace {

bool fits(OptionType type, const Value& value) {
    switch (type) {
        case OptionType::Bool: return std::holds_alternative<bool>(value);
        case OptionType::Int: return std::holds_alternative<long long>(value);
        case OptionType::String: return std::holds_alternative<std::string>(value);
    }
    return false;
}

Options checked(const Format& format, const std::vector<OptionInfo>& infos, const Options& given,
                const std::string& kind) {
    for (auto& [name, value] : given) {
        auto info = std::find_if(infos.begin(), infos.end(), [&](const OptionInfo& i) { return i.name == name; });
        if (info == infos.end()) {
            std::string known;
            for (auto& i : infos) known += (known.empty() ? "" : ", ") + i.name;
            throw std::invalid_argument(format.id + " has no " + kind + " option '" + name + "' (" +
                                        (known.empty() ? "it has none" : "it has " + known) + ")");
        }
        if (!fits(info->type, value))
            throw std::invalid_argument(format.id + " " + kind + " option '" + name + "' takes a " +
                                        option_type_name(info->type));
    }
    Options full;
    for (auto& info : infos) {
        auto it = given.find(info.name);
        if (it != given.end())
            full[info.name] = it->second;
        else if (info.default_value)
            full[info.name] = *info.default_value;
        else
            throw std::invalid_argument(format.id + " " + kind + " needs the '" + info.name + "' option");
    }
    return full;
}

bool flag(const Options& options, const char* name) { return std::get<bool>(options.at(name)); }
long long integer(const Options& options, const char* name) { return std::get<long long>(options.at(name)); }

OptionInfo boolean(const char* name, bool default_value) { return {name, OptionType::Bool, Value(default_value)}; }
OptionInfo required_int(const char* name) { return {name, OptionType::Int, std::nullopt}; }

using S = Support;
using Ft = Feature;

std::map<Feature, Support> support_table(S extended_lanes, S layers, S note_speed_ratios, S measure_extensions,
                                         S fake_notes, S damage_notes, S skill_fever, S holodori_skill_fever,
                                         S extended_eases, S extended_guide_colors, S guide_fades,
                                         S critical_end_on_plain_slide, S volume_changes) {
    return {
        {Ft::ExtendedLanes, extended_lanes},
        {Ft::Layers, layers},
        {Ft::NoteSpeedRatios, note_speed_ratios},
        {Ft::MeasureExtensions, measure_extensions},
        {Ft::FakeNotes, fake_notes},
        {Ft::DamageNotes, damage_notes},
        {Ft::SkillFever, skill_fever},
        {Ft::HolodoriSkillFever, holodori_skill_fever},
        {Ft::ExtendedEases, extended_eases},
        {Ft::ExtendedGuideColors, extended_guide_colors},
        {Ft::GuideFades, guide_fades},
        {Ft::CriticalEndOnPlainSlide, critical_end_on_plain_slide},
        {Ft::VolumeChanges, volume_changes},
    };
}

const std::string LEVEL_DATA = "The Sonolus LevelData format for the custom engine ";
const std::string MMWS = "Binary-based format for the ";

std::vector<Format> build() {
    const S Y = S::Yes, O = S::Option, L = S::Lossy, N = S::No;
    std::vector<Format> list;

    {
        Format f;
        f.id = "sus";
        f.description =
            "The classic old .sus format that has been forked many times. This specific implementation follows the "
            "PJSK .sus format, extended to support other .sus specification features.";
        f.file_types = {".sus"};
        f.features = support_table(N, O, O, O, N, O, Y, L, L, L, N, L, Y);
        f.import_options = {boolean("skip_note_speed_ratios", false)};
        f.export_options = {boolean("allow_layers", false), boolean("allow_damage", false),
                            boolean("allow_note_speed_ratios", false), boolean("allow_measure_extensions", false),
                            boolean("skip_shift", false)};
        f.load_fn = [](const std::string& data, const Options& o) {
            cv::sus::ImportOptions opts;
            opts.skip_note_speed_ratios = flag(o, "skip_note_speed_ratios");
            return cv::sus::loads(data, opts);
        };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::sus::ExportOptions opts;
            opts.allow_layers = flag(o, "allow_layers");
            opts.allow_damage = flag(o, "allow_damage");
            opts.allow_note_speed_ratios = flag(o, "allow_note_speed_ratios");
            opts.allow_measure_extensions = flag(o, "allow_measure_extensions");
            opts.skip_shift = flag(o, "skip_shift");
            return cv::sus::dumps(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "yume_sus";
        f.description = "A fork of the PJSK .sus format used by the game Yume (developed by Plumnet).";
        f.file_types = {".sus"};
        f.features = support_table(N, N, N, N, N, N, N, N, L, L, N, L, N);
        f.export_options = {boolean("skip_shift", false), boolean("strip_layers", false)};
        f.load_fn = [](const std::string& data, const Options&) { return cv::yume_sus::loads(data); };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::yume_sus::ExportOptions opts;
            opts.skip_shift = flag(o, "skip_shift");
            opts.strip_layers = flag(o, "strip_layers");
            return cv::yume_sus::dumps(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "holodori_sus";
        f.description =
            "A fork of the PJSK .sus format from back before PJSK's 3rd Anniversary for the game hololive Dreams, "
            "adding their own implementations of some features and changing how data is stored.";
        f.file_types = {".sus"};
        f.features = support_table(L, N, N, N, N, N, N, Y, N, L, Y, Y, N);
        f.load_fn = [](const std::string& data, const Options&) {
            auto [score, bezier_count] = cv::holodori_sus::loads(data);
            score.format_metadata["bezier_count"] = Value(bezier_count);
            return score;
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "usc";
        f.description =
            "An old json-based format made popular by custom charting apps and editors mimicking PJSK in Sonolus, "
            "adding some extended features.";
        f.deprecated = true;
        f.file_types = {".usc"};
        f.features = support_table(Y, Y, Y, Y, N, Y, Y, L, Y, Y, L, Y, N);
        f.export_options = {boolean("minified", true)};
        f.load_fn = [](const std::string& data, const Options&) { return cv::usc::load_text(data); };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::usc::ExportOptions opts;
            opts.minified = flag(o, "minified");
            return cv::usc::dumps(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "mmws";
        f.description = MMWS + "MikuMikuWorld chart editor.";
        f.binary = true;
        f.file_types = {".mmws"};
        f.features = support_table(N, N, N, Y, N, N, Y, L, L, L, N, Y, N);
        f.export_options = {boolean("strip_layers", false)};
        f.load_fn = [](const std::string& data, const Options&) { return cv::mmws::load_bytes(data, ".mmws"); };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::mmws::ExportOptions opts;
            opts.strip_layers = flag(o, "strip_layers");
            return cv::mmws::export_bytes(score, ".mmws", opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "ccmmws";
        f.description = MMWS + "MikuMikuWorld4CC chart editor.";
        f.binary = true;
        f.file_types = {".ccmmws"};
        f.features = support_table(Y, Y, L, Y, N, Y, Y, L, Y, Y, L, Y, N);
        f.load_fn = [](const std::string& data, const Options&) { return cv::mmws::load_bytes(data, ".ccmmws"); };
        f.dumps_fn = [](const Score& score, const Options&) { return cv::mmws::export_bytes(score, ".ccmmws"); };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "unchmmws";
        f.description = MMWS + "MikuMikuWorld4UC chart editor.";
        f.binary = true;
        f.file_types = {".unchmmws"};
        f.features = support_table(Y, Y, L, Y, Y, Y, Y, L, Y, Y, L, Y, N);
        f.load_fn = [](const std::string& data, const Options&) { return cv::mmws::load_bytes(data, ".unchmmws"); };
        f.dumps_fn = [](const Score& score, const Options&) { return cv::mmws::export_bytes(score, ".unchmmws"); };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "pjsk";
        f.description =
            "An internal class dump to JSON from the game PJSK, used as the format in their Score Maker gamemode.";
        f.binary = true;
        f.file_types = {".pjsk", ".json", ".json.gz"};
        f.features = support_table(L, N, L, Y, N, N, N, N, L, L, N, Y, Y);
        f.export_options = {required_int("music_id"), boolean("compressed", true), boolean("strip_layers", false)};
        f.load_fn = [](const std::string& data, const Options&) { return cv::pjsk::load_bytes(data); };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::pjsk::ExportOptions opts;
            opts.music_id = integer(o, "music_id");
            opts.compressed = flag(o, "compressed");
            opts.strip_layers = flag(o, "strip_layers");
            return cv::pjsk::export_bytes(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "bdon";
        f.description = "The primary JSON-based chart format for BanG Dream! Our Notes.";
        f.binary = true;
        f.file_types = {".bdon", ".json", ".json.gz"};
        f.features = support_table(Y, N, N, Y, N, N, N, Y, N, N, L, N, N);
        f.import_options = {boolean("guide_traces", true), boolean("trace_ends", true),
                            boolean("flick_body_collide", true)};
        f.load_fn = [](const std::string& data, const Options& o) {
            cv::bdon::ImportOptions opts;
            opts.guide_traces = flag(o, "guide_traces");
            opts.trace_ends = flag(o, "trace_ends");
            opts.flick_body_collide = flag(o, "flick_body_collide");
            return cv::bdon::loads(data, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "next_sekai";
        f.description = LEVEL_DATA + "\"Next SEKAI\".";
        f.binary = true;
        f.file_types = {".nsld", ".json.gz"};
        f.features = support_table(Y, Y, L, Y, Y, Y, Y, L, Y, Y, Y, Y, N);
        f.import_options = {boolean("error_on_unsupported_feature", true),
                            boolean("attempt_attached_note_convert", true)};
        f.export_options = {boolean("compressed", true), boolean("smooth_guide_fade", false),
                            boolean("use_guide_layer", false)};
        f.load_fn = [](const std::string& data, const Options& o) {
            cv::leveldata::next_sekai::ImportOptions opts;
            opts.error_on_unsupported_feature = flag(o, "error_on_unsupported_feature");
            opts.attempt_attached_note_convert = flag(o, "attempt_attached_note_convert");
            return cv::leveldata::next_sekai::load_text(data, opts);
        };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::leveldata::next_sekai::ExportOptions opts;
            opts.compressed = flag(o, "compressed");
            opts.smooth_guide_fade = flag(o, "smooth_guide_fade");
            opts.use_guide_layer = flag(o, "use_guide_layer");
            return cv::leveldata::next_sekai::export_bytes(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "chart_cyanvas";
        f.description = LEVEL_DATA + "\"Chart Cyanvas\".";
        f.deprecated = true;
        f.binary = true;
        f.file_types = {".json", ".json.gz"};
        f.features = support_table(Y, Y, L, Y, N, Y, N, N, Y, Y, L, Y, N);
        f.import_options = {boolean("error_on_unsupported_feature", true)};
        f.export_options = {boolean("compressed", true)};
        f.load_fn = [](const std::string& data, const Options& o) {
            cv::leveldata::chart_cyanvas::ImportOptions opts;
            opts.error_on_unsupported_feature = flag(o, "error_on_unsupported_feature");
            return cv::leveldata::chart_cyanvas::load_text(data, opts);
        };
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::leveldata::chart_cyanvas::ExportOptions opts;
            opts.compressed = flag(o, "compressed");
            return cv::leveldata::chart_cyanvas::export_bytes(score, opts);
        };
        list.push_back(std::move(f));
    }
    {
        Format f;
        f.id = "untitled_sekai";
        f.description = LEVEL_DATA + "\"Untitled Sekai\".";
        f.deprecated = true;
        f.binary = true;
        f.file_types = {".json", ".json.gz"};
        f.features = support_table(Y, Y, L, Y, N, N, N, N, L, Y, L, Y, N);
        f.export_options = {boolean("compressed", true)};
        f.dumps_fn = [](const Score& score, const Options& o) {
            cv::leveldata::untitled_sekai::ExportOptions opts;
            opts.compressed = flag(o, "compressed");
            return cv::leveldata::untitled_sekai::export_bytes(score, opts);
        };
        list.push_back(std::move(f));
    }
    return list;
}

const std::string BOM = "\xEF\xBB\xBF";

// ---- features_used ----

bool outside_lanes(double lane, double size) { return lane - size < -6 || lane + size > 6; }

bool is_extended_ease(const std::string& ease) { return ease == "inout" || ease == "outin"; }

}  // namespace

const std::vector<Format>& all() {
    static const std::vector<Format> list = build();
    return list;
}

const Format* get(const std::string& id) {
    for (auto& f : all())
        if (f.id == id) return &f;
    return nullptr;
}

const Format& require(const std::string& id) {
    if (const Format* f = get(id)) return *f;
    std::string known;
    for (auto& f : all()) known += (known.empty() ? "" : ", ") + f.id;
    throw std::invalid_argument("Unknown format '" + id + "' (known: " + known + ")");
}

Score Format::load(const std::string& data, const Options& options) const {
    if (!can_load()) throw std::invalid_argument(id + " can't be loaded, only exported");
    Options full = checked(*this, import_options, options, "import");
    bool bom = !binary && data.compare(0, BOM.size(), BOM) == 0;
    Score score = load_fn(bom ? data.substr(BOM.size()) : data, full);
    score.format_metadata["format"] = Value(id);
    return score;
}

ExportResult Format::dumps(const Score& score, const Options& options) const {
    if (!can_export()) throw std::invalid_argument(id + " can't be exported, only loaded");
    return dumps_fn(score, checked(*this, export_options, options, "export"));
}

std::vector<Feature> features_used(const Score& score) {
    std::map<Feature, bool> used;
    // Measure 1000 starts at beat 4000: every measure of a Score is 4 beats long.
    auto at = [&](const Node* n) {
        if (core::node_beat(const_cast<Node*>(n)) >= 4000) used[Ft::MeasureExtensions] = true;
    };
    auto point = [&](const Node* n) {
        if (outside_lanes(core::node_lane(n), core::node_size(n))) used[Ft::ExtendedLanes] = true;
        at(n);
    };
    for (auto& note_sp : score.notes) {
        const Node* note = note_sp.get();
        switch (note->kind) {
            case Kind::Single: {
                auto* s = static_cast<const core::Single*>(note);
                point(note);
                if (s->fake) used[Ft::FakeNotes] = true;
                if (s->type == "damage") used[Ft::DamageNotes] = true;
                break;
            }
            case Kind::Slide: {
                auto* slide = static_cast<const core::Slide*>(note);
                if (slide->fake) used[Ft::FakeNotes] = true;
                for (auto& p : slide->connections) {
                    point(p.get());
                    if (p->kind == Kind::SlideStartPoint &&
                        is_extended_ease(static_cast<const core::SlideStartPoint*>(p.get())->ease))
                        used[Ft::ExtendedEases] = true;
                    if (p->kind == Kind::SlideRelayPoint) {
                        auto* relay = static_cast<const core::SlideRelayPoint*>(p.get());
                        if (relay->fake) used[Ft::FakeNotes] = true;
                        if (is_extended_ease(relay->ease)) used[Ft::ExtendedEases] = true;
                    }
                    if (p->kind == Kind::SlideEndPoint && !slide->critical &&
                        static_cast<const core::SlideEndPoint*>(p.get())->critical)
                        used[Ft::CriticalEndOnPlainSlide] = true;
                }
                break;
            }
            case Kind::Guide: {
                auto* guide = static_cast<const core::Guide*>(note);
                if ((guide->color != "green" && guide->color != "yellow") || guide->color_code)
                    used[Ft::ExtendedGuideColors] = true;
                if (guide->fade != "out") used[Ft::GuideFades] = true;
                for (auto& p : guide->midpoints) {
                    point(p.get());
                    if (p->alpha) used[Ft::GuideFades] = true;
                    if (is_extended_ease(p->ease)) used[Ft::ExtendedEases] = true;
                }
                break;
            }
            case Kind::Skill:
            case Kind::FeverChance:
            case Kind::FeverStart:
                used[Ft::SkillFever] = true;
                at(note);
                break;
            case Kind::Volume:
                used[Ft::VolumeChanges] = true;
                at(note);
                break;
            case Kind::Bpm:
                at(note);
                break;
            default:
                if (core::is_holodori_event(note->kind)) {
                    used[Ft::HolodoriSkillFever] = true;
                    at(note);
                }
                break;
        }
    }
    if (score.layer_count() > 1) used[Ft::Layers] = true;
    if (score.has_speed_ratios()) used[Ft::NoteSpeedRatios] = true;
    std::vector<Feature> out;
    for (auto& info : features())
        if (used.count(info.feature)) out.push_back(info.feature);
    return out;
}

}  // namespace rg::formats
