// holodori sus -> Score. a sekai-sus dialect run through a port of the game's SusConverter (1.3.0)

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "rg/converters/holodori_sus.hpp"
#include "rg/converters/sus_common.hpp"
#include "rg/core/model.hpp"
#include "rg/core/score.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::holodori_sus {

using namespace rg::core;
using namespace rg::util;
using rg::converters::sus::_Bar;
using rg::converters::sus::_get_bars;
using rg::converters::sus::_get_ticks;
using rg::converters::sus::_sus_to_usc_lane;
using rg::converters::sus::_sus_to_usc_size;
using rg::converters::sus::_tick_to_beat;
using rg::converters::sus::TICKS_PER_BEAT;

namespace {

// SusUtility.CharToInt: case-sensitive base62 (0-9, a-z = 10-35, A-Z = 36-61)
long long _char_to_int(char c) {
    int o = static_cast<unsigned char>(c);
    if (48 <= o && o <= 57) return o - 48;
    if (97 <= o && o <= 122) return o - 87;
    if (65 <= o && o <= 90) return o - 29;
    return 0;
}

// parse a note line the way TryReadNextSegmentToken does with compressed empties: a "0x" token
// skips (base62 d1*62 + d2) empty positions; any other 2-char cell is one position and, unless it
// is "00", a token (kind = char0, width = char1). beat = position / total positions.
// returns the (position, kind, width) tokens and the total position count
std::pair<std::vector<std::tuple<long long, long long, long long>>, long long> _holodori_cells(
    const std::string& data) {
    std::vector<std::tuple<long long, long long, long long>> parsed;
    std::size_t i = 0;
    long long pos = 0;
    std::size_t n = data.size();
    while (i < n) {
        if (i + 4 <= n && data[i] == '0' && data[i + 1] == 'x') {
            pos += _char_to_int(data[i + 2]) * 62 + _char_to_int(data[i + 3]);
            i += 4;
        } else if (i + 2 <= n) {
            if (data[i] != '0' || data[i + 1] != '0')
                parsed.push_back({pos, _char_to_int(data[i]), _char_to_int(data[i + 1])});
            pos += 1;
            i += 2;
        } else {
            break;
        }
    }
    return {parsed, pos};
}

// bpm (08) / nms (0A) / sp skill (0B) / fever (0C, 0D) grid -> (position, cell, total). these
// channels are read without compressed empties, so "0x" is an ordinary cell; only "00" is empty
std::vector<std::tuple<long long, std::string, long long>> _meta_cells(const std::string& data) {
    std::vector<std::tuple<long long, std::string, long long>> out;
    long long total = static_cast<long long>(data.size() / 2);
    for (long long pos = 0; pos < total; ++pos) {
        std::string cell = data.substr(static_cast<std::size_t>(pos) * 2, 2);
        if (cell != "00") out.push_back({pos, cell, total});
    }
    return out;
}

// 0B/0C/0D grid -> (position, value, total). the cell is a base62 pair, so "10" is 62.
std::vector<std::tuple<long long, long long, long long>> _meta_values(const std::string& data) {
    std::vector<std::tuple<long long, long long, long long>> out;
    for (auto& [pos, cell, total] : _meta_cells(data))
        out.push_back({pos, _char_to_int(cell[0]) * 62 + _char_to_int(cell[1]), total});
    return out;
}

// TryResolveFeverSectionPointType: 1 charge start, 2 charge end, 3 fever start, 4 fever end, 0 none
int _fever_point_type(char lane_char, long long value) {
    if (value != 1 && value != 2) return 0;
    if (lane_char == 'C') return static_cast<int>(value);
    if (lane_char == 'D') return static_cast<int>(value) + 2;
    return 0;
}

struct _FeverEvent {
    long long measure;
    float progress;
    long long tick;
    int type;
};

// TryValidateExplicitFeverSection: the last point of each type (measure order, then file order)
// must exist and come in order, compared by measure then float progress. returns their ticks
std::optional<std::array<long long, 4>> _fever_section(const std::vector<_FeverEvent>& events) {
    std::array<const _FeverEvent*, 4> last{};
    for (auto& e : events)
        if (last[e.type - 1] == nullptr || e.measure >= last[e.type - 1]->measure) last[e.type - 1] = &e;
    for (auto* e : last)
        if (e == nullptr) return std::nullopt;
    auto after = [](const _FeverEvent* a, const _FeverEvent* b) {
        return a->measure != b->measure ? a->measure > b->measure : a->progress > b->progress;
    };
    for (int i = 0; i < 3; ++i)
        if (after(last[i], last[i + 1])) return std::nullopt;
    return std::array<long long, 4>{last[0]->tick, last[1]->tick, last[2]->tick, last[3]->tick};
}

// ---- the game's note model ----

enum : int {
    NORMAL = 1,
    LONG_START = 2,
    LONG_END = 3,
    LONG_FLICK_END = 4,
    RELAY_ACTIVE = 5,
    RELAY_DEACTIVE = 6,
    BEZIER = 7,
    FLICK = 9,
    DAMAGE = 10,
    GHOST_START = 11,
    GHOST_END = 12,
    GHOST_RELAY = 13,
};

// RationalProgress
struct _Prog {
    long long num = 0;
    long long den = 1;
};

bool _prog_equal(const _Prog& a, const _Prog& b) {
    return (a.den == 0 && b.den == 0) || b.den * a.num == b.num * a.den;
}

int _prog_compare(const _Prog& a, const _Prog& b) {
    if (a.den == 0 && b.den == 0) return 0;
    long long x = b.den * a.num, y = b.num * a.den;
    return (x > y) - (x < y);
}

float _prog_float(const _Prog& p) {
    return p.den != 0 ? static_cast<float>(p.num) / static_cast<float>(p.den) : 0.0f;
}

// NoteTokenSourceInfo: the tap and directional tokens a note info was built from
struct _Src {
    int def = 0;
    int crit = 0;
    int dir = 0;
    int flick = 0;
    int lead = 0;
    bool tap = false;
    int first = 0;

