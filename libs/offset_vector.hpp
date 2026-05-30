#include "uncopiable.hpp"
#include <cstdint>
#include <optional>
#include <vector>

template <typename T> struct offset_vector : uncopyable {
  std::vector<T> data;
  offset_vector() = default;

  void allocate(std::size_t capacity) { data.reserve(capacity); }
  std::uint32_t size() const { return static_cast<std::uint32_t>(data.size()); }
  std::uint32_t capacity() const {
    return static_cast<std::uint32_t>(data.capacity());
  }
  [[nodiscard]] std::uint32_t push_back(const T &value) {
    data.push_back(value);
    return static_cast<std::uint32_t>(data.size() - 1);
  }
  [[nodiscard]] std::uint32_t push_back(T &&value) {
    data.push_back(std::move(value));
    return static_cast<std::uint32_t>(data.size() - 1);
  }

  template <typename... Args>
  [[nodiscard]] std::uint32_t emplace_back(Args &&...args) {
    data.emplace_back(std::forward<Args>(args)...);
    return static_cast<std::uint32_t>(data.size() - 1);
  }

  template <typename... Args>
  [[nodiscard]] std::uint32_t alloc(Args &&...args) {
    return emplace(std::forward<Args>(args)...);
  }
  std::size_t allocated_size() const { return data.size() * sizeof(T); }

  T &operator[](std::uint32_t offset) { return data[offset]; }
  const T &operator[](std::uint32_t offset) const { return data[offset]; }
};

template <typename T> struct index_t {

  struct proxy {
    T *data = nullptr;
    std::uint32_t offset = UINT32_MAX;

    proxy() {}
    proxy(std::nullptr_t) {}
    proxy(T *d, std::uint32_t o) : data{d}, offset{o} {}

    void *as_void() { return data; }
    uintptr_t as_uint() const { return (uintptr_t)data; }

    T &operator*() { return *data; }
    const T &operator*() const { return *data; }

    T *operator->() { return data; }
    const T *operator->() const { return data; }

    bool is_null() const { return data != nullptr; }
    explicit operator bool() const { return is_null(); }

    operator index_t<T>() { return index_t{offset}; }
  };

  index_t(std::uint32_t o) : offset(o) {}
  index_t() : offset(UINT32_MAX) {}
  index_t(std::nullptr_t) : offset(UINT32_MAX) {}

  operator bool() { return offset != UINT32_MAX; }

  proxy ptr(offset_vector<T> &v) { return {&v[offset], offset}; }
  const proxy ptr(const offset_vector<T> &v) const {
    return {&v[offset], offset};
  }

// private:
  std::uint32_t offset;
};

template <typename T> constexpr std::uintptr_t type_index() {
  return reinterpret_cast<std::uintptr_t>(&type_index<T>);
}
