#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/holodori_sus.hpp"
#include "rg/converters/sus.hpp"
#include "rg/util/json.hpp"

using namespace rg;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string test_files = argv[1], refs = argv[2];
    util::Json meta = util::json_parse(read_file(refs + "/meta.json"));
    int fails = 0;
    for (auto& rec : meta.arr()) {
        std::string name = rec.at("name").as_str();
        long long combo = rec.at("combo").as_int();
        long long bezier = rec.at("bezier").as_int();
        std::string text = read_file(test_files + "/" + name);
        std::string want = read_file(refs + "/" + name + ".fromholo.sus");
        auto [sc, bez] = converters::holodori_sus::loads(text);
        long long got_combo = sc.combo_count();
        std::string got = converters::sus::dumps(sc);
        bool c = got_combo == combo, b = bez == bezier, e = got == want;
        std::printf("%-32s combo %s(%lld/%lld) bezier %s(%lld/%lld) load->sus %s\n", name.c_str(),
                    c ? "OK" : "BAD", got_combo, combo, b ? "OK" : "BAD", bez, bezier,
                    e ? "OK" : "BAD");
        if (!(c && b && e)) {
            ++fails;
            if (!e) {
                size_t k = 0;
                while (k < got.size() && k < want.size() && got[k] == want[k]) ++k;
                size_t a = k > 40 ? k - 40 : 0;
                std::fprintf(stderr, "  diff@%zu exp:%s | got:%s\n", k,
                             want.substr(a, 80).c_str(), got.substr(a, 80).c_str());
            }
        }
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
