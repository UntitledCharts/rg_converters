#include "rg/util/base36.hpp"
#include "rg/util/pygzip.hpp"

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

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    int fails = 0;

    // base36
    {
        std::ifstream in(dir + "/b36_refs.txt");
        std::string line;
        long long n;
        std::string exp;
        long total = 0, bad = 0;
        while (in >> n >> exp) {
            ++total;
            std::string got = base36_dumps(n);
            if (got != exp) {
                ++bad;
                if (bad <= 10)
                    std::fprintf(stderr, "B36 MM %lld exp=%s got=%s\n", n, exp.c_str(),
                                 got.c_str());
            }
        }
        std::printf("base36 total=%ld bad=%ld\n", total, bad);
        fails += (bad != 0);
    }

    // gzip byte parity
    const char* names[] = {"small", "empty", "bin", "rand", "sus"};
    for (const char* nm : names) {
        std::string in = read_file(dir + "/gz_" + nm + ".in");
        std::string want = read_file(dir + "/gz_" + nm + ".gz");
        std::string got = gzip_compress(in, 0, 9);
        bool ok = (got == want);
        std::printf("gzip %-6s in=%zu want=%zu got=%zu %s\n", nm, in.size(),
                    want.size(), got.size(), ok ? "OK" : "MISMATCH");
        if (!ok) {
            ++fails;
            size_t k = 0;
            while (k < got.size() && k < want.size() && got[k] == want[k]) ++k;
            std::fprintf(stderr, "  first diff at byte %zu\n", k);
        }
        // round-trip
        std::string back = gzip_decompress(want);
        if (back != in) {
            std::fprintf(stderr, "  decompress MISMATCH for %s\n", nm);
            ++fails;
        }
    }
    return fails ? 1 : 0;
}
