#ifndef KHCT_CPP_DYN_HPP
   #define KHCT_CPP_DYN_HPP

   #include <algorithm>
   #include <concepts>
   #include <flat_map>
   #include <functional>
   #include <meta>
   #include <optional>
   #include <ranges>

namespace khct::detail {

template<std::size_t I>
constexpr std::array<char, 16> tuple_name = [] {
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

template<typename TupleTypeTo, typename TupleTypeFrom>
constexpr TupleTypeTo transform_tuple(const TupleTypeFrom& from) noexcept
{
   static constexpr auto impl = []<typename... Ts, typename... Us>(const tuple<Us...>& from, tuple<Ts...>*) noexcept {
      return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
         return tuple<Ts...>{from.template get<Is>()...};
      }(std::index_sequence_for<Ts...>{});
   };
   return impl(from, static_cast<TupleTypeTo*>(nullptr));
}

template<std::meta::info F, typename Ptr, typename ClassPtr, typename... Args>
constexpr auto vtable_entry_ptr = +[](Ptr c, Args&&... args) noexcept(noexcept(
                                      static_cast<ClassPtr>(c)->[:F:](std::forward<Args>(args)...))) -> decltype(auto) {
   return static_cast<ClassPtr>(c)->[:F:](std::forward<Args>(args)...);
};

template<std::meta::info F, typename... Args>
constexpr auto static_vtable_entry_ptr
   = +[](const void*, Args&&... args) noexcept(noexcept([:F:](std::forward<Args>(args)...))) -> decltype(auto) {
   return [:F:](std::forward<Args>(args)...);
};

consteval auto get_trait_funcs(std::meta::info c) -> std::vector<std::meta::info>
{
   auto non_cv_c = std::meta::remove_const(std::meta::remove_volatile(c));
   auto f = std::meta::members_of(non_cv_c, std::meta::access_context::current())
          | std::views::filter(std::meta::is_function) | std::views::filter(std::not_fn(std::meta::is_constructor))
          | std::views::filter(std::not_fn(std::meta::is_operator_function))
          | std::views::filter(std::not_fn(std::meta::is_destructor)) | std::ranges::to<std::vector>();
   std::ranges::sort(f, {}, std::meta::display_string_of);

   return f;
}

consteval auto is_const_qualified_function(std::meta::info func) -> bool
{
   if (std::meta::is_static_member(func)) {
      return false;
   }

   const auto args = std::meta::parameters_of(func);
   if (!args.empty() && std::meta::is_explicit_object_parameter(args.front())) {
      auto self_arg_type = std::meta::type_of(args.front());
      return std::meta::is_const_type(std::meta::remove_reference(self_arg_type));
   }

   return std::meta::is_const(func);
}

consteval auto params_without_explicit_this(std::meta::info func) -> std::vector<std::meta::info>
{
   const auto params = std::meta::parameters_of(func);
   if (!params.empty()) {
      if (std::meta::is_explicit_object_parameter(params.front())) {
         return params | std::views::drop(1) | std::ranges::to<std::vector>();
      }
   }
   return params;
}

consteval auto make_vtable_entry(std::meta::info impl_func) -> std::meta::info
{
   const auto impl_args = std::meta::parameters_of(impl_func);

   // static function; no special handling needed
   if (std::meta::is_static_member(impl_func)) {
      return std::meta::substitute(
         ^^static_vtable_entry_ptr,
         std::views::concat(
            std::views::single(std::meta::reflect_constant(impl_func)),
            impl_args | std::views::transform(std::meta::type_of)));
   }

   // member function; need to check if the first parameter is an explicit
   // object parameter and handle accordingly if so
   if (!impl_args.empty() && std::meta::is_explicit_object_parameter(impl_args.front())) {
      auto self_arg_type = std::meta::type_of(impl_args.front());
      const auto is_const = std::meta::is_const_type(std::meta::remove_reference(self_arg_type));

      return std::meta::substitute(
         ^^vtable_entry_ptr,
         std::views::concat(
            std::array{
               std::meta::reflect_constant(impl_func),
               is_const ? ^^const void* : ^^void*,
               std::meta::add_pointer(is_const ? std::meta::add_const(self_arg_type) : self_arg_type),
            },
            impl_args | std::views::drop(1) | std::views::transform(std::meta::type_of)));
   }

   // normal member function
   const auto is_const = std::meta::is_const(impl_func);
   auto class_type = std::meta::parent_of(impl_func);
   return std::meta::substitute(
      ^^vtable_entry_ptr,
      std::views::concat(
         std::array{
            std::meta::reflect_constant(impl_func),
            is_const ? ^^const void* : ^^void*,
            std::meta::add_pointer(is_const ? std::meta::add_const(class_type) : class_type)},
         impl_args | std::views::transform(std::meta::type_of)));
}

consteval auto get_matching_impl_func(std::meta::info trait_func, const std::span<const std::meta::info> impl_funcs)
   -> std::optional<std::meta::info>
{
   const auto iter = std::ranges::find_if(impl_funcs, [&](auto x) {
      return std::meta::identifier_of(x) == std::meta::identifier_of(trait_func)
          && std::meta::is_same_type(std::meta::return_type_of(x), std::meta::return_type_of(trait_func))
          && params_without_explicit_this(x).size() == params_without_explicit_this(trait_func).size()
          && is_const_qualified_function(x) == is_const_qualified_function(trait_func)
          && std::ranges::all_of(
                std::views::zip(params_without_explicit_this(x), params_without_explicit_this(trait_func)),
                [](auto tup) {
                   auto [a, b] = tup;
                   return std::meta::is_same_type(std::meta::type_of(a), std::meta::type_of(b));
                });
   });
   return iter == impl_funcs.end() ? std::optional<std::meta::info>{} : *iter;
}

template<std::meta::info Trait, std::meta::info ImplementingClass>
consteval auto make_vtable() -> auto
{
   static constexpr auto trait_funcs = std::define_static_array(get_trait_funcs(Trait));
   static constexpr auto impl_funcs = std::define_static_array(get_trait_funcs(ImplementingClass));

   return []<std::size_t... Is>(std::index_sequence<Is...>) {
      constexpr auto find_matching_impl_func = [](const std::size_t index) -> std::meta::info {
         return *get_matching_impl_func(trait_funcs[index], impl_funcs);
      };

      return tuple{[:make_vtable_entry(find_matching_impl_func(Is)):]...};
   }(std::make_index_sequence<trait_funcs.size()>{});
}

template<typename T>
using vtable_type = decltype(make_vtable<^^T, ^^T>());

consteval auto partition_functions_by_name(std::meta::info cls)
   -> std::flat_map<std::string_view, std::vector<std::meta::info>>
{
   std::flat_map<std::string_view, std::vector<std::meta::info>> to_ret;

   for (const auto& f : get_trait_funcs(cls)) {
      to_ret[std::meta::identifier_of(f)].push_back(f);
   }

   return to_ret;
}

template<std::meta::info Trait, std::size_t Index, typename RetType, typename SelfPtr, typename... Args>
struct base_caller_single_func {
   static constexpr auto
      call_with_vtable_and_ptr(const vtable_type<typename[:Trait:]>& vtable, SelfPtr ptr, Args&&... args) noexcept(
         noexcept(vtable.template get<Index>()(ptr, std::forward<Args>(args)...))) -> RetType
   { return vtable.template get<Index>()(ptr, std::forward<Args>(args)...); }