    static _Src tap_token(bool critical) {
        return critical ? _Src{0, 1, 0, 0, 1, true, 1} : _Src{1, 0, 0, 0, 0, true, 0};
    }
    static _Src directional(bool is_flick) { return _Src{0, 0, 1, is_flick ? 1 : 0, 0, false, 0}; }
    int line_control() const { return dir - flick; }
    bool any() const { return def > 0 || crit > 0 || dir > 0; }
    bool independent_tap() const { return def > 0 || crit > lead; }
    bool starts_with_independent_critical_tap() const { return tap && first == 0 && crit > 0; }
    bool independent_critical_tap_marker() const { return def >= 1 && crit > lead; }
    _Src merge(const _Src& b) const {
        return _Src{def + b.def, crit + b.crit, dir + b.dir, flick + b.flick, def > 0 ? lead : lead + b.lead,
                    tap || b.tap, tap ? first : b.first};
    }
    _Src for_critical_long_start_line_control() const {
        _Src r = *this;
        r.lead = crit;
        r.first = 1;
        return r;
    }
    _Src consume_lead() const {
        _Src r = *this;
        if (lead > 0) --r.crit, --r.lead;
        return r;
    }
    _Src consume_default() const {
        _Src r = *this;
        if (def > 0) --r.def;
        return r;
    }
    _Src consume_flick() const {
        _Src r = *this;
        if (flick > 0) --r.dir, --r.flick;
        return r;
    }
    _Src consume_line_control() const {
        _Src r = *this;
        if (dir > flick) --r.dir;
        return r;
    }
    _Src consume_all_line_control() const {
        _Src r = *this;
        if (dir > flick) r.dir = flick;
        return r;
    }
    _Src consume_legacy_directional() const {
        _Src r = *this;
        if (dir > 0) --r.dir, r.flick = std::max(flick - 1, 0);
        return r;
    }
    _Src consume_all_legacy_directional() const {
        _Src r = *this;
        if (dir > 0) r.dir = 0, r.flick = 0;
        return r;
    }
};

struct _Owner {
    bool is_long = false;
    long long id = 0;
    bool operator==(const _Owner&) const = default;
};

// LiveNoteDataBase as the converter fills it
struct _GNote {
    long long id = 0;
    int type = 0;
    int crit = 0;
    int line = 0;
    long long lane = 0;
    float width = 0.0f;
    long long channel = -1;
    bool lc = false;
    long long measure = 0;
    _Prog prog;
    long long color = 0;
    std::optional<_Src> src;
    std::optional<_Owner> owner;
    std::vector<_GNote*> children;
    float measure_progress() const { return _prog_float(prog) + static_cast<float>(measure); }
};

// NotePositionKey (the progress hashes as a reduced fraction)
using _PosKey = std::tuple<long long, long long, long long, float>;

_PosKey _pos_key(const _Prog& p, long long lane, float width) {
    long long num = p.num, den = p.den;
    if (num == 0) {
        den = 1;
    } else {
        long long g = std::gcd(num, den);
        if (g == 0) g = 1;
        num /= g;
        den /= g;
        if (den < 0) num = -num, den = -den;
    }
    return {num, den, lane, width};
}

_PosKey _pos_key(const _GNote* n) { return _pos_key(n->prog, n->lane, n->width); }

// LongNoteAttributeKey: measure, position, channel, type
using _AttrKey = std::tuple<long long, _PosKey, long long, int>;

struct _Attrs {
    int crit = 0;
    bool lc = false;
    int line = 0;
};

// System.Collections.Generic.ArraySortHelper.IntrospectiveSort (unstable)
template <typename T, typename Cmp>
void _dotnet_sort(std::vector<T>& keys, Cmp cmp) {
    long long n = static_cast<long long>(keys.size());
    if (n < 2) return;
    auto swap_if_greater = [&](long long i, long long j) {
        if (i != j && cmp(keys[i], keys[j]) > 0) std::swap(keys[i], keys[j]);
    };
    auto insertion_sort = [&](long long lo, long long hi) {
        for (long long i = lo; i < hi; ++i) {
            long long j = i;
            T t = keys[i + 1];
            while (j >= lo && cmp(t, keys[j]) < 0) {
                keys[j + 1] = keys[j];
                --j;
            }
            keys[j + 1] = t;
        }
    };
    auto down_heap = [&](long long i, long long cnt, long long lo) {
        T d = keys[lo + i - 1];
        while (i <= cnt / 2) {
            long long child = 2 * i;
            if (child < cnt && cmp(keys[lo + child - 1], keys[lo + child]) < 0) ++child;
            if (!(cmp(d, keys[lo + child - 1]) < 0)) break;
            keys[lo + i - 1] = keys[lo + child - 1];
            i = child;
        }
        keys[lo + i - 1] = d;
    };
    auto heapsort = [&](long long lo, long long hi) {
        long long cnt = hi - lo + 1;
        for (long long i = cnt / 2; i > 0; --i) down_heap(i, cnt, lo);
        for (long long i = cnt; i > 1; --i) {
            std::swap(keys[lo], keys[lo + i - 1]);
            down_heap(1, i - 1, lo);
        }
    };
    auto partition = [&](long long lo, long long hi) {
        long long middle = lo + ((hi - lo) >> 1);
        swap_if_greater(lo, middle);
        swap_if_greater(lo, hi);
        swap_if_greater(middle, hi);
        T pivot = keys[middle];
        std::swap(keys[middle], keys[hi - 1]);
        long long left = lo, right = hi - 1;
        while (left < right) {
            ++left;
            while (cmp(keys[left], pivot) < 0) ++left;
            --right;
            while (cmp(pivot, keys[right]) < 0) --right;
            if (left >= right) break;
            std::swap(keys[left], keys[right]);
        }
        std::swap(keys[left], keys[hi - 1]);
        return left;
    };
    std::function<void(long long, long long, long long)> intro = [&](long long lo, long long hi, long long depth) {
        while (hi > lo) {
            long long size = hi - lo + 1;
            if (size <= 16) {
                if (size == 2) {
                    swap_if_greater(lo, hi);
                } else if (size == 3) {
                    swap_if_greater(lo, hi - 1);
                    swap_if_greater(lo, hi);
                    swap_if_greater(hi - 1, hi);
                } else if (size > 3) {
                    insertion_sort(lo, hi);
                }
                return;
            }
            if (depth == 0) {
                heapsort(lo, hi);
                return;
            }
            --depth;
            long long p = partition(lo, hi);
            intro(p + 1, hi, depth);
            hi = p - 1;
        }
    };
    long long log2 = 0;
    for (long long k = n; k >= 1; k /= 2) ++log2;
    intro(0, n - 1, 2 * log2);
}

// SusUtility.IsValidLane returns true for an invalid lane
bool _invalid_lane(long long lane, float width) {
    if (width > 12.0f || lane < 0 || lane > 12 || width < 0.0f) return true;
    return static_cast<float>(lane) + width > 12.0f;
}

// INoteExtensions.CompareNoteTime: 1 before, 2 after, 3 same time, 4 another channel
int _compare_note_time(const _GNote* a, const _GNote* b) {
    if (a->channel != b->channel) return 4;
    float x = a->measure_progress(), y = b->measure_progress();
    if (x < y) return 1;
    return x <= y ? 3 : 2;
}

int _measure_progress_cmp(const _GNote* a, const _GNote* b) {
    float x = a->measure_progress(), y = b->measure_progress();
    return (x > y) - (x < y);
}

int _sign(long long x) { return (x > 0) - (x < 0); }

bool _independent_tap_note(int t) { return (t & ~8) == NORMAL; }
bool _default_independent_tap_note(int t, int crit) { return (t & ~8) == NORMAL && crit == 0; }
bool _relay_merge_marker(int t) { return t >= RELAY_ACTIVE && t <= BEZIER; }
bool _relay_line_control(const _GNote* n) { return (n->line & ~1) == 2; }
bool _ignored_relay_marker(int t, int line) { return t == RELAY_ACTIVE && line == 0; }

bool _same_position(const _GNote* n, const _Prog& p, long long lane, float width, long long channel) {
    return _prog_equal(n->prog, p) && n->lane == lane && std::fabs(n->width - width) < 1.401298464324817e-45f &&
           n->channel == channel;
}

bool _unpaired_default_tap(const _GNote* n, const _Src& s) {
    return n->type == NORMAL && s.def == 1 && s.crit == 0 && s.dir == 0;
}

int _compare_common_attributes(const _GNote* a, const _GNote* b) {
    if (int r = _sign(a->type - b->type)) return r;
    if (int r = _sign(a->line - b->line)) return r;
    if (a->lc != b->lc) return a->lc ? 1 : -1;
    return 0;
}

int _compare_common_counts(const _Src& a, const _Src& b) {
    for (auto [x, y] : {std::pair{a.def, b.def}, std::pair{a.crit, b.crit}, std::pair{a.dir, b.dir},
                        std::pair{a.flick, b.flick}})
        if (x != y) return x > y ? 1 : -1;
    return 0;
}

int _prefer(bool a, bool b) { return a == b ? 0 : (a ? 1 : -1); }

// SusConverterBase + SusConverter (1.3.0). version is SusFormatVersion.Major: 1 runs the legacy rules
struct _Game {
    const std::vector<_Bar>& bars;
    int version = 1;
    bool supports_owner = false;
    bool supports_attrs = false;
    std::optional<_Owner> pending_owner;
    std::deque<_GNote> pool;
    long long next_id = 0;
    std::map<long long, std::vector<_GNote*>> notes, longs, ghosts;
    std::map<long long, std::map<_PosKey, bool>> single_start;
    std::set<long long> ghost_tokens;
    std::map<_AttrKey, _Attrs> attrs;
    std::vector<_GNote*> owned;
    std::vector<_GNote*> stock_start, stock_relay, stock_ghost_start, stock_ghost_relay;
    std::vector<_GNote*> holds, guides;  // starts in the order their ends closed them
    long long measure_count = 0;   // MusicChartBase.MeasureCount from #MEASURE_COUNT
    long long last_measure = -1;   // highest measure list the note dictionaries hold

    explicit _Game(const std::vector<_Bar>& b) : bars(b) {}

    // the measure lists a note line makes: EnsureListCapacity, then GetOrCreateMeasureList fills
    // every measure up to it
    void note_line(long long m) { last_measure = std::max(last_measure, m); }

    // #MEASURE_COUNT clears every note list read so far and makes empty lists for 0..count-1
    void set_measure_count(long long count) {
        measure_count = count;
        notes.clear();
        longs.clear();
        ghosts.clear();
        last_measure = count - 1;
    }

    _GNote* make(int type, int crit, int line, long long lane, float width, long long channel, long long measure,
                 const _Prog& prog) {
        pool.emplace_back();
        _GNote* n = &pool.back();
        n->id = next_id++;
        n->type = type;
        n->crit = crit;
        n->line = line;
        n->lane = lane;
        n->width = width;
        n->channel = channel;
        n->measure = measure;
        n->prog = prog;
        return n;
    }

    static _Src src_of(const _GNote* n) { return n->src.value_or(_Src{}); }

    long long tick_of(const _GNote* n) const { return _get_ticks(bars, n->measure, n->prog.num, n->prog.den); }

    // ---- tap side ----

    bool single_long_start(long long m, const _Prog& p, long long lane, float width) const {
        auto it = single_start.find(m);
        if (it == single_start.end()) return false;
        auto jt = it->second.find(_pos_key(p, lane, width));
        return jt != it->second.end() && jt->second;
    }

    // IndexLongNotePositions: true only where a lone long start is the sole long point
    void index_long_positions(const std::vector<std::string>& lines) {
        static const std::regex long_re(R"(^#(\d{3})([0-9a-z])([0-9a-z])([0-9a-zA-Z]):[ \t]*([0-9a-zA-Z]+))");
        single_start.clear();
        for (auto& line : lines) {
            if (line.size() < 8 || line[0] != '#' || line[4] < '2' || line[4] > '4') continue;
            std::smatch m;
            if (!std::regex_search(line, m, long_re)) continue;
            long long measure = py_int(m[1].str());
            long long lane = _char_to_int(m[3].str()[0]) - 2;
            auto& d = single_start[measure];
            auto [cells, total] = _holodori_cells(m[5].str());
            for (auto& [p, kind, width] : cells) {
                _PosKey key = _pos_key(_Prog{p, total}, lane, static_cast<float>(width));
                bool fresh = d.find(key) == d.end();
                d[key] = kind == 1 && fresh;
            }
        }
    }

    static bool keep_overlapped(int nt, int crit, int line, const _GNote* ex, const _Src& ex_src, bool single) {
        if (nt == RELAY_ACTIVE && line == 0 && _independent_tap_note(ex->type)) return true;
        if (_independent_tap_note(nt) && ex->type == RELAY_ACTIVE && ex->line == 0) return true;
        if (_default_independent_tap_note(nt, crit) && _relay_line_control(ex)) return true;
        if ((line & ~1) != 2 || single) return false;
        if (_default_independent_tap_note(ex->type, ex->crit)) return true;
        return ex_src.independent_critical_tap_marker();
    }

