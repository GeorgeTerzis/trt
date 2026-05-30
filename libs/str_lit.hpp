#pragma once

#include <cstddef>
#include <algorithm>

template<std::size_t N>
struct str_lit_t{
  consteval str_lit_t(const char (&str)[N]) { std::copy_n(str, N, value); }
  char value[N];
  static constexpr size_t length = N;
};
