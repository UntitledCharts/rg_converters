#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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
    std::string test_files = argv[1];
    std::string refs = argv[2];
    std::string meta_text = read_file(refs + "/meta.json");
    util::Json meta = util::json_parse(meta_text);

    int fails = 0;
    for (auto& rec : meta.arr()) {
        std::string name, detect_fmt;
        bool detect_null = true;
        long long combo = -1;
        for (auto& kv : rec.items()) {
            if (kv.first == "name") name = kv.second.as_str();
            else if (kv.first == "combo") combo = kv.second.as_int();
            else if (kv.first == "detect" && kv.second.is_array())
                detect_null = false, detect_fmt = kv.second.arr()[0].as_str();
        }
        std::string input = read_file(test_files + "/" + name);
        std::string expected = read_file(refs + "/" + name + ".expected");

        core::Score score = converters::sus::loads(input);
        long long got_combo = score.combo_count();
        std::string got = converters::sus::dumps(score);

        auto det = converters::sus::detect(input);
        bool det_ok = detect_null ? !det.has_value() : (det.has_value() && *det == detect_fmt);

        bool combo_ok = (got_combo == combo);
        bool export_ok = (got == expected);
        std::printf("%-32s combo %s(%lld/%lld) detect %s export %s (%zu/%zu bytes)\n",
                    name.c_str(), combo_ok ? "OK" : "BAD", got_combo, combo,
                    det_ok ? "OK" : "BAD", export_ok ? "OK" : "BAD", got.size(),
                    expected.size());
        if (!combo_ok || !det_ok || !export_ok) {
            ++fails;
            if (!export_ok) {
                // find first differing line
                std::size_t k = 0;
                while (k < got.size() && k < expected.size() && got[k] == expected[k]) ++k;
                std::size_t ls = got.rfind('\n', k == 0 ? 0 : k - 1);
                std::fprintf(stderr, "  first diff at byte %zu (ctx near %zu)\n", k,
                             ls == std::string::npos ? 0 : ls);
                std::size_t a = k > 40 ? k - 40 : 0;
                std::fprintf(stderr, "  exp: ...%s\n",
                             expected.substr(a, 80).c_str());
                std::fprintf(stderr, "  got: ...%s\n", got.substr(a, 80).c_str());
            }
        }
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
