## Dyn Trait Structs

There are two different primary dyn trait structs, owning and non-owning:
```cpp
template<typename Trait, non_owning_dyn_options Opt = {}>
struct khct::non_owning_dyn_trait;

template<typename Trait, owning_dyn_options Opt = {}>
struct khct::owning_dyn_trait;
```

The `non_owning_dyn_options` and `owning_dyn_options` are described [here](#dyn-trait-struct-options).

There are two overload sets for creating instances of these structs
(with noexcept specifiers omitted):

```cpp
template<typename DynTrait, non_owning_dyn_options Opt = {}, typename ToStore>
constexpr auto khct::dyn(const ToStore* ptr) noexcept;

template<typename DynTrait, non_owning_dyn_options Opt = {}, typename ToStore>
constexpr auto khct::dyn(ToStore* ptr) noexcept;

template<typename DynTrait, owning_dyn_options Opt = {}, typename ToStore>
constexpr auto khct::owning_dyn(ToStore&& to_store);
```

`khct::dyn` is used to create non-owning dyn traits and `khct::owning_dyn` is used to create
owning dyn traits.

These are used as such (assuming `my_trait` is a trait and `my_obj` is a value that conforms to it):

```cpp
// Create a non-owning trait
const auto t1 = khct::dyn<my_trait>(&my_obj);

// Create a non-owning trait with const semantics
const auto t2 = khct::dyn<const my_trait>(&my_obj);

// Create an owning trait that copies from my_obj
// Note that a const template parameter is not allowed here
const auto t3 = khct::owning_dyn<my_trait>(my_obj);
```

### Defining Traits and Implementations

There are two ways to define a trait, either with the `khct::trait` annotation
or the `khct::auto_trait` annotation.  If a trait is marked with `khct::trait`, any structures
which implement the trait must be marked as such using `khct::impl_for`.  If a trait is defined
using `khct::auto_trait`, any structures that implement the interface can be used.

Below is a basic example of using `khct::trait`, `khct::auto_trait`, and `khct::impl_for`:

```cpp
#include "cpp_dyn/cpp_dyn.hpp"
#include <cassert>

// Defining an interface
struct [[=khct::trait]] my_interface {
   int get_data() const noexcept;
   void set_data(int) noexcept;
};

// Defining a class that implements the first interface
struct [[=khct::impl_for<my_interface>]] my_struct {
   int get_data() const noexcept { return data_; }
   void set_data(int new_data) noexcept { data_ = new_data; }

private:
   int data_ = 1;
};

// An automatic trait
struct [[=khct::auto_trait]] interface2 {
   int get_data() const noexcept;
   void set_data(int) noexcept;
};

// Using the first interface
int take_interface1(khct::non_owning_dyn_trait<my_interface> obj) noexcept
{
   obj.call(obj.set_data, 20);
   return obj.call(obj.get_data);
}

// Using the second interface
int take_interface2(khct::non_owning_dyn_trait<interface2> obj) noexcept
{
   obj.call(obj.set_data, 40);
   return obj.call(obj.get_data);
}

int main()
{
   my_struct s;
   assert(take_interface(khct::dyn<my_interface>(&s)) == 20);
   assert(take_interface(khct::dyn<interface2>(&s)) == 40);
}
```

Default implementations can also be provided as follows:

```cpp
struct [[=khct::trait]] my_interface {
   // Default implementation for a static function
   [[=khct::default_impl]] static void do_nothing() noexcept {}

   // Default implementation for a member function
   // Note that it must be a template and that the const-ness
   // of the method and object must match
   template<typename T>
      [[=khct::default_impl]]
   const void* (const T& obj) get_addr() const noexcept { return &obj; }
};
```

### Dyn Trait Struct Options

```cpp
namespace khct {

struct non_owning_dyn_options {
   // If the vtable should be stored directly in the object
   // (if true) or if the object should store a pointer to it
   bool store_vtable_inline = false;
}

struct owning_dyn_options {
   // If the vtable should be stored directly in the object
   // (if true) or if the object should store a pointer to it
   bool store_vtable_inline = false;

   // The number of bytes to store objects into.
   // If this is 0, dynamically allocate objects
   // instead of locally storing them.
   std::size_t stack_size = 0;
}

}
```
