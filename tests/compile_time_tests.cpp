#include "cpp_dyn/cpp_dyn.hpp"
#include "test_common.hpp"

#include <cassert>

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
   const auto trait2
      = khct::dyn<noise_trait, khct::non_owning_dyn_options{.is_const = false, .store_vtable_inline = true}>(&cow2);
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

int main() {}
