#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

#include "rg/util/size_limit.hpp"

namespace rg::util {

inline bool is_zip(const std::string& data) { return data.compare(0, 4, "PK\x03\x04") == 0; }

// The file called `name` in a zip archive, or nullopt when there is none. Only stored and deflated
// files are read (no zip64, no encryption); a broken archive throws std::invalid_argument, a file
// over max_size DecompressedSizeError.
inline std::optional<std::string> zip_read(const std::string& archive, const std::string& name,
                                           std::size_t max_size = DEFAULT_MAX_DECOMPRESSED_SIZE) {
    auto fail = [](const std::string& what) { throw std::invalid_argument("Invalid zip archive: " + what); };
    auto has = [&](std::size_t at, std::size_t length) { return at <= archive.size() && length <= archive.size() - at; };
    auto u16 = [&](std::size_t at) -> std::uint32_t {
        if (!has(at, 2)) fail("truncated");
        auto b = reinterpret_cast<const unsigned char*>(archive.data()) + at;
        return b[0] | (b[1] << 8);
    };
    auto u32 = [&](std::size_t at) -> std::uint32_t { return u16(at) | (u16(at + 2) << 16); };
    auto signature = [&](std::size_t at, const char* magic) { return has(at, 4) && archive.compare(at, 4, magic) == 0; };

    if (archive.size() < 22) fail("too short");
    std::size_t eocd = archive.size() - 22;
    std::size_t lowest = archive.size() > 22 + 0xffff ? archive.size() - 22 - 0xffff : 0;
    while (!signature(eocd, "PK\x05\x06")) {
        if (eocd == lowest) fail("no end of central directory");
        --eocd;
    }
    std::uint32_t entries = u16(eocd + 10);
    std::size_t at = u32(eocd + 16);
    for (std::uint32_t i = 0; i < entries; ++i) {
        if (!signature(at, "PK\x01\x02")) fail("bad central directory");
        std::uint32_t name_length = u16(at + 28);
        std::size_t next = at + 46 + name_length + u16(at + 30) + u16(at + 32);
        if (!has(at + 46, name_length)) fail("truncated");
        if (archive.compare(at + 46, name_length, name) != 0) {
            at = next;
            continue;
        }
        std::uint32_t flags = u16(at + 8), method = u16(at + 10), crc = u32(at + 16);
        std::uint32_t packed = u32(at + 20), size = u32(at + 24), local = u32(at + 42);
        if (flags & 1) fail(name + " is encrypted");
        if (packed == 0xffffffff || size == 0xffffffff || local == 0xffffffff) fail(name + " needs zip64");
        if (size > max_size)
            throw DecompressedSizeError(name + " decompresses to " + std::to_string(size) + " bytes, " +
                                        over_size_limit(max_size));
        if (!signature(local, "PK\x03\x04")) fail("bad local header for " + name);
        std::size_t start = local + 30 + u16(local + 26) + u16(local + 28);
        if (!has(start, packed)) fail("truncated");

        std::string out;
        if (method == 0) {
            if (packed != size) fail(name + " is corrupt");
            out = archive.substr(start, packed);
        } else if (method == 8) {
            z_stream zs{};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) throw std::runtime_error("inflateInit2 failed");
            zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(archive.data() + start));
            zs.avail_in = packed;
            std::vector<char> chunk(65536);
            int ret;
            do {
                zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
                zs.avail_out = static_cast<uInt>(chunk.size());
                ret = inflate(&zs, Z_NO_FLUSH);
                std::size_t produced = chunk.size() - zs.avail_out;
                if (produced > size - out.size()) {
                    inflateEnd(&zs);
                    fail(name + " is corrupt");
                }
                append_within(out, chunk.data(), produced, size);
            } while (ret == Z_OK);
            inflateEnd(&zs);
            if (ret != Z_STREAM_END) fail(name + " doesn't inflate");
        } else {
            fail(name + " uses compression method " + std::to_string(method));
        }
        if (out.size() != size ||
            crc32(0L, reinterpret_cast<const Bytef*>(out.data()), static_cast<uInt>(out.size())) != crc)
            fail(name + " is corrupt");
        return out;
    }
    return std::nullopt;
}

}  // namespace rg::util
