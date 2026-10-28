#pragma once

#include <string>
#include <vector>

namespace rg {

// Every format the data could be, most likely first; empty when it is none of them. Format ids:
// sus, yume_sus, holodori_sus, usc, mmws, ccmmws, unchmmws, pjsk, bdon, next_sekai,
// chart_cyanvas, untitled_sekai. More than one means the content can't tell them apart.
std::vector<std::string> detect(const std::string& data);

}  // namespace rg