   using self = base_caller_single_func;
   constexpr auto operator()(
      this std::conditional_t<std::is_const_v<std::remove_pointer_t<SelfPtr>>, const self&, self&>,
      Args&&... args) noexcept(noexcept(std::declval<const vtable_type<typename[:Trait:]>&>()
                                           .template get<Index>()(
                                              std::declval<SelfPtr>(), std::forward<Args>(args)...))) -> RetType;
};

// clang-format off
template<std::meta::info Trait, std::size_t Index, std::meta::info Func>
struct base_caller_single
   :
   [:
      std::meta::substitute(
         ^^base_caller_single_func,
         std::views::concat(
            std::array{
               std::meta::reflect_constant(Trait),
               std::meta::reflect_constant(Index),
               std::meta::return_type_of(Func),
               is_const_qualified_function(Func) ? ^^const void* : ^^void*
            },
            params_without_explicit_this(Func) | std::views::transform(std::meta::type_of)
         )
      )
   :] {};
// clang-format on

// public so that ::khct::call can access everything in a requires clause
template<std::meta::info Trait, tuple<std::size_t, std::meta::info>... Funcs>
struct base_caller : base_caller_single<Trait, Funcs.get<0>(), Funcs.get<1>()>... {
   using base_caller_single<Trait, Funcs.get<0>(), Funcs.get<1>()>::call_with_vtable_and_ptr...;
   using base_caller_single<Trait, Funcs.get<0>(), Funcs.get<1>()>::operator()...;
};

// private so that users can't access the implementation
template<std::meta::info Trait, tuple<std::size_t, std::meta::info>... Funcs>
struct private_base_caller : private base_caller<Trait, Funcs...> {};

// clang-format off
template<typename Trait, std::size_t StartIndex, std::meta::info... Funcs>
struct caller
   :
   [:
      std::meta::substitute(
         ^^private_base_caller,
         std::views::concat(
            std::views::single(std::meta::reflect_constant(^^Trait)),
            std::views::enumerate(std::array{Funcs...})
               | std::views::transform([](auto pair) {
                  const auto [i, x] = pair;
                  return std::meta::reflect_constant(tuple{StartIndex + i, x});
               }
            )
         )
      )
   :] {};
// clang-format on

template<typename Trait>
struct caller_holder_generate {
   struct type;

