#pragma once

#include "../include/utf8/checked.h"
#include "../libs/become.hpp"
#include "diagnostic.cpp"
#include <cstdint>
#include <format>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSwitch.h>
#include <llvm/Support/SourceMgr.h>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "source.hpp"

struct node;
struct median;
struct final;

struct final {
    enum e {
        first,
#define TOKEN_BASE(code) code,
#include "./def"
        last,
    };
    e code;
    std::uint32_t data = 0; // IDs use it for names

    explicit final(e c) : code(c) {}
    explicit final(e c, std::uint32_t d) : code(c), data(d) {}

    operator e() const {
        return code;
    }

    bool operator==(e other) const {
        return code == other;
    }
};

struct median {
    enum e {
        final,
        FILE,
        PARENS,
        BRACES,
        CBRACES,
        BLOCK_EXPR,
        DECL,
        last,
    };

    e code;
    std::uint32_t len;
    explicit median(e c, std::uint16_t l) : code(c), len(l) {}

    inline std::span<node> children();

    inline std::span<const node> children() const;

    bool operator==(e other) const {
        return code == other;
    }
};

struct node {
    union u {
        final final;
        median median;
    };

    enum class e {
        FINAL,
        MEDIAN,
    };

    u payload;
    e tag;

    explicit node(median::e v, std::uint16_t c) :
        payload{.median = median{v, c}},
        tag{e::MEDIAN} {}
    explicit node(final::e v, uint32_t d) :
        payload{.final = final{v, d}},
        tag{e::FINAL} {}
    explicit node(final::e v) : payload{.final = final{v}}, tag{e::FINAL} {}

    explicit node(final m) : payload{.final = m}, tag{e::FINAL} {}
    explicit node(median m) : payload{.median = m}, tag{e::MEDIAN} {}

    decltype(auto) unsafe_final(this auto&& self) {
        return (self.payload.final);
    }
    decltype(auto) unsafe_median(this auto&& self) {
        return (self.payload.median);
    }

    template <typename FinalFn, typename MedianFn, typename... Args>
    decltype(auto)
    visit(this auto&& self, FinalFn&& final_arm, MedianFn&& median_arm, Args&&... args)
        requires std::is_invocable_v<FinalFn,
                                     decltype(std::forward<decltype(self)>(self)
                                                  .unsafe_final()),
                                     Args...> &&
                 std::is_invocable_v<MedianFn,
                                     decltype(std::forward<decltype(self)>(self)
                                                  .unsafe_median()),
                                     Args...>
    {
        switch (self.tag) {
        case e::FINAL:
            return std::forward<FinalFn>(final_arm)(
                std::forward<decltype(self)>(self).unsafe_final(),
                std::forward<Args>(args)...);

        case e::MEDIAN:
            return std::forward<MedianFn>(median_arm)(
                std::forward<decltype(self)>(self).unsafe_median(),
                std::forward<Args>(args)...);

        default:
            std::unreachable();
        }
    }

    template <typename T>
    decltype(auto) unsafe_get(this auto&& self) {
        if constexpr (std::is_same_v<T, final>)
            return self.unsafe_final();
        if constexpr (std::is_same_v<T, median>)
            return self.unsafe_median();
        else
            static_assert(false, "Invalid alternative");
    }

    auto as_final(this auto&& self) {
        using value_t = std::remove_reference_t<decltype(self.unsafe_final())>;

        using ref_t =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               const value_t,
                               value_t>;

        if (self.tag != e::FINAL)
            return std::optional<std::reference_wrapper<ref_t>>{};

        return std::optional<std::reference_wrapper<ref_t>>{self.unsafe_final()};
    }

    auto as_median(this auto&& self) {
        using value_t = std::remove_reference_t<decltype(self.unsafe_median())>;

        using ref_t =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>,
                               const value_t,
                               value_t>;

        if (self.tag != e::MEDIAN)
            return std::optional<std::reference_wrapper<ref_t>>{};

        return std::optional<std::reference_wrapper<ref_t>>{self.unsafe_median()};
    }

    template <typename T_>
    auto safe_get(this auto&& self) {
        using T = std::remove_cvref_t<T_>;
        static_assert(std::is_same_v<T, final> || std::is_same_v<T, median>,
                      "Invalid node_t alternative");

        if constexpr (std::is_same_v<T, final>)
            return self.as_final();
        else
            return self.as_median();
    }

    inline std::span<node> children();
    inline std::span<const node> children() const;

    bool isa(final::e v) const {
        if (tag == e::FINAL && unsafe_final() == v)
            return true;
        return false;
    }
    bool isa(median::e v) const {
        if (tag == e::MEDIAN && unsafe_median() == v)
            return true;
        return false;
    }
};

inline std::span<const node> median::children() const {
    auto n = reinterpret_cast<const node*>(this) + 1;
    return {n, n + this->len};
}

inline std::span<node> median::children() {
    auto n = reinterpret_cast<node*>(this) + 1;
    return {n, n + this->len};
}