    static bool keep_overlapped_legacy(int nt, int crit, int line, const _GNote* ex) {
        if (nt == RELAY_ACTIVE && line == 0 && _independent_tap_note(ex->type)) return true;
        if (_independent_tap_note(nt) && ex->type == RELAY_ACTIVE && ex->line == 0) return true;
        if (_default_independent_tap_note(nt, crit) && _relay_line_control(ex)) return true;
        return _default_independent_tap_note(ex->type, ex->crit) && (line & ~1) == 2;
    }

    static bool keep_independent_tap_token(int nt, int crit, const _GNote* ex, const _Src& s) {
        if (nt == NORMAL && crit == 1 && ex->type == NORMAL && _unpaired_default_tap(ex, s)) return false;
        if (nt == NORMAL && crit == 0 && ex->type == NORMAL && s.def == 0 && s.lead >= 1 && s.dir == 0) return false;
        return nt == NORMAL && _independent_tap_note(ex->type);
    }

    static bool keep_independent_tap_token_legacy(int nt, int crit, const _GNote* ex) {
        if (nt == NORMAL && crit == 1 && ex->type == NORMAL) {
            _Src s = src_of(ex);
            if (s.def == 1 && s.crit == 0 && s.dir == 0) return false;
        }
        return nt == NORMAL && _independent_tap_note(ex->type);
    }

    // CompareNoteInfoMergeCandidates: > 0 when cand beats sel
    int compare_note_candidates(const _GNote* cand, const _Src& cs, const _GNote* sel, const _Src& ss, int nt, int crit,
                                int line, bool single) const {
        if (nt == NORMAL && crit == 1) {
            bool skip = false;
            if ((_relay_line_control(cand) && !cs.tap) || (_relay_line_control(sel) && !ss.tap))
                skip = single_long_start(cand->measure, cand->prog, cand->lane, cand->width);
            if (!skip)
                if (int r = _prefer(_unpaired_default_tap(cand, cs), _unpaired_default_tap(sel, ss))) return r;
        }
        bool by_markers;
        if (nt == FLICK && line == 0) {
            if (int r = _prefer(cs.tap && cs.dir == 0, ss.tap && ss.dir == 0)) return r;
            by_markers = _relay_merge_marker(nt);
        } else {
            by_markers = (line & ~1) == 2 || _relay_merge_marker(nt);
        }
        if (by_markers) {
            if (int r = _prefer(cs.lead > 0, ss.lead > 0)) return r;
            if (single)
                if (int r = _prefer(_unpaired_default_tap(cand, cs), _unpaired_default_tap(sel, ss))) return r;
            if (int r = _prefer(_relay_line_control(cand), _relay_line_control(sel))) return r;
            if (int r = _prefer(_relay_merge_marker(cand->type), _relay_merge_marker(sel->type))) return r;
        }
        if (int r = _prefer(cand->crit == 1, sel->crit == 1)) return r;
        if (int r = _compare_common_attributes(cand, sel)) return r;
        if (int r = _sign(cs.line_control() - ss.line_control())) return r;
        return _compare_common_counts(cs, ss);
    }

    int find_note_merge_index(const std::vector<_GNote*>& lst, const _Prog& p, long long lane, float width, int nt,
                              int crit, int line, bool single) const {
        long long size = static_cast<long long>(lst.size());
        if (pending_owner.has_value()) {
            for (long long i = size - 1; i >= 0; --i)
                if (lst[i]->owner == pending_owner && _same_position(lst[i], p, lane, width, -1))
                    return static_cast<int>(i);
            return -1;
        }
        if (version <= 1) {
            for (long long i = size - 1; i >= 0; --i) {
                const _GNote* n = lst[i];
                if (_same_position(n, p, lane, width, -1) && !keep_overlapped_legacy(nt, crit, line, n) &&
                    !keep_independent_tap_token_legacy(nt, crit, n))
                    return static_cast<int>(i);
            }
            return -1;
        }
        int best = -1;
        _Src best_src;
        for (long long i = size - 1; i >= 0; --i) {
            const _GNote* n = lst[i];
            if (!_same_position(n, p, lane, width, -1) || n->owner.has_value()) continue;
            _Src s = src_of(n);
            if (keep_overlapped(nt, crit, line, n, s, single) || keep_independent_tap_token(nt, crit, n, s)) continue;
            if (best < 0 || compare_note_candidates(n, s, lst[best], best_src, nt, crit, line, single) >= 1) {
                best = static_cast<int>(i);
                best_src = s;
            }
        }
        return best;
    }

    // AddNoteInfo
    void add_note(int nt, int crit, long long m, const _Prog& p, long long lane, long long width_i, _Src src, int line) {
        if (lane < 0 || lane > 12 || width_i < 0 || width_i > 12 || lane + width_i > 12) return;
        float width = static_cast<float>(width_i);
        auto& lst = notes[m];
        bool single = (line & ~1) == 2 && version >= 2 && single_long_start(m, p, lane, width);
        int idx = find_note_merge_index(lst, p, lane, width, nt, crit, line, single);
        if (idx >= 0) {
            _GNote* ex = lst[idx];
            lst.erase(lst.begin() + idx);
            _Src ex_src = src_of(ex);
            if (!pending_owner && version == 2 && single && ex->type == NORMAL &&
                ex_src.starts_with_independent_critical_tap() && ex_src.def == 1 && ex_src.crit == 1 && ex_src.dir == 0) {
                ex_src = ex_src.for_critical_long_start_line_control();
                nt = NORMAL;
            }
            auto merge_critical_tap = [&](int t, int c) {
                return t == NORMAL && c == 1 && _relay_merge_marker(ex->type) && _relay_line_control(ex);
            };
            if (version <= 1) {
                src = ex_src.merge(src);
            } else if (merge_critical_tap(nt, crit)) {
                src = src.merge(ex_src.consume_default());
            } else if (crit == 0 && nt == NORMAL && !pending_owner && ex->type == NORMAL && ex_src.def == 0 &&
                       ex_src.lead >= 1 && ex_src.dir == 0) {
                src = src.merge(ex_src);
            } else {
                src = ex_src.merge(src);
            }
            ex->src.reset();
            ex->owner.reset();
            if (ex->line != 0) line = ex->line;
            if (ex->crit != 0) crit = ex->crit;
            if (version >= 2 && merge_critical_tap(nt, crit)) nt = ex->type;
            if (nt == RELAY_DEACTIVE && ex->type == RELAY_ACTIVE && !ex->lc) nt = ex->type;
        }
        if (pending_owner && src.flick >= 1) nt = FLICK;
        // SusConverter.CreateNoteInfo
        if (nt == DAMAGE) crit = 0, line = 0;
        if (nt == GHOST_RELAY) crit = 0;
        _GNote* n = make(nt, crit, line, lane, width, -1, m, p);
        n->lc = nt == RELAY_DEACTIVE;
        n->src = src;
        if (pending_owner) {
            n->owner = pending_owner;
            owned.push_back(n);
        }
        lst.push_back(n);
    }

    // CreateNoteData: base 1 is the tap lane '1', base 9 the directional lane '5'
    void create_note_data(int base, long long m, long long lane, const std::string& data) {
        auto [cells, total] = _holodori_cells(data);
        if (pending_owner) {
            bool ok = cells.size() == 1;
            if (ok) {
                auto [p, v, w] = cells[0];
                if (pending_owner->is_long)
                    ok = base == FLICK ? (v == 1 || v == 2 || v == 6) : (v >= 1 && v <= 3);
                else
                    ok = base == FLICK ? v == 1 : (v == 1 || v == 2);
                ok = ok && w <= 12 && lane >= 0 && lane <= 12 && lane + w <= 12;
            }
            if (!ok) return;  // the game throws on a bad owned token
        }
        for (auto& [pos, v, w] : cells) {
            _Prog p{pos, total};
            _Src src = base == FLICK ? _Src::directional(v == 1) : _Src::tap_token(v == 2);
            switch (v) {
                case 1: add_note(base, 0, m, p, lane, w, src, 0); break;
                case 2:
                    if (base == NORMAL)
                        add_note(NORMAL, 1, m, p, lane, w, src, 0);
                    else
                        add_note(RELAY_DEACTIVE, 0, m, p, lane, w, src, 2);
                    break;
                case 3: add_note(RELAY_ACTIVE, 0, m, p, lane, w, src, 0); break;
                case 4: add_note(GHOST_RELAY, 0, m, p, lane, w, src, 2); break;
                case 6: add_note(RELAY_DEACTIVE, 0, m, p, lane, w, src, 3); break;
                case 7: add_note(GHOST_RELAY, 0, m, p, lane, w, src, 3); break;
                case 8: add_note(DAMAGE, 0, m, p, lane, w, src, 0); break;
                case 9: add_note(GHOST_RELAY, 0, m, p, lane, w, src, 0); break;
                default: break;
            }
        }
    }

    // ---- long and ghost points ----

    // CreateLongNoteData + AddLongNoteInfo: kinds 1-5 are start, end, active relay, bezier, inactive relay
    void create_long_data(long long m, long long lane, long long channel, const std::string& data) {
        static const int types[] = {LONG_START, LONG_END, RELAY_ACTIVE, BEZIER, RELAY_DEACTIVE};
        auto [cells, total] = _holodori_cells(data);
        auto& lst = longs[m];
        for (auto& [pos, v, w] : cells) {
            if (v < 1 || v > 5) continue;  // the game throws
            if (_invalid_lane(lane, static_cast<float>(w))) continue;
            _GNote* n = make(types[v - 1], 0, 1, lane, static_cast<float>(w), channel, m, _Prog{pos, total});
            n->lc = true;
            lst.push_back(n);
        }
    }

