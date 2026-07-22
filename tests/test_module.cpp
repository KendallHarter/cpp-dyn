#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>

import khct.cpp_dyn;

// Because of module weirdness with GCC(?) need to include the headers first
// but trying to include test_common.hpp means we aren't using the module or khct namespace is
// undefined
// ...so the best way to fix this is to copy/paste

struct[[= khct::auto_trait]] noise_trait {
   static std::string_view get_noise() noexcept;
   static std::string_view get_secondary_noise() noexcept;
   int volume() const noexcept;
   int volume(int) const noexcept;
   void get_louder();
};

struct cow {
   static constexpr std::string_view get_noise() noexcept { return "moo"; }
   static constexpr std::string_view get_secondary_noise() noexcept { return "(none)"; };
   constexpr int volume() const noexcept { return volume_; }
   constexpr int volume(int multiplier) const noexcept { return volume_ * multiplier; }
   constexpr void get_louder() noexcept { volume_ += 1; }

   int volume_ = 1;
};

struct dog {
   static constexpr std::string_view get_noise() noexcept { return "arf"; }
   static constexpr std::string_view get_secondary_noise() noexcept { return "bark"; }
   constexpr int volume() const noexcept { return volume_; }
   constexpr int volume(int multiplier) const noexcept { return volume_ * multiplier; }
   constexpr void get_louder() noexcept { volume_ *= 2; }

   int volume_ = 9;
};

TEST_CASE("Basic functionality", "[basic]")
{
   const auto trait = khct::owning_dyn<noise_trait, khct::owning_options{.impl_storage_size = 8}>(cow{});
   REQUIRE(trait.call(trait.volume) == 1);

   auto trait2 = khct::owning_dyn<noise_trait, khct::owning_options{.store_vtable_inline = true}>(dog{});
   trait2.call(trait2.get_louder);
   auto trait3 = std::move(trait2);
   REQUIRE(trait3.call(trait3.volume) == 18);

   cow cow2{};
   auto trait4 = khct::dyn<noise_trait>(&cow2);
   auto trait5 = khct::dyn<noise_trait, khct::non_owning_options{.store_vtable_inline = true}>(&cow2);
   trait4.call(trait4.get_louder);
   REQUIRE(trait4.call(trait4.volume, 1) == 2);
   REQUIRE(trait5.call(trait5.get_secondary_noise) == "(none)");
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

template<khct::non_owning_options Opt>
int take_interface(khct::dyn<my_interface, Opt> obj) noexcept
{
   obj.call(obj.set_data, 20);
   return obj.call(obj.get_data);
}

int take_interface2(khct::owning_dyn<my_interface> obj) noexcept
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
