#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/leveldata/next_sekai.hpp"
#include "rg/converters/sus.hpp"

using namespace rg;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    // argv[1]=sus input, argv[2]=output json, argv[3]=smooth(0/1), argv[4]=layer(0/1)
    std::string sus_text = read_file(argv[1]);
    core::Score sc = converters::sus::loads(sus_text);
    converters::leveldata::next_sekai::ExportOptions opts;
    opts.as_compressed = false;
    opts.smooth_guide_fade = argc > 3 && std::string(argv[3]) == "1";
    opts.use_guide_layer = argc > 4 && std::string(argv[4]) == "1";
    std::string out = converters::leveldata::next_sekai::export_bytes(sc, opts);
    std::ofstream(argv[2], std::ios::binary) << out;
    return 0;
}
