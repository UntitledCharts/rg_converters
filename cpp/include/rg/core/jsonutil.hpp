#pragma once

#include "rg/core/model.hpp"
#include "rg/util/json.hpp"
#include "rg/util/pyops.hpp"

namespace rg::core {

// Convert a JSON number to a Num, preserving int vs float.
inline Num json_to_num(const util::Json& j) {
    if (j.type() == util::Json::Type::Int) return Num::I(j.as_int());
    if (j.type() == util::Json::Type::Bool) return Num::I(j.as_bool() ? 1 : 0);
    return Num::F(j.as_float());
}

// Emit a Num as JSON, preserving int vs float.
inline util::Json num_to_json(const Num& n) {
    if (n.is_int) return util::Json::integer(static_cast<long long>(n.v));
    return util::Json(n.v);
}

// round(x, 6): round() on a Python int returns the int unchanged.
inline Num round6(const Num& n) {
    if (n.is_int) return n;
    return Num::F(util::py_round(n.v, 6));
}

// obj.get(key, default_num) where default is a float.
inline Num get_num_or(const util::Json& obj, const std::string& key, double dflt) {
    const util::Json* p = obj.find(key);
    if (p) return json_to_num(*p);
    return Num::F(dflt);
}

}  // namespace rg::core
