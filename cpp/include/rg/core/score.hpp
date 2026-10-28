#pragma once

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "rg/core/model.hpp"

namespace rg::core {

// 1tickをbeatに変換
inline const double BEAT_PER_TICK = 0.002083;  // round(4 / 1920, 6)

// ノーツリストを何小節区切りにするか
// ※ノーツリスト = 重なりを調べるときに使用するリスト
inline const double BAR_INTERVAL = 0.5;

// uscのレーン表記(中央が0.0)
// ↓
// 左端を1に変換するオフセット
inline const long long LANE_OFFSET = 7;

long long usc_lanes_to_sus_lanes(double lane, double size);

NodePtr clone_node(const Node* n);

using Value = std::variant<bool, long long, double, std::string>;

// A loader found things in the file that a Score can't hold: one per line.
struct UnsupportedFeatureError : std::runtime_error {
    explicit UnsupportedFeatureError(const std::string& message) : std::runtime_error(message) {}
};
struct Score;
Score deep_copy(const Score& s);

// Replace holodori events with their sekai equivalents, for exporting to a format that
// has skill/fever but no section ends. ChargeEnd and FeverEnd are dropped.
std::vector<NodePtr> convert_holodori_events(const std::vector<NodePtr>& notes);

struct Score {
    shared_ptr<MetaData> metadata;
    vector<NodePtr> notes;
    // What loading the file told us beyond the chart: "format" (the id it was loaded as) and
    // format-specific keys such as holodori_sus's "bezier_count".
    std::map<std::string, Value> format_metadata;

    Score() = default;
    Score(shared_ptr<MetaData> metadata_, vector<NodePtr> notes_)
        : metadata(std::move(metadata_)), notes(std::move(notes_)) {}

    void delete_fake_notes();
    void delete_damage_notes();
    void replace_extended_guide_colors();
    void replace_extended_ease();
    void sort_by_beat();
    void add_point_without_fade();
    void strip_extended_lanes(bool resize_if_possible = false);
    void strip_speed_ratios();
    // Keeps the first timescale group and moves every note onto it.
    void strip_layers();
    long long layer_count() const;
    bool has_speed_ratios() const;
    // Ends every slide that has no end with an unjudged one a tick after its last point.
    void close_open_slides();
    void flatten_speed_ratios_to_layers();
    bool check_skill_overlap() const;
    std::pair<Score, long long> export_overlaps_score() const;
    void shift();

    std::vector<std::pair<double, double>> bpm_timeline() const;
    double time_at_beat(double target_beat) const;
    double beat_at_time(double target_time) const;
    double note_beat(const Node* note) const;
    double note_max_beat(const Node* note) const;
    double duration() const;
    long long combo_count() const;
    long long note_count() const;
    long long combo_before_beat(double cutoff_beat) const;

    // combo_events yields (category, critical, beat); exposed for combo counting.
    struct ComboEvent {
        std::string category;
        bool critical;
        double beat;
    };
    std::vector<ComboEvent> combo_events() const;

    std::tuple<long long, std::pair<long long, long long>> cut(
        std::optional<double> start_at, std::optional<double> end_at,
        bool keep_position = false);
};

// For an exporter that writes one layer: more than one is stripped when `strip` is set, or throws
// naming `allow` (how to keep or strip them). With `speed_ratios_are_layers` (the format can't
// write note speed ratios), speed ratios become layers first.
void limit_to_one_layer(Score& score, const std::string& format, bool strip, const std::string& allow,
                        bool speed_ratios_are_layers = true);

// What an exporter wrote, and the score it wrote: a copy of its input after the exporter's own
// changes (the input is never modified).
struct ExportResult {
    std::string data;
    Score score;
};

}  // namespace rg::core
