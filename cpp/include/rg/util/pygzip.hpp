#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "rg/util/size_limit.hpp"

namespace rg::util {

using Bytes = std::string;

// Reproduces Python's gzip output (gzip.GzipFile(mode="wb", mtime=...) / gzip.compress), fed piece
// by piece so the data is never held whole. Header: magic 1f8b, CM=8 (deflate), FLG=0, MTIME (LE),
// XFL, OS=0xff (unknown). XFL is 2 for level 9, 4 for level 1, else 0 -- matching CPython's gzip
// module. Data longer than max_size is only counted, and finish() throws DecompressedSizeError.
class GzipWriter {
public:
    explicit GzipWriter(std::uint32_t mtime = 0, int level = 9,
                        std::size_t max_size = DEFAULT_MAX_DECOMPRESSED_SIZE)
        : max_size_(max_size) {
        std::uint8_t xfl = 0;
        if (level == 9)
            xfl = 2;
        else if (level == 1)
            xfl = 4;
        out_.push_back(static_cast<char>(0x1f));
        out_.push_back(static_cast<char>(0x8b));
        out_.push_back(static_cast<char>(0x08));
        out_.push_back(static_cast<char>(0x00));
        out_.push_back(static_cast<char>(mtime & 0xff));
        out_.push_back(static_cast<char>((mtime >> 8) & 0xff));
        out_.push_back(static_cast<char>((mtime >> 16) & 0xff));
        out_.push_back(static_cast<char>((mtime >> 24) & 0xff));
        out_.push_back(static_cast<char>(xfl));
        out_.push_back(static_cast<char>(0xff));
        if (deflateInit2(&zs_, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            throw std::runtime_error("deflateInit2 failed");
        open_ = true;
    }
    ~GzipWriter() { close(); }
    GzipWriter(const GzipWriter&) = delete;
    GzipWriter& operator=(const GzipWriter&) = delete;

    void write(std::string_view data) {
        size_ += data.size();
        if (size_ > max_size_) {
            close();
            Bytes().swap(out_);
            return;
        }
        while (!data.empty()) {
            std::size_t piece = std::min<std::size_t>(data.size(), 1u << 30);
            crc_ = crc32(crc_, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(piece));
            compress(data.data(), piece, Z_NO_FLUSH);
            data.remove_prefix(piece);
        }
    }

    Bytes finish() {
        if (size_ > max_size_)
            throw DecompressedSizeError("The data to compress is " + std::to_string(size_) + " bytes, " +
                                        over_size_limit(max_size_));
        compress(nullptr, 0, Z_FINISH);
        close();
        std::uint32_t crc = static_cast<std::uint32_t>(crc_);
        std::uint32_t isize = static_cast<std::uint32_t>(size_ & 0xffffffff);
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<char>((crc >> (8 * i)) & 0xff));
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<char>((isize >> (8 * i)) & 0xff));
        return std::move(out_);
    }

private:
    z_stream zs_{};
    bool open_ = false;
    std::size_t max_size_;
    std::size_t size_ = 0;
    uLong crc_ = crc32(0L, Z_NULL, 0);
    Bytes out_;
    std::vector<char> chunk_ = std::vector<char>(65536);

    void compress(const char* data, std::size_t size, int flush) {
        zs_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        zs_.avail_in = static_cast<uInt>(size);
        int ret;
        do {
            zs_.next_out = reinterpret_cast<Bytef*>(chunk_.data());
            zs_.avail_out = static_cast<uInt>(chunk_.size());
            ret = deflate(&zs_, flush);
            out_.append(chunk_.data(), chunk_.size() - zs_.avail_out);
        } while (flush == Z_FINISH ? ret != Z_STREAM_END : zs_.avail_out == 0);
    }

    void close() {
        if (open_) deflateEnd(&zs_);
        open_ = false;
    }
};

// Python's gzip.compress(data, compresslevel=level, mtime=mtime); data longer than max_size throws
// DecompressedSizeError.
inline Bytes gzip_compress(const Bytes& data, std::uint32_t mtime = 0, int level = 9,
                           std::size_t max_size = DEFAULT_MAX_DECOMPRESSED_SIZE) {
    GzipWriter gzip(mtime, level, max_size);
    gzip.write(data);
    return gzip.finish();
}

// Decompress a gzip (or zlib) stream, matching gzip.decompress / GzipFile read. Output past
// max_size throws DecompressedSizeError before it is kept.
inline Bytes gzip_decompress(const Bytes& data, std::size_t max_size = DEFAULT_MAX_DECOMPRESSED_SIZE) {
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    Bytes out;
    // a gzip trailer's ISIZE, when deflate could reach it from this much data, as the size to expect
    if (data.size() >= 18 && static_cast<unsigned char>(data[0]) == 0x1f &&
        static_cast<unsigned char>(data[1]) == 0x8b) {
        auto byte = [&](std::size_t at) { return static_cast<std::size_t>(static_cast<unsigned char>(data[at])); };
        std::size_t at = data.size() - 4;
        std::size_t isize = byte(at) | byte(at + 1) << 8 | byte(at + 2) << 16 | byte(at + 3) << 24;
        if (isize <= max_size && isize / 1032 <= data.size()) out.reserve(isize);
    }
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
        std::size_t produced = chunk.size() - zs.avail_out;
        if (produced > max_size - out.size()) {
            inflateEnd(&zs);
            throw DecompressedSizeError("The data decompresses to " + over_size_limit(max_size));
        }
        append_within(out, chunk.data(), produced, max_size);
        if (ret == Z_BUF_ERROR && zs.avail_in == 0) break;
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

}  // namespace rg::util
