#include "rg_converters.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rg/core/score.hpp"
#include "rg/core/transforms.hpp"
#include "rg/detect.hpp"
#include "rg/formats.hpp"
#include "rg/util/json.hpp"
#include "score_json.hpp"

#ifndef RG_VERSION
#error "RG_VERSION must be defined (CMake reads it from rg_converters/version.py)"
#endif

struct rg_score {
    rg::core::Score score;
};

namespace {

namespace fm = rg::formats;
namespace tf = rg::transforms;
using rg::core::ExportResult;
using rg::core::Score;
using rg::util::Json;

thread_local std::string t_last_error;

struct ApiError : std::runtime_error {
    rg_status status;
    ApiError(rg_status s, const std::string& message) : std::runtime_error(message), status(s) {}
};

[[noreturn]] void invalid(const std::string& message) {
    throw ApiError(RG_ERR_INVALID_ARGUMENT, message);
}

[[noreturn]] void unsupported(const std::string& message) {
    throw ApiError(RG_ERR_UNSUPPORTED, message);
}

rg_status fail(rg_status status, const char* message) {
    try {
        t_last_error = message;
    } catch (...) {
    }
    return status;
}

// Runs `body`, turning any exception into a status; one the core throws becomes `core_status`.
template <class F>
rg_status guard(rg_status core_status, F&& body) {
    try {
        t_last_error.clear();
        body();
        return RG_OK;
    } catch (const ApiError& e) {
        return fail(e.status, e.what());
    } catch (const rg::capi::SchemaError& e) {
        return fail(RG_ERR_INVALID_ARGUMENT, e.what());
    } catch (const rg::core::UnsupportedFeatureError& e) {
        return fail(RG_ERR_UNSUPPORTED_FEATURE, e.what());
    } catch (const std::bad_alloc&) {
        return fail(RG_ERR_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& e) {
        return fail(core_status, e.what());
    } catch (...) {
        return fail(RG_ERR_INTERNAL, "unknown error");
    }
}

char* dup_string(const std::string& s) {
    auto* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) throw std::bad_alloc();
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

std::string to_bytes(const void* data, size_t size) {
    if (!data && size) invalid("data is NULL");
    return size ? std::string(static_cast<const char*>(data), size) : std::string();
}

const rg::core::Score& score_of(const rg_score* score) {
    if (!score) invalid("score is NULL");
    return score->score;
}

class Options {
public:
    Options(const char* json, std::string context) : context_(std::move(context)) {
        if (!json || !*json) return;
        try {
            json_ = rg::util::json_parse(json);
        } catch (const std::exception& e) {
            invalid(context_ + ": options are not valid JSON (" + e.what() + ")");
        }
        if (!json_.is_null() && !json_.is_object())
            invalid(context_ + ": options must be a JSON object");
    }

    bool flag(const char* key, bool dflt) {
        const Json* p = get(key);
        if (!p) return dflt;
        if (p->type() != Json::Type::Bool) wrong_type(key, "a boolean");
        return p->as_bool();
    }

    std::optional<double> opt_number(const char* key) {
        const Json* p = get(key);
        if (!p || p->is_null()) return std::nullopt;
        if (p->type() != Json::Type::Int && p->type() != Json::Type::Float)
            wrong_type(key, "a number or null");
        return p->as_float();
    }

    double number(const char* key) {
        std::optional<double> v = opt_number(key);
        if (!v) missing(key);
        return *v;
    }

    std::optional<long long> opt_integer(const char* key) {
        const Json* p = get(key);
        if (!p || p->is_null()) return std::nullopt;
        if (p->type() != Json::Type::Int) wrong_type(key, "an integer");
        return p->as_int();
    }

    // The options of a format, typed as its registry entry says (JSON bool, integer or string);
    // any other key is an error. Defaults are left to the registry.
    fm::Options format(const std::vector<fm::OptionInfo>& infos) {
        fm::Options out;
        for (const auto& info : infos) {
            const char* key = info.name.c_str();
            const Json* p = get(key);
            if (!p) {
                if (!info.default_value) missing(key);
                continue;
            }
            switch (info.type) {
                case fm::OptionType::Bool:
                    if (p->type() != Json::Type::Bool) wrong_type(key, "a boolean");
                    out[info.name] = p->as_bool();
                    break;
                case fm::OptionType::Int:
                    if (p->type() != Json::Type::Int) wrong_type(key, "an integer");
                    out[info.name] = p->as_int();
                    break;
                case fm::OptionType::String:
                    if (p->type() != Json::Type::Str) wrong_type(key, "a string");
                    out[info.name] = p->as_str();
                    break;
            }
        }
        finish();
        return out;
    }

    // Call after reading every option and before doing any work: anything left is a typo.
    void finish() const {
        if (!json_.is_object()) return;
        for (const auto& kv : json_.items()) {
            bool known = false;
            for (const auto& k : seen_) known = known || k == kv.first;
            if (!known) invalid(context_ + ": unknown option " + quoted(kv.first) + known_options());
        }
    }

private:
    std::string context_;
    Json json_;
    std::vector<std::string> seen_;

    const Json* get(const char* key) {
        seen_.emplace_back(key);
        return json_.find(key);
    }
    std::string known_options() const {
        std::string known;
        for (const auto& k : seen_) known += (known.empty() ? "" : ", ") + k;
        return known.empty() ? " (it takes none)" : " (known: " + known + ")";
    }
    [[noreturn]] void wrong_type(const char* key, const char* what) const {
        invalid(context_ + ": option " + quoted(key) + " must be " + what);
    }
    [[noreturn]] void missing(const char* key) const {
        invalid(context_ + ": option " + quoted(key) + " is required");
    }
};

const fm::Format& find_format(const char* id, bool load) {
    std::string operation = load ? "load" : "export";
    if (!id) invalid(operation + " format is NULL");
    if (const fm::Format* f = fm::get(id)) {
        if (load ? !f->can_load() : !f->can_export())
            unsupported(f->id + (load ? " can only be exported" : " can only be loaded"));
        return *f;
    }
    std::string known;
    for (const auto& f : fm::all())
        if (load ? f.can_load() : f.can_export()) known += (known.empty() ? "" : ", ") + f.id;
    unsupported("unknown " + operation + " format " + quoted(id) + " (known: " + known + ")");
}

Json option_infos_json(const std::vector<fm::OptionInfo>& infos) {
    Json out = Json::array();
    for (const auto& info : infos) {
        Json o = Json::object();
        o.set("name", info.name);
        o.set("type", fm::option_type_name(info.type));
        o.set("required", !info.default_value.has_value());
        o.set("default", info.default_value ? rg::capi::value_to_json(*info.default_value) : Json());
        out.push_back(std::move(o));
    }
    return out;
}

Json formats_json() {
    Json features = Json::array();
    for (const auto& info : fm::features()) {
        Json o = Json::object();
        o.set("id", info.id);
        o.set("name", info.name);
        features.push_back(std::move(o));
    }
    Json formats = Json::array();
    for (const auto& f : fm::all()) {
        Json o = Json::object();
        o.set("id", f.id);
        o.set("description", f.description);
        o.set("deprecated", f.deprecated);
        o.set("binary", f.binary);
        Json operations = Json::array();
        if (f.can_load()) operations.push_back("load");
        if (f.can_export()) operations.push_back("export");
        o.set("operations", std::move(operations));
        Json file_types = Json::array();
        for (const auto& t : f.file_types) file_types.push_back(t);
        o.set("file_types", std::move(file_types));
        Json support = Json::object();
        for (const auto& info : fm::features()) support.set(info.id, fm::support_name(f.support(info.feature)));
        o.set("features", std::move(support));
        o.set("import_options", option_infos_json(f.import_options));
        o.set("export_options", option_infos_json(f.export_options));
        formats.push_back(std::move(o));
    }
    Json root = Json::object();
    root.set("features", std::move(features));
    root.set("formats", std::move(formats));
    return root;
}

struct TransformEntry {
    std::string_view name;
    Score (*fn)(const Score& score, Options& o, Json& info);
};

template <Score (*F)(const Score&)>
Score simple(const Score& s, Options& o, Json&) {
    o.finish();
    return F(s);
}

const TransformEntry kTransforms[] = {
    {"delete_fake_notes", &simple<&tf::delete_fake_notes>},
    {"delete_damage_notes", &simple<&tf::delete_damage_notes>},
    {"replace_extended_guide_colors", &simple<&tf::replace_extended_guide_colors>},
    {"replace_extended_ease", &simple<&tf::replace_extended_ease>},
    {"sort_by_beat", &simple<&tf::sort_by_beat>},
    {"add_point_without_fade", &simple<&tf::add_point_without_fade>},
    {"strip_speed_ratios", &simple<&tf::strip_speed_ratios>},
    {"strip_layers", &simple<&tf::strip_layers>},
    {"close_open_slides", &simple<&tf::close_open_slides>},
    {"flatten_speed_ratios_to_layers", &simple<&tf::flatten_speed_ratios_to_layers>},
    {"shift", &simple<&tf::shift>},
    {"convert_holodori_events", &simple<&tf::convert_holodori_events>},
    {"strip_extended_lanes",
     [](const Score& s, Options& o, Json&) {
         bool resize = o.flag("resize_if_possible", false);
         o.finish();
         return tf::strip_extended_lanes(s, resize);
     }},
    {"export_overlaps_score",
     [](const Score& s, Options& o, Json& info) {
         o.finish();
         auto result = tf::export_overlaps_score(s);
         info.set("count", Json::integer(result.second));
         return std::move(result.first);
     }},
    {"cut",
     [](const Score& s, Options& o, Json& info) {
         std::optional<double> start_at = o.opt_number("start_at");
         std::optional<double> end_at = o.opt_number("end_at");
         bool keep_position = o.flag("keep_position", false);
         o.finish();
         if (!start_at && !end_at) invalid("cut: at least one of start_at or end_at must be given");
         auto result = tf::cut(s, start_at, end_at, keep_position);
         Json ticks = Json::array();
         ticks.push_back(Json::integer(result.ticks.first));
         ticks.push_back(Json::integer(result.ticks.second));
         info.set("combo_before", Json::integer(result.combo_before));
         info.set("ticks", std::move(ticks));
         return std::move(result.score);
     }},
};

struct QueryEntry {
    std::string_view name;
    Json (*fn)(const Score& score, Options& o);
};

const QueryEntry kQueries[] = {
    {"combo_count", [](const Score& s, Options& o) { o.finish(); return Json::integer(s.combo_count()); }},
    {"note_count", [](const Score& s, Options& o) { o.finish(); return Json::integer(s.note_count()); }},
    {"duration", [](const Score& s, Options& o) { o.finish(); return Json(s.duration()); }},
    {"check_skill_overlap",
     [](const Score& s, Options& o) { o.finish(); return Json(s.check_skill_overlap()); }},
    {"time_at_beat",
     [](const Score& s, Options& o) {
         double beat = o.number("target_beat");
         o.finish();
         return Json(s.time_at_beat(beat));
     }},
    {"beat_at_time",
     [](const Score& s, Options& o) {
         double time = o.number("target_time");
         o.finish();
         return Json(s.beat_at_time(time));
     }},
    {"combo_events",
     [](const Score& s, Options& o) {
         o.finish();
         Json out = Json::array();
         for (const auto& e : s.combo_events()) {
             Json item = Json::array();
             item.push_back(e.category);
             item.push_back(e.critical);
             item.push_back(e.beat);
             out.push_back(std::move(item));
         }
         return out;
     }},
    {"features_used",
     [](const Score& s, Options& o) {
         o.finish();
         Json out = Json::array();
         for (fm::Feature f : fm::features_used(s)) out.push_back(fm::feature_info(f).id);
         return out;
     }},
    {"format_metadata",
     [](const Score& s, Options& o) {
         o.finish();
         return rg::capi::format_metadata_to_json(s);
     }},
};

template <class Entry, std::size_t N>
const Entry& find(const Entry (&table)[N], const char* name, const char* what) {
    if (!name) invalid(std::string(what) + " is NULL");
    for (const Entry& e : table)
        if (e.name == name) return e;
    std::string known;
    for (const Entry& e : table) known += (known.empty() ? "" : ", ") + std::string(e.name);
    unsupported("unknown " + std::string(what) + " " + quoted(name) + " (known: " + known + ")");
}

}  // namespace

extern "C" {

RG_API const char* RG_CALL rg_version(void) { return RG_VERSION; }

RG_API int32_t RG_CALL rg_abi_version(void) { return RG_ABI_VERSION; }

RG_API const char* RG_CALL rg_last_error(void) { return t_last_error.c_str(); }

RG_API void RG_CALL rg_free(void* ptr) { std::free(ptr); }

RG_API void RG_CALL rg_score_free(rg_score* score) { delete score; }

RG_API rg_status RG_CALL rg_formats(char** out_json) {
    if (out_json) *out_json = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_json) invalid("rg_formats: out_json is NULL");
        static const std::string json = formats_json().dump(-1, false);
        *out_json = dup_string(json);
    });
}

