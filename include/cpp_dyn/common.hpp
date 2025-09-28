#ifndef CPP_DYN_COMMON_HPP
#define CPP_DYN_COMMON_HPP

#include <initializer_list>
#include <meta>

namespace khct::detail {

template<typename... T>
struct overload_set : T... {
   using T::operator()...;
};

// The current implementation doesn't have this, so provide it here
consteval auto define_static_object(const auto& obj)
{
   return &std::define_static_array(std::initializer_list{obj})[0];
}

} // namespace khct::detail

#endif // CPP_DYN_COMMON_HPP
