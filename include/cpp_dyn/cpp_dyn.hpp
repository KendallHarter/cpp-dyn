#ifndef CPP_DYN_HPP
#define CPP_DYN_HPP

#include "common.hpp"
#include "tuple.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <meta>
#include <ranges>

namespace khct {

constexpr struct {
} default_impl;

namespace detail {

template<std::meta::info... Info>
struct outer {
   struct inner;
   consteval { std::meta::define_aggregate(^^inner, {Info...}); }
};

template<std::meta::info... Info>
using cls = outer<Info...>::inner;

template<typename RetType, typename... Args>
using func_ptr_maker = RetType (*)(Args...);

template<typename RetType, typename... Args>
using noexcept_func_ptr_maker = RetType (*)(Args...) noexcept;

consteval std::meta::info
   member_func_to_non_member_func(std::meta::info f, std::meta::info trait, bool skip_first = false)
{
   std::vector<std::meta::info> infos;
   if (std::meta::is_function_template(f)) {
      return member_func_to_non_member_func(std::meta::substitute(f, {trait}), trait, true);
   }
   infos.push_back(std::meta::return_type_of(f));
   if (std::meta::is_const(f) || std::meta::is_static_member(f)) {
      infos.push_back(^^const void*);
   }
   else {
      infos.push_back(^^void*);
   }
   for (const auto i : std::meta::parameters_of(f) | std::views::drop(skip_first)) {
      infos.push_back(std::meta::type_of(i));
   }
   return std::meta::substitute(std::meta::is_noexcept(f) ? ^^noexcept_func_ptr_maker : ^^func_ptr_maker, infos);
}

consteval auto get_sorted_funcs_by_name(std::meta::info c) -> std::vector<std::meta::info>
{
   auto f = std::meta::members_of(c, std::meta::access_context::current())
          | std::views::filter([](auto x) { return std::meta::is_function_template(x) || std::meta::is_function(x); })
          | std::views::filter(std::not_fn(std::meta::is_constructor))
          | std::views::filter(std::not_fn(std::meta::is_operator_function))
          | std::views::filter(std::not_fn(std::meta::is_destructor)) | std::ranges::to<std::vector>();

   // This should really be using stable_sort, but Clang currently doesn't support it in constexpr contexts
   // This should be OK because it's always going in in the same order though.
   std::ranges::sort(f, {}, [](auto x) { return std::meta::identifier_of(x); });

   return f;
}

consteval auto partition_sorted_funcs_by_name(const std::span<const std::meta::info> funcs)
   -> std::vector<std::vector<std::meta::info>>
{
   std::vector<std::vector<std::meta::info>> to_ret;
   to_ret.emplace_back();

   auto cur_name = std::meta::identifier_of(funcs[0]);

   for (const auto f : funcs) {
      if (cur_name != std::meta::identifier_of(f)) {
         to_ret.emplace_back();
         cur_name = std::meta::identifier_of(f);
      }
      to_ret.back().push_back(f);
   }

   return to_ret;
}

// We don't need TraitClass, but have it to prevent passing other dyn_trait functions
template<typename TraitClass, auto... Rest>
struct non_owning_func_caller;

static constexpr auto non_owning_get_vtable
   = []<bool InlineVtable, typename Class, std::size_t I>(auto* c) consteval noexcept -> auto& {
   static constexpr bool is_const = std::is_const_v<std::remove_pointer_t<decltype(c)>>;
   using cast_type = std::conditional_t<is_const, const Class*, Class*>;
   if constexpr (InlineVtable) {
      return static_cast<cast_type>(c)->funcs_.template get<I>();
   }
   else {
      return static_cast<cast_type>(c)->funcs_->template get<I>();
   }
};

template<typename TraitClass, std::size_t FuncIndex>
struct non_owning_func_caller<TraitClass, FuncIndex> {
   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto operator()(const void* c, Args&&... args) noexcept(
      noexcept(non_owning_get_vtable.operator()<InlineVtable, Class, FuncIndex>(c)(
         static_cast<const Class*>(c)->data_, std::forward<Args>(args)...))) -> decltype(auto)
   {
      const auto* const ptr = static_cast<const Class*>(c);
      return non_owning_get_vtable.operator()<InlineVtable, Class, FuncIndex>(c)(
         ptr->data_, std::forward<Args>(args)...);
   }

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto operator()(void* c, Args&&... args) noexcept(
      noexcept(non_owning_get_vtable.operator()<InlineVtable, Class, FuncIndex>(c)(
         static_cast<Class*>(c)->data_, std::forward<Args>(args)...))) -> decltype(auto)
   {
      auto* const ptr = static_cast<Class*>(c);
      return non_owning_get_vtable.operator()<InlineVtable, Class, FuncIndex>(c)(
         ptr->data_, std::forward<Args>(args)...);
   }
};

template<std::size_t Index, typename FuncType>
struct non_owning_func_caller_helper;

template<std::size_t Index, typename RetType, typename... Args>
struct non_owning_func_caller_helper<Index, RetType (*)(Args...)> {
   static consteval auto operator()(Args...) noexcept -> std::integral_constant<std::size_t, Index>;
};

template<std::size_t Index, typename RetType, typename... Args>
struct non_owning_func_caller_helper<Index, RetType (*)(Args...) noexcept> {
   static consteval auto operator()(Args...) noexcept -> std::integral_constant<std::size_t, Index>;
};

template<typename TraitClass, std::size_t StartIndex, const char* Name>
struct non_owning_func_caller<TraitClass, StartIndex, Name> {
   static constexpr std::span<const std::meta::info> funcs = []() consteval -> std::span<const std::meta::info> {
      static constexpr auto funcs = std::define_static_array(get_sorted_funcs_by_name(^^TraitClass));
      const auto sorted_funcs = partition_sorted_funcs_by_name(funcs);

      for (const auto& f : sorted_funcs) {
         if (std::meta::identifier_of(f.front()) == Name) {
            return std::define_static_array(f);
         }
      }

      return {};
   }();

