#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/typing.h>

#include <utility>

namespace rg::bindings {

// A T converted to and from Python exactly as T is, but named in signatures (and so in the
// generated .pyi stubs) as Hint is.
template <class T, class Hint>
struct hinted {
    T value;
};

// A hint spelled out: Text is a char array, e.g. "typing.Any".
template <const auto& Text>
struct hint {};

}  // namespace rg::bindings

namespace pybind11::detail {

template <class T, class Hint>
struct type_caster<rg::bindings::hinted<T, Hint>> {
    using hinted = rg::bindings::hinted<T, Hint>;
    PYBIND11_TYPE_CASTER(hinted, make_caster<Hint>::name);

    bool load(handle src, bool convert) {
        make_caster<T> inner;
        if (!inner.load(src, convert)) return false;
        value.value = std::move(static_cast<T&>(inner));
        return true;
    }

    static handle cast(const hinted& src, return_value_policy policy, handle parent) {
        return make_caster<T>::cast(src.value, policy, parent);
    }

    static handle cast(hinted&& src, return_value_policy policy, handle parent) {
        return make_caster<T>::cast(std::move(src.value), policy, parent);
    }
};

template <const auto& Text>
struct type_caster<rg::bindings::hint<Text>> {
    static constexpr auto name = const_name(Text);
};

}  // namespace pybind11::detail
