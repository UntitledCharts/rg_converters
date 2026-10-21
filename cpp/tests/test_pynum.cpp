#include "rg/util/pynum.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace rg::util;

static float f32_from_bits(std::uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
static double f64_from_bits(std::uint64_t b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "num_refs.txt";
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    std::string line;
    long long total = 0, bad = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        char kind = line[0];
        std::size_t sp1 = line.find(' ', 2);
        std::string hexs = line.substr(2, sp1 - 2);
        std::string expected = line.substr(sp1 + 1);
        // strip trailing \r if present
        if (!expected.empty() && expected.back() == '\r') expected.pop_back();
        std::string got;
        if (kind == 'F') {
            std::uint32_t b = static_cast<std::uint32_t>(std::stoul(hexs, nullptr, 16));
            got = np_float32_str(f32_from_bits(b));
        } else {
            std::uint64_t b = std::stoull(hexs, nullptr, 16);
            got = py_float_repr(f64_from_bits(b));
        }
        ++total;
        if (got != expected) {
            ++bad;
            if (bad <= 30) {
                std::fprintf(stderr, "MISMATCH %c %s expected=%s got=%s\n", kind,
                             hexs.c_str(), expected.c_str(), got.c_str());
            }
        }
    }
    std::printf("total=%lld bad=%lld\n", total, bad);
    return bad == 0 ? 0 : 1;
}
