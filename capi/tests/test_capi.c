/* Usage: rg_capi_test <test_files dir> [<yume sus chart> <expected combo>] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_converters.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s (last error: %s)\n", __FILE__,       \
                    __LINE__, #cond, rg_last_error());                                    \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

#define CHECK_STATUS(expr, expected)                                                      \
    do {                                                                                  \
        rg_status st_ = (expr);                                                           \
        if (st_ != (expected)) {                                                          \
            fprintf(stderr, "%s:%d: %s returned %d, expected %d (%s)\n", __FILE__,       \
                    __LINE__, #expr, (int)st_, (int)(expected), rg_last_error());         \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

#define CHECK_OK(expr) CHECK_STATUS(expr, RG_OK)

static const char* test_dir;

static unsigned char* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    unsigned char* data;
    long n;
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char*)malloc((size_t)n + 1);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

static void test_path(char* out, size_t size, const char* name) {
    snprintf(out, size, "%s/%s", test_dir, name);
}

static rg_score* load_path(const char* format, const char* path, const char* options) {
    size_t size;
    unsigned char* data = read_file(path, &size);
    rg_score* score = NULL;
    CHECK_OK(rg_load(format, data, size, options, &score, NULL));
    free(data);
    return score;
}

static rg_score* load_test_file(const char* format, const char* name) {
    char path[1024];
    test_path(path, sizeof path, name);
    return load_path(format, path, NULL);
}

static char* query(const rg_score* score, const char* name, const char* args) {
    char* out = NULL;
    CHECK_OK(rg_score_query(score, name, args, &out));
    return out;
}

static void check_query(const rg_score* score, const char* name, const char* args,
                        const char* expected) {
    char* out = query(score, name, args);
    if (!out || strcmp(out, expected) != 0) {
        fprintf(stderr, "query %s %s: %s, expected %s\n", name, args ? args : "",
                out ? out : "(none)", expected);
        ++failures;
    }
    rg_free(out);
}

static long long combo(const rg_score* score) {
    char* out;
    long long n;
    if (!score) return -1;
    out = query(score, "combo_count", NULL);
    n = out ? atoll(out) : -1;
    rg_free(out);
    return n;
}

static char* to_json(const rg_score* score) {
    char* out = NULL;
    CHECK_OK(rg_score_to_json(score, NULL, &out));
    return out;
}

static char* detect(const void* data, size_t size) {
    char* out = NULL;
    CHECK_OK(rg_detect(data, size, NULL, &out));
    return out;
}

static void check_detected(const void* data, size_t size, const char* id, const char* what) {
    char quoted[64];
    char* ids = detect(data, size);
    snprintf(quoted, sizeof quoted, "\"%s\"", id);
    if (!ids || ids[0] != '[' || !strstr(ids, quoted)) {
        fprintf(stderr, "%s detected as %s, expected it to include %s\n", what,
                ids ? ids : "(none)", quoted);
        ++failures;
    }
    rg_free(ids);
}

/* Exports, checks the detected format, loads the result back and returns its combo count. */
static long long round_trip(const rg_score* score, const char* format, const char* options,
                            const char* load_format, const char* expected_id) {
    uint8_t* data = NULL;
    size_t size = 0;
    rg_score* written = NULL;
    rg_score* loaded = NULL;
    long long n = -1;
    CHECK_OK(rg_export(format, score, options, &data, &size, &written));
    if (!data) return -1;
    CHECK(data[size] == 0);
    CHECK(written != NULL);
    if (expected_id) check_detected(data, size, expected_id, format);
    CHECK_OK(rg_load(load_format, data, size, NULL, &loaded, NULL));
    n = combo(loaded);
    CHECK(combo(written) == n);
    rg_score_free(loaded);
    rg_score_free(written);
    rg_free(data);
    return n;
}

static void test_version(void) {
    CHECK(strcmp(rg_version(), RG_EXPECTED_VERSION) == 0);
    CHECK(rg_abi_version() == RG_ABI_VERSION);
}

