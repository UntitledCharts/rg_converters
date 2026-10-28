#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rg/converters/mmw_io.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pynum.hpp"
#include "rg/util/pyops.hpp"

namespace rg::converters::mmws {

using namespace rg::core;

namespace {

long long pt_tsg(const Node* n) {
    switch (n->kind) {
        case Kind::Single: return static_cast<const Single*>(n)->timeScaleGroup;
        case Kind::SlideStartPoint: return static_cast<const SlideStartPoint*>(n)->timeScaleGroup;
        case Kind::SlideRelayPoint: return static_cast<const SlideRelayPoint*>(n)->timeScaleGroup;
        case Kind::SlideEndPoint: return static_cast<const SlideEndPoint*>(n)->timeScaleGroup;
        case Kind::GuidePoint: return static_cast<const GuidePoint*>(n)->timeScaleGroup;
        default: return 0;
    }
}
std::optional<std::string> pt_direction(const Node* n) {
    if (n->kind == Kind::Single) return static_cast<const Single*>(n)->direction;
    if (n->kind == Kind::SlideEndPoint) return static_cast<const SlideEndPoint*>(n)->direction;
    return std::nullopt;
}
bool pt_fake(const Node* n) {
    if (n->kind == Kind::Single) return static_cast<const Single*>(n)->fake;
    if (n->kind == Kind::SlideRelayPoint) return static_cast<const SlideRelayPoint*>(n)->fake;
    return false;
}
std::string pt_judgeType(const Node* n) {
    if (n->kind == Kind::SlideStartPoint) return static_cast<const SlideStartPoint*>(n)->judgeType;
    if (n->kind == Kind::SlideEndPoint) return static_cast<const SlideEndPoint*>(n)->judgeType;
    return "normal";
}
bool pt_critical_truthy(const Node* n) {
    switch (n->kind) {
        case Kind::Single: {
            auto& c = static_cast<const Single*>(n)->critical;
            return c.has_value() && c.value();
        }
        case Kind::SlideStartPoint: return static_cast<const SlideStartPoint*>(n)->critical;
        case Kind::SlideEndPoint: return static_cast<const SlideEndPoint*>(n)->critical;
        case Kind::SlideRelayPoint: {
            auto& c = static_cast<const SlideRelayPoint*>(n)->critical;
            return c.has_value() && c.value();
        }
        default: return false;
    }
}
std::string pt_ease(const Node* n) {
    if (n->kind == Kind::SlideStartPoint) return static_cast<const SlideStartPoint*>(n)->ease;
    if (n->kind == Kind::SlideRelayPoint) return static_cast<const SlideRelayPoint*>(n)->ease;
    if (n->kind == Kind::GuidePoint) return static_cast<const GuidePoint*>(n)->ease;
    return "linear";
}

double lane_offset(double lane, double size) {
    return std::max((lane > 0 ? lane + size : -(lane - size)) - 6, 0.0);
}

double usc_object_get_max_lane_offset(const Node* n) {
    double offset = 0;
    if (n->kind == Kind::Single) {
        offset = lane_offset(static_cast<const Single*>(n)->lane, static_cast<const Single*>(n)->size);
    } else if (n->kind == Kind::Slide) {
        for (auto& p : static_cast<const Slide*>(n)->connections)
            offset = std::max(offset, lane_offset(node_lane(p.get()), node_size(p.get())));
    } else if (n->kind == Kind::Guide) {
        for (auto& p : static_cast<const Guide*>(n)->midpoints)
            offset = std::max(offset, lane_offset(p->lane, p->size));
    }
    return offset;
}

// The value a helper rejected, and the note it was on.
template <class Fn>
auto on_note(const Node* note, Fn&& f) {
    try {
        return f();
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string(e.what()) + " on the note at beat " +
                                    util::py_float_repr(node_beat(const_cast<Node*>(note))));
    }
}

void write_metadata(BinWriter& fbin, const Version& version, const MetaData& metadata,
                    const std::vector<NodePtr>& notes) {
    fbin.write_cstr(metadata.title);
    fbin.write_cstr(metadata.designer);
    fbin.write_cstr(metadata.artist);
    fbin.write_cstr("");  // musicFile
    fbin.write_float(static_cast<double>(metadata.waveoffset) * -1000);
    if (version.has_jacket) fbin.write_cstr("");  // jacketFile
    if (version.has_laneExtension) {
        double mx = 0;
        bool first = true;
        for (auto& note : notes) {
            double v = usc_object_get_max_lane_offset(note.get());
            if (first || v > mx) {
                mx = v;
                first = false;
            }
        }
        long long lane_extension = static_cast<long long>(std::ceil(mx));
        fbin.write_int(static_cast<long long>(lane_extension));
    }
}

void write_events(BinWriter& fbin, const Version& version, const NoteGroups& noteGroup) {
    // Timesignature count
    fbin.write_int(0);  // Should be fine to set it to 0

    auto& bpmChanges = noteGroup.by(Kind::Bpm);
    fbin.write_int(static_cast<long long>(bpmChanges.size()));
    for (auto& bc : bpmChanges) {
        auto* b = static_cast<Bpm*>(bc.get());
        fbin.write_int(beat_to_tick(b->beat));
        fbin.write_float(b->bpm);
    }

    if (version.has_hispeed) {
        auto& groups = noteGroup.by(Kind::TimeScaleGroup);
        long long time_scale_count = 0;
        if (version.has_layers) {
            for (auto& g : groups)
                time_scale_count += static_cast<long long>(static_cast<TimeScaleGroup*>(g.get())->changes.size());
        } else {
            time_scale_count = static_cast<long long>(static_cast<TimeScaleGroup*>(groups[0].get())->changes.size());
        }
        fbin.write_int(time_scale_count);
        for (long long layer = 0; layer < static_cast<long long>(groups.size()); ++layer) {
            auto* group = static_cast<TimeScaleGroup*>(groups[static_cast<std::size_t>(layer)].get());
            for (auto& sc : group->changes) {
                fbin.write_int(beat_to_tick(sc->beat));
                fbin.write_float(sc->timeScale);
                if (version.has_layers) fbin.write_int(layer);
            }
            if (!version.has_layers) break;
        }
    }

    if (version.has_skill_fever) {
        auto& skills = noteGroup.by(Kind::Skill);
        fbin.write_int(static_cast<long long>(skills.size()));
        for (auto& sk : skills) fbin.write_int(beat_to_tick(static_cast<Skill*>(sk.get())->beat));
        auto& fever_chances = noteGroup.by(Kind::FeverChance);
        auto& fever_starts = noteGroup.by(Kind::FeverStart);
        long long fever_chance_tick, fever_start_tick;
        if (fever_chances.size() == 1 && fever_starts.size() == 1) {
            fever_chance_tick = beat_to_tick(static_cast<FeverChance*>(fever_chances[0].get())->beat);
            fever_start_tick = beat_to_tick(static_cast<FeverStart*>(fever_starts[0].get())->beat);
        } else {
            fever_chance_tick = -1;
            fever_start_tick = -1;
        }
        fbin.write_int(fever_chance_tick, 32, true);
        fbin.write_int(fever_start_tick, 32, true);
    }
}

void write_note_data(BinWriter& fbin, const Version& version, const Node* note,
                     const std::string& note_type, const Slide* slide = nullptr,
                     bool critical_guide = false) {
    double beat = node_beat(const_cast<Node*>(note));
    double lane = node_lane(note);
    double size = node_size(note);
    if (version.has_floatLaneWidth) {
        fbin.write_int(beat_to_tick(beat));
        fbin.write_float(to_mmw_lane_f(lane, size));
        fbin.write_float(size_to_width_f(size));
    } else {
        fbin.write_int(beat_to_tick(beat));
        fbin.write_int(to_mmw_lane_i(lane, size));
        fbin.write_int(size_to_width_i(size));
    }
    if (version.has_layers) fbin.write_int(pt_tsg(note));
    if (note_type == "tap" || note_type == "end")
        fbin.write_int(on_note(note, [&] { return direction_to_flick(pt_direction(note)); }));
    long long flag = NF_NONE;
    if (pt_fake(note)) flag |= NOTE_DUMMY;
    if (pt_judgeType(note) == "trace") flag |= NOTE_FRICTION;
    if (note->kind == Kind::Single && static_cast<const Single*>(note)->trace.value_or(false))
        flag |= NOTE_FRICTION;
    if (pt_critical_truthy(note) || (note_type == "mid" && slide && slide->critical) || critical_guide)
        flag |= NOTE_CRITICAL;
    fbin.write_int(flag);
    if (note_type == "mid") {
        if (note->kind == Kind::GuidePoint) {
            fbin.write_int(0);  // Hold point
        } else if (static_cast<const SlideRelayPoint*>(note)->type == "tick") {
            if (!static_cast<const SlideRelayPoint*>(note)->critical.has_value())
                fbin.write_int(1);
            else
                fbin.write_int(0);
        } else if (static_cast<const SlideRelayPoint*>(note)->type == "attach") {
            fbin.write_int(2);
        } else {
            throw std::runtime_error("Invalid hold mid type");
        }
    }
    if (note_type == "start" || note_type == "mid")
        fbin.write_int(on_note(note, [&] { return ease_to_ease_num(pt_ease(note)); }));
}

void write_taps(BinWriter& fbin, const Version& version, const std::string& type,
                const NoteGroups& noteGroup) {
    std::vector<Node*> notes;
    for (auto& n : noteGroup.by(Kind::Single))
        if (static_cast<Single*>(n.get())->type == type) notes.push_back(n.get());
    fbin.write_int(static_cast<long long>(notes.size()));
    for (auto* note : notes) write_note_data(fbin, version, note, "tap");
}

void write_holds(BinWriter& fbin, const Version& version, const NoteGroups& noteGroup) {
    auto& slides = noteGroup.by(Kind::Slide);
    auto& guides = noteGroup.by(Kind::Guide);
    fbin.write_int(static_cast<long long>(slides.size() + guides.size()));
    for (auto& sl_sp : slides) {
        auto* slide = static_cast<Slide*>(sl_sp.get());
        Node* startHold = slide->connections.front().get();
        Node* endHold = slide->connections.back().get();
        long long flag = HF_NONE;
        if (static_cast<SlideStartPoint*>(startHold)->judgeType == "none")
            flag |= HOLD_START_HIDDEN;
        if (static_cast<SlideEndPoint*>(endHold)->judgeType == "none")
            flag |= HOLD_END_HIDDEN;
        if (slide->fake) flag |= HOLD_FAKE;
        fbin.write_int(flag);
        write_note_data(fbin, version, startHold, "start");

        if (version.has_fadeType) fbin.write_int(FT_OUT);
        if (version.has_guideColor) fbin.write_int(GC_GREEN);

        long long nsteps = static_cast<long long>(slide->connections.size()) - 2;
        fbin.write_int(nsteps);
        for (long long i = 1; i + 1 < static_cast<long long>(slide->connections.size()); ++i)
            write_note_data(fbin, version, slide->connections[static_cast<std::size_t>(i)].get(),
                            "mid", slide);
        write_note_data(fbin, version, endHold, "end");
    }
    for (auto& g_sp : guides) {
        auto* guide = static_cast<Guide*>(g_sp.get());
        Node* startGuide = guide->midpoints.front().get();
        Node* endGuide = guide->midpoints.back().get();
        // DIVERGES from the original python mmws exporter on purpose: its guide block tagged the
        // LAST point "mid" and the earlier ones "end" (inverted vs the loader and vs the slide
        // block above), so guide-containing .mmws files could never be loaded back. We mirror the
        // slide block instead -- middle points -> "mid", dedicated endpoint -> "end" -- so guide
        // round-trips work. Output is therefore NOT byte-identical to python for guides.
        long long nsteps = static_cast<long long>(guide->midpoints.size()) - 2;
        fbin.write_int(HOLD_GUIDE);
        // Without a colour field, a guide reads back yellow when its first point is critical.
        write_note_data(fbin, version, startGuide, "start", nullptr,
                        !version.has_guideColor && guide->color == "yellow");

        if (version.has_fadeType)
            fbin.write_int(on_note(startGuide, [&] { return fade_to_fade_type(guide->fade); }));
        if (version.has_guideColor)
            fbin.write_int(on_note(startGuide, [&] { return color_to_guide_color(guide->color); }));

        fbin.write_int(nsteps);
        for (long long i = 1; i + 1 < static_cast<long long>(guide->midpoints.size()); ++i)
            write_note_data(fbin, version, guide->midpoints[static_cast<std::size_t>(i)].get(),
                            "mid");
        write_note_data(fbin, version, endGuide, "end");
    }
}

}  // namespace

