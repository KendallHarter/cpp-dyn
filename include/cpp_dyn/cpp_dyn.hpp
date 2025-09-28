#ifndef CPP_DYN_HPP
#define CPP_DYN_HPP

#include "common.hpp"
#include "tuple.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <meta>
#include <ranges>
#include <type_traits>

namespace khct {

constexpr struct {
} default_impl;

struct non_owning_dyn_options {
   bool is_const = true;
   bool store_vtable_inline = false;
};

struct owning_dyn_options {
   bool store_vtable_inline = false;
   /// @brief The number of bytes to store objects into.
   ///        If this is 0, dynamically allocate objects instead of locally storing them.
   std::size_t stack_size = 0;
};

template<typename Trait, non_owning_dyn_options Opt>
struct non_owning_dyn_trait;

template<typename Trait, owning_dyn_options Opt>
struct owning_dyn_trait;

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

consteval auto member_func_to_non_member_func(std::meta::info f, std::meta::info trait, bool skip_first = false)
   -> std::meta::info
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
struct func_caller;

template<bool InlineVtable, typename Class, std::size_t I>
constexpr auto get_vtable(auto* c) noexcept -> auto&
{
   static constexpr bool is_const = std::is_const_v<std::remove_pointer_t<decltype(c)>>;
   using cast_type = std::conditional_t<is_const, const Class*, Class*>;
   if constexpr (InlineVtable) {
      return static_cast<cast_type>(c)->funcs_.template get<I>();
   }
   else {
      return static_cast<cast_type>(c)->funcs_->template get<I>();
   }
}

// This specialization is used for single functions (non-overloaded)
template<typename TraitClass, std::size_t FuncIndex, bool IsOwning>
struct func_caller<TraitClass, FuncIndex, IsOwning> {
   template<typename Trait, non_owning_dyn_options Opt>
   friend struct ::khct::non_owning_dyn_trait;

   template<typename Trait, owning_dyn_options Opt>
   friend struct ::khct::owning_dyn_trait;

private:
   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto
      call(const void* c, Args&&... args) noexcept(noexcept(get_vtable<InlineVtable, Class, FuncIndex + IsOwning>(c)(
         static_cast<const Class*>(c)->data(), std::forward<Args>(args)...))) -> decltype(auto)
   {
      const auto* const ptr = static_cast<const Class*>(c);
      return get_vtable<InlineVtable, Class, FuncIndex + IsOwning>(c)(ptr->data(), std::forward<Args>(args)...);
   }

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto
      call(void* c, Args&&... args) noexcept(noexcept(get_vtable<InlineVtable, Class, FuncIndex + IsOwning>(c)(
         static_cast<Class*>(c)->data(), std::forward<Args>(args)...))) -> decltype(auto)
   {
      auto* const ptr = static_cast<Class*>(c);
      return get_vtable<InlineVtable, Class, FuncIndex + IsOwning>(c)(ptr->data(), std::forward<Args>(args)...);
   }
};

template<std::size_t Index, typename FuncType>
struct func_caller_helper;

template<std::size_t Index, typename RetType, typename... Args>
struct func_caller_helper<Index, RetType (*)(Args...)> {
   static consteval auto operator()(Args...) noexcept -> std::integral_constant<std::size_t, Index>;
};

template<std::size_t Index, typename RetType, typename... Args>
struct func_caller_helper<Index, RetType (*)(Args...) noexcept> {
   static consteval auto operator()(Args...) noexcept -> std::integral_constant<std::size_t, Index>;
};

// This specialization is used for overload sets with the specified name
template<typename TraitClass, std::size_t StartIndex, const char* Name, bool IsOwning>
struct func_caller<TraitClass, StartIndex, Name, IsOwning> {
   template<typename Trait, non_owning_dyn_options Opt>
   friend struct ::khct::non_owning_dyn_trait;

   template<typename Trait, owning_dyn_options Opt>
   friend struct ::khct::owning_dyn_trait;

private:
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
         return ::khct::detail::overload_set{func_caller_helper<
            StartIndex + I + IsOwning,
            typename[:member_func_to_non_member_func(funcs[I], ^^TraitClass):]>{}...};
      }(std::make_index_sequence<funcs.size()>{});
   }();

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto call(const void* c, Args&&... args) noexcept(
      noexcept(get_vtable<
               InlineVtable,
               Class,
               decltype(get_indexer(std::declval<const Class*>(), std::declval<Args>()...))::value>(c)(
         static_cast<const Class*>(c)->data(), std::forward<Args>(args)...))) -> decltype(auto)
   {
      const auto* const ptr = static_cast<const Class*>(c);
      return get_vtable<
         InlineVtable,
         Class,
         decltype(get_indexer(std::declval<const Class*>(), std::declval<Args>()...))::value>(c)(
         ptr->data(), std::forward<Args>(args)...);
   }

   template<bool InlineVtable, typename Class, typename... Args>
   static constexpr auto call(void* c, Args&&... args) noexcept(noexcept(
      get_vtable<InlineVtable, Class, decltype(get_indexer(std::declval<Class*>(), std::declval<Args>()...))::value>(c)(
         static_cast<Class*>(c)->data(), std::forward<Args>(args)...))) -> decltype(auto)
   {
      auto* const ptr = static_cast<Class*>(c);

      return get_vtable<
         InlineVtable,
         Class,
         decltype(get_indexer(std::declval<Class*>(), std::declval<Args>()...))::value>(c)(
         ptr->data(), std::forward<Args>(args)...);
   }
};

