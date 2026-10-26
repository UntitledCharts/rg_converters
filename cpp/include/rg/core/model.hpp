#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rg::core {

using std::optional;
using std::shared_ptr;
using std::string;
using std::vector;

// A Python-style number that remembers whether it is an int or a float, so that
// json output preserves the distinction (float != int). Reads convert to double;
// any float arithmetic (+=/-=/= double) makes it a float, matching Python.
struct Num {
    double v = 0.0;
    bool is_int = false;

    Num() = default;
    Num(int x) : v(static_cast<double>(x)), is_int(true) {}
    Num(long long x) : v(static_cast<double>(x)), is_int(true) {}
    Num(double x) : v(x), is_int(false) {}

    operator double() const { return v; }

    Num& operator=(int x) {
        v = static_cast<double>(x);
        is_int = true;
        return *this;
    }
    Num& operator=(long long x) {
        v = static_cast<double>(x);
        is_int = true;
        return *this;
    }
    Num& operator=(double x) {
        v = x;
        is_int = false;
        return *this;
    }
    Num& operator+=(double x) {
        v += x;
        is_int = false;
        return *this;
    }
    Num& operator-=(double x) {
        v -= x;
        is_int = false;
        return *this;
    }

    static Num I(long long x) { return Num(x); }
    static Num F(double x) { return Num(x); }
};

enum class Kind {
    Bpm,
    TimeScalePoint,
    TimeScaleGroup,
    Volume,
    Single,
    Skill,
    FeverChance,
    FeverStart,
    HolodoriSkill,
    HolodoriChargeStart,
    HolodoriChargeEnd,
    HolodoriFeverStart,
    HolodoriFeverEnd,
    SlideStartPoint,
    SlideRelayPoint,
    SlideEndPoint,
    Slide,
    GuidePoint,
    Guide,
};

struct Node {
    Kind kind;
    explicit Node(Kind k) : kind(k) {}
    virtual ~Node() = default;
};

using NodePtr = shared_ptr<Node>;

struct Bpm : Node {
    Num beat;
    Num bpm;
    string type = "bpm";
    Bpm(Num beat_, Num bpm_, string type_ = "bpm")
        : Node(Kind::Bpm), beat(beat_), bpm(bpm_), type(std::move(type_)) {}
    long long get_sus_sort_number() const { return 1; }
};

struct TimeScalePoint : Node {
    Num beat;
    Num timeScale;
    TimeScalePoint(Num beat_, Num timeScale_)
        : Node(Kind::TimeScalePoint), beat(beat_), timeScale(timeScale_) {}
};

struct TimeScaleGroup : Node {
    vector<shared_ptr<TimeScalePoint>> changes;
    string type = "timeScaleGroup";
    TimeScaleGroup(vector<shared_ptr<TimeScalePoint>> changes_ = {},
                   string type_ = "timeScaleGroup")
        : Node(Kind::TimeScaleGroup),
          changes(std::move(changes_)),
          type(std::move(type_)) {}
    void append(shared_ptr<TimeScalePoint> time_scale) {
        changes.push_back(std::move(time_scale));
    }
    void insert(long long index, shared_ptr<TimeScalePoint> time_scale) {
        if (index < 0) index += static_cast<long long>(changes.size());
        if (index < 0) index = 0;
        if (index > static_cast<long long>(changes.size()))
            index = static_cast<long long>(changes.size());
        changes.insert(changes.begin() + index, std::move(time_scale));
    }
    long long get_sus_sort_number() const { return 2; }
};

struct Volume : Node {
    Num beat;
    Num volume;
    string type = "volume";
    Volume(Num beat_, Num volume_, string type_ = "volume")
        : Node(Kind::Volume), beat(beat_), volume(volume_), type(std::move(type_)) {}
    long long get_sus_sort_number() const { return 1; }
};

