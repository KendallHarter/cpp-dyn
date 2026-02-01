#include "khct/cpp_dyn.hpp"
#include "test_common.hpp"

#include <cassert>

static constexpr cow c{};
static constexpr dog d{};

static constexpr auto owner = khct::dyn<const noise_trait>(&c);
static_assert(owner.call(owner.get_noise) == "moo");
static_assert(noexcept(owner.call(owner.get_noise)));

// One pointer to the vtable, one pointer to data
static_assert(sizeof(owner) == sizeof(void*) * 2);

static constexpr auto owner2
   = khct::dyn<const noise_trait, khct::non_owning_dyn_options{.store_vtable_inline = true}>(&d);
static_assert(owner2.call(owner2.volume) == 9);
static_assert(owner2.call(owner2.volume, 2) == 18);
static_assert(owner2.call(owner2.get_secondary_noise) == "bark");

// One pointer to the data, 5 pointers to methods
static_assert(sizeof(owner2) == sizeof(void*) + sizeof(void*) * 5);

consteval
{
   cow cow2{};
   auto trait = khct::dyn<noise_trait>(&cow2);
   auto trait2 = khct::dyn<noise_trait, khct::non_owning_dyn_options{.store_vtable_inline = true}>(&cow2);
   trait.call(trait.get_louder);
   assert(trait.call(trait.volume, 1) == 2);
   assert(trait2.call(trait.get_secondary_noise) == "(none)");
   trait.call(trait.get_louder);
   assert(trait2.call(trait.volume, 1) == 3);
}

int main() {}