   consteval
   {
      std::flat_map<std::string_view, std::vector<std::meta::info>> caller_template_args;

      const auto funcs = partition_functions_by_name(^^Trait);

      std::size_t i = 0;
      for (const auto& [name, named_funcs] : funcs) {
         caller_template_args[name]
            = std::views::concat(
                 std::array{^^Trait, std::meta::reflect_constant(i)},
                 named_funcs | std::views::transform([](auto x) { return std::meta::reflect_constant(x); }))
            | std::ranges::to<std::vector>();
         i += named_funcs.size();
      }

      std::meta::define_aggregate(^^type, caller_template_args | std::views::transform([](auto x) {
                                             return data_member_spec(
                                                std::meta::substitute(^^caller, x.second), {.name = x.first});
                                          }));
   }
};

struct trait_struct {};
struct auto_trait_struct {};

// TODO: Add display_string_of to show violating function
consteval auto is_valid_trait(std::meta::info raw_trait) -> bool
{
   const auto trait = std::meta::remove_const(raw_trait);

   if (!std::meta::is_complete_type(trait)) {
      throw std::meta::exception{"Traits must be complete types", trait};
   }

   if (!std::meta::is_class_type(trait)) {
      throw std::meta::exception{"Traits must be class types", trait};
   }

   if (!std::meta::bases_of(trait, std::meta::access_context::unchecked()).empty()) {
      throw std::meta::exception{"Traits must not have bases", trait};
   }

   if (
      !std::meta::static_data_members_of(trait, std::meta::access_context::unchecked()).empty()
      || !std::meta::nonstatic_data_members_of(trait, std::meta::access_context::unchecked()).empty()) {
      throw std::meta::exception{"Traits must not have any data members", trait};
   }

   const auto members = std::meta::members_of(trait, std::meta::access_context::unchecked());

   const auto funcs = members | std::views::filter(std::meta::is_function) | std::ranges::to<std::vector>();

   if (!std::ranges::all_of(funcs, std::meta::is_public)) {
      throw std::meta::exception{"Traits must have all public functions", trait};
   }

   if (
      std::ranges::any_of(
         funcs | std::views::filter(std::meta::is_special_member_function), std::meta::is_user_declared)) {
      throw std::meta::exception{"Traits must not have user declared special member functions", trait};
   }

   if (!std::empty(funcs | std::views::filter(std::meta::is_function_template) | std::ranges::to<std::vector>())) {
      throw std::meta::exception{"Traits must not have any function templates", trait};
   }

   const auto r_ref_funcs
      = funcs | std::views::filter(std::meta::is_rvalue_reference_qualified) | std::ranges::to<std::vector>();
   const auto r_ref_funcs2 = funcs | std::views::filter([](auto x) {
                                const auto params = std::meta::parameters_of(x);
                                return !params.empty() && std::meta::is_explicit_object_parameter(params.front())
                                    && std::meta::is_rvalue_reference_type(std::meta::type_of(params.front()));
                             })
                           | std::ranges::to<std::vector>();
   if (!r_ref_funcs.empty() || !r_ref_funcs2.empty()) {
      throw std::meta::exception{"Traits must not have rvalue reference qualified functions", trait};
   }

   const auto value_funcs = funcs | std::views::filter([](auto x) {
                               const auto params = std::meta::parameters_of(x);
                               return !params.empty() && std::meta::is_explicit_object_parameter(params.front())
                                   && !std::meta::is_reference_type(std::meta::type_of(params.front()));
                            })
                          | std::ranges::to<std::vector>();
   if (!value_funcs.empty()) {
      throw std::meta::exception{"Traits must not have explicit object parameters passed by value", trait};
   }

   // std::meta::is_vararg_function appears to be missing even though it's in the header?
   // not sure what's going on here so skip it for now
   // if (!std::empty(funcs | std::views::filter(std::meta::is_vararg_function) | std::ranges::to<std::vector>())) {
   //    throw std::meta::exception{"Traits must not have vararg functions", trait};
   // }

   constexpr auto is_conv
      = [](auto x) { return std::meta::is_conversion_function(x) || std::meta::is_conversion_function_template(x); };
   if (!std::empty(funcs | std::views::filter(is_conv) | std::ranges::to<std::vector>())) {
      throw std::meta::exception{"Traits must not have any conversion functions", trait};
   }

   constexpr auto is_declared_op = [](auto x) {
      return std::meta::is_user_declared(x)
          && (std::meta::is_operator_function(x) || std::meta::is_operator_function_template(x));
   };
   if (!std::empty(funcs | std::views::filter(is_declared_op) | std::ranges::to<std::vector>())) {
      throw std::meta::exception{"Traits must not have any operator functions", trait};
   }

   constexpr auto is_alias = [](auto x) { return std::meta::is_type_alias(x) || std::meta::is_alias_template(x); };
   if (!std::empty(members | std::views::filter(is_alias) | std::ranges::to<std::vector>())) {
      throw std::meta::exception{"Traits must not have any type aliases", trait};
   }

   constexpr auto is_class_or_class_template = [](auto x) {
      return (std::meta::is_type(x) && std::meta::is_class_type(x)) || std::meta::is_class_template(x);
   };
   if (!std::empty(members | std::views::filter(is_class_or_class_template) | std::ranges::to<std::vector>())) {
      throw std::meta::exception{"Traits must not have any class declarations", trait};
   }

   const auto marked_trait = !std::meta::annotations_of_with_type(trait, ^^trait_struct).empty();
   const auto marked_auto_trait = !std::meta::annotations_of_with_type(trait, ^^auto_trait_struct).empty();
   if ((!marked_trait && !marked_auto_trait) || (marked_trait && marked_auto_trait)) {
      throw std::meta::exception{"Traits must be annotated with exactly one of khct::trait or khct::auto_trait", trait};
   }

   return true;
}

consteval auto is_valid_impl_for(std::meta::info impl, std::meta::info trait) -> bool
{
   if (!std::meta::is_complete_type(impl)) {
      throw std::meta::exception{"Implementations must be complete types", impl};
   }

   if (!std::meta::is_class_type(impl)) {
      throw std::meta::exception{"Implementations must be class types", impl};
   }

   const auto impl_funcs = get_trait_funcs(impl);
   for (const auto& trait_func : get_trait_funcs(trait)) {
      if (!get_matching_impl_func(trait_func, impl_funcs)) {
         throw std::meta::exception{
            std::string{"Implementation does not have a matching function for trait function: "}
               + std::meta::display_string_of(trait_func),
            trait_func};
      }
   }

   return true;
}

} // namespace khct::detail

