#include "rg/util/json.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace rg::util;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const std::string& field(const Json& obj, const std::string& k) {
    for (auto& kv : obj.items())
        if (kv.first == k) return kv.second.as_str();
    static std::string empty;
    return empty;
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    std::string text = read_file(dir + "/json_refs.json");
    Json refs = json_parse(text);
    int fails = 0, idx = 0;
    for (auto& rec : refs.arr()) {
        const std::string& compact = field(rec, "compact");
        const std::string& indented = field(rec, "indented");
        Json parsed = json_parse(compact);
        std::string got_compact = parsed.dump(-1, true);
        std::string got_indented = parsed.dump(4, false);
        if (got_compact != compact) {
            ++fails;
            std::fprintf(stderr, "[%d] COMPACT MISMATCH\n exp=%s\n got=%s\n", idx,
                         compact.c_str(), got_compact.c_str());
        }
        if (got_indented != indented) {
            ++fails;
            std::fprintf(stderr, "[%d] INDENTED MISMATCH\n exp=%s\n got=%s\n", idx,
                         indented.c_str(), got_indented.c_str());
        }
        ++idx;
    }
    std::printf("json records=%d fails=%d\n", idx, fails);
    return fails ? 1 : 0;
}
