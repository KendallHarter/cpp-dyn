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

// This is std::meta::annotations_of_with_type in C++26
consteval auto annotations_of_with_type(std::meta::info class_, std::meta::info type) -> std::vector<std::meta::info>
{
   return std::meta::annotations_of(class_, type);
}

} // namespace khct::detail

#endif // CPP_DYN_COMMON_HPP