    // CreateGhostNoteData + AddGhostNoteInfo: ease tokens fold into the ghost point they sit on
    void create_ghost_data(long long m, long long lane, long long channel, long long color, const std::string& data) {
        auto [cells, total] = _holodori_cells(data);
        auto& lst = ghosts[m];
        for (auto& [pos, v, w] : cells) {
            int nt, line;
            switch (v) {
                case 1: nt = GHOST_START, line = 1; break;
                case 2: nt = GHOST_END, line = 1; break;
                case 3: nt = GHOST_RELAY, line = 1; break;
                case 4: nt = GHOST_RELAY, line = 2; break;
                case 7: nt = GHOST_RELAY, line = 3; break;
                default: continue;  // the game throws
            }
            float width = static_cast<float>(w);
            if (_invalid_lane(lane, width)) continue;
            bool token = v == 4 || v == 7;
            _Prog p{pos, total};
            int idx = -1;
            for (long long i = static_cast<long long>(lst.size()) - 1; i >= 0; --i)
                if (_same_position(lst[i], p, lane, width, channel)) {
                    idx = static_cast<int>(i);
                    break;
                }
            if (idx >= 0) {
                _GNote* ex = lst[idx];
                bool in_set = ghost_tokens.count(ex->id) != 0;
                if (in_set || token) {
                    lst.erase(lst.begin() + idx);
                    ghost_tokens.erase(ex->id);
                    if (ex->type == GHOST_START || ex->type == GHOST_END) nt = ex->type;
                    if ((line & ~1) != 2) line = ex->line;
                    token = in_set && token;
                } else {
                    token = false;
                }
            }
            _GNote* n = make(nt, 0, line, lane, width, channel, m, p);
            n->color = color;
            if (token) ghost_tokens.insert(n->id);
            lst.push_back(n);
        }
    }

    // ---- long point vs tap token merging ----

    // TryConsumeLongMergeToken -> ok, residual, merged critical, becomes a long flick end
    struct _Consume {
        bool ok;
        _Src residual;
        int crit;
        bool flick_end;
    };

    _Consume try_consume(const _GNote* L, const _GNote* S, const _Src& src) const {
        _Consume no{false, src, S->crit, false};
        if (version <= 1) return try_consume_legacy(L, S, src);
        if (S->owner.has_value()) {
            if (!S->owner->is_long || S->owner->id != L->channel) return no;
            return {true, _Src{}, S->crit, L->type == LONG_END && S->type == FLICK};
        }
        if (!src.any()) return no;
        if (_relay_merge_marker(L->type) && _relay_line_control(S)) {
            if (src.starts_with_independent_critical_tap())
                return {true, src.consume_default().consume_line_control(), L->crit, false};
            if (src.lead >= 1) {
                _Src r = src.consume_lead();
                r = r.consume_line_control();
                return {true, r, 1, false};
            }
            _Src r;
            if (src.def >= 1 && (L->type == LONG_START || L->type == BEZIER || _relay_merge_marker(S->type))) {
                r = src.consume_default();
                r = r.independent_tap() ? r.consume_line_control() : r.consume_all_line_control();
            } else {
                r = src.consume_line_control();
            }
            return {true, r, L->crit, false};
        }
        int st = S->type;
        if (_relay_merge_marker(st)) {
            _Src r = src;
            int c = S->crit;
            if (src.lead >= 1) r = r.consume_lead(), c = 1;
            return {true, r.consume_default().consume_line_control(), c, false};
        }
        if (st == NORMAL) {
            if (src.starts_with_independent_critical_tap()) return no;
            if (src.lead > 0) {
                _Src r = src.consume_lead();
                if (L->type != LONG_END && src.line_control() >= 1) r = r.consume_line_control();
                return {true, r, 1, false};
            }
            if (L->type == LONG_END || src.line_control() < 1) return no;
            return {true, src.consume_line_control(), L->crit, false};
        }
        if (st == FLICK) {
            if (src.starts_with_independent_critical_tap() && src.flick < 2) return no;
            if (src.independent_tap()) {
                if (src.lead < 1) return no;
                _Src r = src.consume_lead();
                if (L->type == LONG_END && src.flick >= 2) return {true, r.consume_flick(), 1, true};
                return {true, r, 1, false};
            }
            if (L->type != LONG_END || src.flick < 1) return no;
            _Src r = src;
            int c = L->crit;
            if (src.lead >= 1) r = r.consume_lead(), c = 1;
            return {true, r.consume_flick(), c, true};
        }
        if (st == DAMAGE) return no;
        return {true, _Src{}, S->crit, false};
    }

    _Consume try_consume_legacy(const _GNote* L, const _GNote* S, const _Src& src) const {
        _Consume no{false, src, S->crit, false};
        auto relay_lc_dir = [](const _Src& s) {
            return s.independent_tap() ? s.consume_legacy_directional() : s.consume_all_legacy_directional();
        };
        if (_relay_merge_marker(L->type) && _relay_line_control(S)) {
            if (src.starts_with_independent_critical_tap())
                return {true, src.consume_default().consume_legacy_directional(), L->crit, false};
            if (src.lead >= 1) {
                _Src r = src.consume_lead();
                r = r.independent_critical_tap_marker() ? r.consume_legacy_directional() : relay_lc_dir(r);
                return {true, r, 1, false};
            }
            _Src r;
            if (src.def >= 1 && (L->type == LONG_START || L->type == BEZIER || _relay_merge_marker(S->type))) {
                r = src.consume_default();
                r = r.independent_tap() ? r.consume_legacy_directional() : r.consume_all_legacy_directional();
            } else {
                r = relay_lc_dir(src);
            }
            return {true, r, L->crit, false};
        }
        int st = S->type;
        if (_relay_merge_marker(st)) return {true, _Src{}, S->crit, false};
        if (st == NORMAL) {
            if (!src.starts_with_independent_critical_tap() && src.lead >= 1)
                return {true, src.consume_lead(), 1, false};
            return no;
        }
        if (st == FLICK) {
            if (src.starts_with_independent_critical_tap() && src.dir < 2) return no;
            if (src.independent_tap()) {
                if (src.lead < 1) {
                    if (L->type == LONG_END && src.dir >= 1)
                        return {true, src.consume_legacy_directional(), L->crit, true};
                    return no;
                }
                _Src r = src.consume_lead();
                if (L->type == LONG_END && src.dir >= 2) return {true, r.consume_legacy_directional(), 1, true};
                return {true, r, 1, false};
            }
            if (L->type != LONG_END || src.dir < 1) return no;
            _Src r = src;
            int c = L->crit;
            if (src.lead >= 1) r = r.consume_lead(), c = 1;
            return {true, r.consume_legacy_directional(), c, true};
        }
        if (st == DAMAGE) return no;
        return {true, _Src{}, S->crit, false};
    }

    // ApplyResidualNoteInfo: what is left of a tap note after a long point took its share
    void apply_residual(_GNote* S, const _Src& r) const {
        if (version <= 1) {
            S->type = r.dir >= 1 ? FLICK : NORMAL;
            S->crit = r.crit > 0 ? 1 : 0;
            if (r.independent_tap()) S->line = 0;
            return;
        }
        S->crit = r.crit > 0 ? 1 : 0;
        if (r.def <= 0 && r.flick < 1) return;
        if (S->type == RELAY_ACTIVE && S->line == 0) return;
        S->type = r.flick >= 1 ? FLICK : NORMAL;
        if (r.independent_tap() || r.line_control() <= 0) S->line = 0;
    }

    int compare_long_candidates(const _GNote* c, const _Src& sc, const _GNote* p, const _Src& sp) const {
        if (int r = _prefer(_relay_line_control(c), _relay_line_control(p))) return r;
        if (int r = _sign(sc.line_control() - sp.line_control())) return r;
        if (int r = _sign(sc.lead - sp.lead)) return r;
        if (int r = _prefer(_relay_merge_marker(c->type), _relay_merge_marker(p->type))) return r;
        if (int r = _sign(c->crit - p->crit)) return r;
        if (int r = _compare_common_attributes(c, p)) return r;
        return _compare_common_counts(sc, sp);
    }

    // FindMergeNoteIndexForLongNote: the tap note at a long point's spot whose tokens it takes
    int find_long_merge_index(const std::vector<_GNote*>& lst, const _GNote* L) const {
        long long size = static_cast<long long>(lst.size());
        bool relay = _relay_merge_marker(L->type);
        if (version <= 1) {
            for (long long i = size - 1; i >= 0; --i) {
                const _GNote* c = lst[i];
                if (!_same_position(c, L->prog, L->lane, L->width, -1)) continue;
                if (relay && !_relay_merge_marker(c->type) && !_relay_line_control(c)) continue;
                if (try_consume(L, c, src_of(c)).ok) return static_cast<int>(i);
            }
            return -1;
        }
        int best = -1;
        bool best_pri = false;
        _Src best_src;
        for (long long i = size - 1; i >= 0; --i) {
            const _GNote* c = lst[i];
            if (!_same_position(c, L->prog, L->lane, L->width, -1)) continue;
            if (c->owner.has_value()) {
                if (!c->owner->is_long || c->owner->id != L->channel) continue;
                if (src_of(c).any()) return static_cast<int>(i);
                continue;
            }
            if (relay && !_relay_merge_marker(c->type) && !_relay_line_control(c)) continue;
            _Src s = src_of(c);
            if (L->type == LONG_END && s.line_control() >= 1 && s.lead <= s.line_control()) continue;
            if (!try_consume(L, c, s).ok) continue;
            bool pri = L->type == LONG_END || s.line_control() > 0;
            if (best >= 0 && (best_pri || !pri)) {
                if (best_pri != pri) continue;
                if (compare_long_candidates(c, s, lst[best], best_src) < 1) continue;
            }
            best = static_cast<int>(i);
            best_pri = pri;
            best_src = s;
        }
        return best;
    }

