#pragma once

#include <pybind11/critical_section.h>
#include <pybind11/pybind11.h>

#include <type_traits>
#include <utility>

namespace rg::bindings {

namespace py = pybind11;

// Free-threaded builds have no GIL serializing calls, so every bound access to a model object
// holds that object's Python critical section; another thread sharing the same Score/node can't
// mutate it mid-call. Compiles to nothing on GIL builds.
class object_lock {
#ifdef Py_GIL_DISABLED
    py::object self_;
    py::scoped_critical_section cs_;

public:
    template <class T>
    explicit object_lock(const T& obj)
        : self_(py::cast(&obj, py::return_value_policy::reference)), cs_(self_) {}
#else
public:
    template <class T>
    explicit object_lock(const T&) {}
#endif
    object_lock(const object_lock&) = delete;
    object_lock& operator=(const object_lock&) = delete;
};

template <class C, class R, class... A>
auto locked(R (C::*f)(A...)) {
    return [f](C& self, A... args) -> R {
        object_lock lock(self);
        return (self.*f)(std::forward<A>(args)...);
    };
}

template <class C, class R, class... A>
auto locked(R (C::*f)(A...) const) {
    return [f](const C& self, A... args) -> R {
        object_lock lock(self);
        return (self.*f)(std::forward<A>(args)...);
    };
}

// class_ with def_field(): a read/write attribute (like def_readwrite) that copies in/out under
// the object's lock. def() is re-declared only so chains keep returning locked_class.
template <class T, class... Options>
class locked_class : public py::class_<T, Options...> {
    using base = py::class_<T, Options...>;

public:
    using base::base;

    template <class... Args>
    locked_class& def(Args&&... args) {
        base::def(std::forward<Args>(args)...);
        return *this;
    }

    template <class C, class D>
    locked_class& def_field(const char* name, D C::*pm) {
        static_assert(std::is_base_of_v<C, T>);
        py::cpp_function fget(
            [pm](const T& self) -> D {
                object_lock lock(self);
                return self.*pm;
            },
            py::is_method(*this));
        py::cpp_function fset(
            [pm](T& self, const D& value) {
                object_lock lock(self);
                self.*pm = value;
            },
            py::is_method(*this));
        base::def_property(name, fget, fset);
        return *this;
    }
};

}  // namespace rg::bindings
