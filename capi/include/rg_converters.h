/*
 * rg_converters C API: a stable C ABI over the rg-converters core.
 *
 * Conventions
 *   - Strings are UTF-8 and NUL-terminated. Every function that can fail returns an rg_status;
 *     on failure rg_last_error() holds the message and every out pointer is set to NULL / 0.
 *   - Memory the library returns through char** / uint8_t** is freed with rg_free, scores with
 *     rg_score_free. Never free it with your own allocator.
 *   - options_json / args_json may be NULL or "" (all defaults). Option names, types and defaults
 *     match the Python API; an unknown option, a wrong type or a missing required option is
 *     RG_ERR_INVALID_ARGUMENT.
 *   - A score handle is never modified: load, export, transform and rg_score_from_json return new
 *     scores. Every function may be called from any thread, concurrently, on the same or different
 *     scores (rg_score_free excepted: no other call may be using that score).
 *   - No C++ exception crosses the boundary and the library never aborts the host on bad input.
 *
 * Formats and options
 *   Format ids (shared with the CLI, the Python API and the npm package): sus, yume_sus,
 *   holodori_sus, usc, mmws, ccmmws, unchmmws, pjsk, bdon, next_sekai, chart_cyanvas,
 *   untitled_sekai. rg_formats() describes each one and its options; this table is the same.
 *   Format options are JSON booleans, integers or strings, as rg_formats() types them.
 *   load       sus             skip_note_speed_ratios=false
 *              yume_sus, holodori_sus, usc, pjsk  no options
 *              mmws, ccmmws, unchmmws  no options; each loads only its own variant
 *              bdon            guide_traces=true, trace_ends=true, flick_body_collide=true
 *              next_sekai      error_on_unsupported_feature=true, attempt_attached_note_convert=true
 *              chart_cyanvas   error_on_unsupported_feature=true
 *              (untitled_sekai can only be exported)
 *              info: the loaded score's format_metadata, e.g. {"format": "sus"};
 *              holodori_sus adds "bezier_count" (int)
 *   export     sus             allow_layers=false, allow_damage=false, allow_note_speed_ratios=false,
 *                              allow_measure_extensions=false, skip_shift=false
 *              yume_sus        skip_shift=false, strip_layers=false
 *              usc             minified=true
 *              mmws            strip_layers=false
 *              ccmmws, unchmmws  no options
 *              pjsk            music_id (int, required), compressed=true (false: plain JSON),
 *                              strip_layers=false
 *              next_sekai      compressed=true, smooth_guide_fade=false, use_guide_layer=false
 *              chart_cyanvas   compressed=true
 *              untitled_sekai  compressed=true
 *              (bdon and holodori_sus can only be loaded)
 *              A format that holds one layer fails (RG_ERR_FAILED) on a score with more unless
 *              strip_layers (sus: allow_layers) is set; sus also fails past measure 999 without
 *              allow_measure_extensions.
 *   transform  delete_fake_notes, delete_damage_notes, replace_extended_guide_colors,
 *              replace_extended_ease, sort_by_beat, add_point_without_fade, strip_speed_ratios,
 *              strip_layers, close_open_slides, flatten_speed_ratios_to_layers, shift,
 *              convert_holodori_events
 *              strip_extended_lanes    resize_if_possible=false
 *              export_overlaps_score   info: {"count": int}
 *              cut             start_at=null, end_at=null (numbers; at least one), keep_position=false
 *                              info: {"combo_before": int, "ticks": [int, int]}
 *   detect     no options; a JSON array of every format id the data could be, most likely
 *              first ([] when unrecognised)
 *   query      combo_count, note_count -> int; duration -> number; check_skill_overlap -> bool;
 *              time_at_beat {"target_beat": number} -> number;
 *              beat_at_time {"target_time": number} -> number;
 *              combo_events -> [[category, critical, beat], ...];
 *              features_used -> [feature id, ...] (in rg_formats() "features" order);
 *              format_metadata -> {str: bool | int | number | str}
 *
 * Format registry JSON (rg_formats)
 *   {"features": [{"id": str, "name": str}, ...],
 *    "formats": [{"id": str, "description": str, "deprecated": bool,
 *                 "binary": bool (the file is bytes: gzip, base64, binary; otherwise UTF-8 text),
 *                 "operations": ["load", "export"] (or just one of them),
 *                 "file_types": [str, ...] (e.g. ".sus"),
 *                 "features": {feature id: "yes" | "option" | "lossy" | "no", ...},
 *                 "import_options": [option, ...], "export_options": [option, ...]}, ...]}
 *   An option is {"name": str, "type": "bool" | "int" | "str", "required": bool,
 *   "default": value | null (null when required)}. A feature's support says what exporting does
 *   with it: "lossy" keeps it approximately or in another form, "option" only with an export
 *   option; for a load-only format, whether a loaded chart can have it.
 *
 * Score JSON (rg_score_to_json / rg_score_from_json)
 *   {"metadata": {"title": str, "artist": str, "designer": str, "waveoffset": number,
 *                 "requests": [str] | null},
 *    "format_metadata": {str: bool | int | number | str},
 *    "notes": [node, ...]}
 *   format_metadata is what loading the file told us beyond the chart: "format" (the id it was
 *   loaded as) and format-specific keys such as holodori_sus's "bezier_count". Exports and
 *   transforms keep it (export_overlaps_score's new score has {}, as does one made from JSON
 *   without it).
 *   A node is an object with "kind" (the Python class name) and that class's fields. Output always
 *   has every field; on input the fields marked ? may be left out (defaults as in Python), as may
 *   "metadata", "format_metadata" and any of their fields, and "kind" inside "changes" and
 *   "midpoints". Unknown keys are an error. Numbers keep int vs float (1 vs 1.0), which only
 *   changes how JSON exporters print them.
 *     Bpm                  beat, bpm, type?="bpm"
 *     TimeScaleGroup       changes?=[TimeScalePoint...], type?="timeScaleGroup"
 *     TimeScalePoint       beat, timeScale
 *     Volume               beat, volume, type?="volume"
 *     Skill, FeverChance, FeverStart, HolodoriChargeStart, HolodoriChargeEnd, HolodoriFeverStart,
 *     HolodoriFeverEnd     beat, type?=<class default, e.g. "skill", "feverChance">
 *     HolodoriSkill        beat, slot (int), type?="holodoriSkill"
 *     Single               beat, lane, size, timeScaleGroup (int), critical?=null (bool|null),
 *                          fake?=false, speedRatio?=1.0, trace?=null (bool|null),
 *                          direction?=null (str|null), type?="single"
 *     Slide                critical, connections (non-empty, Slide*Point), fake?=false, type?="slide"
 *     SlideStartPoint      beat, critical, ease, judgeType, lane, size, timeScaleGroup,
 *                          speedRatio?=1.0, type?="start"
 *     SlideRelayPoint      beat, ease, lane, size, timeScaleGroup, type ("tick"|"attach"),
 *                          critical?=null (bool|null), fake?=false, speedRatio?=1.0
 *     SlideEndPoint        beat, critical, judgeType, lane, size, timeScaleGroup, speedRatio?=1.0,
 *                          direction?=null (str|null), type?="end"
 *     Guide                color, fade, midpoints (non-empty, GuidePoint), type?="guide",
 *                          color_code?=null (str|null)
 *     GuidePoint           beat, ease, lane, size, timeScaleGroup, speedRatio?=1.0,
 *                          alpha?=null (number|null)
 */
