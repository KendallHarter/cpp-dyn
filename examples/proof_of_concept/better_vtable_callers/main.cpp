/**
 * Example of how to have non-forwarding vtable calls and instead
 * copy the function signatures.  Should result in better error messages.
 *
 * Excludes a _lot_ of potential things for the functions such as qualification,
 * noexcept, etc.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <meta>
#include <ranges>
#include <string_view>

namespace khct::detail {

template<std::size_t I>
inline constexpr std::array<char, 16> tuple_name = [] {
   std::array<char, 16> to_ret;
   std::ranges::fill(to_ret, '\0');
   to_ret[0] = 'm';
   std::to_chars(to_ret.data() + 1, to_ret.data() + to_ret.size(), I);
   return to_ret;
}();

template<typename... Ts>
struct tuple {
private:
   struct impl;
   consteval
   {
      []<std::size_t... Is>(std::index_sequence<Is...>) {
         std::meta::define_aggregate(
            ^^impl, {std::meta::data_member_spec(^^Ts, {.name = tuple_name<Is>.data(), .no_unique_address = true})...});
      }(std::index_sequence_for<Ts...>{});
   }

   static constexpr auto impl_members
      = std::define_static_array(std::meta::nonstatic_data_members_of(^^impl, std::meta::access_context::current()));

public:
   // public so that this is a structural type
   impl data_;

   template<std::size_t I>
      requires(I < sizeof...(Ts))
   constexpr auto get() const noexcept -> const auto&
   { return data_.[:impl_members[I]:]; }

   explicit constexpr tuple(const Ts&... vals) noexcept(noexcept(impl{vals...})) : data_{vals...} {}
};

// Don't really need the FuncName, but include it so it can be sorted
// to match the sorted function names
template<const char* FuncName, typename RetType, typename... Args>
struct base_caller_single_func {
   [[= FuncName]] auto operator()(Args...) -> RetType;
};

// clang-format off
template<std::meta::info Func>
struct base_caller_single
   : [: std::meta::substitute(
        ^^base_caller_single_func,
        std::views::concat(
           std::views::single(std::meta::reflect_constant(std::define_static_string(std::meta::identifier_of(Func)))),
           std::views::single(std::meta::return_type_of(Func)),
           std::meta::parameters_of(Func) | std::views::transform(std::meta::type_of))) :] {};
// clang-format on

template<std::meta::info... Funcs>
struct base_caller : base_caller_single<Funcs>... {
   using base_caller_single<Funcs>::operator()...;
};

consteval auto get_funcs_sorted_by_name(std::meta::info cls) -> std::vector<std::meta::info>
{
   auto funcs = std::meta::members_of(cls, std::meta::access_context::current())
              | std::views::filter(std::meta::is_function) | std::views::filter(std::not_fn(std::meta::is_constructor))
              | std::views::filter(std::not_fn(std::meta::is_operator_function))
              | std::views::filter(std::not_fn(std::meta::is_destructor)) | std::ranges::to<std::vector>();
   std::ranges::sort(funcs, {}, [](auto x) { return std::meta::identifier_of(x); });
   return funcs;
}

} // namespace khct::detail

namespace khct {

// clang-format off
template<typename T>
struct caller
   : [: std::meta::substitute(
        ^^detail::base_caller, detail::get_funcs_sorted_by_name(^^T) | std::views::transform([](auto x) {
                                  return std::meta::reflect_constant(x);
                               })) :] {};
// clang-format on

} // namespace khct

struct tester {
   void void_func() {}
   auto int_func(int x) -> int { return x; }
};

consteval
{
   const auto tester_funcs = khct::detail::get_funcs_sorted_by_name(^^tester);

   // Need to extract the base's base's base's etc.
   const auto base_caller = std::meta::bases_of(^^khct::caller<tester>, std::meta::access_context::current()).front();
   const auto bases_caller_single
      = std::meta::bases_of(std::meta::type_of(base_caller), std::meta::access_context::current())
      | std::views::transform(std::meta::type_of) | std::ranges::to<std::vector>();
   const auto base_caller_single_funcs
      = std::views::join(bases_caller_single | std::views::transform([](auto x) {
                            return std::meta::bases_of(x, std::meta::access_context::current());
                         }))
      | std::views::transform(std::meta::type_of) | std::ranges::to<std::vector>();
   auto caller_funcs
      = std::views::join(base_caller_single_funcs | std::views::transform([](auto x) {
                            return std::meta::members_of(x, std::meta::access_context::current());
                         }))
      | std::views::filter(std::meta::is_operator_function)
      | std::views::filter([](auto x) { return std::meta::operator_of(x) == std::meta::operators::op_parentheses; })
      | std::ranges::to<std::vector>();

   std::ranges::sort(caller_funcs, {}, [](auto x) {
      return std::string_view{std::meta::extract<const char*>(std::meta::annotations_of(x).front())};
   });

   // Same number of named functions
   assert(tester_funcs.size() == caller_funcs.size());

   // For each function...
   for (const auto [f1, f2] : std::views::zip(tester_funcs, caller_funcs)) {
      // Same return type
      assert(std::meta::return_type_of(f1) == std::meta::return_type_of(f2));

      // Same argument types
      const auto params1 = std::meta::parameters_of(f1);
      const auto params2 = std::meta::parameters_of(f2);
      assert(params1.size() == params2.size());
      for (const auto [a1, a2] : std::views::zip(params1, params2)) {
         assert(std::meta::type_of(a1) == std::meta::type_of(a2));
      }
   }
}

auto main() -> int {}
