#include "cpp_dyn/cpp_dyn.hpp"
#include "test_common.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic functionality", "[basic]")
{
   const auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.stack_size = 8}>(cow{});
   REQUIRE(khct::call(trait, trait.volume) == 1);

   auto trait2 = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true}>(dog{});
   khct::call(trait2, trait2.get_louder);
   auto trait3 = std::move(trait2);
   REQUIRE(khct::call(trait3, trait3.volume) == 18);

   cow cow2{};
   const auto trait4 = khct::dyn<noise_trait>(&cow2);
   const auto trait5
      = khct::dyn<noise_trait, khct::non_owning_dyn_options{.is_const = false, .store_vtable_inline = true}>(&cow2);
   khct::call(trait4, trait4.get_louder);
   REQUIRE(khct::call(trait4, trait4.volume, 1) == 2);
   REQUIRE(khct::call(trait5, trait5.get_secondary_noise) == "(none)");
   khct::call(trait5, trait5.get_louder_twice);
   REQUIRE(khct::call(trait5, trait5.volume, 1) == 4);
}

TEST_CASE("Benchmark basic", "[!benchmark]")
{
   BENCHMARK("Stack allocated, external vtable")
   {
      auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.stack_size = 8}>(cow{});
      trait.call(trait.get_louder);
      return trait.call(trait.volume);
   };

   BENCHMARK("Stack allocated, inline vtable")
   {
      auto trait
         = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true, .stack_size = 8}>(cow{});
      trait.call(trait.get_louder);
      return trait.call(trait.volume);
   };

   BENCHMARK("Heap allocated, external vtable")
   {
      auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = false}>(cow{});
      trait.call(trait.get_louder);
      return trait.call(trait.volume);
   };

   BENCHMARK("Heap allocated, inline vtable")
   {
      auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true}>(cow{});
      trait.call(trait.get_louder);
      return trait.call(trait.volume);
   };
}