#ifndef RG_CONVERTERS_H
#define RG_CONVERTERS_H

#include <stddef.h>
#include <stdint.h>

#if defined(RG_STATIC)
#define RG_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef RG_CAPI_BUILD
#define RG_API __declspec(dllexport)
#else
#define RG_API __declspec(dllimport)
#endif
#else
#define RG_API __attribute__((visibility("default")))
#endif

#if defined(_WIN32)
#define RG_CALL __cdecl
#else
#define RG_CALL
#endif

/* Bumped on any incompatible change to this header. */
#define RG_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rg_score rg_score;

typedef enum rg_status {
    RG_OK = 0,
    /* NULL where a value is required, malformed or mistyped options / score JSON. */
    RG_ERR_INVALID_ARGUMENT = 1,
    /* Unknown format, transform or query, or one a format can't do (loading untitled_sekai,
       exporting bdon). */
    RG_ERR_UNSUPPORTED = 2,
    /* The data could not be loaded. */
    RG_ERR_INVALID_DATA = 3,
    /* The file uses something a score can't hold (UnsupportedFeatureError; next_sekai and
       chart_cyanvas with error_on_unsupported_feature). */
    RG_ERR_UNSUPPORTED_FEATURE = 4,
    /* An exporter, transform or query failed on this score. */
    RG_ERR_FAILED = 5,
    RG_ERR_OUT_OF_MEMORY = 6,
    RG_ERR_INTERNAL = 7
} rg_status;