   static_assert(!funcs.empty(), "No matching function name");

   static constexpr auto get_indexer = []() consteval {
      return []<std::size_t... I>(std::index_sequence<I...>) {
         return ::khct::detail::overload_set{non_owning_func_caller_helper<
            StartIndex + I,
            typename[:member_func_to_non_member_func(funcs[I], ^^TraitClass):]>{}...};
      }(std::make_index_sequence<funcs.size()>{});
   }();

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr decltype(auto) operator()(const void* c, Args&&... args) noexcept(
      noexcept(non_owning_get_vtable.operator()<
               InlineVtable,
               Class,
               decltype(get_indexer(std::declval<const Class*>(), std::declval<Args>()...))::value>(c)(
         static_cast<const Class*>(c)->data_, std::forward<Args>(args)...)))
   {
      const auto* const ptr = static_cast<const Class*>(c);
      return non_owning_get_vtable.operator()<
         InlineVtable,
         Class,
         decltype(get_indexer(std::declval<const Class*>(), std::declval<Args>()...))::value>(c)(
         ptr->data_, std::forward<Args>(args)...);
   }

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr decltype(auto) operator()(void* c, Args&&... args) noexcept(noexcept(
      non_owning_get_vtable.
      operator()<InlineVtable, Class, decltype(get_indexer(std::declval<Class*>(), std::declval<Args>()...))::value>(c)(
         static_cast<Class*>(c)->data_, std::forward<Args>(args)...)))
   {
      auto* const ptr = static_cast<Class*>(c);

      return non_owning_get_vtable.
         operator()<InlineVtable, Class, decltype(get_indexer(std::declval<Class*>(), std::declval<Args>()...))::value>(
            c)(ptr->data_, std::forward<Args>(args)...);
   }
};

consteval auto get_members_and_tuple_type(std::meta::info trait)
   -> std::pair<std::vector<std::meta::info>, std::vector<std::meta::info>>
{
   const auto funcs_by_name = partition_sorted_funcs_by_name(get_sorted_funcs_by_name(trait));
   std::vector<std::meta::info> members;
   std::vector<std::meta::info> func_ptrs;
   std::size_t index = 0;
   for (const auto funcs : funcs_by_name) {
      if (funcs.size() == 1) {
         // No overloads, just a single function
         const auto f = funcs.front();
         if (std::meta::is_function_template(f)) {
            // This is std::meta::annotations_of_with_type in C++26
            const auto is_default_impl
               = !std::meta::annotations_of(std::meta::substitute(f, {trait}), ^^decltype(default_impl)).empty();
            assert(is_default_impl && "Templated functions can only be used for default implementations");
         }
         members.push_back(
            std::meta::reflect_constant(
               std::meta::data_member_spec(
                  std::meta::substitute(^^non_owning_func_caller, {trait, std::meta::reflect_constant(index)}),
                  {.name = std::meta::identifier_of(f), .no_unique_address = true})));
         index += 1;
         func_ptrs.push_back(member_func_to_non_member_func(f, trait));
      }
      else {
         // Oh no, there's an overload; gotta handle it
         for (const auto& f : funcs) {
            if (std::meta::is_function_template(f)) {
               // This is std::meta::annotations_of_with_type in C++26
               const auto is_default_impl
                  = !std::meta::annotations_of(std::meta::substitute(f, {trait}), ^^decltype(default_impl)).empty();
               assert(is_default_impl && "Templated functions can only be used for default implementations");
            }
            func_ptrs.push_back(member_func_to_non_member_func(f, trait));
         }
         members.push_back(
            std::meta::reflect_constant(
               std::meta::data_member_spec(
                  std::meta::substitute(
                     ^^non_owning_func_caller,
                     {trait,
                      std::meta::reflect_constant(index),
                      std::meta::reflect_constant_string(std::meta::identifier_of(funcs.front()))}),
                  {.name = std::meta::identifier_of(funcs.front()), .no_unique_address = true})));
         index += funcs.size();
      }
   }
   return {members, func_ptrs};
}

consteval std::meta::info
   make_non_owning_dyn_trait(std::meta::info trait, bool is_const, bool store_vtable_inline) noexcept
{
   auto [members, func_ptrs] = get_members_and_tuple_type(trait);
   members.push_back(
      std::meta::reflect_constant(std::meta::data_member_spec(is_const ? ^^const void* : ^^void*, {.name = "data_"})));

   const auto tuple_func_ptrs = std::meta::substitute(^^detail::tuple, func_ptrs);

   members.push_back(
      std::meta::reflect_constant(
         std::meta::data_member_spec(
            store_vtable_inline ? tuple_func_ptrs : std::meta::add_pointer(std::meta::add_const(tuple_func_ptrs)),
            {.name = "funcs_"})));
   return std::meta::substitute(^^cls, members);
}

template<std::meta::info F, typename Ptr, typename Class, typename... Args>
constexpr auto produce_func_ptr
   = +[](Ptr c, Args... args) noexcept(noexcept(static_cast<Class>(c)->[:F:](args...))) -> decltype(auto) {
   return static_cast<Class>(c)->[:F:](args...);
};

template<std::meta::info F, typename Trait, typename Ptr, typename Class, typename... Args>
constexpr auto produce_default_func_ptr
   = +[](Ptr c, Args... args) noexcept(noexcept(Trait{}.[:F:](*static_cast<Class>(c), args...))) -> decltype(auto) {
   return Trait{}.[:F:](*static_cast<Class>(c), args...);
};

template<std::meta::info F, typename... Args>
constexpr auto produce_default_static_func_ptr
   = +[](const void*, Args... args) noexcept(noexcept([:F:](args...))) -> decltype(auto) { return [:F:](args...); };

template<typename Trait, typename ToStore>
consteval auto make_dyn_trait_pointers()
{
   static constexpr auto func_ptrs = std::define_static_array(get_members_and_tuple_type(^^Trait).second);
   static constexpr auto trait_funcs = std::define_static_array(get_sorted_funcs_by_name(^^Trait));

   static constexpr auto to_store_func = std::define_static_array(
      std::meta::members_of(^^ToStore, std::meta::access_context::current())
      | std::views::filter([](auto x) { return std::meta::is_function_template(x) || std::meta::is_function(x); })
      | std::views::filter(std::not_fn(std::meta::is_constructor))
      | std::views::filter(std::not_fn(std::meta::is_operator_function))
      | std::views::filter(std::not_fn(std::meta::is_destructor)));

   using ret_type = [:std::meta::substitute(^^detail::tuple, func_ptrs):];

   return []<std::size_t... Is>(std::index_sequence<Is...>) {
      return ret_type {
         // clang-format off
         []<std::size_t I>() -> [:func_ptrs[I]:] {
            // clang-format on
            static constexpr auto produce_func_ptr_from_info = [](std::meta::info func_info, std::meta::info sub_into) {
               const auto is_default = sub_into == ^^produce_default_func_ptr;
               std::vector<std::meta::info> args;
               args.push_back(std::meta::reflect_constant(func_info));
               if (is_default) {
                  args.push_back(^^Trait);
               }
               if (std::meta::is_const(func_info) || std::meta::is_static_member(func_info)) {
                  args.push_back(^^const void*);
                  args.push_back(^^const ToStore*);
               }
               else {
                  args.push_back(^^void*);
                  args.push_back(^^ToStore*);
               }
               for (const auto arg :
                    std::meta::parameters_of(func_info) | std::views::drop(static_cast<int>(is_default))) {
                  args.push_back(std::meta::type_of(arg));
               }

               return std::meta::substitute(sub_into, args);
            };
            template for (constexpr auto f : to_store_func)
            {
               static constexpr bool func_signatures_match = []() consteval {
                  if constexpr (!std::meta::is_function_template(f)) {
                     const auto cur_func = std::meta::is_function_template(trait_funcs[I])
                                            ? std::meta::substitute(trait_funcs[I], {^^ToStore})
                                            : trait_funcs[I];
                     const auto params1 = std::meta::parameters_of(f);
                     const std::vector<std::meta::info> params2
                        = std::meta::parameters_of(cur_func)
                        | std::views::drop(static_cast<int>(cur_func != trait_funcs[I]))
                        | std::ranges::to<std::vector>();

                     if (params1.size() != params2.size()) {
                        return false;
                     }

                     for (std::size_t i = 0; i < params1.size(); ++i) {
                        if (std::meta::type_of(params1[i]) != std::meta::type_of(params2[i])) {
                           return false;
                        }
                     }

                     return std::meta::return_type_of(f) == std::meta::return_type_of(cur_func);
                  }

                  return false;
               }();
               if constexpr (
                  std::meta::identifier_of(f) == std::meta::identifier_of(trait_funcs[I]) && func_signatures_match) {
                  return [:produce_func_ptr_from_info(f, ^^produce_func_ptr):];
               }
            }
            // Default implementation
            static constexpr auto f = trait_funcs[I];
            if constexpr (std::meta::is_function_template(f)) {
               // This is std::meta::annotations_of_with_type in C++26
               static constexpr auto f_sub = std::meta::substitute(f, {^^ToStore});
               static constexpr auto is_default = !std::meta::annotations_of(f_sub, ^^decltype(default_impl)).empty();
               static_assert(is_default, "Function templates must be default implementations");

               return [:produce_func_ptr_from_info(f_sub, ^^produce_default_func_ptr):];
            }
            else {
               // This is std::meta::annotations_of_with_type in C++26
               static constexpr auto is_default = !std::meta::annotations_of(f, ^^decltype(default_impl)).empty();
               if constexpr (is_default && std::meta::is_static_member(trait_funcs[I])) {
                  static constexpr auto to_ret = []() {
                     std::vector<std::meta::info> args;
                     args.push_back(std::meta::reflect_constant(f));
                     for (const auto arg : std::meta::parameters_of(f)) {
                        args.push_back(arg);
                     }
                     return std::meta::substitute(^^produce_default_static_func_ptr, args);
                  }();
                  return [:to_ret:];
               }
            }
            throw "invalid name/no default";
         }.template operator()<Is>()...
      };
   }.template operator()(std::make_index_sequence<trait_funcs.size()>{});
}

template<typename Trait, bool IsConst, bool StoreVtableInline>
using non_owning_dyn_trait_impl = [:make_non_owning_dyn_trait(^^Trait, IsConst, StoreVtableInline):];

} // namespace detail

struct non_owning_dyn_options {
   bool store_vtable_inline = false;
};

struct owning_dyn_options {
   bool store_vtable_inline = false;
   /// @brief The maximum number of bytes to allocate for local storage
   std::size_t max_no_alloc_size = 0;
};

// Something about the previous type alias inhibits argument deduction, so do this instead
template<typename Trait, bool IsConst, bool StoreVtableInline>
struct non_owning_dyn_trait : detail::non_owning_dyn_trait_impl<Trait, IsConst, StoreVtableInline> {};

template<typename DynTrait, non_owning_dyn_options Opt = {}, typename ToStore>
constexpr auto dyn(const ToStore* ptr) noexcept
{
   if constexpr (Opt.store_vtable_inline) {
      return non_owning_dyn_trait<DynTrait, true, Opt.store_vtable_inline>{
         {.data_ = ptr, .funcs_ = detail::make_dyn_trait_pointers<DynTrait, ToStore>()}};
   }
   else {
      return non_owning_dyn_trait<DynTrait, true, Opt.store_vtable_inline>{
         {.data_ = ptr, .funcs_ = detail::define_static_object(detail::make_dyn_trait_pointers<DynTrait, ToStore>())}};
   }
}

template<typename DynTrait, non_owning_dyn_options Opt = {}, typename ToStore>
constexpr auto dyn(ToStore* ptr) noexcept
{
   return non_owning_dyn_trait<DynTrait, false, Opt.store_vtable_inline>{
      {.data_ = ptr, .funcs_ = detail::define_static_object(detail::make_dyn_trait_pointers<DynTrait, ToStore>())}};
}

template<typename Trait, bool ConstSelf, bool InlineVtable, auto... FuncCallerRest, typename... T>
constexpr auto call(
   non_owning_dyn_trait<Trait, ConstSelf, InlineVtable> self,
   detail::non_owning_func_caller<Trait, FuncCallerRest...> to_call,
   T&&... args) noexcept(noexcept(to_call.template
                                  operator()<InlineVtable, non_owning_dyn_trait<Trait, ConstSelf, InlineVtable>>(
                                     &self, std::forward<T>(args)...)))
{
   return to_call.template operator()<InlineVtable, non_owning_dyn_trait<Trait, ConstSelf, InlineVtable>>(
      &self, std::forward<T>(args)...);
}

} // namespace khct

#endif // CPP_DYN_HPP