consteval auto get_members_and_tuple_type(std::meta::info trait, bool is_owned)
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
                  std::meta::substitute(
                     ^^func_caller, {trait, std::meta::reflect_constant(index), std::meta::reflect_constant(is_owned)}),
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
                     ^^func_caller,
                     {trait,
                      std::meta::reflect_constant(index),
                      std::meta::reflect_constant_string(std::meta::identifier_of(funcs.front())),
                      std::meta::reflect_constant(is_owned)}),
                  {.name = std::meta::identifier_of(funcs.front()), .no_unique_address = true})));
         index += funcs.size();
      }
   }
   return {members, func_ptrs};
}

consteval auto make_non_owning_dyn_trait(std::meta::info trait, bool is_const, bool store_vtable_inline) noexcept
   -> std::meta::info
{
   return std::meta::substitute(^^cls, get_members_and_tuple_type(trait, false).first);
}

template<owning_dyn_options Opt>
consteval auto make_owning_dyn_trait(std::meta::info trait) -> std::meta::info
{
   return std::meta::substitute(^^cls, get_members_and_tuple_type(trait, true).first);
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

template<typename Trait, typename ToStore, bool IsOwned>
constexpr auto make_dyn_trait_pointers(void (*deleter)(void*) noexcept = nullptr)
{
   static constexpr auto func_ptrs = []() consteval {
      auto func_ptrs = get_members_and_tuple_type(^^Trait, IsOwned).second;
      if (IsOwned) {
         func_ptrs.insert(func_ptrs.begin(), ^^void (*)(void*) noexcept);
      }
      return std::define_static_array(func_ptrs);
   }();
   static constexpr auto trait_funcs = []() consteval {
      auto trait_funcs = get_sorted_funcs_by_name(^^Trait);
      if (IsOwned) {
         trait_funcs.insert(trait_funcs.begin(), ^^void (*)(void*) noexcept);
      }
      return std::define_static_array(trait_funcs);
   }();

   static constexpr auto to_store_func = std::define_static_array(
      std::meta::members_of(^^ToStore, std::meta::access_context::current())
      | std::views::filter([](auto x) { return std::meta::is_function_template(x) || std::meta::is_function(x); })
      | std::views::filter(std::not_fn(std::meta::is_constructor))
      | std::views::filter(std::not_fn(std::meta::is_operator_function))
      | std::views::filter(std::not_fn(std::meta::is_destructor)));

   using ret_type = [:std::meta::substitute(^^detail::tuple, func_ptrs):];

   return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      return ret_type {
         // clang-format off
         [&]<std::size_t I>() -> [:func_ptrs[I]:] {
            // clang-format on
            if constexpr (I == 0 && IsOwned) {
               return deleter;
            }
            else {
               static constexpr auto produce_func_ptr_from_info
                  = [](std::meta::info func_info, std::meta::info sub_into) {
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
                  static constexpr auto is_default
                     = !std::meta::annotations_of(f_sub, ^^decltype(default_impl)).empty();
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
            }
         }.template operator()<Is>()...
      };
   }.template operator()(std::make_index_sequence<trait_funcs.size()>{});
}

template<typename Trait, bool IsConst, bool StoreVtableInline>
using non_owning_dyn_trait_impl = [:make_non_owning_dyn_trait(^^Trait, IsConst, StoreVtableInline):];

template<typename Trait, owning_dyn_options Opt>
using owning_dyn_trait_impl = [:make_owning_dyn_trait<Opt>(^^Trait):];

} // namespace detail

