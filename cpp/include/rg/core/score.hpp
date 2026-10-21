#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
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
struct Score;
Score deep_copy(const Score& s);

// Replace holodori events with their sekai equivalents, for exporting to a format that
// has skill/fever but no section ends. ChargeEnd and FeverEnd are dropped.
std::vector<NodePtr> convert_holodori_events(const std::vector<NodePtr>& notes);

struct Score {
    shared_ptr<MetaData> metadata;
    vector<NodePtr> notes;

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

}  // namespace rg::core