namespace khct {

inline constexpr detail::trait_struct trait{};
inline constexpr detail::auto_trait_struct auto_trait{};

template<typename Trait>
concept valid_trait = detail::is_valid_trait(^^Trait);

namespace detail {

template<typename Trait>
   requires valid_trait<Trait>
struct impl_for_struct {};

consteval auto is_marked_impl_for(std::meta::info impl, std::meta::info trait) -> bool
{
   return !std::meta::annotations_of_with_type(
              std::meta::remove_const(impl), std::meta::substitute(^^impl_for_struct, {std::meta::remove_const(trait)}))
              .empty();
}

} // namespace detail

template<typename Trait>
concept is_auto_trait
   = valid_trait<Trait>
  && !std::meta::annotations_of_with_type(^^std::remove_const_t<Trait>, ^^detail::auto_trait_struct).empty();

template<typename Impl, typename Trait>
concept is_impl_for
   = valid_trait<Trait> && detail::is_marked_impl_for(^^Impl, ^^Trait) && detail::is_valid_impl_for(^^Impl, ^^Trait);

template<typename T>
   requires valid_trait<T>
inline constexpr auto impl_for = detail::impl_for_struct<T>{};

// TODO: This doesn't produce very good error messages
//       Write own checker and throw better ones?
template<auto... CallerValues, typename DynTrait, typename... Args>
   requires std::
      invocable<decltype(std::forward_like<DynTrait>(std::declval<detail::base_caller<CallerValues...>>())), Args...>
   constexpr auto call(detail::private_base_caller<CallerValues...>, DynTrait&& trait, Args&&... args) noexcept(
      std::is_nothrow_invocable_v<
         decltype(std::forward_like<DynTrait>(std::declval<detail::base_caller<CallerValues...>>())),
         Args...>) -> decltype(auto);

template<valid_trait Trait>
struct dyn : detail::caller_holder_generate<Trait>::type {
   using basic_trait_type = Trait;