    // ReserveOverlappingLongStartCritical: one critical ease marker under a long start that other
    // channels only pass through with hidden relays goes to the start alone
    std::set<long long> reserve_start_critical(long long m, const std::vector<_GNote*>& lst,
                                               const std::vector<_GNote*>& tap_notes) const {
        std::set<long long> out;
        if (version != 2) return out;
        bool any = false;
        for (auto* n : tap_notes)
            if (n->type == RELAY_DEACTIVE && n->crit == 1 && _relay_line_control(n)) any = true;
        if (!any) return out;
        std::map<_PosKey, _GNote*> at;
        std::set<_PosKey> shared;
        for (auto* n : tap_notes) {
            if (n->channel != -1) continue;
            _PosKey k = _pos_key(n);
            if (at.count(k) || shared.count(k)) {
                at.erase(k);
                shared.insert(k);
            } else {
                at[k] = n;
            }
        }
        auto git = ghosts.find(m);
        if (git != ghosts.end())
            for (auto* g : git->second) at.erase(_pos_key(g));
        std::vector<_PosKey> order;
        std::map<_PosKey, std::vector<_GNote*>> groups;
        for (auto* L : lst) {
            _PosKey k = _pos_key(L);
            if (!at.count(k)) continue;
            if (!groups.count(k)) order.push_back(k);
            groups[k].push_back(L);
        }
        for (auto& k : order) {
            _GNote* n = at.at(k);
            if (n->type != RELAY_DEACTIVE || !_relay_line_control(n)) continue;
            _Src s = src_of(n);
            if (s.crit != 1 || s.lead != 1 || s.def != 0 || s.flick != 0 || s.line_control() < 1) continue;
            auto& group = groups[k];
            if (group.size() < 2) continue;
            std::set<long long> channels;
            const _GNote* start = nullptr;
            bool ok = true;
            for (auto* L : group) {
                if (L->channel < 0 || channels.count(L->channel)) {
                    ok = false;
                    break;
                }
                channels.insert(L->channel);
                if (start == nullptr && L->type == LONG_START) {
                    start = L;
                    continue;
                }
                if (L->type != RELAY_DEACTIVE) {
                    ok = false;
                    break;
                }
            }
            if (!ok || start == nullptr) continue;
            _Src r = s.consume_lead();
            apply_residual(n, r);
            n->src = r;
            out.insert(start->id);
        }
        return out;
    }

    int find_separated_line_control_tap(const std::vector<_GNote*>& lst, const _GNote* L, long long exclude) const {
        for (long long i = static_cast<long long>(lst.size()) - 1; i >= 0; --i) {
            const _GNote* n = lst[i];
            if (n->owner.has_value() || n->id == exclude) continue;
            if (!_same_position(n, L->prog, L->lane, L->width, -1)) continue;
            if (!_default_independent_tap_note(n->type, n->crit)) continue;
            if (src_of(n).def > 0) return static_cast<int>(i);
        }
        return -1;
    }

    // MergeNoteInfos
    void merge() {
        std::set<long long> removed;
        for (auto& [m, lst] : longs) {
            auto& tap_notes = notes[m];
            std::set<long long> reserved = reserve_start_critical(m, lst, tap_notes);
            std::map<_PosKey, std::vector<long long>> markers;
            if (version >= 2)
                for (auto* n : tap_notes)
                    if (!n->owner && _ignored_relay_marker(n->type, n->line) && n->channel == -1)
                        markers[_pos_key(n)].push_back(n->id);
            std::vector<std::pair<_GNote*, int>> flick_ends;
            for (auto* L : lst) {
                int nt = L->type, crit = L->crit, line = L->line;
                bool lc = nt != LONG_FLICK_END;
                int idx = find_long_merge_index(tap_notes, L);
                if (idx >= 0) {
                    _GNote* S = tap_notes[idx];
                    int s_type = S->type, s_line = S->line;
                    bool s_lc = S->lc;
                    _Src src = src_of(S);
                    _Consume c = try_consume(L, S, src);
                    if (c.ok) {
                        if (version >= 2 && !_ignored_relay_marker(S->type, S->line)) {
                            auto it = markers.find(_pos_key(L));
                            if (it != markers.end())
                                for (long long id : it->second)
                                    if (id != S->id) removed.insert(id);
                        }
                        if (c.residual.any()) {
                            apply_residual(S, c.residual);
                            S->src = c.residual;
                        } else {
                            removed.insert(S->id);
                            S->src.reset();
                        }
                        if (!S->owner && (L->type == LONG_START || L->type == BEZIER) && src.def < 1) {
                            int sep = find_separated_line_control_tap(tap_notes, L, S->id);
                            if (sep >= 0) {
                                _GNote* t = tap_notes[sep];
                                _Src r = src_of(t).consume_default();
                                if (r.any()) {
                                    apply_residual(t, r);
                                    t->src = r;
                                } else {
                                    removed.insert(t->id);
                                    t->src.reset();
                                }
                            }
                        }
                        if (version > 1) {
                            if (L->type == LONG_END || L->type == LONG_FLICK_END)
                                line = L->line;
                            else
                                line = s_line != 0 && L->line == 1 ? s_line : L->line;
                        } else {
                            line = s_line != 0 ? s_line : L->line;
                        }
                        crit = c.crit;
                        lc = _relay_merge_marker(s_type) ? s_lc : nt != LONG_FLICK_END;
                        if (nt == LONG_FLICK_END || c.flick_end) {
                            if (L->type == LONG_END) flick_ends.push_back({L, crit});
                            nt = LONG_FLICK_END;
                        }
                    }
                }
                if (reserved.count(L->id)) crit = 1;
                L->type = nt;
                L->crit = crit;
                L->line = line;
                L->lc = lc;
                tap_notes.push_back(L);
            }
            // CreateLongFlickEndNoteInfo makes a new note with the same id at the end of the list
            for (auto& [L, c] : flick_ends) {
                for (long long i = static_cast<long long>(tap_notes.size()) - 1; i >= 0; --i)
                    if (tap_notes[i]->id == L->id) {
                        tap_notes.erase(tap_notes.begin() + i);
                        break;
                    }
                _GNote* n = make(LONG_FLICK_END, c, L->line, L->lane, L->width, L->channel, L->measure, L->prog);
                --next_id;
                n->id = L->id;
                tap_notes.push_back(n);
            }
        }
        if (!removed.empty())
            for (auto& [m, lst] : notes)
                for (long long i = static_cast<long long>(lst.size()) - 1; i >= 0; --i)
                    if (removed.count(lst[i]->id)) lst.erase(lst.begin() + i);
        longs.clear();
        for (auto& [m, glst] : ghosts) {
            auto& tap_notes = notes[m];
            for (auto* g : glst) {
                for (std::size_t i = 0; i < tap_notes.size(); ++i) {
                    _GNote* n = tap_notes[i];
                    if (n->type == GHOST_RELAY && _same_position(n, g->prog, g->lane, g->width, -1)) {
                        tap_notes.erase(tap_notes.begin() + static_cast<long long>(i));
                        if (n->line != 0) g->line = n->line;
                        break;
                    }
                }
                tap_notes.push_back(g);
            }
        }
        ghosts.clear();
        apply_attributes();
        for (auto* n : owned) n->owner.reset();
        owned.clear();
        single_start.clear();
    }

    // ApplyLongNoteAttributes
    void apply_attributes() {
        if (attrs.empty()) return;
        for (auto& [m, lst] : notes)
            for (auto* n : lst) {
                if (n->channel < 0 || n->type < LONG_START || n->type > BEZIER) continue;
                _AttrKey key{m, _pos_key(n), n->channel, n->type == LONG_FLICK_END ? LONG_END : n->type};
                auto it = attrs.find(key);
                if (it == attrs.end()) continue;
                n->crit = it->second.crit;
                n->lc = it->second.lc;
                n->line = it->second.line;
            }
        attrs.clear();
    }

    // SortNoteInfo: per measure by RationalProgress only
    void sort() {
        for (auto& [m, lst] : notes) {
            if (lst.size() < 2) continue;
            bool sorted = true;
            for (std::size_t i = 1; i < lst.size(); ++i)
                if (_prog_compare(lst[i - 1]->prog, lst[i]->prog) >= 1) sorted = false;
            if (!sorted)
                _dotnet_sort(lst, [](const _GNote* a, const _GNote* b) { return _prog_compare(a->prog, b->prog); });
        }
    }

    // GetStockStartNote: the newest stocked start of the end's channel strictly before it
    static _GNote* take_start(std::vector<_GNote*>& stock, const _GNote* end) {
        for (std::size_t j = stock.size(); j-- > 0;)
            if (stock[j]->channel == end->channel && _compare_note_time(stock[j], end) == 1) {
                _GNote* s = stock[j];
                stock.erase(stock.begin() + static_cast<long long>(j));
                return s;
            }
        return nullptr;
    }

