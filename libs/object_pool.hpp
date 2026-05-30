#pragma once
#include "./uncopiable.hpp"
#include "./archipelago.hpp"
#include "./meta.hpp"
#include <any>
#include <boost/container/vector.hpp>

// name might be bad but it is what it is

template <typename... TYPES> struct object_pool: uncopyable {
  using types_list = type_list<TYPES...>;
  std::tuple<archipelago<TYPES>...> sub_pools;
  boost::container::vector<std::any> rest_pool;

  object_pool(const object_pool &) = delete;
  object_pool &operator=(const object_pool &) = delete;
  object_pool() = default; 
  object_pool(object_pool &&) = default;
  object_pool &operator=(object_pool &&) = default;

  template <typename T> auto &get() {
    constexpr auto T_is_in_list = is_in_list<T, types_list>::value;
    if constexpr (T_is_in_list)
      return std::get<archipelago<T>>(sub_pools);
    else
      return rest_pool;
  }
  template <typename T, typename... Args> T *alloc(Args &&...args) {
    constexpr auto T_is_in_list = is_in_list<T, types_list>::value;
    if constexpr (T_is_in_list) {
      return get<T>().emplace_back(std::forward<Args>(args)...).value();
    } else {
      std::any &any_ref =
          rest_pool.emplace_back(T(std::forward<Args>(args)...));
      T &ref = std::any_cast<T &>(any_ref);
      return &ref;
    }
  }

  std::size_t size() const {
    std::size_t total = std::apply(
        [](auto const &...pools) {
          return (std::size_t{0} + ... + pools.allocated_size());
        },
        sub_pools);

    return total;
  }

  template <typename T>
  std::size_t size_for() const {
    static_assert(is_in_list<T, types_list>::value, "Type T is not in the pool list");
    return std::get<archipelago<T>>(sub_pools).allocated_size();
  }
};
