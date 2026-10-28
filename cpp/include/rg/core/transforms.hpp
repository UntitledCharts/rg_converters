#pragma once

#include <optional>
#include <utility>

#include "rg/core/score.hpp"

// Score -> Score functions. Each works on a deep copy, so the score passed in is never modified
// and the result shares nothing with it.
namespace rg::transforms {

using core::Score;

Score delete_fake_notes(const Score& score);
Score delete_damage_notes(const Score& score);
Score replace_extended_guide_colors(const Score& score);
Score replace_extended_ease(const Score& score);
Score sort_by_beat(const Score& score);
Score add_point_without_fade(const Score& score);
Score strip_extended_lanes(const Score& score, bool resize_if_possible = false);
Score strip_speed_ratios(const Score& score);
// Keeps the first timescale group and moves every note onto it (note speed ratios stay).
Score strip_layers(const Score& score);
Score close_open_slides(const Score& score);
Score flatten_speed_ratios_to_layers(const Score& score);
Score shift(const Score& score);
// Holodori events replaced with their sekai equivalents (see core::convert_holodori_events).
Score convert_holodori_events(const Score& score);
// The overlapping notes as their own score, and how many overlaps there are.
std::pair<Score, long long> export_overlaps_score(const Score& score);

struct CutResult {
    Score score;
    long long combo_before;
    std::pair<long long, long long> ticks;
};
CutResult cut(const Score& score, std::optional<double> start_at, std::optional<double> end_at,
              bool keep_position = false);

}  // namespace rg::transforms
