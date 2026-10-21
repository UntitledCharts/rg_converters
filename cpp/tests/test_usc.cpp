#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/sus.hpp"
#include "rg/converters/usc.hpp"
#include "rg/util/json.hpp"

using namespace rg;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string test_files = argv[1];
    std::string refs = argv[2];
    util::Json meta = util::json_parse(read_file(refs + "/meta.json"));

    int fails = 0;
    for (auto& rec : meta.arr()) {
        std::string name;
        long long usc_combo = -1;
        std::string det;
        for (auto& kv : rec.items()) {
            if (kv.first == "name") name = kv.second.as_str();
            else if (kv.first == "usc_combo") usc_combo = kv.second.as_int();
            else if (kv.first == "detect" && !kv.second.is_null()) det = kv.second.as_str();
        }
        std::string sus_text = read_file(test_files + "/" + name);
        std::string want_min = read_file(refs + "/" + name + ".usc.min");
        std::string want_pretty = read_file(refs + "/" + name + ".usc.pretty");
        std::string want_fromusc = read_file(refs + "/" + name + ".fromusc.sus");

        // 1) sus -> usc export (minified + pretty)
        core::Score sc = converters::sus::loads(sus_text);
        std::string got_min = converters::usc::dumps(sc, true);
        core::Score sc2 = converters::sus::loads(sus_text);
        std::string got_pretty = converters::usc::dumps(sc2, false);

        // 2) usc -> load -> sus export
        core::Score from = converters::usc::load_text(want_min);
        long long from_combo = from.combo_count();
        std::string got_fromusc = converters::sus::dumps(from);
        auto d = converters::usc::detect(want_min);

        bool min_ok = got_min == want_min;
        bool pretty_ok = got_pretty == want_pretty;
        bool fromusc_ok = got_fromusc == want_fromusc;
        bool combo_ok = from_combo == usc_combo;
        bool det_ok = d.has_value() && *d == det;

        std::printf("%-32s uscmin %s pretty %s uscload->sus %s combo %s(%lld/%lld) detect %s\n",
                    name.c_str(), min_ok ? "OK" : "BAD", pretty_ok ? "OK" : "BAD",
                    fromusc_ok ? "OK" : "BAD", combo_ok ? "OK" : "BAD", from_combo,
                    usc_combo, det_ok ? "OK" : "BAD");
        if (!(min_ok && pretty_ok && fromusc_ok && combo_ok && det_ok)) {
            ++fails;
            auto showdiff = [](const std::string& g, const std::string& w) {
                size_t k = 0;
                while (k < g.size() && k < w.size() && g[k] == w[k]) ++k;
                size_t a = k > 40 ? k - 40 : 0;
                std::fprintf(stderr, "   diff@%zu\n   exp: %s\n   got: %s\n", k,
                             w.substr(a, 90).c_str(), g.substr(a, 90).c_str());
            };
            if (!min_ok) showdiff(got_min, want_min);
            if (!pretty_ok) showdiff(got_pretty, want_pretty);
            if (!fromusc_ok) showdiff(got_fromusc, want_fromusc);
        }
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