static void test_test_files(void) {
    static const struct {
        const char* format;
        const char* name;
        long long combo; /* -1: any positive count */
    } files[] = {
        {"sus", "88s_append.sus", -1},
        {"sus", "asyoulikeit_append.sus", -1},
        {"sus", "chounights_master.sus", -1},
        {"sus", "empurple_append.sus", -1},
        {"sus", "intense_voice_full_append.sus", -1},
        {"sus", "nouten_master.sus", -1},
        {"sus", "rollinggirl_master.sus", -1},
        {"pjsk", "88s_append.pjsk.json", 1388},
        {"pjsk", "empurple_append.pjsk.json", 1126},
        {"pjsk", "intense_voice_full_append.pjsk.json", 4500},
    };
    size_t i;
    for (i = 0; i < sizeof files / sizeof files[0]; ++i) {
        rg_score* score = load_test_file(files[i].format, files[i].name);
        long long n = combo(score);
        if (files[i].combo < 0 ? n <= 0 : n != files[i].combo) {
            fprintf(stderr, "%s: combo %lld, expected %lld\n", files[i].name, n, files[i].combo);
            ++failures;
        }
        if (strcmp(files[i].format, "sus") == 0) {
            char path[1024];
            size_t size;
            unsigned char* data;
            test_path(path, sizeof path, files[i].name);
            data = read_file(path, &size);
            check_detected(data, size, "sus", files[i].name);
            free(data);
        }
        rg_score_free(score);
    }
}

static void test_exports(const rg_score* score) {
    long long expected = combo(score);
    char* before = to_json(score);
    char* after;
    CHECK(round_trip(score, "sus", NULL, "sus", "sus") == expected);
    CHECK(round_trip(score, "sus", "{\"allow_layers\":true,\"skip_shift\":true}", "sus", NULL) ==
          expected);
    CHECK(round_trip(score, "sus",
                     "{\"allow_damage\":true,\"allow_note_speed_ratios\":true,"
                     "\"allow_measure_extensions\":true}",
                     "sus", "sus") == expected);
    CHECK(round_trip(score, "yume_sus", NULL, "yume_sus", "yume_sus") == expected);
    CHECK(round_trip(score, "yume_sus", "{\"skip_shift\":true,\"strip_layers\":true}", "yume_sus",
                     "yume_sus") == expected);
    CHECK(round_trip(score, "usc", "{\"minified\":false}", "usc", "usc") == expected);
    CHECK(round_trip(score, "mmws", NULL, "mmws", "mmws") == expected);
    CHECK(round_trip(score, "mmws", "{\"strip_layers\":true}", "mmws", "mmws") == expected);
    CHECK(round_trip(score, "ccmmws", NULL, "ccmmws", "ccmmws") == expected);
    CHECK(round_trip(score, "unchmmws", NULL, "unchmmws", "unchmmws") == expected);
    CHECK(round_trip(score, "pjsk", "{\"music_id\":1}", "pjsk", "pjsk") == expected);
    CHECK(round_trip(score, "pjsk", "{\"music_id\":1,\"compressed\":false,\"strip_layers\":true}",
                     "pjsk", "pjsk") == expected);
    CHECK(round_trip(score, "next_sekai", NULL, "next_sekai", "next_sekai") == expected);
    CHECK(round_trip(score, "next_sekai", "{\"compressed\":false,\"use_guide_layer\":true}",
                     "next_sekai", "next_sekai") == expected);
    CHECK(round_trip(score, "chart_cyanvas", NULL, "chart_cyanvas", "chart_cyanvas") == expected);
    CHECK(round_trip(score, "chart_cyanvas", "{\"compressed\":false}", "chart_cyanvas",
                     "chart_cyanvas") == expected);
    {
        uint8_t* data = NULL;
        size_t size = 0;
        CHECK_OK(rg_export("pjsk", score, "{\"music_id\":7,\"compressed\":false}", &data, &size, NULL));
        CHECK(data && data[0] == '{' && strstr((const char*)data, "\"MusicId\":7") != NULL);
        rg_free(data);
    }
    {
        uint8_t* data = NULL;
        size_t size = 0;
        rg_score* loaded = (rg_score*)1;
        CHECK_OK(rg_export("untitled_sekai", score, "{\"compressed\":false}", &data, &size, NULL));
        CHECK(data != NULL && size > 0);
        if (data) check_detected(data, size, "untitled_sekai", "untitled_sekai");
        CHECK_STATUS(rg_load("untitled_sekai", data, size, NULL, &loaded, NULL),
                     RG_ERR_UNSUPPORTED);
        CHECK(loaded == NULL);
        rg_free(data);
    }
    after = to_json(score);
    CHECK(before && after && strcmp(before, after) == 0);
    rg_free(before);
    rg_free(after);
}

