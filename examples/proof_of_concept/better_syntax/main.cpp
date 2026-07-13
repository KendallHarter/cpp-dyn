/**
 * This file presents a possible way to have more natural syntax for the dyn-like traits.
 *
 * It, however, involves raw memory inspection; meaning that it cannot be used at all at
 * compile-time and *may* inhibit optimizations/inlining/etc.  It also may be undefined behavior,
 * but, to the best of my knowledge, it should not be.
 *
 * This copies a lot of code from the current version of the library here so that it may stand
 * alone in case of changes to the details of the library.  This is obviously normally not great,
 * but this will not be updated and not be as robust as the main library.
 *
 * Marking some deliberate limitations/choices to simplify implementation:
 *    - Overload sets are not supported
 *    - Static functions are not supported
 *    - There are no annotations
 *    - Most fields are public even if they shouldn't be
 *    - const interfaces are not supported
 *    - noexcept is not supported
 *    - const qualification is not supported
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <functional>
#include <meta>
#include <new>
#include <optional>
#include <ranges>

namespace khct::detail {

template<typename... Ts>
struct overload_set : Ts... {
   using Ts::operator()...;
};

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

template<typename Tuple1, typename Tuple2>
struct append_tuple_types;

template<typename... Ts1, typename... Ts2>
struct append_tuple_types<tuple<Ts1...>, tuple<Ts2...>> {
   using type = tuple<Ts1..., Ts2...>;
};

template<typename Tuple1, typename Tuple2>
using append_tuple_types_t = append_tuple_types<Tuple1, Tuple2>::type;

consteval auto get_named_member(std::meta::info cls, const char* name) -> std::optional<std::meta::info>
{
   auto m = std::meta::members_of(cls, std::meta::access_context::current())
          | std::views::filter(std::meta::has_identifier)
          | std::views::filter([&](auto x) { return std::meta::identifier_of(x) == name; });
   if (m.empty()) {
      return {};
   }
   return m.front();
}

consteval auto get_funcs_sorted_by_name(std::meta::info cls) -> std::vector<std::meta::info>
{
   auto funcs = std::meta::members_of(cls, std::meta::access_context::current())
              | std::views::filter(std::meta::is_function) | std::views::filter(std::not_fn(std::meta::is_constructor))
              | std::views::filter(std::not_fn(std::meta::is_operator_function))
              | std::views::filter(std::not_fn(std::meta::is_destructor)) | std::ranges::to<std::vector>();
   std::ranges::sort(funcs, {}, [](auto x) { return std::meta::identifier_of(x); });
   return funcs;
}

template<typename BaseClass, typename OwningClass, typename Interface, const char* FuncName>
struct caller {
   template<typename Self, typename... Ts>
   constexpr auto operator()(this Self&& self, Ts&&... vals) -> decltype(auto)
   {
      // These are in the functions as having them in the class means they'll get instantiated
      // too early

      static constexpr auto interface_funcs = std::define_static_array(get_funcs_sorted_by_name(^^Interface));

      // Index into the vtable to call
      static constexpr auto vtable_index = std::ranges::distance(
         interface_funcs.begin(),
         std::ranges::find_if(interface_funcs, [](auto x) { return std::meta::identifier_of(x) == FuncName; }));

      // The offset of this member into the object
      static constexpr auto this_offset = std::meta::offset_of(get_named_member(^^BaseClass, FuncName).value());

      // Now call the function
      std::byte* const mem_ptr = reinterpret_cast<std::byte*>(&self);
      std::byte* const obj_ptr = mem_ptr - this_offset.bytes;
      // I'm fairly certain that std::launder is needed here for standards compliance...
      // but having it produces less optimal code?
      OwningClass* const obj = std::launder(reinterpret_cast<OwningClass*>(obj_ptr));

      return obj->vtable_.template get<vtable_index>()(obj->data_, std::forward<Ts>(vals)...);
   }
};

template<std::meta::info Interface, std::meta::info DynClass>
struct make_base_dyn_class {
   struct interface_base;

   consteval
   {
      const auto funcs = get_funcs_sorted_by_name(Interface);

      std::vector<std::meta::info> infos{std::meta::data_member_spec(^^void*, {.name = "data_"})};
      for (const auto& f : funcs) {
         infos.push_back(
            std::meta::data_member_spec(
               std::meta::substitute(
                  ^^caller,
                  {^^interface_base,
                   DynClass,
                   Interface,
                   std::meta::reflect_constant_string(std::meta::identifier_of(f))}),
               {.name = std::meta::identifier_of(f), .no_unique_address = true}));
      }
      std::meta::define_aggregate(^^interface_base, infos);
   }
};

template<typename Interface, typename DynClass>
using base_dyn = make_base_dyn_class<^^Interface, ^^DynClass>::interface_base;

template<typename RetType, typename... Args>
using func_ptr_maker = RetType (*)(Args...);

template<std::meta::info F, typename Class, typename... Args>
constexpr auto produce_func_ptr
   = +[](void* c, Args... args) -> decltype(auto) { return static_cast<Class*>(c)->[:F:](args...); };

template<std::meta::info Interface, std::meta::info ImplementingClass>
constexpr auto make_vtable()
{
   static constexpr auto interface_funcs = std::define_static_array(get_funcs_sorted_by_name(Interface));
   static constexpr auto impl_funcs = std::define_static_array(get_funcs_sorted_by_name(ImplementingClass));

   // This should work, but doesn't, grrrrr
   // constexpr auto [...Is] = std::make_index_sequence<funcs.size()>{};
   // static_assert(sizeof...(Is) == funcs.size());

   return []<std::size_t... Is>(std::index_sequence<Is...>) {
      return tuple{[:std::meta::substitute(
                        ^^produce_func_ptr,
                        std::views::concat(
                           std::array{
                              std::meta::reflect_constant(*std::ranges::find_if(
                                 impl_funcs,
                                 [](const auto x) {
                                    return std::meta::identifier_of(x) == std::meta::identifier_of(interface_funcs[Is]);
                                 })),
                              ImplementingClass},
                           std::meta::parameters_of(interface_funcs[Is])
                              | std::views::transform(std::meta::type_of))):]...};
   }(std::make_index_sequence<interface_funcs.size()>{});
}

template<typename Interface>
using vtable_type = decltype(make_vtable<^^Interface, ^^Interface>());

} // namespace khct::detail

namespace khct {

template<typename Interface>
struct dyn : detail::base_dyn<Interface, dyn<Interface>> {
   using base = detail::base_dyn<Interface, dyn<Interface>>;
   using vtable_type = detail::vtable_type<Interface>;

   template<typename U>
   explicit constexpr dyn(U* const data) noexcept : base{data}, vtable_{detail::make_vtable<^^Interface, ^^U>()}
   {}

   vtable_type vtable_;
};

} // namespace khct

struct interface {
   auto get_x() -> int;
   void set_x(int);
};

struct normal_x {
   auto get_x() -> int { return x; }
   void set_x(int new_x) { x = new_x; }

   int x = 0;
};

struct weird_x {
   auto get_x() -> int { return x + old_x; }
   void set_x(int new_x)
   {
      old_x = x;
      x = new_x;
   }

   int x = 0;
   int old_x = 0;
};

auto main() -> int
{
   normal_x a;
   khct::dyn<interface> test1{&a};
   test1.set_x(10);
   assert(test1.get_x() == 10);

   weird_x b;
   khct::dyn<interface> test2{&b};
   test2.set_x(10);
   test2.set_x(20);
   assert(test2.get_x() == 10 + 20);
}
