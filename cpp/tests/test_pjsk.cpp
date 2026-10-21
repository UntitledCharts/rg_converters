#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/pjsk.hpp"
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
    util::Json meta = util::json_parse(read_file(refs + "/meta.json"));
    int fails = 0;
    for (auto& rec : meta.arr()) {
        std::string kind = rec.at("kind").as_str();
        std::string name = rec.at("name").as_str();
        bool want_detect = rec.at("detect").as_bool();
        long long want_combo = rec.at("combo").as_int();

        if (kind == "sus") {
            std::string sus_text = read_file(test_files + "/" + name);
            std::string want_b64 = read_file(refs + "/" + name + ".pjsk.b64");
            std::string want_fromsus = read_file(refs + "/" + name + ".frompjsk.sus");

            core::Score sc = converters::sus::loads(sus_text);
            std::string got_b64 = converters::pjsk::export_bytes(sc, 0);
            core::Score back = converters::pjsk::load_bytes(want_b64);
            long long got_combo = back.combo_count();
            std::string got_fromsus = converters::sus::dumps(back);
            bool got_detect = converters::pjsk::detect(want_b64);

            bool exp_ok = got_b64 == want_b64;
            bool round_ok = got_fromsus == want_fromsus;
            bool combo_ok = got_combo == want_combo;
            bool det_ok = got_detect == want_detect;
            std::printf("%-36s export %s load->sus %s combo %s(%lld/%lld) detect %s\n",
                        name.c_str(), exp_ok ? "OK" : "BAD", round_ok ? "OK" : "BAD",
                        combo_ok ? "OK" : "BAD", got_combo, want_combo,
                        det_ok ? "OK" : "BAD");
            if (!(exp_ok && round_ok && combo_ok && det_ok)) {
                ++fails;
                if (!exp_ok)
                    std::fprintf(stderr, "   b64 sizes got=%zu want=%zu\n", got_b64.size(),
                                 want_b64.size());
                if (!round_ok) {
                    size_t k = 0;
                    while (k < got_fromsus.size() && k < want_fromsus.size() &&
                           got_fromsus[k] == want_fromsus[k]) ++k;
                    std::fprintf(stderr, "   sus diff@%zu\n", k);
                }
            }
        } else {
            std::string content = read_file(test_files + "/" + name);
            std::string want_fromjson = read_file(refs + "/" + name + ".frompjskjson.sus");
            core::Score sc = converters::pjsk::load_bytes(content);
            long long got_combo = sc.combo_count();
            std::string got = converters::sus::dumps(sc);
            bool got_detect = converters::pjsk::detect(content);
            bool round_ok = got == want_fromjson;
            bool combo_ok = got_combo == want_combo;
            bool det_ok = got_detect == want_detect;
            std::printf("%-36s (json) load->sus %s combo %s(%lld/%lld) detect %s\n",
                        name.c_str(), round_ok ? "OK" : "BAD", combo_ok ? "OK" : "BAD",
                        got_combo, want_combo, det_ok ? "OK" : "BAD");
            if (!(round_ok && combo_ok && det_ok)) {
                ++fails;
                if (!round_ok) {
                    size_t k = 0;
                    while (k < got.size() && k < want_fromjson.size() && got[k] == want_fromjson[k]) ++k;
                    size_t a = k > 40 ? k - 40 : 0;
                    std::fprintf(stderr, "   diff@%zu exp:%s | got:%s\n", k,
                                 want_fromjson.substr(a, 80).c_str(), got.substr(a, 80).c_str());
                }
            }
        }
    }
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
