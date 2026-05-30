#pragma once

#include "meta.hpp"
#include "overloaded.hpp"
#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/detail/mp_list.hpp>
#include <boost/mp11/function.hpp>
#include <frozen/map.h>
#include <type_traits>
#include <utility>

template <class L>
using is_set = std::is_same<L, mp::mp_unique<L>>;
template <typename T, typename List>
using indexof = mp::mp_find<List, T>;

template <typename VariantT, typename... CategoryTs>
struct category {
  using variant = VariantT;
  using variant_types = typename std::remove_cvref_t<VariantT>::types;
  using types = type_list<CategoryTs...>;

  static_assert(sizeof...(CategoryTs), "Expected Category types");
  static_assert(mp::mp_all_of_q<types,
                                mp::mp_bind_front<mp::mp_set_contains,
                                                  variant_types>>::value,
                "all Category types must be part of the Variant");
  static_assert(is_set<types>::value, "all Category types must be unique");

  using map_t = frozen::map<int, int, sizeof...(CategoryTs)>;
  static constexpr map_t index_map = {
      {indexof<CategoryTs, types>::value,
       indexof<CategoryTs, variant_types>::value}...};

  using rmap_t = frozen::map<int, int, sizeof...(CategoryTs)>;
  static constexpr map_t rindex_map = {
      {indexof<CategoryTs, variant_types>::value,
       indexof<CategoryTs, types>::value}...};
};

template <typename Category, typename ValT, typename OverloadT>
auto cvisit(ValT&& val, OverloadT overload) {
  using category_set = typename std::remove_cvref_t<Category>::types;
  using overload_explicit = typename std::remove_cvref_t<OverloadT>::arg_types;

  using cleaned_overloads = mp::mp_remove<overload_explicit, void>;
  static_assert(
      mp::mp_all_of_q<
          category_set,
          mp::mp_bind_front<mp::mp_set_contains, cleaned_overloads>>::value,
      "Explicit visitors must correspond to Variant types");

  return ::visit(std::forward<ValT>(val), std::forward<OverloadT>(overload));
}

template <typename Category, typename ValT, typename... VisitorsTs>
auto cvisit(ValT&& val, VisitorsTs&&... visitors) {
  return cvisit(std::forward<ValT>(val),
                overloaded{std::forward<VisitorsTs>(visitors)...});
}

template <typename Category>
bool belongs(const typename Category::variant& v) {
  for (auto&& kv : Category::index_map)
    if (kv.second == v.index())
      return true;
  return false;
}
