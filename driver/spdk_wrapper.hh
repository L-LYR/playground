#include <tuple>

template <typename T>
struct Signature {};

template <typename R, typename... As>
struct Signature<R(As...)> {
  const static std::size_t NArgs = sizeof...(As);
  using ReturnType = R;

  template <std::size_t n>
  struct Arg {
    using Type = std::tuple_element_t<n, std::tuple<As...>>;
  };
};

auto func(int, double) -> void;

static_assert(std::is_same_v<Signature<decltype(func)>::ReturnType, void>, "?");
static_assert(Signature<decltype(func)>::NArgs == 2, "?");
static_assert(std::is_same_v<Signature<decltype(func)>::Arg<0>::Type, int>,
              "?");