core::ExportResult export_bytes(const core::Score& score_in, const std::string& format,
                                const ExportOptions& opts) {
    core::Score score = core::deep_copy(score_in);
    score.close_open_slides();
    Version version;
    std::string signature;
    if (format == ".mmws") {
        version = Version(0, 0);
        signature = Signature::MikuMikuWorld;
        score.replace_extended_ease();
        score.replace_extended_guide_colors();
        score.delete_fake_notes();
        score.delete_damage_notes();
        score.strip_extended_lanes();
        limit_to_one_layer(score, ".mmws", opts.strip_layers,
                                  "Set the strip_layers export option to strip them");
    } else if (format == ".ccmmws") {
        version = Version(0);
        signature = Signature::MikuMikuWorld4ChartCyanvas;
        score.delete_fake_notes();
        score.flatten_speed_ratios_to_layers();
    } else if (format == ".unchmmws") {
        version = Version();
        signature = Signature::MikuMikuWorld4UntitledChart;
        score.flatten_speed_ratios_to_layers();
    } else {
        throw std::invalid_argument("Unsupported format: " + format);
    }
    // Notes are written on layer 0, so there has to be one.
    if (score.layer_count() == 0)
        score.notes.insert(score.notes.begin(),
                           std::make_shared<TimeScaleGroup>(std::vector<shared_ptr<TimeScalePoint>>{
                               std::make_shared<TimeScalePoint>(Num::I(0), Num::F(1.0))}));
    NoteGroups noteGroups(convert_holodori_events(score.notes));

    BinWriter fbin;

    fbin.write_cstr(signature);
    fbin.write_int(version.value);

    std::size_t table_address = 0;
    if (version.has_address) {
        long long address_size = 4 * (4 + (version.has_damageNote ? 1 : 0) +
                                      (version.has_layers ? 1 : 0) +
                                      (version.has_waypoints ? 1 : 0));
        table_address = fbin.tell();
        fbin.fill_zero(static_cast<std::size_t>(address_size));
    }

    std::size_t metadata_address = 0, events_address = 0, tapsAddress = 0, holdsAddress = 0;
    std::size_t damagesAddress = 0, layersAddress = 0, waypointAddress = 0;

    if (version.has_address) metadata_address = fbin.tell();
    write_metadata(fbin, version, *score.metadata, score.notes);

    if (version.has_address) events_address = fbin.tell();
    write_events(fbin, version, noteGroups);

    if (version.has_address) tapsAddress = fbin.tell();
    write_taps(fbin, version, "single", noteGroups);

    if (version.has_address) holdsAddress = fbin.tell();
    write_holds(fbin, version, noteGroups);

    if (version.has_address && version.has_damageNote) {
        damagesAddress = fbin.tell();
        write_taps(fbin, version, "damage", noteGroups);
    }

    if (version.has_address && version.has_layers) {
        layersAddress = fbin.tell();
        auto& layers = noteGroups.by(Kind::TimeScaleGroup);
        fbin.write_int(static_cast<long long>(layers.size()));
        for (long long i = 0; i < static_cast<long long>(layers.size()); ++i)
            fbin.write_cstr("#" + std::to_string(i + 1));
    }

    if (version.has_address && version.has_waypoints) {
        waypointAddress = fbin.tell();
        fbin.write_int(0);  // waypoint count
    }

    if (version.has_address) {
        fbin.seek_set(table_address);
        fbin.write_int(static_cast<long long>(metadata_address));
        fbin.write_int(static_cast<long long>(events_address));
        fbin.write_int(static_cast<long long>(tapsAddress));
        fbin.write_int(static_cast<long long>(holdsAddress));
        if (version.has_damageNote) fbin.write_int(static_cast<long long>(damagesAddress));
        if (version.has_layers) fbin.write_int(static_cast<long long>(layersAddress));
        if (version.has_waypoints) fbin.write_int(static_cast<long long>(waypointAddress));
    }

    return {std::move(fbin.buf), std::move(score)};
}

}  // namespace rg::converters::mmws
