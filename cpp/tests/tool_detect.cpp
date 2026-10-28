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
    std::string line;
    for (auto& id : detect(data)) line += (line.empty() ? "" : "|") + id;
    std::printf("%s\n", line.empty() ? "None" : line.c_str());
    return 0;
}
