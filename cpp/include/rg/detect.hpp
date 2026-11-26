#pragma once

#include <string>
#include <vector>

#include "rg/util/size_limit.hpp"

namespace rg {

// Every format the data could be, most likely first; empty when it is none of them. Format ids:
// sus, yume_sus, holodori_sus, usc, mmws, ccmmws, unchmmws, pjsk, bdon, next_sekai,
// chart_cyanvas, untitled_sekai. More than one means the content can't tell them apart. Data that
// decompresses past max_decompressed_size throws util::DecompressedSizeError.
std::vector<std::string> detect(const std::string& data,
                                std::size_t max_decompressed_size = util::DEFAULT_MAX_DECOMPRESSED_SIZE);

}  // namespace rg