RG_API rg_status RG_CALL rg_load(const char* format, const void* data, size_t size,
                                 const char* options_json, rg_score** out_score,
                                 char** out_info_json) {
    if (out_score) *out_score = nullptr;
    if (out_info_json) *out_info_json = nullptr;
    return guard(RG_ERR_INVALID_DATA, [&] {
        if (!out_score) invalid("rg_load: out_score is NULL");
        const fm::Format& f = find_format(format, true);
        fm::Options options = Options(options_json, f.id + " load").format(f.import_options);
        auto score = std::make_unique<rg_score>(rg_score{f.load(to_bytes(data, size), options)});
        char* info_json =
            out_info_json ? dup_string(rg::capi::format_metadata_to_json(score->score).dump(-1, false)) : nullptr;
        *out_score = score.release();
        if (out_info_json) *out_info_json = info_json;
    });
}

RG_API rg_status RG_CALL rg_export(const char* format, const rg_score* score,
                                   const char* options_json, uint8_t** out_data, size_t* out_size,
                                   rg_score** out_written) {
    if (out_data) *out_data = nullptr;
    if (out_size) *out_size = 0;
    if (out_written) *out_written = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_data || !out_size) invalid("rg_export: out_data or out_size is NULL");
        const Score& input = score_of(score);
        const fm::Format& f = find_format(format, false);
        fm::Options options = Options(options_json, f.id + " export").format(f.export_options);
        ExportResult result = f.dumps(input, options);
        std::unique_ptr<rg_score> written;
        if (out_written) written = std::make_unique<rg_score>(rg_score{std::move(result.score)});
        char* bytes = dup_string(result.data);
        *out_data = reinterpret_cast<uint8_t*>(bytes);
        *out_size = result.data.size();
        if (out_written) *out_written = written.release();
    });
}

