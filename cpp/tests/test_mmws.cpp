#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/mmws.hpp"
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
    int fails = 0, exports = 0, loads = 0;
    for (auto& rec : meta.arr()) {
        std::string name = rec.at("name").as_str();
        std::string fmt = rec.at("fmt").as_str();
        std::string det;
        if (!rec.at("detect").is_null()) det = rec.at("detect").as_str();
        bool load_ok = rec.at("load_ok").as_bool();

        std::string sus_text = read_file(test_files + "/" + name);
        std::string want_bin = read_file(refs + "/" + name + fmt + ".bin");

        core::Score sc = converters::sus::loads(sus_text);
        std::string got_bin = converters::mmws::export_bytes(sc, fmt).data;
        bool exp_ok = got_bin == want_bin;
        auto d = converters::mmws::detect(want_bin);
        bool det_ok = d.has_value() && *d == det;
        ++exports;

        std::string load_status = "n/a";
        bool load_pass = true;
        if (load_ok) {
            std::string want_rt = read_file(refs + "/" + name + fmt + ".rt.sus");
            long long want_combo = rec.at("combo").as_int();
            core::Score back = converters::mmws::load_bytes(want_bin);
            long long got_combo = back.combo_count();
            std::string got_rt = converters::sus::dumps(back).data;
            load_pass = (got_combo == want_combo) && (got_rt == want_rt);
            load_status = load_pass ? "OK" : "BAD";
            ++loads;
        }

        bool ok = exp_ok && det_ok && load_pass;
        std::printf("%-28s %-10s export %s detect %s load %s\n", name.c_str(), fmt.c_str(),
                    exp_ok ? "OK" : "BAD", det_ok ? "OK" : "BAD", load_status.c_str());
        if (!ok) {
            ++fails;
            if (!exp_ok)
                std::fprintf(stderr, "   bin sizes got=%zu want=%zu\n", got_bin.size(),
                             want_bin.size());
        }
    }
    std::printf("\n%s (%d failures; %d exports, %d loads checked)\n",
                fails ? "FAILED" : "ALL PASS", fails, exports, loads);
    return fails ? 1 : 0;
}
