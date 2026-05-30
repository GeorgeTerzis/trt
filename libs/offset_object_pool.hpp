#pragma once

#include "./meta.hpp"
#include "./offset_vector.hpp"
#include "./uncopiable.hpp"
#include <any>
#include <boost/container/vector.hpp>
#include <tuple>

template <typename... TYPES> struct offset_object_pool : uncopyable {
  using types_list = type_list<TYPES...>;
  std::tuple<offset_vector<TYPES>...> sub_pools;
  // boost::container::vector<std::any> rest_pool;

  offset_object_pool(const offset_object_pool &) = delete;
  offset_object_pool &operator=(const offset_object_pool &) = delete;
  offset_object_pool() = default;
  offset_object_pool(offset_object_pool &&) = default;
  offset_object_pool &operator=(offset_object_pool &&) = default;

  template <typename T> auto get(index_t<T> ref) { return ref.ptr(get<T>()); }

  template <typename T> auto &get() {
    constexpr auto T_is_in_list = is_in_list_t<T, types_list>::value;
    if constexpr (T_is_in_list)
      return std::get<offset_vector<T>>(sub_pools);
    // else
      // return rest_pool;
  }
  template <typename T, typename... Args> index_t<T> alloc(Args &&...args) {
    constexpr auto T_is_in_list = is_in_list_t<T, types_list>::value;
    auto index =
        get<T>().emplace_back(std::forward<Args>(args)...);
    return index_t<T>(index);
  }

  std::size_t size() const {
    std::size_t total = std::apply(
        [](auto const &...pools) {
          return (std::size_t{0} + ... + pools.allocated_size());
        },
        sub_pools);

    return total;
  }
};
