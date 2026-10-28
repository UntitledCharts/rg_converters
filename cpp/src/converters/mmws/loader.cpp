#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rg/converters/mmw_io.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"

namespace rg::converters::mmws {

using namespace rg::core;

namespace {

struct NoteData {
    double beat;
    double lane;
    double size;
    long long timeScaleGroup;
    std::optional<bool> critical;  // bool normally; nullopt for hidden mid
    bool trace;
    std::optional<std::string> direction;
    bool fake;
    bool friction;               // flag & NOTE_FRICTION (for judgeType="trace")
    std::optional<std::string> type;  // mid: "tick"/"attach"
    std::optional<std::string> ease;  // start/mid
};

MetaData read_metadata(BinReader& fbin) {
    MetaData m;
    m.title = fbin.read_cstr();
    m.designer = fbin.read_cstr();
    m.artist = fbin.read_cstr();
    fbin.read_cstr();  // artist + musicFile(read to advance buffer)
    m.waveoffset = Num::F(fbin.read_float() / -1000);
    m.requests = std::vector<std::string>{};
    return m;
}

std::vector<NodePtr> read_events(BinReader& fbin, const Version& version,
                                 std::vector<std::shared_ptr<TimeScaleGroup>>& time_scale_groups) {
    std::vector<NodePtr> events;
    long long time_signature_count = fbin.read_int();
    if (time_signature_count) fbin.seek_cur(3 * time_signature_count * (32 / 8));
    long long tempo_count = fbin.read_int();
    for (long long i = 0; i < tempo_count; ++i) {
        long long tick = fbin.read_int();
        double bpm = fbin.read_float();
        events.push_back(std::make_shared<Bpm>(Num::F(tick_to_beat(tick)), Num::F(bpm)));
    }

    if (version.has_hispeed) {
        long long hispeed_count = fbin.read_int();
        for (long long i = 0; i < hispeed_count; ++i) {
            long long tick = fbin.read_int();
            double ts = fbin.read_float();
            long long group_id = version.has_layers ? fbin.read_int() : 0;
            time_scale_groups[static_cast<std::size_t>(group_id)]->append(
                std::make_shared<TimeScalePoint>(Num::F(tick_to_beat(tick)), Num::F(ts)));
        }
    }

    if (version.has_skill_fever) {
        long long skill_count = fbin.read_int();
        for (long long i = 0; i < skill_count; ++i)
            events.push_back(std::make_shared<Skill>(Num::F(tick_to_beat(fbin.read_int()))));
        long long fever_chance_tick = fbin.read_int(32, true);
        long long fever_start_tick = fbin.read_int(32, true);
        // UNVERIFIED: -1 is taken as "no fever" because that is what the exporter writes. This used
        // to test > 0, which dropped a fever at tick 0; check against MikuMikuWorld's serializer.
        if (fever_chance_tick != -1)
            events.push_back(std::make_shared<FeverChance>(Num::F(tick_to_beat(fever_chance_tick))));
        if (fever_start_tick != -1)
            events.push_back(std::make_shared<FeverStart>(Num::F(tick_to_beat(fever_start_tick))));
    }
    return events;
}

NoteData read_note_data(BinReader& fbin, const Version& version, const std::string& type) {
    long long tick;
    double lane, width;
    if (version.has_floatLaneWidth) {
        tick = fbin.read_int();
        lane = fbin.read_float();
        width = fbin.read_float();
    } else {
        tick = fbin.read_int();
        lane = static_cast<double>(fbin.read_int());
        width = static_cast<double>(fbin.read_int());
    }
    long long layer = version.has_layers ? fbin.read_int() : 0;
    long long flick = (type != "start" && type != "mid") ? fbin.read_int() : FLICK_NONE;
    long long flag = fbin.read_int();
    bool dummy = version.has_dummyNote ? static_cast<bool>(flag & NOTE_DUMMY) : false;

    NoteData d;
    d.beat = tick_to_beat(tick);
    d.lane = to_usc_lane(lane, width);
    d.size = width_to_size(width);
    d.timeScaleGroup = layer;
    d.critical = static_cast<bool>(flag & NOTE_CRITICAL);
    d.trace = static_cast<bool>(flag & NOTE_FRICTION);
    d.direction = flick_to_direction(flick);
    d.fake = dummy;
    d.friction = static_cast<bool>(flag & NOTE_FRICTION);

    if (type == "mid") {
        long long hold_step_type = fbin.read_int();
        // Normal (0): changes shape + adds combo (tick + critical)
        // Hidden (1): changes shape, no combo (tick + critical=None)
        // Skip (2): no shape change, adds combo (attach + critical)
        if (hold_step_type == 0) {
            d.type = "tick";
        } else if (hold_step_type == 1) {
            d.type = "tick";
            d.critical = std::nullopt;
        } else if (hold_step_type == 2) {
            d.type = "attach";
        }
    }
    if (type == "start" || type == "mid") d.ease = ease_num_to_ease(fbin.read_int());
    return d;
}

std::shared_ptr<Single> make_single(const NoteData& d, const std::string& type) {
    auto s = std::make_shared<Single>();
    s->beat = Num::F(d.beat);
    s->critical = d.critical;
    s->lane = Num::F(d.lane);
    s->size = Num::F(d.size);
    s->fake = d.fake;
    s->timeScaleGroup = d.timeScaleGroup;
    s->trace = d.trace;
    s->direction = d.direction;
    s->type = type;
    return s;
}

std::vector<NodePtr> read_taps(BinReader& fbin, const Version& version, const std::string& type) {
    std::vector<NodePtr> notes;
    long long note_count = fbin.read_int();
    for (long long i = 0; i < note_count; ++i)
        notes.push_back(make_single(read_note_data(fbin, version, "tap"), type));
    return notes;
}

std::shared_ptr<GuidePoint> make_guide_point(const NoteData& d, const std::string& ease_override) {
    auto p = std::make_shared<GuidePoint>();
    p->beat = Num::F(d.beat);
    p->ease = ease_override.empty() ? (d.ease.has_value() ? *d.ease : "linear") : ease_override;
    p->lane = Num::F(d.lane);
    p->size = Num::F(d.size);
    p->timeScaleGroup = d.timeScaleGroup;
    return p;
}

std::vector<NodePtr> read_holds(BinReader& fbin, const Version& version) {
    std::vector<NodePtr> holds;
    long long hold_count = fbin.read_int();
    for (long long i = 0; i < hold_count; ++i) {
        long long flag = version.has_guideNote ? fbin.read_int() : 0;
        bool is_guide = static_cast<bool>(flag & HOLD_GUIDE);
        bool is_fake = static_cast<bool>(flag & HOLD_FAKE);
        std::string start_judge = (flag & HOLD_START_HIDDEN) ? "none" : "normal";
        std::string end_judge = (flag & HOLD_END_HIDDEN) ? "none" : "normal";
        NoteData start_data = read_note_data(fbin, version, "start");

        std::shared_ptr<GuidePoint> guide_start;
        std::shared_ptr<SlideStartPoint> hold_start;
        if (is_guide) {
            guide_start = make_guide_point(start_data, "");
        } else {
            hold_start = std::make_shared<SlideStartPoint>();
            hold_start->beat = Num::F(start_data.beat);
            hold_start->critical = start_data.critical.value_or(false);
            hold_start->ease = start_data.ease.has_value() ? *start_data.ease : "linear";
            hold_start->judgeType = start_data.friction ? "trace" : start_judge;
            hold_start->lane = Num::F(start_data.lane);
            hold_start->size = Num::F(start_data.size);
            hold_start->timeScaleGroup = start_data.timeScaleGroup;
        }

        long long fade_type = version.has_fadeType ? fbin.read_int() : static_cast<long long>(FT_OUT);
        long long guide_color = version.has_guideColor
                                    ? fbin.read_int()
                                    : (start_data.critical.value_or(false) ? GC_YELLOW : GC_GREEN);

        long long hold_step_count = fbin.read_int();
        if (is_guide) {
            auto guide = std::make_shared<Guide>(guide_color_to_color(guide_color),
                                                 fade_type_to_fade(fade_type));
            guide->midpoints.push_back(guide_start);
            for (long long s = 0; s < hold_step_count; ++s)
                guide->append(make_guide_point(read_note_data(fbin, version, "mid"), ""));
            guide->append(make_guide_point(read_note_data(fbin, version, "end"), "linear"));
            holds.push_back(guide);
        } else {
            auto slide = std::make_shared<Slide>(start_data.critical.value_or(false), is_fake,
                                                 std::vector<NodePtr>{hold_start});
            for (long long s = 0; s < hold_step_count; ++s) {
                NoteData d = read_note_data(fbin, version, "mid");
                auto r = std::make_shared<SlideRelayPoint>();
                r->beat = Num::F(d.beat);
                r->ease = d.ease.has_value() ? *d.ease : "linear";
                r->lane = Num::F(d.lane);
                r->size = Num::F(d.size);
                r->timeScaleGroup = d.timeScaleGroup;
                r->critical = d.critical;
                r->type = d.type.has_value() ? *d.type : "tick";
                slide->append(r);
            }
            NoteData d = read_note_data(fbin, version, "end");
            auto e = std::make_shared<SlideEndPoint>();
            e->beat = Num::F(d.beat);
            e->critical = d.critical.value_or(false);
            e->judgeType = d.friction ? "trace" : end_judge;
            e->lane = Num::F(d.lane);
            e->size = Num::F(d.size);
            e->timeScaleGroup = d.timeScaleGroup;
            e->direction = d.direction;
            slide->append(e);
            holds.push_back(slide);
        }
    }
    return holds;
}

}  // namespace

static std::string variant_of(const std::string& signature) {
    if (signature == Signature::MikuMikuWorld) return ".mmws";
    if (signature == Signature::MikuMikuWorld4ChartCyanvas) return ".ccmmws";
    if (signature == Signature::MikuMikuWorld4UntitledChart) return ".unchmmws";
    return "";
}

core::Score load_bytes(const std::string& data, const std::string& format) {
    BinReader fbin(data);
    std::string signature =
        fbin.read_cstr();  // chunk_size len(UCMMWS)+1 irrelevant; cstr reads to null
    if (!format.empty() && variant_of(signature) != format) {
        std::string found = variant_of(signature);
        throw std::invalid_argument("Not a " + format + " file (" +
                                    (found.empty() ? "no MMWS signature" : "it is a " + found + " file") + ")");
    }
    Version version;
    if (signature == Signature::MikuMikuWorld) {
        version = Version(0, 0, fbin.read_int());
    } else if (signature == Signature::MikuMikuWorld4ChartCyanvas) {
        long long v = fbin.read_int(16);
        long long cc = fbin.read_int(16);
        version = Version(0, cc, v);
    } else if (signature == Signature::MikuMikuWorld4UntitledChart) {
        version = Version(fbin.read_int());
    } else {
        throw std::runtime_error("Invalid MMWS file. Unrecognized signature");
    }

    long long metadata_address = 0, events_address = 0, tapsAddress = 0, holdsAddress = 0;
    bool has_damages = false, has_layers_addr = false;
    long long damagesAddress = 0, layersAddress = 0;
    if (version.has_address) {
        metadata_address = fbin.read_int();
        events_address = fbin.read_int();
        tapsAddress = fbin.read_int();
        holdsAddress = fbin.read_int();
        if (version.has_damageNote) {
            damagesAddress = fbin.read_int();
            has_damages = true;
        }
        if (version.has_layers) {
            layersAddress = fbin.read_int();
            has_layers_addr = true;
        }
        if (version.has_waypoints) fbin.read_int();
    }

    if (version.has_address) fbin.seek_set(static_cast<std::size_t>(metadata_address));
    auto metadata = std::make_shared<MetaData>(read_metadata(fbin));

    std::vector<NodePtr> notes_data;

    std::vector<std::shared_ptr<TimeScaleGroup>> time_scale_groups;
    if (version.has_address && has_layers_addr && layersAddress) {
        fbin.seek_set(static_cast<std::size_t>(layersAddress));
        long long layerCount = fbin.read_int();
        for (long long i = 0; i < layerCount; ++i)
            time_scale_groups.push_back(std::make_shared<TimeScaleGroup>());
    } else {
        time_scale_groups.push_back(std::make_shared<TimeScaleGroup>());
    }
    for (auto& g : time_scale_groups) notes_data.push_back(g);

    if (version.has_address) fbin.seek_set(static_cast<std::size_t>(events_address));
    {
        auto ev = read_events(fbin, version, time_scale_groups);
        notes_data.insert(notes_data.end(), ev.begin(), ev.end());
    }

    if (version.has_address) fbin.seek_set(static_cast<std::size_t>(tapsAddress));
    {
        auto t = read_taps(fbin, version, "single");
        notes_data.insert(notes_data.end(), t.begin(), t.end());
    }

    if (version.has_address) fbin.seek_set(static_cast<std::size_t>(holdsAddress));
    {
        auto h = read_holds(fbin, version);
        notes_data.insert(notes_data.end(), h.begin(), h.end());
    }

    if (version.has_address && has_damages && damagesAddress) {
        fbin.seek_set(static_cast<std::size_t>(damagesAddress));
        auto t = read_taps(fbin, version, "damage");
        notes_data.insert(notes_data.end(), t.begin(), t.end());
    }

    return core::Score(metadata, notes_data);
}

}  // namespace rg::converters::mmws
