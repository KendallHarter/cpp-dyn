#include "cpp_dyn/cpp_dyn.hpp"
#include "test_common.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic functionality", "[basic]")
{
   const auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.stack_size = 8}>(cow{});
   REQUIRE(khct::call(trait, trait.volume) == 1);

   auto trait2 = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true}>(dog{});
   khct::call(trait2, trait2.get_louder);
   auto trait3 = std::move(trait2);
   REQUIRE(khct::call(trait3, trait3.volume) == 18);
}