struct Skill : Node {
    // lane: int = 0
    // width: int = 1
    Num beat;
    string type = "skill";
    explicit Skill(Num beat_) : Node(Kind::Skill), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct FeverChance : Node {
    // lane: int = 15
    // width: int = 1
    Num beat;
    string type = "feverChance";
    explicit FeverChance(Num beat_) : Node(Kind::FeverChance), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct FeverStart : Node {
    // lane: int = 15
    // width: int = 1
    Num beat;
    string type = "feverStart";
    explicit FeverStart(Num beat_) : Node(Kind::FeverStart), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct Single : Node {
    Num beat;
    optional<bool> critical = std::nullopt;
    Num lane;
    Num size;
    bool fake = false;  // isdummy for UntitledSekai
    long long timeScaleGroup;
    Num speedRatio = 1.0;
    optional<bool> trace = std::nullopt;
    optional<string> direction = std::nullopt;
    string type = "single";
    Single() : Node(Kind::Single) {}
    long long get_sus_sort_number() const { return 3; }
};

struct SlideStartPoint : Node {
    Num beat;
    bool critical;
    string ease;
    string judgeType;
    Num lane;
    Num size;
    long long timeScaleGroup;
    Num speedRatio = 1.0;
    string type = "start";
    SlideStartPoint() : Node(Kind::SlideStartPoint) {}
};

struct SlideRelayPoint : Node {
    Num beat;
    string ease;
    Num lane;
    Num size;
    long long timeScaleGroup;
    string type;  // Literal["tick", "attach"]
    optional<bool> critical = std::nullopt;
    bool fake = false;
    Num speedRatio = 1.0;
    SlideRelayPoint() : Node(Kind::SlideRelayPoint) {}
};

struct SlideEndPoint : Node {
    Num beat;
    bool critical;
    string judgeType;
    Num lane;
    Num size;
    long long timeScaleGroup;
    Num speedRatio = 1.0;
    optional<string> direction = std::nullopt;
    string type = "end";
    SlideEndPoint() : Node(Kind::SlideEndPoint) {}
};

struct Slide : Node {
    bool critical;
    bool fake = false;  // isdummy for UntitledSekai
    vector<NodePtr> connections;
    string type = "slide";
    Slide(bool critical_ = false, bool fake_ = false, vector<NodePtr> connections_ = {},
          string type_ = "slide")
        : Node(Kind::Slide),
          critical(critical_),
          fake(fake_),
          connections(std::move(connections_)),
          type(std::move(type_)) {}
    void append(NodePtr slidepoint);
    void sort();
    long long get_sus_sort_number() const { return 4; }
};

struct GuidePoint : Node {
    Num beat;
    string ease;
    Num lane;
    Num size;
    long long timeScaleGroup;
    Num speedRatio = 1.0;
    // An alpha keyframe (0..1). A guide's alpha runs linearly in time between its keyframes and
    // holds past the first and last; a guide with none fades by `fade`.
    optional<Num> alpha = std::nullopt;
    GuidePoint() : Node(Kind::GuidePoint) {}
};

struct Guide : Node {
    string color;
    string fade;
    vector<shared_ptr<GuidePoint>> midpoints;
    string type = "guide";
    // holodori sus guides carry an arbitrary color code
    // TODO: implement a default map to color if this is set
    optional<string> color_code = std::nullopt;
    Guide(string color_ = "", string fade_ = "",
          vector<shared_ptr<GuidePoint>> midpoints_ = {}, string type_ = "guide",
          optional<string> color_code_ = std::nullopt)
        : Node(Kind::Guide),
          color(std::move(color_)),
          fade(std::move(fade_)),
          midpoints(std::move(midpoints_)),
          type(std::move(type_)),
          color_code(std::move(color_code_)) {}
    void append(shared_ptr<GuidePoint> guidepoint);
    void sort();
    long long get_sus_sort_number() const { return 5; }
};

// holodori-only timeline events. holodori marks the fever charge and fever windows
// with a start and an end each, where sekai has just the two open-ended
// FeverChance/FeverStart markers.

struct HolodoriSkill : Node {
    Num beat;
    long long slot;  // deck position (1-5) whose special skill fires here
    string type = "holodoriSkill";
    HolodoriSkill(Num beat_, long long slot_)
        : Node(Kind::HolodoriSkill), beat(beat_), slot(slot_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct HolodoriChargeStart : Node {
    Num beat;
    string type = "holodoriChargeStart";
    explicit HolodoriChargeStart(Num beat_)
        : Node(Kind::HolodoriChargeStart), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct HolodoriChargeEnd : Node {
    Num beat;
    string type = "holodoriChargeEnd";
    explicit HolodoriChargeEnd(Num beat_)
        : Node(Kind::HolodoriChargeEnd), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct HolodoriFeverStart : Node {
    Num beat;
    string type = "holodoriFeverStart";
    explicit HolodoriFeverStart(Num beat_)
        : Node(Kind::HolodoriFeverStart), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct HolodoriFeverEnd : Node {
    Num beat;
    string type = "holodoriFeverEnd";
    explicit HolodoriFeverEnd(Num beat_)
        : Node(Kind::HolodoriFeverEnd), beat(beat_) {}
    long long get_sus_sort_number() const { return 3; }
};

struct MetaData {
    string title;
    string artist;
    string designer;
    Num waveoffset;  // Offset in seconds, where negative means late and positive means early
    optional<vector<string>> requests;
};

// ---- helpers for positioned nodes (Single / slide points / guide points) ----

inline bool is_holodori_event(Kind k) {
    return k == Kind::HolodoriSkill || k == Kind::HolodoriChargeStart ||
           k == Kind::HolodoriChargeEnd || k == Kind::HolodoriFeverStart ||
           k == Kind::HolodoriFeverEnd;
}

inline bool is_event(Kind k) {
    // _EVENTS = (Skill, FeverStart, FeverChance) + HOLODORI_EVENTS
    return k == Kind::Skill || k == Kind::FeverStart || k == Kind::FeverChance ||
           is_holodori_event(k);
}

Num& node_beat(Node* n);
double node_lane(const Node* n);
double node_size(const Node* n);

inline long long get_sus_sort_number(const Node* n) {
    switch (n->kind) {
        case Kind::Bpm:
        case Kind::Volume:
            return 1;
        case Kind::TimeScaleGroup:
            return 2;
        case Kind::Single:
        case Kind::Skill:
        case Kind::FeverChance:
        case Kind::FeverStart:
        case Kind::HolodoriSkill:
        case Kind::HolodoriChargeStart:
        case Kind::HolodoriChargeEnd:
        case Kind::HolodoriFeverStart:
        case Kind::HolodoriFeverEnd:
            return 3;
        case Kind::Slide:
            return 4;
        case Kind::Guide:
            return 5;
        default:
            return 0;
    }
}

}  // namespace rg::core