template<typename Trait, non_owning_dyn_options Opt = {}>
struct non_owning_dyn_trait trivially_relocatable_if_eligible replaceable_if_eligible
   : detail::non_owning_dyn_trait_impl<Trait, Opt.is_const, Opt.store_vtable_inline> {
   template<typename TraitClass, auto... Rest>
   friend struct detail::func_caller;

   template<bool InlineVtable, typename Class, std::size_t I>
   friend constexpr auto detail::get_vtable(auto* c) noexcept -> auto&;

   non_owning_dyn_trait() = delete;
   non_owning_dyn_trait(const non_owning_dyn_trait&) = default;
   non_owning_dyn_trait(non_owning_dyn_trait&&) = default;
   non_owning_dyn_trait& operator=(const non_owning_dyn_trait&) = default;
   non_owning_dyn_trait& operator=(non_owning_dyn_trait&&) = default;

   template<typename ToStore>
      requires(Opt.is_const)
   constexpr non_owning_dyn_trait(const ToStore* ptr) noexcept : data_{ptr}, funcs_{gen_funcs<ToStore>()}
   {}

   template<typename ToStore>
      requires(!Opt.is_const)
   constexpr non_owning_dyn_trait(ToStore* ptr) noexcept : data_{ptr}, funcs_{gen_funcs<ToStore>()}
   {}

   template<auto... FuncCallerRest, typename... T>
   constexpr auto call(detail::func_caller<Trait, FuncCallerRest...> to_call, T&&... args) noexcept(
      noexcept(to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...))) -> decltype(auto)
   {
      return to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...);
   }

   template<auto... FuncCallerRest, typename... T>
   constexpr auto call(detail::func_caller<Trait, FuncCallerRest...> to_call, T&&... args) const
      noexcept(noexcept(to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...))) -> decltype(auto)
   {
      return to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...);
   }

private:
   using base = detail::non_owning_dyn_trait_impl<Trait, Opt.is_const, Opt.store_vtable_inline>;

   using tuple_func_ptrs
      = [:std::meta::substitute(^^detail::tuple, detail::get_members_and_tuple_type(^^Trait, false).second):];

   std::conditional_t<Opt.is_const, const void*, void*> data_;
   std::conditional_t<Opt.store_vtable_inline, tuple_func_ptrs, std::add_pointer_t<std::add_const_t<tuple_func_ptrs>>>
      funcs_;

   // clang-format off
   constexpr auto data() noexcept -> void*
      requires(!Opt.is_const)
   {
      return this->data_;
   }

   constexpr auto data() const noexcept -> const void*
   {
      return this->data_;
   }
   // clang-format on

   template<typename ToStore>
   static constexpr auto gen_funcs() noexcept
   {
      if constexpr (Opt.store_vtable_inline) {
         return detail::make_dyn_trait_pointers<Trait, ToStore, false>();
      }
      else {
         return detail::define_static_object(detail::make_dyn_trait_pointers<Trait, ToStore, false>());
      }
   };
};

// This is marked final because of the way storage is done
template<typename Trait, owning_dyn_options Opt = {}>
struct owning_dyn_trait trivially_relocatable_if_eligible replaceable_if_eligible final
   : detail::owning_dyn_trait_impl<Trait, Opt> {
   template<typename TraitClass, auto... Rest>
   friend struct detail::func_caller;

   template<bool InlineVtable, typename Class, std::size_t I>
   friend constexpr auto detail::get_vtable(auto* c) noexcept -> auto&;

   owning_dyn_trait() = delete;
   // Disallow copying (for now?)
   owning_dyn_trait(const owning_dyn_trait&) = delete;
   owning_dyn_trait& operator=(const owning_dyn_trait&) = delete;

   // Allow moving for heap allocated data
   owning_dyn_trait(owning_dyn_trait&&)
      requires(Opt.stack_size == 0)
   = default;
   owning_dyn_trait& operator=(owning_dyn_trait&&)
      requires(Opt.stack_size == 0)
   = default;

   // Disable moving for stack allocated things
   owning_dyn_trait(owning_dyn_trait&&)
      requires(Opt.stack_size > 0)
   = delete;
   owning_dyn_trait& operator=(owning_dyn_trait&&)
      requires(Opt.stack_size > 0)
   = delete;

   // TODO: Add a "alloc never throws" option
   template<typename ToStore>
      requires(sizeof(ToStore) <= Opt.stack_size || Opt.stack_size == 0)
   constexpr owning_dyn_trait(ToStore&& obj) noexcept(
      Opt.stack_size == 0 && noexcept(new (data()) ToStore{std::forward<ToStore>(obj)}))
      : data_{gen_data<ToStore>()}, funcs_{gen_funcs<ToStore>()}
   {
      new (data()) ToStore{std::forward<ToStore>(obj)};
   }

   constexpr ~owning_dyn_trait()
   {
      if (data()) {
         if constexpr (Opt.store_vtable_inline) {
            this->funcs_.template get<0>()(this);
         }
         else {
            this->funcs_->template get<0>()(this);
         }
      }
   }

   template<auto... FuncCallerRest, typename... T>
   constexpr auto call(detail::func_caller<Trait, FuncCallerRest...> to_call, T&&... args) noexcept(
      noexcept(to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...))) -> decltype(auto)
   {
      return to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...);
   }

   template<auto... FuncCallerRest, typename... T>
   constexpr auto call(detail::func_caller<Trait, FuncCallerRest...> to_call, T&&... args) const
      noexcept(noexcept(to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...))) -> decltype(auto)
   {
      return to_call.template call<Opt.store_vtable_inline, std::remove_reference_t<decltype(*this)>>(
         this, std::forward<T>(args)...);
   }

