#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rg/core/score.hpp"

// Every chart format, what it can do and hold, and its options: the one list the Python, C and
// Node.js APIs are built from.
namespace rg::formats {

enum class Feature {
    ExtendedLanes,
    Layers,
    NoteSpeedRatios,
    MeasureExtensions,
    FakeNotes,
    DamageNotes,
    SkillFever,
    HolodoriSkillFever,
    ExtendedEases,
    ExtendedGuideColors,
    GuideFades,
    CriticalEndOnPlainSlide,
    VolumeChanges,
};

struct FeatureInfo {
    Feature feature;
    std::string id;
    std::string name;
};
// In display order.
const std::vector<FeatureInfo>& features();
const FeatureInfo& feature_info(Feature feature);

// How a format holds a feature: what exporting does with it (for a load-only format, whether a
// loaded chart can have it). Lossy: kept approximately or in another form (note speed ratios as
// layers). Option: kept only with an export option.
enum class Support { Yes, Option, Lossy, No };
std::string support_name(Support support);

using core::Value;
using Options = std::map<std::string, Value>;

enum class OptionType { Bool, Int, String };
std::string option_type_name(OptionType type);

struct OptionInfo {
    std::string name;
    OptionType type;
    // nullopt: required.
    std::optional<Value> default_value;
};

struct Format {
    std::string id;
    std::string description;
    bool deprecated = false;
    // The file is bytes (gzip, base64, binary); otherwise it is text.
    bool binary = false;
    std::vector<std::string> file_types;
    std::map<Feature, Support> features;
    std::vector<OptionInfo> import_options;
    std::vector<OptionInfo> export_options;

    core::Score (*load_fn)(const std::string& data, const Options& options) = nullptr;
    core::ExportResult (*dumps_fn)(const core::Score& score, const Options& options) = nullptr;

    bool can_load() const { return load_fn != nullptr; }
    bool can_export() const { return dumps_fn != nullptr; }
    Support support(Feature feature) const;

    // Options are checked against import_options / export_options (unknown names, wrong types and
    // missing required ones throw std::invalid_argument). The loaded score's format_metadata has
    // "format" set to this id.
    core::Score load(const std::string& data, const Options& options = {}) const;
    core::ExportResult dumps(const core::Score& score, const Options& options = {}) const;
};

const std::vector<Format>& all();
// nullptr when there is no such format.
const Format* get(const std::string& id);
// Throws std::invalid_argument naming the known ids.
const Format& require(const std::string& id);

// The features a score uses, in display order.
std::vector<Feature> features_used(const core::Score& score);

}  // namespace rg::formats
