#pragma once

#include "../libs/meta.hpp"
#include "ref.hpp"
#include <boost/mp11/algorithm.hpp>
#include <format>
#include <type_traits>
#include <utility>
#include <variant>

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;

    using arg_types =
        mp::mp_remove<mp::mp_list<std::remove_cvref_t<first_arg_t<Ts>>...>,
                      void>;
};

template <typename Variant, typename... Visitors>
auto ovisit(Variant&& variant, Visitors&&... visitors) {
    return std::visit(overloaded{std::forward<Visitors>(visitors)...},
                      std::forward<Variant>(variant));
}
template <typename Variant, typename... Visitors>
auto ovisit(const Variant&& variant, Visitors&&... visitors) {
    return std::visit(overloaded{std::forward<Visitors>(visitors)...},
                      std::forward<Variant>(variant));
}
template <typename VariantType, typename T, std::size_t index = 0>
constexpr std::size_t variant_index() {
    static_assert(std::variant_size_v<VariantType> > index,
                  "Type not found in variant");
    if constexpr (index == std::variant_size_v<VariantType>) {
        return index;
    } else if constexpr (std::is_same_v<
                             std::variant_alternative_t<index, VariantType>,
                             T>) {
        return index;
    } else {
        return variant_index<VariantType, T, index + 1>();
    }
}
template <typename... Ts>
struct variants {
    using _types = mp::mp_list<std::monostate, Ts...>;
    using base = mp::mp_rename<_types, std::variant>;
    struct t : base {
        using types = _types;
        using parent = base;

        static constexpr std::string metadata = "variant";

        using base::base;

        auto& get_base() { return *this; }
        const auto& get_base() const { return *this; }

        auto type_str() const {
            return ovisit(*this, [](const auto& val) {
                return ::type_str(val);
            });
        }

        template <typename Input>
        static consteval void rules() {
            static_assert(can_contain<Input>(),
                          "Input does not belong to the type list");
        }

      private:
        template <typename T>
        T& expect_impl() {
            if (!std::holds_alternative<T>(*this)) {
                const auto expected_type_str = ::type_str<T>();
                throw std::runtime_error(
                    std::format("Expected variant type '{}' but found '{}'",
                                expected_type_str,
                                this->type_str()));
            }
            return std::get<T>(*this);
        }

      public:
        template <typename T>
        static consteval auto can_contain() {
            return mp::mp_contains<_types, T>::value;
        }

        template <typename T>
        T& expect() {
            return expect_impl<T>();
        }
        template <typename T>
        const T& expect() const {
            return expect_impl<T>();
        }

        template <typename T>
        ref<T> get_if() {
            rules<T>();
            return std::get_if<T>(this);
        }
        template <typename T>
        ref<const T> get_if() const {
            rules<T>();
            return std::get_if<T>(this);
        }
        operator base&() { return *this; }
        operator const base&() const { return *this; }

        template <typename T>
        bool has() const {
            rules<T>();
            return std::holds_alternative<T>(*this);
        }

        template <typename T>
        T& unsafe_get() {
            rules<T>();
            return std::get<T>(get_base());
        }
        template <typename T>
        const T& unsafe_get() const {
            rules<T>();
            return std::get<T>(get_base());
        }
        template <typename T>
        T& get() {
            rules<T>();
            return expect<T>();
        }
        template <typename T>
        const T& get() const {
            rules<T>();
            return expect<T>();
        }
    };
};

template <template <typename...> class Category, typename... CategoryTs>
bool internal_belongs_to_category(Category<CategoryTs...>, const auto& var) {
    return ((var.template has<CategoryTs>()) || ...);
}

template <typename T>
bool belongs_to_category(const auto& var) {
    return internal_belongs_to_category(T{}, var);
}

template <typename VariantT, typename OverloadT>
auto visit(VariantT&& var, OverloadT&& visitors) {
    using raw_variant_set = typename std::remove_cvref_t<VariantT>::types;
    using raw_visitor_types =
        typename std::remove_cvref_t<OverloadT>::arg_types;

    using variant_set = mp::mp_transform<std::remove_cvref_t, raw_variant_set>;
    using visitor_explicit =
        mp::mp_transform<std::remove_cvref_t, raw_visitor_types>;

    static_assert(
        mp::mp_all_of_q<
            visitor_explicit,
            mp::mp_bind_front<mp::mp_set_contains, variant_set>>::value,
        "Explicit visitors must correspond to Variant types");

    return std::visit(std::forward<OverloadT>(visitors),
                      std::forward<VariantT>(var));
}

template <typename VariantT, typename... OverloadTs>
auto visit(VariantT&& var, OverloadTs&&... visitors) {
    return visit(std::forward<VariantT>(var),
                 overloaded{std::forward<OverloadTs>(visitors)...});
}
