#ifndef CPP_DYN_TUPLE_HPP
#define CPP_DYN_TUPLE_HPP

#include <cstdint>
#include <tuple>

namespace khct::detail {

template<typename T1, typename T2>
struct pair {
   [[no_unique_address]] T1 first;
   [[no_unique_address]] T2 second;
   friend constexpr auto operator<=>(const pair&, const pair&) noexcept = default;
};

template<typename T>
struct type_holder {
   using type = T;
};

template<typename T, std::size_t I>
struct tuple_base {
   // TODO: Move only types?
   // This is needed for some reason???
   constexpr tuple_base(const T& val) : value{val} {}

   [[no_unique_address]] T value;
   friend constexpr bool operator==(const tuple_base&, const tuple_base&) noexcept = default;
   friend constexpr auto operator<=>(const tuple_base&, const tuple_base&) noexcept = default;
};

template<pair... TypesAndIndexes>
struct tuple_impl : tuple_base<typename decltype(TypesAndIndexes.first)::type, TypesAndIndexes.second>... {
   template<std::size_t I>
   constexpr const auto& get() const& noexcept
   {
      constexpr auto use_pair = TypesAndIndexes...[I];
      return static_cast<const tuple_base<typename decltype(use_pair.first)::type, use_pair.second>*>(this)->value;
   }

   template<std::size_t I>
   constexpr auto& get() & noexcept
   {
      constexpr auto use_pair = TypesAndIndexes...[I];
      return static_cast<tuple_base<typename decltype(use_pair.first)::type, use_pair.second>*>(this)->value;
   }

   template<std::size_t I>
   constexpr auto get() && noexcept
   {
      return get<I>();
   }

   friend constexpr bool operator==(const tuple_impl&, const tuple_impl&) noexcept = default;
   friend constexpr auto operator<=>(const tuple_impl&, const tuple_impl&) noexcept = default;
};

template<typename... Ts, std::size_t... Is>
consteval auto make_tuple(std::index_sequence<Is...>) noexcept
{
   return static_cast<tuple_impl<pair{type_holder<Ts>{}, Is}...>*>(nullptr);
}

template<typename... Ts>
struct tuple : std::remove_reference_t<decltype(*make_tuple<Ts...>(std::index_sequence_for<Ts...>{}))> {
   using base = std::remove_reference_t<decltype(*make_tuple<Ts...>(std::index_sequence_for<Ts...>{}))>;

   // TODO: Move only types?
   constexpr tuple(const Ts&... values) noexcept : base{values...} {}

   friend constexpr bool operator==(const tuple&, const tuple&) noexcept = default;
   friend constexpr auto operator<=>(const tuple&, const tuple&) noexcept = default;

   inline static constexpr auto size = sizeof...(Ts);
};

} // namespace khct::detail

#endif // CPP_DYN_TUPLE_HPP
