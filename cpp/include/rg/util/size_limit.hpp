#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace rg::util {

inline constexpr std::size_t DEFAULT_MAX_DECOMPRESSED_SIZE = 128 * 1024 * 1024;

// Data that decompresses past max_decompressed_size, or that an export would compress past it
// (Python: DecompressedSizeError, a ValueError).
struct DecompressedSizeError : std::runtime_error {
    explicit DecompressedSizeError(const std::string& message) : std::runtime_error(message) {}
};

inline std::string over_size_limit(std::size_t max_size) {
    return "over the " + std::to_string(max_size) + " byte limit (max_decompressed_size)";
}

// A max_decompressed_size option as a size; negative throws std::invalid_argument.
inline std::size_t size_limit(long long max_decompressed_size) {
    if (max_decompressed_size < 0) throw std::invalid_argument("max_decompressed_size can't be negative");
    return static_cast<std::size_t>(
        std::min<unsigned long long>(static_cast<unsigned long long>(max_decompressed_size), SIZE_MAX));
}

// Appends what the caller made sure fits in max_size, never letting out's capacity pass it
// (std::string's own growth can double past it).
inline void append_within(std::string& out, const char* data, std::size_t size, std::size_t max_size) {
    if (size > out.capacity() - out.size()) {
        std::string grown;
        grown.reserve(std::min(max_size, std::max(out.size() + size, out.size() * 2)));
        grown.append(out);
        out.swap(grown);
    }
    out.append(data, size);
}

}  // namespace rg::util
