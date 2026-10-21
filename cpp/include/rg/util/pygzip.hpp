#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

namespace rg::util {

using Bytes = std::string;

// Reproduces Python's gzip output (gzip.GzipFile(mode="wb", mtime=...) / gzip.compress).
// Header: magic 1f8b, CM=8 (deflate), FLG=0, MTIME (LE), XFL, OS=0xff (unknown).
// XFL is 2 for level 9, 4 for level 1, else 0 -- matching CPython's gzip module.
inline Bytes gzip_compress(const Bytes& data, std::uint32_t mtime = 0, int level = 9) {
    Bytes out;
    std::uint8_t xfl = 0;
    if (level == 9)
        xfl = 2;
    else if (level == 1)
        xfl = 4;
    out.push_back(static_cast<char>(0x1f));
    out.push_back(static_cast<char>(0x8b));
    out.push_back(static_cast<char>(0x08));
    out.push_back(static_cast<char>(0x00));
    out.push_back(static_cast<char>(mtime & 0xff));
    out.push_back(static_cast<char>((mtime >> 8) & 0xff));
    out.push_back(static_cast<char>((mtime >> 16) & 0xff));
    out.push_back(static_cast<char>((mtime >> 24) & 0xff));
    out.push_back(static_cast<char>(xfl));
    out.push_back(static_cast<char>(0xff));

    z_stream zs{};
    if (deflateInit2(&zs, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("deflateInit2 failed");
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    std::vector<char> chunk(65536);
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
        zs.avail_out = static_cast<uInt>(chunk.size());
        ret = deflate(&zs, Z_FINISH);
        out.append(chunk.data(), chunk.size() - zs.avail_out);
    } while (ret != Z_STREAM_END);
    deflateEnd(&zs);

    std::uint32_t crc = static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.data()),
              static_cast<uInt>(data.size())));
    std::uint32_t isize = static_cast<std::uint32_t>(data.size() & 0xffffffff);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((crc >> (8 * i)) & 0xff));
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((isize >> (8 * i)) & 0xff));
    return out;
}

// Decompress a gzip (or zlib) stream, matching gzip.decompress / GzipFile read.
inline Bytes gzip_decompress(const Bytes& data) {
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    Bytes out;
    std::vector<char> chunk(65536);
    int ret;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
        zs.avail_out = static_cast<uInt>(chunk.size());
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&zs);
            throw std::runtime_error("inflate failed");
        }
        out.append(chunk.data(), chunk.size() - zs.avail_out);
        if (ret == Z_BUF_ERROR && zs.avail_in == 0) break;
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

}  // namespace rg::util
