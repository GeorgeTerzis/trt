#ifndef PODARRAY_T_DEF
#define PODARRAY_T_DEF

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>

/* WIP */
template <typename T>
struct podarray_t {
  T* data_;
  std::size_t len_;
  podarray_t() : data_(nullptr), len_(0) {}

  auto len() { return len_; }
  auto size() { return len_; }

  template <const std::size_t len = 0>
  constexpr static podarray_t make() {
    if constexpr (len == 0) {
      return podarray_t{nullptr, 0};
    } else {
      return podarray_t{new T[len], len};
    }
  }
  static podarray_t make(const std::size_t len) {
    return podarray_t{new T[len], len};
  }
  static podarray_t make(T* ptr, std::size_t len) {
    return podarray_t{ptr, len};
  }

  // Use a continious array derived form 2 itterators
  // it is gonna use those pointers directly
  // you can move a podlist_t like that to a podarray_t
  static podarray_t inherit(T* begin_ptr, T* end_ptr) {
    return podarray_t{begin_ptr, static_cast<size_t>(end_ptr - begin_ptr)};
  }

  template <typename... Args>
  static podarray_t make(Args&... args) {
    constexpr std::size_t len = sizeof...(Args);

    auto arg_arr = std::array<T, sizeof...(Args)>{std::move(args)...};
    podarray_t p = podarray_t::make<sizeof...(Args)>();

    std::memcpy(p.data_, arg_arr.data(), len * sizeof(T));
  }

  void push_back(std::size_t& state_index, T& val) {
    std::memcpy(this->data_ + state_index, &val, sizeof(T));
    ++state_index;
  }

  template <typename... Args>
  static podarray_t<T> make_from_opt(Args&... args) {
    std::size_t len = 0;
    for (const auto& elm : {args...})
      if (elm)
        ++len;
    auto p = podarray_t<T>::make(len);
    std::size_t i = 0;
    (
        [&](std::optional<T>& v) {
          if (v.has_value())
            p.push_back(i, std::move(v.value()));
        }(args),
        ...);
    return std::move(p);
  }

  void release() {
    if (len_ > 0) {
      this->len_ = 0;
      delete[] this->data_;
    }
  }
};

#endif
