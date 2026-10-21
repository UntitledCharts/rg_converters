#include "rg/util/pynum.hpp"
#include "rg/util/pyops.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace rg::util;

static double f64_from_bits(std::uint64_t b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "round_refs.txt";
    std::ifstream in(path);
    std::string hexs, roundi, round6;
    long long total = 0, bad = 0;
    while (in >> hexs >> roundi >> round6) {
        ++total;
        double x = f64_from_bits(std::stoull(hexs, nullptr, 16));
        std::string gi = std::to_string(py_round_int(x));
        std::string g6 = py_float_repr(py_round(x, 6));
        if (gi != roundi || g6 != round6) {
            ++bad;
            if (bad <= 30)
                std::fprintf(stderr, "MM %s x=%.17g round:exp=%s got=%s  round6:exp=%s got=%s\n",
                             hexs.c_str(), x, roundi.c_str(), gi.c_str(), round6.c_str(),
                             g6.c_str());
        }
    }
    std::printf("total=%lld bad=%lld\n", total, bad);
    return bad ? 1 : 0;
}
