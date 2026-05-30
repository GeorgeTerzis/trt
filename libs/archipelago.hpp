#pragma once

#include "./island.hpp"
#include "uncopiable.hpp"
#include <cstddef>
#include <cstdlib>
#include <vector>

template <typename T>
struct archipelago : uncopyable {
  using island_type = island_t<T>;

  std::size_t initial_capacity = 128;

  std::vector<island_type> islands;

  archipelago(std::size_t i_c = 128) : initial_capacity(i_c) {}

  ~archipelago() {
    for (auto& island : islands) {
      island.deallocate();
    }
  }

  std::optional<T*> push_back(const T& value) {
    return try_emplace_back([&](island_type& island) {
      return island.emplace_back(value);
    });
  }

  std::optional<T*> push_back(T&& value) {
    return try_emplace_back([&](island_type& island) {
      return island.emplace_back(std::move(value));
    });
  }

  template <typename... Args>
  std::optional<T*> emplace_back(Args&&... args) {
    return try_emplace_back([&](island_type& island) {
      return island.emplace_back(std::forward<Args>(args)...);
    });
  }

  std::size_t size() const {
    std::size_t total = 0;
    for (const auto& island : islands)
      total += island.size();
    return total;
  }
  std::size_t allocated_size() const {
    std::size_t total = 0;
    for (const auto& island : islands)
      total += island.allocated_size();
    return total;
  }

  std::span<island_t<T>> span() {
    return std::span{islands.begin(), islands.end()};
  }
  std::span<const island_t<T>> span() const {
    return std::span{islands.begin(), islands.end()};
  }
  std::vector<std::span<T>> spans() {
    std::vector<std::span<T>> result;
    for (auto& island : islands)
      result.emplace_back(island.span());
    return result;
  }

  std::vector<std::span<const T>> spans() const {
    std::vector<std::span<const T>> result;
    for (const auto& island : islands)
      result.emplae_back(island.span());
    return result;
  }
  archipelago(archipelago&&) = default;
  archipelago& operator=(archipelago&&) = default;

private:
  template <typename F>
  std::optional<T*> try_emplace_back(F&& try_insert) {
    if (!islands.empty()) {
      if (auto result = try_insert(islands.back()))
        return result;
    }
    const std::size_t new_capacity =
        islands.empty() ? initial_capacity : islands.back().capacity() * 2;

    island_type new_island;
    new_island.allocate(new_capacity);

    auto result = try_insert(new_island);
    if (!result.has_value())
      std::exit(EXIT_FAILURE);

    islands.emplace_back(std::move(new_island));
    return result;
  }
};
