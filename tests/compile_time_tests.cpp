#include "cpp_dyn/cpp_dyn.hpp"

#include <cassert>
#include <string_view>
#include <type_traits>

struct noise_trait {
   static std::string_view get_noise() noexcept;

   [[= khct::default_impl]] static constexpr std::string_view get_secondary_noise() noexcept { return "(none)"; }

   template<typename T>
   [[= khct::default_impl]] constexpr int volume(const T& obj) const noexcept
   {
      return obj.volume(1);
   }

   int volume(int) const noexcept;
   void get_louder() noexcept;

   template<typename T>
   [[= khct::default_impl]] constexpr void get_louder_twice(T& obj) noexcept
   {
      obj.get_louder();
      obj.get_louder();
   }
};

struct cow {
   static constexpr std::string_view get_noise() noexcept { return "moo"; }
   constexpr int volume(int multiplier) const noexcept { return volume_ * multiplier; }
   constexpr void get_louder() noexcept { volume_ += 1; }

   int volume_ = 1;
};

struct dog {
   static constexpr std::string_view get_noise() noexcept { return "arf"; }
   static constexpr std::string_view get_secondary_noise() noexcept { return "bark"; }
   constexpr int volume(int multiplier) const noexcept { return volume_ * multiplier; }
   constexpr void get_louder() noexcept { volume_ *= 2; }

   int volume_ = 9;
};

static constexpr cow c{};
static constexpr dog d{};

static constexpr auto owner = khct::dyn<noise_trait>(&c);
static_assert(khct::call(owner, owner.get_noise) == "moo");
static_assert(noexcept(khct::call(owner, owner.get_noise)));

// One pointer to the vtable, one pointer to data
static_assert(sizeof(owner) == sizeof(void*) * 2);

static constexpr auto owner2 = khct::dyn<noise_trait, khct::non_owning_dyn_options{.store_vtable_inline = true}>(&d);
static_assert(khct::call(owner2, owner2.volume) == 9);
static_assert(khct::call(owner2, owner2.volume, 2) == 18);
static_assert(khct::call(owner2, owner2.get_secondary_noise) == "bark");

// One pointer to the data, 6 pointers to methods
static_assert(sizeof(owner2) == sizeof(void*) + sizeof(void*) * 6);

consteval
{
   cow cow2{};
   const auto trait = khct::dyn<noise_trait>(&cow2);
   const auto trait2 = khct::dyn<noise_trait, khct::non_owning_dyn_options{.store_vtable_inline = true}>(&cow2);
   khct::call(trait, trait.get_louder);
   assert(khct::call(trait, trait.volume, 1) == 2);
   assert(khct::call(trait2, trait.get_secondary_noise) == "(none)");
   khct::call(trait, trait.get_louder_twice);
   assert(khct::call(trait2, trait.volume, 1) == 4);
}

// Verify some traits (these checking traits are missing right now)
// static_assert(std::is_trivially_relocatable_v<khct::non_owning_dyn_trait<noise_trait>> &&
// std::is_replacable_v<khct::non_owning_dyn_trait<noise_trait>>);
// static_assert(std::is_trivially_relocatable_v<khct::owning_dyn_trait<noise_trait>> &&
// std::is_replacable_v<khct::owning_dyn_trait<noise_trait>>);

int main()
{
   // TODO: Move this to basic_tests.cpp
   const auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.stack_size = 8}>(cow{});
   assert(khct::call(trait, trait.volume) == 1);

   auto trait2 = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true}>(dog{});
   khct::call(trait2, trait2.get_louder);
   auto trait3 = std::move(trait2);
   assert(khct::call(trait3, trait3.volume) == 18);
}