inline std::span<const node> node::children() const {
    return this->visit(
        [](const final&) -> std::span<const node> { return {}; },
        [](const median& m) -> std::span<const node> { return m.children(); });
}

inline std::span<node> node::children() {
    return this->visit([](final&) -> std::span<node> { return {}; },
                       [](median& m) -> std::span<node> { return m.children(); });
}

template <typename ElmT>
struct cursor_base_t {
    using value_type = ElmT;
    using difference_type = std::ptrdiff_t;
    using pointer = ElmT*;
    using reference = ElmT&;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    pointer cursor;
    pointer end;

    cursor_base_t() = default;
    explicit cursor_base_t(pointer b, pointer e) : cursor{b}, end{e} {};
    explicit cursor_base_t(std::span<ElmT> span) :
        cursor{span.begin().base()},
        end{span.end().base()} {};

    decltype(auto) get(this auto&& self) {
        return *self.cursor;
    }

    bool within() const {
        return cursor < end;
    }

    reference operator*() const noexcept {
        return *cursor;
    }
    pointer operator->() const noexcept {
        return cursor;
    }

    cursor_base_t& operator++() {
        cursor = next();
        return *this;
    }

    cursor_base_t operator++(int) {
        cursor_base_t tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const cursor_base_t& other) const {
        return cursor == other.cursor;
    }
    bool operator!=(const cursor_base_t& other) const {
        return !(*this == other);
    }

    pointer next() const {
        const auto ptr = cursor;
        const auto jump_offset =
            ptr->visit([](const final&) -> difference_type { return 1; },
                       [](const median& v) -> difference_type { return v.len + 1; });
        return ptr + jump_offset;
    }
};
// using cursor_t = cursor_base_t<node>;
using const_cursor_t = cursor_base_t<const node>;

namespace lexer {
    using intern_id = std::uint32_t;
    struct intern_table {
        // a bad solution but it works well I guess
        //  I coul make it with using 1 array for each type
        //  and then
        llvm::DenseMap<llvm::StringRef, uint32_t> map;
        llvm::DenseMap<uint32_t, llvm::StringRef> rmap;
        uint32_t current_id = 0;

        uint32_t intern(llvm::StringRef s) {
            auto [it, inserted] = map.try_emplace(s, current_id);
            rmap.try_emplace(current_id, s);
            if (inserted)
                ++current_id;

            return it->second;
        }

        std::string_view lookup(uint32_t id) const {
            return rmap.lookup_or(id, std::string_view{});
        }
    };

    struct buffer {
        intern_table& itable;
        source& src;

        llvm::SmallVector<node, 0> nodes;
        llvm::SmallVector<source_location, 0> locs;

        std::uint32_t to_index(const node* n) const {
            auto res = n - nodes.begin();
            return static_cast<size_t>(res);
        }

        std::uint32_t to_index(const source_location* n) const {
            auto res = n - locs.begin();
            return static_cast<size_t>(res);
        }

        inline decltype(auto) loc(this auto&& self, std::uint32_t index) {
            return self.locs[index];
        }

        inline decltype(auto) get_node(this auto&& self, std::uint32_t index) {
            return self.nodes[index];
        }

        inline decltype(auto) push(node&& n, source_location&& loc) {
            auto& it = nodes.emplace_back(n);
            locs.push_back(loc);
            return &it - nodes.begin();
        }

        std::string_view str(intern_id id) const {
            return itable.lookup(id);
        }
        // std::string_view str(const node* node) const {
        //     auto index = to_index(node);
        //     auto location = loc(index);
        //     return str(location);
        // }
    };

    struct buffer_builder {
        buffer& out;

        inline auto push(node n, source_location s) {
            return out.push(std::move(n), std::move(s));
        }
    };

    struct unit {
        struct symetrical_entry {
            median::e code;
            std::uint32_t index;
        };

        llvm::SourceMgr& source_manager;
        const std::uint32_t src_id;

        diagnostics::unit& diagnostic_unit;

        buffer_builder buffer;
        llvm::SmallVector<symetrical_entry, 0> openstack = {};

        uint32_t line = 1;
        uint32_t line_index = 0;

        uint32_t depth = 0;

        uint32_t last_terminator_depth = 0;
        uint32_t prev_depth = 0; // tracks per open and close

        auto base() const {
            return this->source_manager.getMemoryBuffer(this->src_id)->getBufferStart();
        }

        source_location make_srcloc(uint32_t begin, uint32_t end) const {

            return {llvm::SMLoc::getFromPointer(this->base() + begin),
                    llvm::SMLoc::getFromPointer(this->base() + end)};
        }
    };
    buffer entry(source& src,
                 llvm::SourceMgr& sm,
                 std::uint32_t src_id,
                 diagnostics::unit& dunit,
                 intern_table& itable);
    std::string_view str(final::e e);
    std::string_view str(median::e e);
    std::string str(const source_location val);
    std::string str(const final val);
    std::string str(const median val);
    std::string str(const node& val);
    // std::string str(const node& n, const lexer::buffer& buffer);
    void pretty_print(const buffer& buf, source_view src);
} // namespace lexer