    // GetStockRelayNotes: every stocked relay of the channel strictly between start and end
    static std::vector<_GNote*> take_relays(std::vector<_GNote*>& stock, const _GNote* start, const _GNote* end) {
        std::vector<_GNote*> out;
        for (std::size_t j = stock.size(); j-- > 0;) {
            _GNote* r = stock[j];
            if (r->channel == start->channel && _compare_note_time(r, start) == 2 && _compare_note_time(r, end) == 1) {
                out.push_back(r);
                stock.erase(stock.begin() + static_cast<long long>(j));
            }
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    static void attach(_GNote* start, _GNote* end, std::vector<_GNote*> relays) {
        std::vector<_GNote*> kids{end};
        kids.insert(kids.end(), relays.begin(), relays.end());
        _dotnet_sort(kids, _measure_progress_cmp);
        start->children.insert(start->children.end(), kids.begin(), kids.end());
    }

    // SetLongNoteChildren + SetGhostNoteChildren; SetLongNoteList spreads a critical start to all
    // of its points. an end with no start makes the game throw, so it is dropped here
    void set_children() {
        for (auto& [m, lst] : notes)
            for (auto* n : lst) {
                if (n->type >= RELAY_ACTIVE && n->type <= BEZIER) {
                    stock_relay.push_back(n);
                } else if (n->type == LONG_END || n->type == LONG_FLICK_END) {
                    _GNote* start = take_start(stock_start, n);
                    if (start == nullptr) continue;
                    attach(start, n, take_relays(stock_relay, start, n));
                    holds.push_back(start);
                    if (start->crit == 1)
                        for (auto* c : start->children) c->crit = 1;
                } else if (n->type == LONG_START) {
                    stock_start.push_back(n);
                }
            }
        for (auto& [m, lst] : notes)
            for (auto* n : lst) {
                if (n->type == GHOST_START) {
                    stock_ghost_start.push_back(n);
                } else if (n->type == GHOST_END) {
                    _GNote* start = take_start(stock_ghost_start, n);
                    if (start == nullptr) continue;
                    attach(start, n, take_relays(stock_ghost_relay, start, n));
                    guides.push_back(start);
                } else if (n->type == GHOST_RELAY) {
                    stock_ghost_relay.push_back(n);
                }
            }
    }
};

bool _dotnet_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

std::string _dotnet_trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && _dotnet_space(s[a])) ++a;
    while (b > a && _dotnet_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Int32.TryParse with NumberStyles.None: plain digits, any number of them, up to int.MaxValue
std::optional<long long> _parse_int32(const std::string& s) {
    if (!is_digits(s)) return std::nullopt;
    long long v = 0;
    for (char c : s) {
        v = v * 10 + (c - '0');
        if (v > 2147483647LL) return std::nullopt;
    }
    return v;
}

// Int32.TryParse with NumberStyles.Integer: surrounding white space and one leading sign allowed
std::optional<long long> _parse_int32_integer(const std::string& s) {
    std::string t = _dotnet_trim(s);
    bool negative = false;
    if (!t.empty() && (t[0] == '-' || t[0] == '+')) negative = t[0] == '-', t = t.substr(1);
    if (!is_digits(t)) return std::nullopt;
    long long v = 0;
    for (char c : t) {
        v = v * 10 + (c - '0');
        if (v > 2147483648LL) return std::nullopt;
    }
    if (!negative && v > 2147483647LL) return std::nullopt;
    return negative ? -v : v;
}

// Double.Parse / Single.Parse with NumberStyles.Float | AllowThousands and the invariant culture,
// which Convert.ChangeType uses for the regex groups. nullopt where the game would throw
std::optional<double> _dotnet_number(const std::string& s, bool single) {
    std::string t = _dotnet_trim(s);
    std::size_t i = 0, n = t.size();
    std::string clean;
    if (i < n && (t[i] == '+' || t[i] == '-')) clean += t[i++];
    std::string rest = t.substr(i);
    std::string lower;
    for (char c : rest) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "infinity" || lower == "nan") {
        double v = lower == "nan" ? std::nan("") : HUGE_VAL;
        return !clean.empty() && clean[0] == '-' ? -v : v;
    }
    bool digits = false, decimal = false;
    for (; i < n; ++i) {
        char c = t[i];
        if (c >= '0' && c <= '9') {
            clean += c;
            digits = true;
        } else if (c == '.' && !decimal) {
            clean += c;
            decimal = true;
        } else if (c == ',' && digits && !decimal) {
            continue;
        } else {
            break;
        }
    }
    if (!digits) return std::nullopt;
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        std::size_t j = i + 1;
        std::string exponent = "e";
        if (j < n && (t[j] == '+' || t[j] == '-')) exponent += t[j++];
        std::size_t start = j;
        while (j < n && t[j] >= '0' && t[j] <= '9') exponent += t[j++];
        if (j > start) {
            clean += exponent;
            i = j;
        }
    }
    if (i != n) return std::nullopt;
    return single ? static_cast<double>(std::strtof(clean.c_str(), nullptr)) : std::strtod(clean.c_str(), nullptr);
}

// SusFormatVersion.ResolveVersion: an explicit #VISION_SUS_FORMAT_VERSION wins, else a
// #CREATE_APP_VERSION of at least 1.0.0 means 2.0.0, else 1.0.0. the game throws on a bad or
// repeated tag; here the first readable one is used
using _Version = std::array<long long, 4>;

// TryParseFormatVersion (1 or 3 plain parts, build 0 when only the major is given) and
// System.Version.TryParse (2 to 4 parts, each a non-negative Int32 with NumberStyles.Integer)
std::optional<_Version> _parse_version(const std::string& s, bool format_tag) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t dot = s.find('.', start);
        parts.push_back(s.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    if (format_tag ? parts.size() != 1 && parts.size() != 3 : parts.size() < 2 || parts.size() > 4)
        return std::nullopt;
    _Version v = format_tag ? _Version{0, 0, 0, -1} : _Version{0, 0, -1, -1};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        auto part = format_tag ? _parse_int32(parts[i]) : _parse_int32_integer(parts[i]);
        if (!part.has_value() || *part < 0) return std::nullopt;
        v[i] = *part;
    }
    return v;
}