static void test_transforms(const rg_score* score) {
    static const char* simple[] = {"delete_fake_notes", "delete_damage_notes",
                                   "replace_extended_guide_colors", "replace_extended_ease",
                                   "sort_by_beat", "add_point_without_fade",
                                   "strip_speed_ratios", "strip_layers", "close_open_slides",
                                   "flatten_speed_ratios_to_layers", "convert_holodori_events"};
    long long expected = combo(score);
    char* before = to_json(score);
    char* after;
    char* info = NULL;
    rg_score* out = NULL;
    size_t i;

    CHECK_OK(rg_transform("shift", score, NULL, &out, &info));
    CHECK(combo(out) == expected);
    CHECK(info && strcmp(info, "{}") == 0);
    rg_score_free(out);
    rg_free(info);

    CHECK_OK(rg_transform("cut", score, "{\"start_at\":10,\"end_at\":50.0}", &out, &info));
    CHECK(combo(out) > 0 && combo(out) < expected);
    CHECK(info && strncmp(info, "{\"combo_before\":", 16) == 0);
    CHECK(info && strstr(info, ",\"ticks\":[14000,70000]}") != NULL);
    rg_score_free(out);
    rg_free(info);

    CHECK_OK(rg_transform("export_overlaps_score", score, NULL, &out, &info));
    CHECK(info && strncmp(info, "{\"count\":", 9) == 0);
    rg_score_free(out);
    rg_free(info);

    CHECK_OK(rg_transform("strip_extended_lanes", score, "{\"resize_if_possible\":true}", &out,
                          NULL));
    CHECK(combo(out) == expected);
    rg_score_free(out);

    for (i = 0; i < sizeof simple / sizeof simple[0]; ++i) {
        CHECK_OK(rg_transform(simple[i], score, "{}", &out, NULL));
        CHECK(out != NULL);
        rg_score_free(out);
    }

    after = to_json(score);
    CHECK(before && after && strcmp(before, after) == 0);
    rg_free(before);
    rg_free(after);
}

static void test_queries(const rg_score* score) {
    char* combo_count = query(score, "combo_count", NULL);
    if (combo_count) check_query(score, "note_count", NULL, combo_count);
    rg_free(combo_count);
    check_query(score, "check_skill_overlap", NULL, "false");
    check_query(score, "time_at_beat", "{\"target_beat\":10}", "3.4285714285714284");
    check_query(score, "beat_at_time", "{\"target_time\":10}", "29.166666666666668");
}

static void test_json(const rg_score* score) {
    static const char* edited =
        "{\"metadata\":{\"title\":\"\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88\",\"waveoffset\":0.5},"
        "\"notes\":["
        "{\"kind\":\"Bpm\",\"beat\":0,\"bpm\":120.0},"
        "{\"kind\":\"TimeScaleGroup\",\"changes\":[{\"beat\":0,\"timeScale\":1.0}]},"
        "{\"kind\":\"Single\",\"beat\":1,\"lane\":0,\"size\":1.5,\"timeScaleGroup\":0,"
        "\"critical\":true,\"trace\":false,\"direction\":\"up\"},"
        "{\"kind\":\"Single\",\"beat\":1.5,\"lane\":2,\"size\":1,\"timeScaleGroup\":0,"
        "\"critical\":false,\"fake\":true,\"trace\":false},"
        "{\"kind\":\"Slide\",\"critical\":false,\"connections\":["
        "{\"kind\":\"SlideStartPoint\",\"beat\":2,\"critical\":false,\"ease\":\"linear\","
        "\"judgeType\":\"normal\",\"lane\":0,\"size\":1,\"timeScaleGroup\":0},"
        "{\"kind\":\"SlideRelayPoint\",\"beat\":2.5,\"ease\":\"linear\",\"lane\":1,\"size\":1,"
        "\"timeScaleGroup\":0,\"type\":\"tick\",\"critical\":false},"
        "{\"kind\":\"SlideEndPoint\",\"beat\":3,\"critical\":false,\"judgeType\":\"normal\","
        "\"lane\":0,\"size\":1,\"timeScaleGroup\":0,\"speedRatio\":2}]},"
        "{\"kind\":\"Guide\",\"color\":\"red\",\"fade\":\"none\",\"midpoints\":["
        "{\"beat\":4,\"ease\":\"linear\",\"lane\":0,\"size\":1,\"timeScaleGroup\":0,"
        "\"alpha\":0.5},"
        "{\"kind\":\"GuidePoint\",\"beat\":5,\"ease\":\"linear\",\"lane\":0,\"size\":1,"
        "\"timeScaleGroup\":0}]},"
        "{\"kind\":\"HolodoriSkill\",\"beat\":6,\"slot\":2},"
        "{\"kind\":\"HolodoriFeverEnd\",\"beat\":7}]}";
    char* json = to_json(score);
    char* again = NULL;
    char* pretty = NULL;
    rg_score* copy = NULL;
    rg_score* made = NULL;

    CHECK_OK(rg_score_from_json(json, &copy));
    again = to_json(copy);
    CHECK(json && again && strcmp(json, again) == 0);
    CHECK(combo(copy) == combo(score));
    CHECK_OK(rg_score_to_json(score, "{\"indent\":2}", &pretty));
    CHECK(pretty && strncmp(pretty, "{\n  \"metadata\": {", 17) == 0);
    rg_free(json);
    rg_free(again);
    rg_free(pretty);
    rg_score_free(copy);

    CHECK_OK(rg_score_from_json(edited, &made));
    CHECK(combo(made) == 5);
    check_query(made, "combo_events", NULL,
                "[[\"flicks\",true,1.0],[\"long_starts\",false,2.0],[\"long_relays\",false,2.5],"
                "[\"long_ends\",false,3.0],[\"long_continuations\",false,2.5]]");
    check_query(made, "time_at_beat", "{\"target_beat\":10}", "5.0");
    check_query(made, "beat_at_time", "{\"target_time\":5}", "10.0");
    json = to_json(made);
    CHECK(json && strstr(json, "\"title\":\"\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88\"") != NULL);
    CHECK(json && strstr(json, "{\"kind\":\"Single\",\"beat\":1.5,\"critical\":false,\"lane\":2,"
                               "\"size\":1,\"fake\":true,\"timeScaleGroup\":0,"
                               "\"speedRatio\":1.0,\"trace\":false,\"direction\":null,"
                               "\"type\":\"single\"}") != NULL);
    CHECK(json && strstr(json, "\"midpoints\":[{\"kind\":\"GuidePoint\",\"beat\":4,"
                               "\"ease\":\"linear\",\"lane\":0,\"size\":1,\"timeScaleGroup\":0,"
                               "\"speedRatio\":1.0,\"alpha\":0.5},{\"kind\":\"GuidePoint\","
                               "\"beat\":5,\"ease\":\"linear\",\"lane\":0,\"size\":1,"
                               "\"timeScaleGroup\":0,\"speedRatio\":1.0,\"alpha\":null}],"
                               "\"type\":\"guide\",\"color_code\":null}") != NULL);
    CHECK(json && strstr(json, "{\"kind\":\"HolodoriSkill\",\"beat\":6,\"slot\":2,"
                               "\"type\":\"holodoriSkill\"}") != NULL);
    rg_free(json);
    CHECK(round_trip(made, "next_sekai", NULL, "next_sekai", "next_sekai") == 5);
    rg_score_free(made);
}