RG_API rg_status RG_CALL rg_transform(const char* name, const rg_score* score,
                                      const char* options_json, rg_score** out_score,
                                      char** out_info_json) {
    if (out_score) *out_score = nullptr;
    if (out_info_json) *out_info_json = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_score) invalid("rg_transform: out_score is NULL");
        const Score& input = score_of(score);
        const TransformEntry& entry = find(kTransforms, name, "transform");
        Options options(options_json, std::string(name));
        Json info = Json::object();
        auto result = std::make_unique<rg_score>(rg_score{entry.fn(input, options, info)});
        char* info_json = out_info_json ? dup_string(info.dump(-1, false)) : nullptr;
        *out_score = result.release();
        if (out_info_json) *out_info_json = info_json;
    });
}

RG_API rg_status RG_CALL rg_detect(const void* data, size_t size, const char* options_json,
                                   char** out_json) {
    if (out_json) *out_json = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_json) invalid("rg_detect: out_json is NULL");
        Options options(options_json, "detect");
        options.finish();
        Json ids = Json::array();
        for (auto& id : rg::detect(to_bytes(data, size))) ids.push_back(std::move(id));
        *out_json = dup_string(ids.dump(-1, false));
    });
}

RG_API rg_status RG_CALL rg_score_to_json(const rg_score* score, const char* options_json,
                                          char** out_json) {
    if (out_json) *out_json = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_json) invalid("rg_score_to_json: out_json is NULL");
        const Score& input = score_of(score);
        Options options(options_json, "rg_score_to_json");
        std::optional<long long> indent = options.opt_integer("indent");
        options.finish();
        if (indent && (*indent < 0 || *indent > 64)) invalid("rg_score_to_json: indent must be 0-64");
        *out_json = dup_string(rg::capi::score_to_json(input).dump(indent ? static_cast<int>(*indent) : -1, false));
    });
}

RG_API rg_status RG_CALL rg_score_from_json(const char* json, rg_score** out_score) {
    if (out_score) *out_score = nullptr;
    return guard(RG_ERR_INVALID_ARGUMENT, [&] {
        if (!out_score) invalid("rg_score_from_json: out_score is NULL");
        if (!json) invalid("rg_score_from_json: json is NULL");
        Json parsed;
        try {
            parsed = rg::util::json_parse(json);
        } catch (const std::exception& e) {
            invalid(std::string("score JSON is not valid JSON (") + e.what() + ")");
        }
        *out_score = new rg_score{rg::capi::score_from_json(parsed)};
    });
}

RG_API rg_status RG_CALL rg_score_query(const rg_score* score, const char* name,
                                        const char* args_json, char** out_json) {
    if (out_json) *out_json = nullptr;
    return guard(RG_ERR_FAILED, [&] {
        if (!out_json) invalid("rg_score_query: out_json is NULL");
        const Score& input = score_of(score);
        const QueryEntry& entry = find(kQueries, name, "query");
        Options options(args_json, std::string(name));
        *out_json = dup_string(entry.fn(input, options).dump(-1, false));
    });
}

}  // extern "C"
