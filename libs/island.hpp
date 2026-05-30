#pragma once

#include "uncopiable.hpp"
#include <new>
#include <optional>
#include <span>

template <typename T> struct island_t: uncopyable   {
  using element = T;
  T *_begin = nullptr;
  T *_end = nullptr;
  T *_cursor = nullptr;

  island_t() = default;

  island_t(island_t &&other) noexcept
      : _begin(other._begin), _end(other._end), _cursor(other._cursor) {
    other._begin = other._end = other._cursor = nullptr;
  }

  island_t &operator=(island_t &&other) noexcept {
    if (this != &other) {
      deallocate(); // free current resources
      _begin = other._begin;
      _end = other._end;
      _cursor = other._cursor;
      other._begin = other._end = other._cursor = nullptr;
    }
    return *this;
  }

  void allocate(std::size_t capacity) {
    _begin = static_cast<T *>(::operator new[](capacity * sizeof(T)));
    _end = _begin + capacity;
    _cursor = _begin;
  }

  void deallocate() {
    clear();
    ::operator delete[](_begin);
    _begin = _end = _cursor = nullptr;
  }

  void clear() {
    for (T *ptr = _begin; ptr < _cursor; ++ptr)
      ptr->~T();
    _cursor = _begin;
  }

  std::optional<T *> push_back(const T &value) {
    if (_cursor == _end)
      return std::nullopt;
    T *location = _cursor;
    new (_cursor++) T(value);
    return location;
  }

  std::optional<T *> push_back(T &&value) {
    if (_cursor == _end)
      return std::nullopt;
    T *location = _cursor;
    new (_cursor++) T(std::move(value));
    return location;
  }

  template <typename... Args> std::optional<T *> emplace_back(Args &&...args) {
    if (_cursor == _end)
      return std::nullopt;
    T *location = _cursor;
    new (_cursor++) T(std::forward<Args>(args)...);
    return location;
  }

  std::size_t size() const {
    return static_cast<std::size_t>(_cursor - _begin);
  }
  std::size_t capacity() const {
    return static_cast<std::size_t>(_end - _begin);
  }

  std::size_t allocated_size() const {
    return static_cast<std::size_t>(reinterpret_cast<std::byte *>(_cursor) -
                                    reinterpret_cast<std::byte *>(_begin));
  }

  T *begin_ptr() { return _begin; }
  T *end_ptr() { return _cursor; }

  const T *begin_ptr() const { return _begin; }
  const T *end_ptr() const { return _cursor; }

  T &operator[](std::size_t index) { return _begin[index]; }
  const T &operator[](std::size_t index) const { return _begin[index]; }

  T *begin() { return begin_ptr(); }
  T *end() { return end_ptr(); }

  const T *begin() const { return begin_ptr(); }
  const T *end() const { return end_ptr(); }

  std::span<T> span() { return std::span<T>(_begin, size()); }
  std::span<const T> span() const { return std::span<const T>(_begin, size()); }
};
