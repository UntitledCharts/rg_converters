#include <optional>
#include <string>

#include "rg/converters/mmw_io.hpp"
#include "rg/converters/mmws.hpp"
#include "rg/util/pystr.hpp"

namespace rg::converters::mmws {

std::optional<std::string> detect(const std::string& data) {
    // Signature enum order: MMWS, CCMMWS, UCMMWS -- first startswith match wins.
    if (rg::util::startswith(data, Signature::MikuMikuWorld)) return std::string("base");
    if (rg::util::startswith(data, Signature::MikuMikuWorld4ChartCyanvas))
        return std::string("chcy");
    if (rg::util::startswith(data, Signature::MikuMikuWorld4UntitledChart))
        return std::string("unch");
    return std::nullopt;
}

}  // namespace rg::converters::mmws
