#pragma once

#include <boost/callable_traits.hpp>
#include <boost/mp11.hpp>
#include <boost/pfr.hpp>
#include <boost/pfr/core.hpp>
#include <boost/pfr/tuple_size.hpp>
#include <boost/type_index.hpp>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <variant>

template <typename LHS, typename RHS>
using cmp = std::is_same<LHS, RHS>;

template <typename LHS, typename... RHS>
using cmp_any = std::bool_constant<(std::is_same_v<LHS, RHS> || ...)>;

template <typename LHS, typename... RHS>
constexpr bool cmp_any_v = cmp_any<LHS, RHS...>::value;

template <typename LHS, typename RHS>
constexpr bool cmp_v = std::is_same<LHS, RHS>::value;

namespace mp = boost::mp11;

template <typename List, typename... Ts>
using append = mp::mp_append<List, mp::mp_list<Ts...>>;

template <typename... Ts>
using type_list = mp::mp_list<Ts...>;

template <typename K, typename V>
using pair = mp::mp_list<K, V>;

template <typename Keys, typename Values>
using concat_lists = mp::mp_append<Keys, Values>;

template <typename Keys, typename Values>
using map_type_list = mp::mp_transform<pair, Keys, Values>;

template <typename T>
using Some = mp::mp_list<std::true_type, T>;
using None = mp::mp_list<std::false_type, void>;

template <typename Lookup, typename Mappings>
using query_maped_list = std::conditional_t<
    mp::mp_map_contains<Mappings, Lookup>::value,
    Some<typename mp::mp_second<mp::mp_map_find<Mappings, Lookup>>>,
    None>;

template <template <typename> class T, typename TList>
using apply2list = mp::mp_transform<T, TList>;

template <typename Lookup, typename TList>
using is_in_list = mp::mp_contains<TList, Lookup>;

namespace runtime {
template <typename T, typename... Ts>
constexpr bool is_in_list(std::type_index index, type_list<Ts...>) {
    return ((index == typeid(Ts)) || ...);
}

template <typename T, typename List>
constexpr bool is_in_list(T&&) {
    return is_in_list<T>(typeid(T), List{});
}

} // namespace runtime

template <typename Lookup, typename Tlist>
struct all_have_inherited {};
template <typename Lookup, typename Cursor, typename... Ts>
struct all_have_inherited<Lookup, type_list<Cursor, Ts...>> {
    using type = std::conditional<
        std::is_base_of_v<Lookup, Cursor>,
        typename all_have_inherited<Lookup, type_list<Ts...>>::type,
        std::false_type>::type;
};

template <typename Lookup>
struct all_have_inherited<Lookup, type_list<>> {
    using type = std::true_type;
};

template <typename F, typename = void>
struct first_arg {
    using type = void;
};

template <typename F>
struct first_arg<F, std::void_t<boost::callable_traits::args_t<F>>> {
    // args_t<F> is an mp_list<Args...>
    using type = mp::mp_first<boost::callable_traits::args_t<F>>;
};

template <typename F>
using first_arg_t = typename first_arg<F>::type;

template <typename T>
std::string type_str() {
    return boost::typeindex::type_id<T>().pretty_name();
}
template <typename T>
std::string type_str(T) {
    return type_str<T>();
}

//doesn't really belong with the other """"functions"""" but fits the file name
template <class F, class T, std::size_t... I>
void for_each_member_impl(T& s, std::index_sequence<I...>, F&& f) {
    (f.template operator()(boost::pfr::get<I>(s)), ...);
}

template <class F, class T>
void for_each_member(T& s, F&& f) {
    constexpr std::size_t N = boost::pfr::tuple_size_v<T>;
    for_each_member_impl(s, std::make_index_sequence<N>{}, std::forward<F>(f));
}

template <typename Tuple>
struct tuple_to_typelist;

template <typename... Ts>
struct tuple_to_typelist<std::tuple<Ts...>> {
    using type = boost::mp11::mp_list<Ts...>;
};

template <typename Val>
void for_each_struct_mem(Val& v) {
    using tup_type =
        std::remove_cvref_t<decltype(boost::pfr::structure_to_tuple(v))>;
    using types = tuple_to_typelist<tup_type>::type;
}

template <typename, typename = void>
struct has_metadata : std::false_type {
    static constexpr std::string value = "";
};

template <typename T>
struct has_metadata<T, std::void_t<decltype(T::metadata)>> : std::true_type {
    static constexpr std::string value = T::metadata;
};

template <typename T>
inline constexpr std::string has_metadata_v = has_metadata<T>::value;

template <typename T, typename Tuple>
struct tup_index_of;

// Specialization for non-empty tuple
template <typename T, typename First, typename... Rest>
struct tup_index_of<T, std::tuple<First, Rest...>> {
    static constexpr std::size_t value =
        std::is_same<T, First>::value
            ? 0
            : 1 + tup_index_of<T, std::tuple<Rest...>>::value;
};

// Base case: type not found
template <typename T>
struct tup_index_of<T, std::tuple<>> {
    static_assert(sizeof(T) == 0, "Type not found in tuple");
};

template <typename T>
consteval bool is_monostate_type() {
    return std::is_same_v<std::remove_cvref_t<T>, std::monostate>;
}

template <typename T>
consteval bool is_monostate_value(const T&) {
    return std::is_same_v<std::remove_cvref_t<T>, std::monostate>;
}

template <typename LHS, typename RHS>
consteval bool is_same_type(const LHS&, const RHS&) {
    using L = std::decay_t<std::remove_cvref_t<LHS>>;
    using R = std::decay_t<std::remove_cvref_t<RHS>>;
    return std::is_same_v<L, R>;
}

template <bool Cond, typename True, typename False>
using cond = std::conditional<Cond, True, False>;

template <bool Cond, typename True, typename False>
using cond_t = std::conditional_t<Cond, True, False>;
