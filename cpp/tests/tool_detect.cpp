#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "rg/detect.hpp"

using namespace rg;

static std::string read_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    std::string data = read_file(argv[1]);
    auto r = detect(data);
    if (!r.has_value())
        std::printf("None\n");
    else
        std::printf("%s|%s\n", r->first.c_str(), r->second.c_str());
    return 0;
}