/* The rg-converters version, e.g. "3.0.0". Static storage. */
RG_API const char* RG_CALL rg_version(void);

/* RG_ABI_VERSION of the loaded library. */
RG_API int32_t RG_CALL rg_abi_version(void);

/* The message of this thread's last failed call ("" if the last call succeeded). Valid until the
   next rg_ call on this thread. */
RG_API const char* RG_CALL rg_last_error(void);

/* Frees a string or byte buffer returned by the library. NULL is a no-op. */
RG_API void RG_CALL rg_free(void* ptr);

/* Frees a score. NULL is a no-op. */
RG_API void RG_CALL rg_score_free(rg_score* score);

/* Every format and feature as JSON (see "Format registry JSON"). */
RG_API rg_status RG_CALL rg_formats(char** out_json);

/* Loads `size` bytes of `data` as `format`. out_info_json (may be NULL) receives the loaded
   score's format_metadata as a JSON object. */
RG_API rg_status RG_CALL rg_load(const char* format, const void* data, size_t size,
                                 const char* options_json, rg_score** out_score,
                                 char** out_info_json);

/* Exports `score` as `format`. *out_data receives the bytes written (text formats are UTF-8; a NUL
   byte not counted in *out_size follows them), out_written (may be NULL) the score as written. */
RG_API rg_status RG_CALL rg_export(const char* format, const rg_score* score,
                                   const char* options_json, uint8_t** out_data, size_t* out_size,
                                   rg_score** out_written);

/* Applies the transform `name`, returning a new score. out_info_json (may be NULL) receives a
   JSON object with any extra results (see export_overlaps_score, cut). */
RG_API rg_status RG_CALL rg_transform(const char* name, const rg_score* score,
                                      const char* options_json, rg_score** out_score,
                                      char** out_info_json);

/* Detects the format of `data`; *out_json receives a JSON array of format ids. */
RG_API rg_status RG_CALL rg_detect(const void* data, size_t size, const char* options_json,
                                   char** out_json);

/* The whole score as JSON. options_json: {"indent": int|null} (default null: compact). */
RG_API rg_status RG_CALL rg_score_to_json(const rg_score* score, const char* options_json,
                                          char** out_json);

/* A new score from JSON in the format rg_score_to_json writes. */
RG_API rg_status RG_CALL rg_score_from_json(const char* json, rg_score** out_score);

/* Runs the read-only query `name`; *out_json receives its JSON result. */
RG_API rg_status RG_CALL rg_score_query(const rg_score* score, const char* name,
                                        const char* args_json, char** out_json);

#ifdef __cplusplus
}
#endif

#endif /* RG_CONVERTERS_H */
