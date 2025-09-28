# cpp-dyn
A C++26 library for Rust-like dyn traits

This library depends on reflection, which is currently only implemented
[here](https://github.com/bloomberg/clang-p2996), so it is not ready for
"real" use yet.

## Basic Usage

Defining an interface:

```cpp
struct my_interface {
   int get_data() const noexcept;
   void set_data(int);
};
```

Defining a class that implements that interface:

```cpp
struct my_struct {
   int get_data() const noexcept { return data_; }
   void set_data(int new_data) noexcept { data_ = new_data; }

private:
   int data_ = 1;
};
```

Using that interface:

```cpp
int take_interface(khct::non_owning_dyn_trait<my_interface> obj) noexcept
{
   obj.call(obj.set_data, 20);
   return obj.call(obj.get_data);
}

int main()
{
   my_struct s;
   assert(take_interface(khct::dyn<my_interface>(&s)) == 20);
}
```

See [here](docs/details.md) for more detailed usage.