private:
   using base = detail::owning_dyn_trait_impl<Trait, Opt>;
   using tuple_func_ptrs = detail::append_tuple_types_t<
      detail::tuple<void (*)(void*) noexcept>,
      typename[:std::meta::substitute(^^detail::tuple, detail::get_members_and_tuple_type(^^Trait, true).second):]>;

   std::conditional_t<(Opt.stack_size > 0), std::array<unsigned char, Opt.stack_size>, std::unique_ptr<unsigned char[]>>
      data_;
   std::conditional_t<Opt.store_vtable_inline, tuple_func_ptrs, std::add_pointer_t<std::add_const_t<tuple_func_ptrs>>>
      funcs_;

   constexpr auto data() noexcept -> void*
   {
      if constexpr (Opt.stack_size > 0) {
         return this->data_.data();
      }
      else {
         return this->data_.get();
      }
   }

   constexpr auto data() const noexcept -> const void*
   {
      if constexpr (Opt.stack_size > 0) {
         return this->data_.data();
      }
      else {
         return this->data_.get();
      }
   }

   template<typename ToStore>
   static constexpr auto gen_data() noexcept
   {
      if constexpr (Opt.stack_size > 0) {
         return std::array<unsigned char, Opt.stack_size>{};
      }
      else {
         return std::make_unique<unsigned char[]>(sizeof(ToStore));
      }
   };

   template<typename ToStore>
   static constexpr auto gen_funcs() noexcept
   {
      if constexpr (Opt.store_vtable_inline) {
         return detail::make_dyn_trait_pointers<Trait, ToStore, true>(
            [](void* const c) noexcept { static_cast<ToStore*>(c)->~ToStore(); });
      }
      else {
         return detail::define_static_object(
            detail::make_dyn_trait_pointers<Trait, ToStore, true>(
               [](void* const c) noexcept { static_cast<ToStore*>(c)->~ToStore(); }));
      }
   };
};

template<typename DynTrait, non_owning_dyn_options Opt = {}, typename ToStore>
constexpr auto dyn(const ToStore* ptr) noexcept
{
   return non_owning_dyn_trait<DynTrait, Opt>{ptr};
}

template<typename DynTrait, non_owning_dyn_options Opt = {.is_const = false}, typename ToStore>
constexpr auto dyn(ToStore* ptr) noexcept
{
   return non_owning_dyn_trait<DynTrait, Opt>{ptr};
}

template<typename DynTrait, owning_dyn_options Opt = {}, typename ToStore>
constexpr auto
   owning_dyn(ToStore&& to_store) noexcept(noexcept(owning_dyn_trait<DynTrait, Opt>{std::forward<ToStore>(to_store)}))
{
   return owning_dyn_trait<DynTrait, Opt>{std::forward<ToStore>(to_store)};
}

template<typename Trait, non_owning_dyn_options Opt, auto... FuncCallerRest, typename... T>
constexpr auto call(
   non_owning_dyn_trait<Trait, Opt> self,
   detail::func_caller<Trait, FuncCallerRest...> to_call,
   T&&... args) noexcept(noexcept(self.call(to_call, std::forward<T>(args)...))) -> decltype(auto)
{
   return self.call(to_call, std::forward<T>(args)...);
}

template<typename Trait, owning_dyn_options Opt, auto... FuncCallerRest, typename... T>
constexpr auto call(
   owning_dyn_trait<Trait, Opt>& self,
   detail::func_caller<Trait, FuncCallerRest...> to_call,
   T&&... args) noexcept(noexcept(self.call(to_call, std::forward<T>(args)...))) -> decltype(auto)
{
   return self.call(to_call, std::forward<T>(args)...);
}
template<typename Trait, owning_dyn_options Opt, auto... FuncCallerRest, typename... T>
constexpr auto call(
   const owning_dyn_trait<Trait, Opt>& self,
   detail::func_caller<Trait, FuncCallerRest...> to_call,
   T&&... args) noexcept(noexcept(self.call(to_call, std::forward<T>(args)...))) -> decltype(auto)
{
   return self.call(to_call, std::forward<T>(args)...);
}

} // namespace khct

#endif // CPP_DYN_HPP
