import cpp_dyn;

#include "test_common.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic functionality", "[basic]")
{
   const auto trait = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.stack_size = 8}>(cow{});
   REQUIRE(khct::call(trait, trait.volume) == 1);

   auto trait2 = khct::owning_dyn<noise_trait, khct::owning_dyn_options{.store_vtable_inline = true}>(dog{});
   trait2.call(trait2.get_louder);
   auto trait3 = std::move(trait2);
   REQUIRE(trait3.call(trait3.volume) == 18);

   cow cow2{};
   auto trait4 = khct::dyn<noise_trait>(&cow2);
   auto trait5 = khct::dyn<noise_trait, khct::non_owning_dyn_options{.store_vtable_inline = true}>(&cow2);
   trait4.call(trait4.get_louder);
   REQUIRE(trait4.call(trait4.volume, 1) == 2);
   REQUIRE(trait5.call(trait5.get_secondary_noise) == "(none)");
   trait5.call(trait5.get_louder_twice);
   REQUIRE(trait5.call(trait5.volume, 1) == 4);
}

struct[[= khct::trait]] my_interface {
   int get_data() const noexcept;
   void set_data(int);
};

struct[[= khct::impl_for<my_interface>]] my_struct {
   int get_data() const noexcept { return data_; }
   void set_data(int new_data) noexcept { data_ = new_data; }

private:
   int data_ = 1;
   std::vector<int> make_non_trivial_destructor_{1, 2, 3, 4};
};

template<khct::non_owning_dyn_options Opt>
int take_interface(khct::non_owning_dyn_trait<my_interface, Opt> obj) noexcept
{
   obj.call(obj.set_data, 20);
   return obj.call(obj.get_data);
}

int take_interface2(khct::owning_dyn_trait<my_interface> obj) noexcept
{
   obj.call(obj.set_data, 40);
   return obj.call(obj.get_data);
}

TEST_CASE("Examples", "[example]")
{
   my_struct s;
   REQUIRE(take_interface(khct::dyn<my_interface>(&s)) == 20);
   REQUIRE(take_interface2(khct::owning_dyn<my_interface>(s)) == 40);
}
