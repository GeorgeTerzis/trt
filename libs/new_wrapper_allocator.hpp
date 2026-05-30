#pragma once
#include <utility>

struct new_allocator {
  template <typename T, typename... Args>
  T* alloc(Args&&... args) {
    // allocate with new and forward arguments
    return new T(std::forward<Args>(args)...);
  }

  // optional: deallocate if needed
  template <typename T>
  void dealloc(T* ptr) {
    delete ptr;
  }
};