_Version _resolve_version(const std::vector<std::string>& lines) {
    static const std::regex format_re(R"(^#VISION_SUS_FORMAT_VERSION\s+([0-9]+(?:\.[0-9]+\.[0-9]+)?)\s*$)");
    static const std::regex app_re(R"x(^#CREATE_APP_VERSION\s+(?:"([^"\r\n]+)"|([^\s"\r\n]+))\s*$)x");
    const _Version legacy{1, 0, 0, -1}, current{2, 2, 0, -1};
    for (auto& line : lines) {
        std::smatch m;
        if (!startswith(line, "#VISION_SUS_FORMAT_VERSION") || !std::regex_search(line, m, format_re)) continue;
        auto v = _parse_version(m[1].str(), true);
        if (v.has_value() && *v >= legacy && *v <= current) return *v;
    }
    for (auto& line : lines) {
        std::smatch m;
        if (!startswith(line, "#CREATE_APP_VERSION") || !std::regex_search(line, m, app_re)) continue;
        auto v = _parse_version(m[1].matched ? m[1].str() : m[2].str(), false);
        if (v.has_value()) return *v >= legacy ? _Version{2, 0, 0, -1} : legacy;
    }
    return legacy;
}

std::vector<std::string> _split_ws(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// String.Split('\n'): the game splits the chart on line feeds only, so a "\r" stays on its line
std::vector<std::string> _sus_lines(const std::string& data) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        std::size_t lf = data.find('\n', start);
        if (lf == std::string::npos) {
            out.push_back(data.substr(start));
            return out;
        }
        out.push_back(data.substr(start, lf - start));
        start = lf + 1;
    }
}

bool _blank(const std::string& line) {
    for (char c : line)
        if (!_dotnet_space(c)) return false;
    return true;
}

// SusFormatVersion.SupportsNoteOwnership / SupportsLongNoteAttributes: "<tag> 1" on a 2.x chart,
// or a format version of at least 2.1.0 / 2.2.0
bool _supports(const std::vector<std::string>& lines, const _Version& v, const char* tag, const _Version& min) {
    for (auto& line : lines) {
        if (!startswith(line, tag)) continue;
        auto parts = _split_ws(line);
        return v[0] == 2 && parts.size() == 2 && parts[0] == tag && _parse_int32(parts[1]) == 1;
    }
    return v >= min;
}

std::pair<core::Score, long long> _holodori_to_score(
    _Game& game, const std::map<long long, std::string>& color_definitions,
    std::vector<std::pair<long long, double>> bpms, std::vector<std::pair<long long, double>> nms,
    std::vector<std::pair<long long, long long>> sp_skill_points, std::optional<std::array<long long, 4>> fever,
    std::string music_id, double wave_offset, long long ticks_per_beat);

}  // namespace

std::pair<core::Score, long long> loads(const std::string& data) {
    long long ticks_per_beat = TICKS_PER_BEAT;  // positions are fractional i/N; the file's ticks_per_beat is ignored
    std::string music_id;
    double wave_offset = 0.0;
    double base_bpm = 120.0;

    std::vector<std::pair<long long, double>> bar_lengths;
    std::set<long long> bar_measures;
    std::map<std::string, double> bpm_definitions;
    std::vector<std::pair<long long, std::string>> bpm_events;  // (tick, #BPMxx id)
    std::vector<std::pair<long long, double>> bpm_data_lines;
    std::map<std::string, double> nms_definitions;
    std::vector<std::pair<long long, std::string>> nms_events;
    std::vector<std::pair<long long, double>> nms_data_lines;
    std::map<long long, std::string> color_definitions;
    std::set<long long> event_measures;  // measures holding a bpm or nms change event

    std::vector<std::pair<long long, long long>> sp_skill_points;  // (tick, deck slot)
    bool sp_explicit = false;
    std::vector<_FeverEvent> fever_events;

    static const std::regex note_re(R"(^#([0-9]{3})([0-9a-z])([0-9a-zA-Z]):[ \t]*([0-9a-zA-Z]+))");
    static const std::regex ghost_re(
        R"(^#([0-9]{3})([0-9a-z])([0-9a-z])([0-9a-zA-Z])([0-9A-Fa-f]{4}):[ \t]*([0-9a-zA-Z]+))");
    static const std::regex long_re(R"(^#([0-9]{3})([0-9a-z])([0-9a-z])([0-9a-zA-Z]):[ \t]*([0-9a-zA-Z]+))");
    static const std::regex measure_length_re(R"(^#([0-9]{3})02:[ \t]*([0-9]+(?:\.[0-9]+)?)[\r\s]*$)");
    static const std::regex measure_count_re(R"(^#MEASURE_COUNT ([0-9]+))");
    static const std::regex waveoffset_re(R"(^#WAVEOFFSET ([^\n]+))");
    static const std::regex basebpm_re(R"(^#BASEBPM ([^\n]+))");
    static const std::regex bpm_def_re(R"(^#BPM([0-9a-zA-Z]{2}):([^\n]+))");
    static const std::regex nms_def_re(R"(^#NMS([0-9a-zA-Z]{2}):([^\n]+))");
    static const std::regex color_re(R"(^#COLOR([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8}))");
    static const std::regex sp_skill_re(R"(^#SP_SKILL ([0-9]{3}) ([0-9]+)/([0-9]+) ([0-9]+))");

    auto lines = _sus_lines(data);
    auto skipped = [](const std::string& line) {
        return _blank(line) || startswith(line, "#VISION_NOTE_OWNER") ||
               startswith(line, "#VISION_LONG_NOTE_ATTRIBUTES") || startswith(line, "#VISION_SUS_FORMAT_VERSION") ||
               startswith(line, "#CREATE_APP_VERSION") || startswith(line, "#VISION_SUS_NOTE_OWNERSHIP_VERSION") ||
               startswith(line, "#VISION_SUS_LONG_NOTE_ATTRIBUTES_VERSION");
    };

    // PHASE 1: bar lengths (TryMatchMeasureLengthData; AddMeasureLength keeps the first one)
    for (auto& line : lines) {
        std::smatch m;
        if (skipped(line) || !std::regex_search(line, m, measure_length_re)) continue;
        long long measure = py_int(m[1].str());
        double length = std::strtod(m[2].str().c_str(), nullptr);
        if (length > 0.0 && bar_measures.insert(measure).second) bar_lengths.push_back({measure, length});
    }
    if (bar_lengths.empty()) bar_lengths.push_back({0, 4.0});
    std::vector<_Bar> bars = _get_bars(bar_lengths, ticks_per_beat);

    // PHASE 2: TryMatchMetaData and the event 0 lines of TryMatchNoteData
    for (auto& line : lines) {
        if (skipped(line)) continue;
        std::smatch m;
        std::string stripped = strip(line);
        if (startswith(stripped, "#") && stripped.find(':', 1) == std::string::npos) {
            // #MUSIC_ID is not read by the game; it is kept as the title
            std::string rest = stripped.substr(1);
            std::size_t ws = 0;
            while (ws < rest.size() && !py_isspace_ch(rest[ws])) ++ws;
            if (to_upper(rest.substr(0, ws)) == "MUSIC_ID") {
                std::size_t vs = ws;
                while (vs < rest.size() && py_isspace_ch(rest[vs])) ++vs;
                music_id = ws < rest.size() ? strip_chars(strip(rest.substr(vs)), "\"") : "";
                continue;
            }
        }
        if (std::regex_search(line, m, waveoffset_re)) {
            if (auto v = _dotnet_number(m[1].str(), true)) wave_offset = *v;
        } else if (std::regex_search(line, m, basebpm_re)) {
            if (auto v = _dotnet_number(m[1].str(), false)) base_bpm = *v;
        } else if (std::regex_search(line, m, bpm_def_re)) {
            if (auto v = _dotnet_number(m[2].str(), false)) bpm_definitions.emplace(m[1].str(), *v);
        } else if (std::regex_search(line, m, nms_def_re)) {
            if (auto v = _dotnet_number(m[2].str(), false)) nms_definitions.emplace(m[1].str(), *v);
        } else if (std::regex_search(line, m, color_re)) {
            color_definitions[std::stoll(m[1].str(), nullptr, 16)] = m[2].str();
        } else if (std::regex_search(line, m, sp_skill_re)) {  // legacy #SP_SKILL mmm num/den slot
            auto measure = _parse_int32(m[1].str()), num = _parse_int32(m[2].str()), den = _parse_int32(m[3].str()),
                 slot = _parse_int32(m[4].str());
            if (!measure || !num || !den || !slot) continue;
            sp_skill_points.push_back({_get_ticks(bars, *measure, *den ? *num : 0, *den ? *den : 1), *slot});
            sp_explicit = true;
        } else if (std::regex_search(line, m, note_re) && m[2].str() == "0") {
            long long measure = py_int(m[1].str());
            char lane_char = m[3].str()[0];
            std::string cells = m[4].str();
            if (lane_char == '8') {  // bpm change: each cell is a #BPMxx id
                for (auto& [pos, cell, total] : _meta_cells(cells)) {
                    bpm_events.push_back({_get_ticks(bars, measure, pos, total), cell});
                    event_measures.insert(measure);
                }
            } else if (lane_char == 'A') {  // NMS (hispeed) change: each cell is a #NMSxx id
                for (auto& [pos, cell, total] : _meta_cells(cells)) {
                    nms_events.push_back({_get_ticks(bars, measure, pos, total), cell});
                    event_measures.insert(measure);
                }
            } else if (lane_char == 'B') {  // sp skill: each cell is a deck slot number
                for (auto& [pos, value, total] : _meta_values(cells))
                    sp_skill_points.push_back({_get_ticks(bars, measure, pos, total), value});
                sp_explicit = true;
            } else if (lane_char == 'C' || lane_char == 'D') {  // fever charge / fever section
                for (auto& [pos, value, total] : _meta_values(cells))
                    if (int type = _fever_point_type(lane_char, value))
                        fever_events.push_back({measure, static_cast<float>(pos) / static_cast<float>(total),
                                                _get_ticks(bars, measure, pos, total), type});
            }
        }
    }
    for (auto& [tick, id] : bpm_events) {
        auto it = bpm_definitions.find(id);
        bpm_data_lines.push_back({tick, it != bpm_definitions.end() ? it->second : base_bpm});
    }
    for (auto& [tick, id] : nms_events) {
        auto it = nms_definitions.find(id);
        nms_data_lines.push_back({tick, it != nms_definitions.end() ? it->second : 1.0});
    }
    std::optional<std::array<long long, 4>> fever = _fever_section(fever_events);

    // PHASE 3: notes, the way SusConverterBase.ExecuteLine reads them
    _Game game(bars);
    _Version version = _resolve_version(lines);
    game.version = static_cast<int>(version[0]);
    game.supports_owner = _supports(lines, version, "#VISION_SUS_NOTE_OWNERSHIP_VERSION", {2, 1, 0, -1});
    game.supports_attrs = _supports(lines, version, "#VISION_SUS_LONG_NOTE_ATTRIBUTES_VERSION", {2, 2, 0, -1});
    game.index_long_positions(lines);
    for (auto& line : lines) {
        if (_blank(line)) continue;
        std::smatch m;
        if (startswith(line, "#VISION_NOTE_OWNER")) {  // #VISION_NOTE_OWNER L|I id: owns the next tap or flick token
            auto parts = _split_ws(line);
            auto id = parts.size() == 3 ? _parse_int32(parts[2]) : std::nullopt;
            if (id.has_value() && game.supports_owner && !game.pending_owner && parts[0] == "#VISION_NOTE_OWNER" &&
                (parts[1] == "L" || parts[1] == "I") && (parts[1] == "I" || *id < 62))
                game.pending_owner = _Owner{parts[1] == "L", *id};
            continue;
        }
        if (startswith(line, "#VISION_LONG_NOTE_ATTRIBUTES")) {
            // channel measure num/den lane width type critical line-control line-type
            auto parts = _split_ws(line);
            if (!game.supports_attrs || parts.size() != 10 || parts[0] != "#VISION_LONG_NOTE_ATTRIBUTES") continue;
            auto channel = _parse_int32(parts[1]), measure = _parse_int32(parts[2]), type = _parse_int32(parts[6]),
                 crit = _parse_int32(parts[7]), lc = _parse_int32(parts[8]), line_type = _parse_int32(parts[9]);
            std::size_t slash = parts[3].find('/');
            if (!channel || *channel > 61 || !measure || *measure > 999 || parts[4].size() != 1 ||
                parts[5].size() != 1 || !std::isalnum(static_cast<unsigned char>(parts[4][0])) ||
                !std::isalnum(static_cast<unsigned char>(parts[5][0])) || !type || *type < 1 || *type > 5 || !crit ||
                *crit > 1 || !lc || *lc > 1 || !line_type || *line_type > 3 || slash == std::string::npos ||
                parts[3].find('/', slash + 1) != std::string::npos)
                continue;
            auto num = _parse_int32(parts[3].substr(0, slash)), den = _parse_int32(parts[3].substr(slash + 1));
            if (!num || !den || *den < 1 || *num >= *den) continue;
            long long lane = _char_to_int(parts[4][0]) - 2, width = _char_to_int(parts[5][0]);
            if (width > 12 || lane < 0 || lane > 12 || lane + width > 12) continue;
            static const int types[] = {LONG_START, LONG_END, RELAY_ACTIVE, BEZIER, RELAY_DEACTIVE};
            _AttrKey key{*measure, _pos_key(_Prog{*num, *den}, lane, static_cast<float>(width)), *channel,
                         types[*type - 1]};
            if (!game.attrs.count(key)) game.attrs[key] = _Attrs{static_cast<int>(*crit), *lc == 1, static_cast<int>(*line_type)};
            continue;
        }
        if (std::regex_search(line, m, measure_count_re)) {
            if (auto count = _parse_int32(m[1].str())) game.set_measure_count(*count);
            continue;
        }
        if (std::regex_search(line, m, note_re)) {
            char event = m[2].str()[0];
            if (event == '1' || event == '5') {
                long long measure = py_int(m[1].str());
                game.note_line(measure);
                game.create_note_data(event == '1' ? NORMAL : FLICK, measure, _char_to_int(m[3].str()[0]) - 2,
                                      m[4].str());
                game.pending_owner.reset();
            }
            continue;
        }
        game.pending_owner.reset();  // the game throws when an owner tag is not followed by a tap line
        if (std::regex_search(line, m, ghost_re)) {
            if (m[2].str() == "6") {
                long long measure = py_int(m[1].str());
                game.note_line(measure);
                game.create_ghost_data(measure, _char_to_int(m[3].str()[0]) - 2, _char_to_int(m[4].str()[0]),
                                       std::stoll(m[5].str(), nullptr, 16), m[6].str());
            }
            continue;
        }
        if (std::regex_search(line, m, long_re)) {
            char event = m[2].str()[0];
            if (event >= '2' && event <= '4') {
                long long measure = py_int(m[1].str());
                game.note_line(measure);
                game.create_long_data(measure, _char_to_int(m[3].str()[0]) - 2, _char_to_int(m[4].str()[0]),
                                      m[5].str());
            }
        }
    }
    game.merge();
    game.sort();
    game.set_children();

    // AddSpSkillEventPoint: without any sp skill line the game places slots 1-5 at the middle of
    // measures count/6, 2*count/6, ... where count is CalcMusicScoreData's measure count
    if (!sp_explicit) {
        long long count = std::max(game.measure_count, game.last_measure + 1);
        if (!bar_measures.empty()) count = std::max(count, *bar_measures.rbegin() + 1);
        if (!event_measures.empty()) count = std::max(count, *event_measures.rbegin() + 1);
        if (fever.has_value())
            for (auto& e : fever_events) count = std::max(count, e.measure + 1);
        long long per = std::max(count, 0LL) / 6;
        for (long long slot = 1; slot <= 5; ++slot) sp_skill_points.push_back({_get_ticks(bars, slot * per, 1, 2), slot});
    }

    std::stable_sort(bpm_data_lines.begin(), bpm_data_lines.end(),
                     [](auto& a, auto& b) { return a.first < b.first; });

    return _holodori_to_score(game, color_definitions, bpm_data_lines, nms_data_lines, sp_skill_points, fever,
                              music_id, wave_offset, ticks_per_beat);
}

namespace {

std::pair<core::Score, long long> _holodori_to_score(
    _Game& game, const std::map<long long, std::string>& color_definitions,
    std::vector<std::pair<long long, double>> bpms, std::vector<std::pair<long long, double>> nms,
    std::vector<std::pair<long long, long long>> sp_skill_points, std::optional<std::array<long long, 4>> fever,
    std::string music_id, double wave_offset, long long ticks_per_beat) {
    // returns (score, bezier_tick_count)
    std::vector<NodePtr> notes;

    std::vector<std::pair<long long, double>> bpm_src = bpms;
    if (bpm_src.empty()) bpm_src.push_back({0, 120.0});
    for (auto& [tick, bpm] : bpm_src)
        notes.push_back(std::make_shared<Bpm>(Num::F(_tick_to_beat(tick)), Num::F(bpm)));

    // NMS is a single hispeed layer -> one TimeScaleGroup
    auto tsg = std::make_shared<TimeScaleGroup>();
    if (!nms.empty()) {
        std::vector<std::pair<long long, double>> nms_sorted = nms;
        std::stable_sort(nms_sorted.begin(), nms_sorted.end(),
                         [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [tick, mul] : nms_sorted)
            tsg->append(std::make_shared<TimeScalePoint>(Num::F(_tick_to_beat(tick)), Num::F(mul)));
        bool has0 = false;
        for (auto& p : tsg->changes)
            if ((double)p->beat == 0.0) has0 = true;
        if (!has0) tsg->insert(0, std::make_shared<TimeScalePoint>(Num::F(0.0), Num::F(1.0)));
    } else {
        tsg->append(std::make_shared<TimeScalePoint>(Num::F(0.0), Num::F(1.0)));
    }
    notes.push_back(tsg);

    std::set<std::pair<long long, long long>> sp_set;
    for (auto& sp : sp_skill_points) sp_set.insert(sp);
    for (auto& [tick, slot] : sp_set)
        notes.push_back(std::make_shared<HolodoriSkill>(Num::F(_tick_to_beat(tick)), slot));

    if (fever.has_value()) {
        notes.push_back(std::make_shared<HolodoriChargeStart>(Num::F(_tick_to_beat((*fever)[0]))));
        notes.push_back(std::make_shared<HolodoriChargeEnd>(Num::F(_tick_to_beat((*fever)[1]))));
        notes.push_back(std::make_shared<HolodoriFeverStart>(Num::F(_tick_to_beat((*fever)[2]))));
        notes.push_back(std::make_shared<HolodoriFeverEnd>(Num::F(_tick_to_beat((*fever)[3]))));
    }

    auto beat_of = [&](const _GNote* n) { return Num::F(_tick_to_beat(game.tick_of(n))); };
    auto lane_of = [](const _GNote* n) {
        return Num::F(_sus_to_usc_lane(n->lane + 2, static_cast<long long>(n->width)));
    };
    auto size_of = [](const _GNote* n) { return Num::F(_sus_to_usc_size(static_cast<long long>(n->width))); };
    auto ease_of = [](int line) { return std::string(line == 2 ? "in" : (line == 3 ? "out" : "linear")); };

    long long bezier_ticks = 0;
    for (auto& [m, lst] : game.notes) {
        for (_GNote* n : lst) {
            if (n->type == NORMAL || n->type == FLICK || n->type == DAMAGE) {
                auto s = std::make_shared<Single>();
                s->beat = beat_of(n);
                s->critical = n->crit == 1;
                s->lane = lane_of(n);
                s->size = size_of(n);
                s->timeScaleGroup = 0;
                s->trace = n->type == FLICK;
                if (n->type == FLICK) s->direction = "up";
                if (n->type == DAMAGE) s->type = "damage";
                notes.push_back(s);
            }
        }
    }
    for (_GNote* n : game.holds) {
        bool critical = n->crit == 1;
        auto slide = std::make_shared<Slide>(critical);
        auto start = std::make_shared<SlideStartPoint>();
        start->beat = beat_of(n);
        start->critical = critical;
        start->ease = ease_of(n->line);
        start->judgeType = "normal";
        start->lane = lane_of(n);
        start->size = size_of(n);
        start->timeScaleGroup = 0;
        slide->append(start);
        for (_GNote* c : n->children) {
            if (c->type == LONG_END || c->type == LONG_FLICK_END) {
                auto p = std::make_shared<SlideEndPoint>();
                p->beat = beat_of(c);
                p->critical = c->crit == 1;
                p->judgeType = "trace";
                p->lane = lane_of(c);
                p->size = size_of(c);
                p->timeScaleGroup = 0;
                if (c->type == LONG_FLICK_END) p->direction = "up";
                slide->append(p);
            } else if (c->type == BEZIER) {
                // a hidden bezier shape control - no combo, no score, and the bezier pull is lost in
                // usc, so drop it. it is tallied only because the live-score perfect-coefficient sum
                // still counts it (a game bug)
                ++bezier_ticks;
            } else if (c->type == RELAY_ACTIVE || c->lc) {
                // an active relay is a judged tick, one off the line attaches to it; an inactive one
                // is a hidden shape point. an inactive relay off the line is neither drawn nor judged
                auto p = std::make_shared<SlideRelayPoint>();
                p->beat = beat_of(c);
                p->ease = ease_of(c->line);
                p->lane = lane_of(c);
                p->size = size_of(c);
                p->timeScaleGroup = 0;
                if (c->type == RELAY_ACTIVE) {
                    p->type = c->lc ? "tick" : "attach";
                    p->critical = c->crit == 1;
                } else {
                    p->type = "tick";
                    p->critical = std::nullopt;
                }
                slide->append(p);
            }
        }
        slide->sort();
        notes.push_back(slide);
    }
    for (_GNote* n : game.guides) {
        // ghost notes -> guides. holodori guides hold at the #COLOR alpha (no fade); rgba in color_code
        std::optional<std::string> color_code;
        auto it = color_definitions.find(n->color);
        if (it != color_definitions.end()) {
            color_code = it->second;
        } else {
            static const char* hex = "0123456789ABCDEF";
            std::string id(4, '0');
            for (int k = 0; k < 4; ++k) id[3 - k] = hex[(n->color >> (4 * k)) & 15];
            color_code = id;
        }
        auto guide = std::make_shared<Guide>("green", "none", std::vector<shared_ptr<GuidePoint>>{}, "guide",
                                             color_code);
        std::vector<_GNote*> pts{n};
        pts.insert(pts.end(), n->children.begin(), n->children.end());
        for (_GNote* c : pts) {
            auto p = std::make_shared<GuidePoint>();
            p->beat = beat_of(c);
            p->ease = ease_of(c->line);
            p->lane = lane_of(c);
            p->size = size_of(c);
            p->timeScaleGroup = 0;
            guide->append(p);
        }
        notes.push_back(guide);
    }

    std::stable_sort(notes.begin(), notes.end(), [](const NodePtr& a, const NodePtr& b) {
        return get_sus_sort_number(a.get()) < get_sus_sort_number(b.get());
    });
    auto metadata = std::make_shared<MetaData>();
    metadata->title = music_id;
    metadata->artist = "";
    metadata->designer = "";
    metadata->waveoffset = -wave_offset;
    metadata->requests = std::vector<std::string>{"ticks_per_beat " + std::to_string(ticks_per_beat)};
    return {core::Score(metadata, notes), bezier_ticks};
}

}  // namespace

}  // namespace rg::converters::holodori_sus