   dyn() = delete;
   dyn(const dyn&) = default;
   dyn(dyn&&) = default;
   dyn& operator=(const dyn&) = default;
   dyn& operator=(dyn&&) = default;

   template<is_impl_for<Trait> U>
      requires(!std::is_const_v<Trait>)
   dyn(U* data) noexcept
      : vtable_{detail::transform_tuple<detail::vtable_type<Trait>>(detail::make_vtable<^^Trait, ^^U>())}, data_{&data}
   {}

   template<is_impl_for<Trait> U>
   dyn(const U* data) noexcept
      : vtable_{detail::transform_tuple<detail::vtable_type<Trait>>(detail::make_vtable<^^Trait, ^^U>())}, data_{&data}
   {}

   template<typename Caller, typename... Args>
      requires(!std::is_const_v<Trait>)
   auto call(this dyn& self, Caller to_call, Args&&... args) noexcept(
      noexcept(khct::call(to_call, self, std::forward<Args>(args)...))) -> decltype(auto)
   { return khct::call(to_call, self, std::forward<Args>(args)...); }

   template<typename Caller, typename... Args>
   auto call(this const dyn& self, Caller to_call, Args&&... args) noexcept(
      noexcept(khct::call(to_call, self, std::forward<Args>(args)...))) -> decltype(auto)
   { return khct::call(to_call, self, std::forward<Args>(args)...); }

private:
   detail::vtable_type<std::remove_const_t<Trait>> vtable_;
   std::conditional_t<std::is_const_v<Trait>, const void*, void*> data_;

   template<auto... CallerValues, typename DynTrait, typename... Args>
      requires std::
         invocable<decltype(std::forward_like<DynTrait>(std::declval<detail::base_caller<CallerValues...>>())), Args...>
      friend constexpr auto
      call(detail::private_base_caller<CallerValues...>, DynTrait&& trait, Args&&... args) noexcept(
         std::is_nothrow_invocable_v<
            decltype(std::forward_like<DynTrait>(std::declval<detail::base_caller<CallerValues...>>())),
            Args...>) -> decltype(auto)
   {
      return detail::base_caller<CallerValues...>::call_with_vtable_and_ptr(
         trait.vtable_, std::forward_like<DynTrait>(trait.data_), std::forward<Args>(args)...);
   }
};

} // namespace khct

#endif // KHCT_CPP_DYN_HPP

#include <print>
#include <string>

struct[[= khct::trait]] trait {
   auto test1(this const trait&) -> void;
   auto test1(this trait&, int) -> void;
   auto test1(const std::string&) const -> void;
};

struct[[= khct::impl_for<trait>]] impl {
   auto test1(this const impl&) noexcept -> void { std::println("hi"); }
   auto test1(this impl&, int) -> void { std::println("hi2"); }
   auto test1(this const impl&, const std::string& str) -> void { std::println("-- {} --", str); }
};

auto main() -> int
{
   impl x;
   khct::dyn<trait> test{&x};
   test.call(test.test1);
   test.call(test.test1, 1);
   test.call(test.test1, "woah");
}