static void expect_error(rg_status status, rg_status expected, const char* fragment, int line) {
    const char* message = rg_last_error();
    if (status != expected || !message || !strstr(message, fragment)) {
        fprintf(stderr, "line %d: status %d (expected %d), message \"%s\" (expected \"%s\")\n",
                line, (int)status, (int)expected, message ? message : "", fragment);
        ++failures;
    }
}

#define EXPECT_ERROR(expr, expected, fragment) \
    expect_error((expr), (expected), (fragment), __LINE__)

static void test_errors(const rg_score* score) {
    static const uint8_t garbage[] = {0x00, 0xff, 0x13, 0x37};
    static const char lvd[] =
        "{\"bgmOffset\":0,\"entities\":[{\"archetype\":\"Initialization\",\"data\":[]},"
        "{\"archetype\":\"SomethingNew\",\"data\":[]}]}";
    rg_score* out = (rg_score*)1;
    uint8_t* data = (uint8_t*)1;
    size_t size = 1;
    char* json = (char*)1;

    EXPECT_ERROR(rg_load("nope", garbage, sizeof garbage, NULL, &out, NULL), RG_ERR_UNSUPPORTED,
                 "unknown load format \"nope\"");
    CHECK(out == NULL);
    EXPECT_ERROR(rg_load(NULL, garbage, sizeof garbage, NULL, &out, NULL),
                 RG_ERR_INVALID_ARGUMENT, "NULL");
    EXPECT_ERROR(rg_load("mmws", garbage, sizeof garbage, NULL, &out, NULL), RG_ERR_INVALID_DATA,
                 "signature");
    EXPECT_ERROR(rg_load("next_sekai", garbage, sizeof garbage, NULL, &out, NULL),
                 RG_ERR_INVALID_DATA, "");
    EXPECT_ERROR(rg_load("usc", "{", 1, NULL, &out, NULL), RG_ERR_INVALID_DATA, "");
    EXPECT_ERROR(rg_load("bdon", "{}", 2, "{\"trace_ends\":1}", &out, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"trace_ends\" must be a boolean");
    EXPECT_ERROR(rg_load("sus", "", 0, "{\"bogus\":true}", &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "sus load: unknown option \"bogus\" (known: skip_note_speed_ratios)");
    EXPECT_ERROR(rg_load("usc", "", 0, "{\"minified\":true}", &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "usc load: unknown option \"minified\" (it takes none)");
    EXPECT_ERROR(rg_load("next_sekai", "", 0, "{\"error_on_unsupported_feature\":null}", &out, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"error_on_unsupported_feature\" must be a boolean");
    EXPECT_ERROR(rg_load("sus", "", 0, "[true]", &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "options must be a JSON object");
    EXPECT_ERROR(rg_load("untitled_sekai", "", 0, NULL, &out, NULL), RG_ERR_UNSUPPORTED,
                 "untitled_sekai can only be exported");
    EXPECT_ERROR(rg_load("sus", "", 0, "{not json", &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "not valid JSON");
    EXPECT_ERROR(rg_load("sus", NULL, 5, NULL, &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "data is NULL");
    EXPECT_ERROR(rg_load("sus", "", 0, NULL, NULL, NULL), RG_ERR_INVALID_ARGUMENT, "out_score");
    EXPECT_ERROR(rg_load("next_sekai", lvd, sizeof lvd - 1, NULL, &out, NULL),
                 RG_ERR_UNSUPPORTED_FEATURE, "SomethingNew");

    EXPECT_ERROR(rg_export("bdon", score, NULL, &data, &size, NULL), RG_ERR_UNSUPPORTED,
                 "can only be loaded");
    CHECK(data == NULL && size == 0);
    EXPECT_ERROR(rg_export("mmws", score, "{\"format\":\".mmws\"}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "unknown option \"format\" (known: strip_layers)");
    EXPECT_ERROR(rg_export("sus", score, "{\"allow_extended_lanes\":true}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT,
                 "sus export: unknown option \"allow_extended_lanes\" (known: allow_layers, "
                 "allow_damage, allow_note_speed_ratios, allow_measure_extensions, skip_shift)");
    EXPECT_ERROR(rg_export("next_sekai", score, "{\"as_compressed\":false}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "unknown option \"as_compressed\"");
    EXPECT_ERROR(rg_export("next_sekai", score, "{\"compressed\":0}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "next_sekai export: option \"compressed\" must be a boolean");
    EXPECT_ERROR(rg_export("pjsk", score, NULL, &data, &size, NULL), RG_ERR_INVALID_ARGUMENT,
                 "pjsk export: option \"music_id\" is required");
    EXPECT_ERROR(rg_export("pjsk", score, "{\"compressed\":false}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"music_id\" is required");
    EXPECT_ERROR(rg_export("pjsk", score, "{\"music_id\":\"1\"}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"music_id\" must be an integer");
    EXPECT_ERROR(rg_export("pjsk", score, "{\"music_id\":1.5}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"music_id\" must be an integer");
    EXPECT_ERROR(rg_export("pjsk", score, "{\"music_id\":null}", &data, &size, NULL),
                 RG_ERR_INVALID_ARGUMENT, "\"music_id\" must be an integer");
    CHECK(data == NULL && size == 0);
    EXPECT_ERROR(rg_export("sus", NULL, NULL, &data, &size, NULL), RG_ERR_INVALID_ARGUMENT,
                 "score is NULL");

    EXPECT_ERROR(rg_transform("cut", score, NULL, &out, NULL), RG_ERR_INVALID_ARGUMENT,
                 "start_at");
    EXPECT_ERROR(rg_transform("melt", score, NULL, &out, NULL), RG_ERR_UNSUPPORTED,
                 "known: delete_fake_notes");
    EXPECT_ERROR(rg_score_query(score, "time_at_beat", NULL, &json), RG_ERR_INVALID_ARGUMENT,
                 "\"target_beat\" is required");
    CHECK(json == NULL);
    EXPECT_ERROR(rg_detect("", 0, "{\"skip_gzip\":true}", &json), RG_ERR_INVALID_ARGUMENT,
                 "unknown option \"skip_gzip\"");
    json = detect(garbage, sizeof garbage);
    CHECK(json && strcmp(json, "[]") == 0);
    rg_free(json);
    EXPECT_ERROR(rg_score_to_json(score, "{\"indent\":-1}", &json), RG_ERR_INVALID_ARGUMENT,
                 "indent");

    EXPECT_ERROR(rg_score_from_json("[]", &out), RG_ERR_INVALID_ARGUMENT, "must be an object");
    EXPECT_ERROR(rg_score_from_json("{\"notes\":[{\"kind\":\"Single\",\"lane\":0,\"size\":1,"
                                    "\"timeScaleGroup\":0}]}",
                                    &out),
                 RG_ERR_INVALID_ARGUMENT, "at notes[0]: \"beat\" is required");
    EXPECT_ERROR(rg_score_from_json("{\"notes\":[{\"kind\":\"Slide\",\"critical\":false,"
                                    "\"connections\":[{\"kind\":\"Bpm\",\"beat\":0,\"bpm\":1}]}]}",
                                    &out),
                 RG_ERR_INVALID_ARGUMENT, "at notes[0].connections[0]");
    EXPECT_ERROR(rg_score_from_json("{\"notes\":[{\"kind\":\"Slide\",\"critical\":false,"
                                    "\"connections\":[]}]}",
                                    &out),
                 RG_ERR_INVALID_ARGUMENT, "must not be empty");
    EXPECT_ERROR(
        rg_score_from_json("{\"notes\":[{\"kind\":\"Bpm\",\"beat\":0,\"bpm\":1,\"bmp\":2}]}", &out),
        RG_ERR_INVALID_ARGUMENT, "unknown key \"bmp\"");
    EXPECT_ERROR(rg_score_from_json("{\"notes\":[{\"kind\":\"Laser\"}]}", &out),
                 RG_ERR_INVALID_ARGUMENT, "unknown kind \"Laser\"");
    EXPECT_ERROR(rg_score_from_json("{\"metadata\":{\"title\":1}}", &out), RG_ERR_INVALID_ARGUMENT,
                 "at metadata: \"title\" must be a string");
    EXPECT_ERROR(rg_score_from_json("{\"format_metadata\":{\"format\":[]}}", &out),
                 RG_ERR_INVALID_ARGUMENT,
                 "at format_metadata: \"format\" must be a boolean, a number or a string");
    EXPECT_ERROR(rg_score_from_json("{\"format_metadata\":[]}", &out), RG_ERR_INVALID_ARGUMENT,
                 "at format_metadata: must be an object");
    EXPECT_ERROR(rg_score_from_json("{", &out), RG_ERR_INVALID_ARGUMENT, "not valid JSON");
    CHECK(out == NULL);

    EXPECT_ERROR(rg_formats(NULL), RG_ERR_INVALID_ARGUMENT, "out_json");

    json = query(score, "combo_count", NULL);
    CHECK(rg_last_error()[0] == '\0');
    rg_free(json);
    rg_free(NULL);
    rg_score_free(NULL);
}

static int count(const char* haystack, const char* needle) {
    int n = 0;
    while ((haystack = strstr(haystack, needle)) != NULL) {
        ++n;
        haystack += strlen(needle);
    }
    return n;
}

static void test_formats(void) {
    static const char prefix[] = "{\"features\":[{\"id\":\"extended_lanes\",\"name\":\"Extended Lanes\"},";
    char* json = NULL;
    char* again = NULL;
    CHECK_OK(rg_formats(&json));
    CHECK_OK(rg_formats(&again));
    CHECK(json && again && json != again && strcmp(json, again) == 0);
    if (json) {
        CHECK(strncmp(json, prefix, sizeof prefix - 1) == 0);
        CHECK(count(json, "\"name\":\"") == 13 + 24);
        CHECK(count(json, "\"operations\":") == 12);
        CHECK(strstr(json, "],\"formats\":[{\"id\":\"sus\",\"description\":\"The classic old .sus format") != NULL);
        CHECK(strstr(json, "\"deprecated\":false,\"binary\":false,\"operations\":[\"load\",\"export\"],"
                           "\"file_types\":[\".sus\"],\"features\":{\"extended_lanes\":\"no\","
                           "\"layers\":\"option\",") != NULL);
        CHECK(strstr(json, "\"import_options\":[{\"name\":\"skip_note_speed_ratios\",\"type\":\"bool\","
                           "\"required\":false,\"default\":false}]") != NULL);
        CHECK(strstr(json, "\"file_types\":[\".pjsk\",\".json\",\".json.gz\"]") != NULL);
        CHECK(strstr(json, "\"import_options\":[],\"export_options\":[{\"name\":\"music_id\",\"type\":\"int\","
                           "\"required\":true,\"default\":null},{\"name\":\"compressed\",\"type\":\"bool\","
                           "\"required\":false,\"default\":true},{\"name\":\"strip_layers\","
                           "\"type\":\"bool\",\"required\":false,\"default\":false}]}") != NULL);
        CHECK(strstr(json, "\"operations\":[\"load\"],\"file_types\":[\".sus\"]") != NULL);
        CHECK(strstr(json, "\"deprecated\":true,\"binary\":true,\"operations\":[\"export\"]") != NULL);
        CHECK(json[strlen(json) - 1] == '}');
    }
    rg_free(json);
    rg_free(again);
}

static void test_format_metadata(void) {
    char path[1024];
    size_t size;
    unsigned char* data;
    rg_score* score = NULL;
    rg_score* copy = NULL;
    char* info = NULL;
    char* json = NULL;
    test_path(path, sizeof path, "88s_append.sus");
    data = read_file(path, &size);

    CHECK_OK(rg_load("sus", data, size, NULL, &score, &info));
    CHECK(info && strcmp(info, "{\"format\":\"sus\"}") == 0);
    rg_free(info);
    check_query(score, "format_metadata", NULL, "{\"format\":\"sus\"}");
    json = to_json(score);
    CHECK(json && strstr(json, "},\"format_metadata\":{\"format\":\"sus\"},\"notes\":[") != NULL);
    rg_free(json);
    rg_score_free(score);

    CHECK_OK(rg_load("holodori_sus", data, size, NULL, &score, &info));
    CHECK(info && strncmp(info, "{\"bezier_count\":", 16) == 0 &&
          strstr(info, ",\"format\":\"holodori_sus\"}") != NULL);
    rg_free(info);
    rg_score_free(score);
    free(data);

    CHECK_OK(rg_score_from_json("{\"format_metadata\":{\"format\":\"usc\",\"n\":2,\"x\":0.5,\"b\":true}}",
                                &score));
    check_query(score, "format_metadata", NULL, "{\"b\":true,\"format\":\"usc\",\"n\":2,\"x\":0.5}");
    CHECK_OK(rg_transform("shift", score, NULL, &copy, NULL));
    check_query(copy, "format_metadata", NULL, "{\"b\":true,\"format\":\"usc\",\"n\":2,\"x\":0.5}");
    rg_score_free(copy);
    rg_score_free(score);

    CHECK_OK(rg_score_from_json("{}", &score));
    check_query(score, "format_metadata", NULL, "{}");
    rg_score_free(score);
}

static void test_layers(void) {
    static const char* layered =
        "{\"notes\":["
        "{\"kind\":\"Bpm\",\"beat\":0,\"bpm\":120},"
        "{\"kind\":\"TimeScaleGroup\",\"changes\":[{\"beat\":0,\"timeScale\":1}]},"
        "{\"kind\":\"TimeScaleGroup\",\"changes\":[{\"beat\":0,\"timeScale\":2}]},"
        "{\"kind\":\"Single\",\"beat\":1,\"lane\":0,\"size\":1,\"timeScaleGroup\":0,"
        "\"critical\":false,\"trace\":false},"
        "{\"kind\":\"Single\",\"beat\":2,\"lane\":2,\"size\":1,\"timeScaleGroup\":1,"
        "\"critical\":false,\"trace\":false}]}";
    rg_score* score = NULL;
    rg_score* stripped = NULL;
    uint8_t* data = NULL;
    size_t size = 0;
    char* json = NULL;

    CHECK_OK(rg_score_from_json(layered, &score));
    if (!score) return;
    check_query(score, "features_used", NULL, "[\"layers\"]");
    EXPECT_ERROR(rg_export("mmws", score, NULL, &data, &size, NULL), RG_ERR_FAILED,
                 ".mmws holds one layer, but the score has 2");
    EXPECT_ERROR(rg_export("pjsk", score, "{\"music_id\":1}", &data, &size, NULL), RG_ERR_FAILED,
                 "strip_layers");
    EXPECT_ERROR(rg_export("yume_sus", score, NULL, &data, &size, NULL), RG_ERR_FAILED,
                 "strip_layers");
    EXPECT_ERROR(rg_export("sus", score, NULL, &data, &size, NULL), RG_ERR_FAILED, "allow_layers");
    CHECK(data == NULL && size == 0);
    CHECK(round_trip(score, "mmws", "{\"strip_layers\":true}", "mmws", "mmws") == 2);
    CHECK(round_trip(score, "pjsk", "{\"music_id\":1,\"strip_layers\":true}", "pjsk", "pjsk") == 2);
    CHECK(round_trip(score, "sus", "{\"allow_layers\":true}", "sus", "sus") == 2);
    CHECK(round_trip(score, "next_sekai", NULL, "next_sekai", "next_sekai") == 2);

    CHECK_OK(rg_transform("strip_layers", score, NULL, &stripped, NULL));
    check_query(stripped, "features_used", NULL, "[]");
    CHECK(combo(stripped) == 2);
    json = to_json(stripped);
    CHECK(json && count(json, "\"kind\":\"TimeScaleGroup\"") == 1 && count(json, "\"timeScaleGroup\":1") == 0);
    rg_free(json);
    CHECK(round_trip(stripped, "mmws", NULL, "mmws", "mmws") == 2);
    CHECK(round_trip(stripped, "pjsk", "{\"music_id\":1}", "pjsk", "pjsk") == 2);
    rg_score_free(stripped);
    check_query(score, "features_used", NULL, "[\"layers\"]");

    CHECK_OK(rg_export("ccmmws", score, NULL, &data, &size, NULL));
    if (data) {
        rg_score* loaded = (rg_score*)1;
        EXPECT_ERROR(rg_load("mmws", data, size, NULL, &loaded, NULL), RG_ERR_INVALID_DATA,
                     "Not a .mmws file");
        CHECK(loaded == NULL);
    }
    rg_free(data);
    rg_score_free(score);
}

typedef struct {
    const rg_score* shared;
    const char* expected_sus;
    const char* own_file;
    int failures;
} worker_args;

static void worker(worker_args* args) {
    int i;
    for (i = 0; i < 4; ++i) {
        uint8_t* data = NULL;
        size_t size = 0;
        rg_score* shifted = NULL;
        rg_score* own = NULL;
        rg_score* bad = NULL;
        char* json = NULL;
        size_t file_size;
        unsigned char* file = read_file(args->own_file, &file_size);

        if (rg_export("sus", args->shared, NULL, &data, &size, NULL) != RG_OK ||
            strcmp((const char*)data, args->expected_sus) != 0)
            ++args->failures;
        rg_free(data);
        if (rg_transform("shift", args->shared, NULL, &shifted, NULL) != RG_OK) ++args->failures;
        if (rg_score_to_json(args->shared, NULL, &json) != RG_OK) ++args->failures;
        rg_free(json);
        if (rg_load("sus", file, file_size, NULL, &own, NULL) != RG_OK) ++args->failures;
        if (rg_load("sus", "", 0, "{\"thread\":1}", &bad, NULL) != RG_ERR_INVALID_ARGUMENT ||
            !strstr(rg_last_error(), "\"thread\""))
            ++args->failures;
        if (rg_export("usc", own, NULL, &data, &size, NULL) != RG_OK) ++args->failures;
        if (rg_last_error()[0] != '\0') ++args->failures;
        rg_free(data);
        rg_score_free(shifted);
        rg_score_free(own);
        free(file);
    }
}

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID args) {
    worker((worker_args*)args);
    return 0;
}
#else
static void* worker_main(void* args) {
    worker((worker_args*)args);
    return NULL;
}
#endif

static void test_threads(const rg_score* score) {
    enum { N = 4 };
    static const char* files[N] = {"nouten_master.sus", "rollinggirl_master.sus",
                                   "empurple_append.sus", "chounights_master.sus"};
    char paths[N][1024];
    worker_args args[N];
    uint8_t* expected = NULL;
    size_t size = 0;
    int i;
#ifdef _WIN32
    HANDLE threads[N];
#else
    pthread_t threads[N];
#endif
    CHECK_OK(rg_export("sus", score, NULL, &expected, &size, NULL));
    if (!expected) return;
    for (i = 0; i < N; ++i) {
        test_path(paths[i], sizeof paths[i], files[i]);
        args[i].shared = score;
        args[i].expected_sus = (const char*)expected;
        args[i].own_file = paths[i];
        args[i].failures = 0;
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, worker_main, &args[i], 0, NULL);
#else
        pthread_create(&threads[i], NULL, worker_main, &args[i]);
#endif
    }
    for (i = 0; i < N; ++i) {
#ifdef _WIN32
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
        if (args[i].failures) {
            fprintf(stderr, "thread %d: %d failures\n", i, args[i].failures);
            failures += args[i].failures;
        }
    }
    rg_free(expected);
}

static void test_yume(const char* path, long long expected) {
    rg_score* score = load_path("yume_sus", path, NULL);
    long long n = combo(score);
    if (n != expected) {
        fprintf(stderr, "%s: combo %lld, expected %lld\n", path, n, expected);
        ++failures;
    }
    if (score) test_exports(score);
    rg_score_free(score);
}

int main(int argc, char** argv) {
    rg_score* score;
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s <test_files dir> [<yume sus chart> <expected combo>]\n",
                argv[0]);
        return 2;
    }
    test_dir = argv[1];

    test_version();
    test_formats();
    test_format_metadata();
    test_layers();
    test_test_files();
    score = load_test_file("sus", "88s_append.sus");
    if (score) {
        test_exports(score);
        test_transforms(score);
        test_queries(score);
        test_json(score);
        test_errors(score);
        test_threads(score);
    }
    rg_score_free(score);
    if (argc == 4) test_yume(argv[2], atoll(argv[3]));

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("rg_converters %s: all C API tests passed\n", rg_version());
    return 0;
}
