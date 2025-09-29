#ifndef TEST_COMMON_HPP
#define TEST_COMMON_HPP

#include <string_view>

struct[[= khct::auto_trait]] noise_trait {
   static std::string_view get_noise() noexcept;

   [[= khct::default_impl]] static constexpr std::string_view get_secondary_noise() noexcept { return "(none)"; }

   template<typename T>
   [[= khct::default_impl]] constexpr int volume(const T& obj) const noexcept
   {
      return obj.volume(1);
   }

   int volume(int) const noexcept;
   void get_louder();

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

#endif // TEST_COMMON_HPP
