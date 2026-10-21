#pragma once

#include <pybind11/pybind11.h>

#include "rg/core/model.hpp"

namespace pybind11 {
namespace detail {

// Num <-> Python int/float, preserving the int-vs-float distinction.
template <>
struct type_caster<rg::core::Num> {
public:
    PYBIND11_TYPE_CASTER(rg::core::Num, const_name("int | float"));

    bool load(handle src, bool) {
        if (src.is_none()) return false;
        // bool is an int subclass in Python; treat as int
        if (PyBool_Check(src.ptr())) {
            value = rg::core::Num::I(src.cast<bool>() ? 1 : 0);
            return true;
        }
        if (PyLong_Check(src.ptr())) {
            value = rg::core::Num::I(src.cast<long long>());
            return true;
        }
        if (PyFloat_Check(src.ptr())) {
            value = rg::core::Num::F(src.cast<double>());
            return true;
        }
        return false;
    }

    static handle cast(const rg::core::Num& num, return_value_policy, handle) {
        if (num.is_int)
            return PyLong_FromLongLong(static_cast<long long>(num.v));
        return PyFloat_FromDouble(num.v);
    }
};

}  // namespace detail
}  // namespace pybind11
