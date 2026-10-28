#include "rg/core/transforms.hpp"

namespace rg::transforms {

using core::deep_copy;

namespace {

template <class F>
Score transformed(const Score& score, F&& apply) {
    Score copy = deep_copy(score);
    apply(copy);
    return copy;
}

}  // namespace

Score delete_fake_notes(const Score& score) {
    return transformed(score, [](Score& s) { s.delete_fake_notes(); });
}

Score delete_damage_notes(const Score& score) {
    return transformed(score, [](Score& s) { s.delete_damage_notes(); });
}

Score replace_extended_guide_colors(const Score& score) {
    return transformed(score, [](Score& s) { s.replace_extended_guide_colors(); });
}

Score replace_extended_ease(const Score& score) {
    return transformed(score, [](Score& s) { s.replace_extended_ease(); });
}

Score sort_by_beat(const Score& score) {
    return transformed(score, [](Score& s) { s.sort_by_beat(); });
}

Score add_point_without_fade(const Score& score) {
    return transformed(score, [](Score& s) { s.add_point_without_fade(); });
}

Score strip_extended_lanes(const Score& score, bool resize_if_possible) {
    return transformed(score, [&](Score& s) { s.strip_extended_lanes(resize_if_possible); });
}

Score strip_speed_ratios(const Score& score) {
    return transformed(score, [](Score& s) { s.strip_speed_ratios(); });
}

Score strip_layers(const Score& score) {
    return transformed(score, [](Score& s) { s.strip_layers(); });
}

Score close_open_slides(const Score& score) {
    return transformed(score, [](Score& s) { s.close_open_slides(); });
}

Score flatten_speed_ratios_to_layers(const Score& score) {
    return transformed(score, [](Score& s) { s.flatten_speed_ratios_to_layers(); });
}

Score shift(const Score& score) {
    return transformed(score, [](Score& s) { s.shift(); });
}

Score convert_holodori_events(const Score& score) {
    return transformed(score, [](Score& s) { s.notes = core::convert_holodori_events(s.notes); });
}

std::pair<Score, long long> export_overlaps_score(const Score& score) {
    auto [overlaps, count] = score.export_overlaps_score();
    return {deep_copy(overlaps), count};
}

CutResult cut(const Score& score, std::optional<double> start_at, std::optional<double> end_at,
              bool keep_position) {
    CutResult result{deep_copy(score), 0, {0, 0}};
    auto [combo_before, ticks] = result.score.cut(start_at, end_at, keep_position);
    result.combo_before = combo_before;
    result.ticks = ticks;
    return result;
}

}  // namespace rg::transforms
