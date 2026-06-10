#pragma once

#include <cstdint>
#include <string_view>

namespace mutability {
    enum internal_e : std::int8_t { CONSTANT = 0, IMMUTABLE = 1, MUTABLE = 2, NONE = 3 };

    struct t {
        mutability::internal_e value;

        constexpr t() noexcept : value(NONE) {}
        constexpr t(const mutability::internal_e v) noexcept : value(v) {}
    };

    [[nodiscard]] constexpr bool has(const t v) {
        return v.value != NONE;
    }
    [[nodiscard]] constexpr bool is_none(const t v) {
        return v.value == NONE;
    }
    [[nodiscard]] constexpr bool is_mut(const t v) {
        return v.value == MUTABLE;
    }
    [[nodiscard]] constexpr bool is_imut(const t v) {
        return v.value == IMMUTABLE;
    }
    [[nodiscard]] constexpr bool is_const(const t v) {
        return v.value == CONSTANT;
    }

    [[nodiscard]] constexpr t ifnone(const t v, const t then) {
        if (is_none(v))
            return then;
        return v;
    }

    [[nodiscard]] constexpr bool eq(const t lhs, const t rhs) {
        if (lhs.value == rhs.value)
            return true;

        const t L = ifnone(lhs, rhs);
        const t R = ifnone(rhs, lhs);

        return L.value == R.value;
    }

    [[nodiscard]] constexpr static std::string_view str(const t val) {
        switch (val.value) {
        case NONE:
            return "none";
        case CONSTANT:
            return "constant";
        case IMMUTABLE:
            return "immutable";
        case MUTABLE:
            return "mutable";
        }
    }

    [[nodiscard]] constexpr static mutability::t constant() {
        return CONSTANT;
    }
    [[nodiscard]] constexpr static mutability::t mut() {
        return MUTABLE;
    }
    [[nodiscard]] constexpr static mutability::t imut() {
        return IMMUTABLE;
    }
    [[nodiscard]] constexpr static mutability::t none() {
        return NONE;
    }
}; // namespace mutability
