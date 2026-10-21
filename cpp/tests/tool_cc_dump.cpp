#include <fstream>
#include <sstream>
#include <string>

#include "rg/converters/leveldata/chart_cyanvas.hpp"
#include "rg/converters/pjsk.hpp"

using namespace rg;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    // argv[1]=pjsk input, argv[2]=output, argv[3]=compressed(0/1)
    std::string pjsk_bytes = read_file(argv[1]);
    core::Score sc = converters::pjsk::load_bytes(pjsk_bytes);
    bool compressed = argc > 3 && std::string(argv[3]) == "1";
    std::string out = converters::leveldata::chart_cyanvas::export_bytes(sc, compressed);
    std::ofstream(argv[2], std::ios::binary) << out;
    return 0;
}
