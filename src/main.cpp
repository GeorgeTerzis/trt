// !! check the compile.sh to see how to compile !!

#include "./file_loader.cpp"

#include "../include/utf8.h"
#include "../include/utf8/checked.h"

#include "../libs/llvm_allocator.hpp"
#include "../libs/map.hpp"
#include "../libs/meta.hpp"
#include "../libs/overloaded.hpp"
#include "../libs/ref.hpp"
#include "../libs/vector.hpp"

// Not used yet
// #include <boost/mp11/algorithm.hpp>
// #include <boost/mp11/utility.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <format>
#include <functional>
#include <iostream>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSwitch.h>
#include <locale.h>
#include <memory_resource>
#include <optional>
#include <print>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <wchar.h>

#define become [[clang::musttail]] return

// I hate this
// but it is baisacly what I need
// I could eliminate this by having 2 allocators
// 1 for temporary allocations like vectors and once they are done I copy paste them to
// the main one and 1 for long living allocations refrences
template <typename T>
struct staging_vec {
    llvm_allocator& arena;
    std::vector<T> staging;

    explicit staging_vec(llvm_allocator& a) : arena(a) {}

    void push_back(const T& v) {
        staging.push_back(v);
    }
    void push_back(T&& v) {
        staging.push_back(std::move(v));
    }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        return staging.emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] std::span<T> commit() {
        if (staging.empty())
            return {};
        std::span<T> result = arena.alloc_many<T>(staging.size());
        for (std::size_t i = 0; i < staging.size(); ++i)
            result[i] = std::move(staging[i]);
        staging.clear();
        return result;
    }

    std::size_t size() const {
        return staging.size();
    }
    bool empty() const {
        return staging.empty();
    }
};

inline std::string source_location_to_string(
    const std::source_location& loc = std::source_location::current()) {
    char buffer[1024];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%s:%u in %s",
                  loc.file_name(),
                  loc.line(),
                  loc.function_name());
    return std::string(buffer);
}

template <typename T>
using list = llvm::SmallVector<T, 0>;

using string_view = std::string_view;
using string = std::string;

[[noreturn]] auto
throw_error(std::source_location loc = std::source_location::current()) {
    std::println(std::cerr,
                 "{} :: Throw without message",
                 source_location_to_string(loc));
    std::abort();
}

[[noreturn]] auto
throw_error(std::string_view msg,
            std::source_location loc = std::source_location::current()) {
    std::println(std::cerr, "{} :: {}", source_location_to_string(loc), msg);
    std::abort();
}

struct source {
    string filename;
    string text;

    source(auto f, auto s) : filename(f), text(s) {}

    source(const source&) = delete;
    source& operator=(const source&) = delete;

    // Allow move operations
    source(source&&) noexcept = default;
    source& operator=(source&&) noexcept = default;

    operator string_view() const noexcept {
        return text;
    }

    const char* data() const noexcept {
        return text.data();
    }
    size_t size() const noexcept {
        return text.size();
    }
    bool empty() const noexcept {
        return text.empty();
    }

    char operator[](size_t i) const {
        return text[i];
    }

    string_view substr(size_t pos = 0, size_t count = string_view::npos) const {
        size_t len = std::min(count, text.size() - pos);
        return string_view(text.data() + pos, len);
    }

    auto begin() const noexcept {
        return text.begin();
    }
    auto end() const noexcept {
        return text.end();
    }
};
using source_view = source&;

struct final {
    enum e {
        first,
#define TOKEN_BASE(code) code,
#include "./def"
        last,
    };
    e code;
    uint32_t data = 0;

    explicit final(e c) : code(c) {}
    explicit final(e c, uint32_t d) : code(c), data(d) {}

    operator e() const {
        return code;
    }

    bool operator==(e other) const {
        return code == other;
    }
};
using finale = final::e;

struct node;
struct median;
struct final;

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
            throw_error(
                "While visiting node found something that is not a final or a median");
            // std::unreachable();
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

struct source_location {
    std::uint32_t line;
    std::uint32_t begin_index;
    std::uint32_t end_index;

    source_location(std::uint32_t line, std::uint32_t begin, std::uint32_t end) :
        line(line),
        begin_index(begin),
        end_index(end) {}

    auto length() const {
        return end_index - begin_index;
    }

    string_view source(string_view src) const {
        if (begin_index <= end_index)
            return string_view(src.begin() + begin_index, src.begin() + end_index);
        else
            return "";
    }
};

namespace lexer {
    struct intern_table {
        llvm::DenseMap<llvm::StringRef, uint32_t> map;
        uint32_t current_id = 0;

        uint32_t intern(std::string_view s) {
            auto [it, inserted] = map.try_emplace(s, current_id);
            if (inserted)
                ++current_id;

            return it->second;
        }

        std::string_view lookup(uint32_t id) const {
            for (auto& [key, val] : map)
                if (val == id)
                    return key;
            return {};
        }
    };

    struct buffer {
        intern_table& itable;
        source& src;

        list<node> nodes;
        list<source_location> locs;

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

        std::string_view str(source_location loc) const {
            return loc.source(src.text);
        }

        std::string_view str(const node* node) const {
            auto index = to_index(node);
            auto location = loc(index);
            return str(location);
        }
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

        buffer_builder buffer;

        list<symetrical_entry> openstack = {};

        uint32_t line = 1;
        uint32_t depth = 0;
        std::uint32_t last_terminator_depth = 0;
    };

#define RET void
#define ARGS unit &u, source_view src, const uint32_t token_begin, uint32_t cursor
#define FNSIG (ARGS)->RET

    using dispatch_table = std::array<RET (*)(ARGS), 256>;
    using truth_table = std::array<bool, 256>;

    auto within_src(string_view src, std::uint32_t index) {
        return src.size() > index;
    }

    auto next FNSIG;

    constexpr string_view str(final::e e) {
        switch (e) {
#define TOKEN_BASE(code)                                                                 \
    case final::code:                                                                    \
        return #code;
#include "./def"
        case final::first:
            return "first";
        case final::last:
            return "last";
            break;
        }
    }

    constexpr string_view str(median::e e) {
        switch (e) {
        case median::e::FILE:
            return "FILE";
        case median::e::PARENS:
            return "PARENS";
        case median::e::BRACES:
            return "BRACES";
        case median::e::CBRACES:
            return "CBRACES";
        case median::e::BLOCK_EXPR:
            return "BLOCK_EXPR";
        case median::e::DECL:
            return "DECL";
        default:
            return "UNKNOWN";
        }
    }

    string str(const source_location val) {
        return std::format("loc={{line={}, len={}, index={}}}",
                           val.line,
                           val.length(),
                           val.begin_index);
    }
    string str(const final val) {
        return std::format("final={{code={}}}", str(val.code));
    }
    string str(const median val) {
        return std::format("median={{code={}, len={}}}", str(val.code), val.len);
    }
    string str(const node& val) {
        return std::format("node={{{}}}",
                           val.visit([&](const final& f) { return str(f); },
                                     [&](const median& f) { return str(f); }));
    }
    string str(const node& n, const lexer::buffer& buffer) {
        auto buffer_index = buffer.to_index(&n);
        return std::format("[{}]{}, {}",
                           buffer_index,
                           str(n),
                           str(buffer.loc(buffer_index)));
    }

    namespace whitesapce {
        constexpr bool is_vertical(unsigned char c) {
            return (c == '\n') || (c == '\r');
        }

        constexpr bool is_horizontal(unsigned char c) {
            return (c == ' ') || (c == '\t');
        }

        auto horizontal FNSIG {
            auto end = cursor;
            while (within_src(src, end) && is_horizontal(src[end]))
                ++end;
            become next(u, src, end, end);
        }
        auto vertical FNSIG {
            ++u.line;
            become next(u, src, token_begin + 1, cursor + 1);
        }
    } // namespace whitesapce

    node get_terminator() {
        return node{final::TERMINATOR};
    }
    std::uint32_t open_median(unit& u,
                              median::e code,
                              string_view src,
                              std::uint32_t token_begin,
                              std::uint32_t cursor);
    auto
    close_median(std::uint32_t index, unit& u, string_view src, std::uint32_t cursor);

    void make_terminator(unit& u,
                         source_view src,
                         const uint32_t& token_begin,
                         uint32_t& end) {

        u.last_terminator_depth = u.depth;
        u.buffer.push(get_terminator(), source_location{u.line, token_begin, end});
    }
    auto terminator FNSIG {
        auto end = cursor + 1;
        make_terminator(u, src, token_begin, end);

        become next(u, src, end, end);
    }

    void cond_insert_invisible_separator(unit& u) {
        auto& last_node = u.buffer.out.nodes.back();
        auto& last_loc = u.buffer.out.locs.back();

        last_node.visit(
            [&](const final& f) {
                auto loc = last_loc;
                loc.end_index = 0;

                bool depth_equal = (u.last_terminator_depth == u.depth);
                bool is_terminator = (f == final::TERMINATOR);

                if (depth_equal && !is_terminator) {
                    u.buffer.push(get_terminator(), loc);
                } else if (depth_equal && is_terminator) {
                } else if (!depth_equal) {
                    u.buffer.push(get_terminator(), loc);
                }
            },
            [](const median&) {});
    }

    std::uint32_t open_median(unit& u,
                              median::e code,
                              string_view src,
                              std::uint32_t token_begin,
                              std::uint32_t cursor) {

        auto loc = source_location{u.line, token_begin, token_begin + 1};
        auto index = u.buffer.push(node(code, 0), loc);
        ++u.depth;
        return index;
    }

    auto
    close_median(std::uint32_t index, unit& u, string_view src, std::uint32_t cursor) {
        {
            auto& open_node = u.buffer.out.get_node(index);
            auto& m = open_node.payload.median;
            m.len = u.buffer.out.nodes.size() - 1 - index;
        }

        {
            auto& open_source_location = u.buffer.out.loc(index);
            open_source_location.end_index = cursor;
        }
        --u.depth;
    }

    namespace symmetrical {

        template <median::e code>
        auto open FNSIG {
            auto index = open_median(u, code, src, token_begin, cursor);
            u.openstack.push_back({code, index});

            ++cursor;
            become next(u, src, cursor, cursor);
        }

        auto symetrical_close_match(median::e code, char close_char) {
            switch (code) {
            case median::e::PARENS:
                return close_char == ')';
            case median::e::BRACES:
                return close_char == ']';
            case median::e::CBRACES:
                return close_char == '}';
            default:
                return false;
            }
        }

        auto close FNSIG {
            auto c = src[cursor];
            cursor += 1;

            auto open_index = u.openstack.back();
            u.openstack.pop_back();

            // mismatched delimiter
            if (!symetrical_close_match(open_index.code, c)) [[unlikely]]
                std::abort();
            cond_insert_invisible_separator(u);
            close_median(open_index.index, u, src, cursor);
            return next(u, src, cursor, cursor);
        }

    } // namespace symmetrical

    auto strlit FNSIG {
        ++cursor;
        while (within_src(src, cursor) && src[cursor] != '\"')
            ++cursor;
        auto end = cursor + 1;
        auto loc = source_location{u.line, token_begin, cursor};
        u.buffer.push(node(final(final::STRING_LIT)), loc);

        become next(u, src, end, end);
    }

    constexpr bool is_digit(unsigned char c) {
        return (c >= '0' && c <= '9');
    }

    constexpr bool ascii_is_alpha(unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    constexpr bool ascii_is_id_start(unsigned char c) {
        return ascii_is_alpha(c) || c == '_';
    }

    constexpr bool ascii_is_ident_continue(unsigned char c) {
        return ascii_is_id_start(c) || is_digit(c);
    }

    inline string_view read_word(source_view src, const std::uint32_t begin_index) {
        const auto begin = src.begin() + begin_index;
        auto cursor = begin;
        const auto end = src.end();
        do {
            unsigned char c = *cursor;
            if (c > 128) {
                utf8::next(cursor, end);
            } else if (ascii_is_ident_continue(c)) {
                ++cursor;
            } else {
                break;
            }
        } while (cursor < end);

        return std::string_view(begin, cursor);
    }

    auto keyword FNSIG {
        auto word = read_word(src, token_begin);
        auto end = token_begin + word.size();

        auto type = llvm::StringSwitch<final::e>(word).
#define KEYWORD(spelling, code) Case(spelling, final::code).
#include "def"
                    Default(final::ID);

        // if(type == final::ID){
        //     auto intern_id = u.buffer.out.itable.intern(word);

        // }

        u.buffer.push(
            node(type, ((type == final::ID) ? u.buffer.out.itable.intern(word) : 0)),
            source_location{u.line, token_begin, static_cast<uint32_t>(end)});
        become next(u, src, end, end);
    }

    auto id FNSIG {
        auto word = read_word(src, token_begin);
        auto end = token_begin + word.size();
        auto intern_id = u.buffer.out.itable.intern(word);

        u.buffer.push(node(final::ID, intern_id),
                      source_location{u.line, token_begin, static_cast<uint32_t>(end)});
        become next(u, src, end, end);
    }

    inline bool all_digits(std::string_view v) {
        return !v.empty() &&
               std::ranges::all_of(v, [](unsigned char c) { return is_digit(c); });
    }
    auto prefixed_numeric_or_id FNSIG {

        auto text = read_word(src, token_begin);
        cursor += text.size();
        auto loc = source_location{u.line, token_begin, cursor};

        auto is_digit_pred = [](unsigned char c) { return is_digit(c); };

        const auto len_view = text.substr(1);
        if (text.size() >= 2 && std::ranges::all_of(len_view, is_digit_pred)) {
            const auto prefix = text[0];
            final::e type = final::last;
            switch (prefix) {
            case 's':
                type = final::SINT_TYPE;
                break;
            case 'u':
                type = final::UINT_TYPE;
                break;
            case 'b':
                type = final::BOOL_TYPE;
                break;
            default:
                std::unreachable();
            }

            std::uint32_t len;
            std::from_chars(len_view.data(), len_view.data() + len_view.size(), len);
            u.buffer.push(node(final{type, len}), loc);
            become next(u, src, cursor, cursor);
        }

        become keyword(u, src, token_begin, cursor);
    }

    auto num FNSIG {
        auto i = token_begin;
        bool is_float = false;
        while (i < src.size() && is_digit(static_cast<unsigned char>(src[i]))) {
            ++i;
        }

        if (i < src.size() && src[i] == '.') {
            is_float = true;
            ++i;

            while (i < src.size() && is_digit(src[i])) {
                ++i;
            }
        }
        if (i < src.size() && (src[i] == 'e' || src[i] == 'E')) {
            is_float = true;
            ++i;

            if (i < src.size() && (src[i] == '+' || src[i] == '-'))
                ++i;

            while (i < src.size() && is_digit(src[i]))
                ++i;
        }

        auto loc = source_location{u.line, token_begin, i};
        u.buffer.push(node(final{is_float ? final::FLOAT : final::INT}), loc);

        become next(u, src, i, i);
    }
    [[noreturn]] auto unreachable_state FNSIG {
        std::unreachable();
    }

    constexpr auto symbol_sequence_table = [] consteval {
        truth_table table = {false};
        for (int i = 0; i < 256; i++)
            table[i] = false;

#define SYMBOL_SEQUENCE(spelling, code)                                                  \
    for (std::uint32_t i = 0; i < sizeof(spelling); ++i)                                 \
        table[spelling[i]] = true;
#include "./def"

        return table;
    }();
    constexpr auto id_start_table = [] consteval {
        truth_table table = {false};
        for (char c = 'A'; c <= 'Z'; ++c)
            table[c] = true;
        for (char c = 'a'; c <= 'z'; ++c)
            table[c] = true;
        table['_'] = true;
        return table;
    }();
    constexpr auto id_inner_table = [] consteval {
        truth_table table = id_start_table;
        for (char c = '0'; c <= '9'; ++c)
            table[c] = true;
        return table;
    }();
    constexpr auto builtin_table = [] consteval {
        truth_table table = id_inner_table;
        table['*'] = true;
        return table;
    }();

    [[clang::always_inline]] inline auto scan_for_symbol(string_view src,
                                                         std::uint32_t cursor)
        -> string_view {
        while (within_src(src, cursor) && symbol_sequence_table[src[cursor]])
            ++cursor;
        return src.substr(0, cursor);
    }

    template <auto table>
    [[clang::always_inline]] inline auto scan_for(std::string_view src,
                                                  std::uint32_t cursor)
        -> std::string_view {
        while (within_src(src, cursor) && table[src[cursor]])
            ++cursor;
        return src.substr(0, cursor);
    }

    auto symbol FNSIG {
        const auto text = scan_for<symbol_sequence_table>(src.substr(cursor), 0);
        cursor += text.size();

        // empty text something has failed
        if (text.size() == 0) [[unlikely]]
            std::abort();

        auto subbegin = text.begin();
        auto len = text.size();

        do {
            const auto view = llvm::StringRef(subbegin, len);
            const auto r = llvm::StringSwitch<final::e>(view)
#define SYMBOL_SEQUENCE(spelling, code) .Case(spelling, final::code)
#include "./def"
                               .Default(final::last);

            if (r == final::e::last) [[unlikely]] {
                len--;
                cursor--;
                if (len == 0) [[unlikely]]
                    std::abort();
            } else {
                u.buffer.push(node(final(r)),
                              source_location{u.line, token_begin, cursor});
                become next(u, src, cursor, cursor);
            }
        } while (true);
    }

    auto builtin FNSIG {
        cursor++;
        const auto word = scan_for<builtin_table>(src.substr(cursor), 0);
        auto end = cursor + word.size();

#define BUILTIN_KEYWORD(spelling, code) .Case(spelling, final::e::code)
        auto code =
            llvm::StringSwitch<final::e>(string_view(word.begin() - 1, word.size() + 1))
#include "./def"
                .Default(final::e::last);

        if (code == final::e::last) [[unlikely]]
            std::abort();

        u.buffer.push(node(final(code)),
                      source_location{u.line, token_begin, static_cast<uint32_t>(end)});

        become next(u, src, end, end);
    }
    auto line_comment FNSIG {
        // disambiguate '/' (divide) vs '//' (comment)
        if (!within_src(src, cursor + 1) || src[cursor + 1] != '/') {
            become symbol(u, src, token_begin, cursor);
        }
        cursor += 2; // skip '//'
        while (within_src(src, cursor) && !whitesapce::is_vertical(src[cursor]))
            ++cursor;
        become next(u, src, cursor, cursor);
    }

    constexpr inline bool is_id_continue_ascii(unsigned char c) {
        return id_inner_table[c];
    }

    constexpr auto next_table = [] consteval {
        dispatch_table table = {unreachable_state};

        table['@'] = builtin;
        table[';'] = terminator;

#define SYMBOL_SEQUENCE(spelling, code) table[spelling[0]] = symbol;
#include "./def"

        {
            table['('] = symmetrical::open<median::e::PARENS>;
            table[')'] = symmetrical::close;

            table['['] = symmetrical::open<median::e::BRACES>;
            table[']'] = symmetrical::close;

            table['{'] = symmetrical::open<median::e::CBRACES>;
            table['}'] = symmetrical::close;
        }

        for (unsigned int i = 0; i < 256; ++i) {
            if (whitesapce::is_horizontal(i))
                table[i] = whitesapce::horizontal;
            else if (whitesapce::is_vertical(i))
                table[i] = whitesapce::vertical;
            else if (is_digit(i))
                table[i] = num;
            else if (ascii_is_id_start(i))
                table[i] = id;
        }

        for (unsigned i = 128; i < 256; ++i)
            table[i] = id;

#define KEYWORD(spelling, code) table[spelling[0]] = keyword;
#include "def"
        table['s'] = prefixed_numeric_or_id;
        table['u'] = prefixed_numeric_or_id;
        table['b'] = prefixed_numeric_or_id;

        table['/'] = line_comment;

        return table;
    }();

    auto next FNSIG {
        if (!within_src(src, cursor))
            return;

        const auto c = static_cast<unsigned char>(src[cursor]);
        become next_table[c](u, src, token_begin, cursor);
    }

    auto close_file_median(unit& u, string_view src) {
        cond_insert_invisible_separator(u);
        close_median(0, u, src, src.size());
    }

    std::string format_node_text(const buffer& b, const node& n) {
        const auto buffer_index = b.to_index(&n);
        const auto loc = b.loc(buffer_index);
        const auto text = loc.source(b.src);

        const auto fin_text =
            (n.as_final() && text.size()) ? "'" + std::string(text) + "'" : "";

        return std::format("[{}]{} {}", buffer_index, str(n), fin_text);
    }

    void internal_pretty_print(const buffer& b,
                               source_view src,
                               const_cursor_t i,
                               std::uint32_t&& depth) {
        constexpr int depth_mul = 4;
        constexpr int value_width = 30;

        const string indent_str(depth * depth_mul, ' ');

        while (i.within()) {
            const auto& n = i++.get();

            std::println("{}{}", indent_str, format_node_text(b, n));
            n.visit([&](const final&) {},
                    [&](const median& v) {
                        internal_pretty_print(b,
                                              src,
                                              cursor_base_t<const node>(v.children()),
                                              depth + 1);
                    });
        }
    }

    void pretty_print(const buffer& buf, source_view src) {
        std::span<const node> s(&buf.get_node(0), 1);
        auto i = const_cursor_t(s);
        return internal_pretty_print(buf, src, i, 0);
    }

    buffer entry(source& src, intern_table& itable) {
        buffer buffer{
            .itable = itable,
            .src = src,
            .nodes = {},
            .locs = {},
        };
        buffer_builder builder{buffer};
        unit u{builder};

        open_median(u, median::e::FILE, src, 0, 0);
        next(u, src, 0, 0);
        close_median(0, u, src, src.size());

        assert(!u.openstack.size());

        return buffer;
    }

#undef FNSIG
#undef ARGS
#undef RET
} // namespace lexer

using intern_id = std::uint32_t;

namespace ast {
    using cursor = const_cursor_t;

    struct ast_t;
    struct decl_t;
    struct type_t;
    struct expr_t;
    struct stmt_t;
    struct stmts_t;
    struct symbols_t;

    using ref_type = ref<type_t>;
    using ref_expr = ref<expr_t>;
    using ref_decl = ref<decl_t>;
    using ref_stmts = ref<stmts_t>;
    using ref_stmt = ref<stmt_t>;
    using ref_ast = ref<ast_t>;
    using ref_symbols = ref<symbols_t>;

    struct ast_t {};
    struct symbols_t {
        using entry_t = ref_decl;

        struct insertion {
            const bool is_inserted;
            const intern_id name;
            entry_t symbol;
        };

        struct lookup_result {
            ref_symbols where;
            entry_t symbol;
        };

        ref_symbols parent;
        map<intern_id, entry_t> table = {};

        template <bool is_local>
        static std::optional<lookup_result> lookup_impl(ref_symbols& self,
                                                        intern_id key) {
            assert(self.is_valid());
            auto it = self.deref().table.find(key);
            if (it != self.deref().table.end())
                return lookup_result{self, it->second};
            if constexpr (!is_local)
                if (self.deref().parent)
                    return lookup_impl<false>(self.deref().parent, key);
            return std::nullopt;
        }

        static std::optional<lookup_result> local_lookup(ref_symbols& self,
                                                         intern_id name) {
            return lookup_impl<true>(self, name);
        }

        static std::optional<lookup_result> ancestor_lookup(ref_symbols& self,
                                                            intern_id name) {
            return lookup_impl<false>(self, name);
        }

        static ref_symbols get_root(ref_symbols current) {
            if (!current.deref().parent)
                return current;
            [[clang::musttail]] return get_root(current.deref().parent);
        }
    };

    namespace util {
        struct frame {
            ref_symbols symbols;
            ref_stmts stmts;
        };

        struct unresolved {
            cursor begin;
            cursor end;
        };
    } // namespace util

    struct fnsig_t {
        ref_symbols symbols;
        ref_type ret;
        vector<ref_decl> args;
    };

    struct template_inputs_t {
        vector<ref_decl> args;
    };

    template <typename T>
    struct template_input_t {
        using type = T;
        T data;
    };

    struct template_init {
        using list = type_list<ref_decl, ref_type, ref_expr>;

        ref_decl original;

        using input_t = ref_type;
        using inputs_t = vector<input_t>;
        inputs_t inputs;
    };

    namespace decl_var {
        struct var_t {
            ref_type type;
            ref_expr init_expr;
        };

        struct rec_member_t {
            ref_type type;
            std::uint32_t index;
        };

        struct tagged_union_member_t {
            ref_type type;
        };

        struct fn_parameter_t {
            ref_type type;
            std::uint32_t index;
        };

        struct fn_t {
            ref_symbols symbols;
            std::span<ref_decl> args;
            ref_type ret_type;
            ref_expr body;
        };

        struct type_alias_t {
            ref_type type;
        };

        using variant = std::variant<var_t,
                                     rec_member_t,
                                     tagged_union_member_t,
                                     fn_parameter_t,
                                     fn_t,
                                     type_alias_t>;
    }; // namespace decl_var

    namespace stmt_var {
        using decl = ref_decl;
        using expr = ref_expr;

        struct loop_t {
            ref_expr cond;
            ref_expr incr;
        };

        struct return_t {
            std::optional<ref_expr> val;
        };
        struct break_t {
            std::optional<ref_expr> val;
        };

        using variant = std::variant<decl, expr, return_t, break_t>;
    } // namespace stmt_var

    struct stmt_t {
        stmt_var::variant data;
    };

    struct stmts_t {
        std::span<ref_stmt> span;
    };

    namespace expr_var {
        struct unresolved_t {
            util::unresolved data;
        };

        struct int_literal_t {
            std::uint64_t value;
        };
        struct float_literal_t {
            double value;
        };
        struct bool_literal_t {
            bool value;
        };

        struct name_t {
            ref_decl decl;
        };
        struct rec_access_t {
            ref_decl decl;
        };

        struct call_payload_t {
            std::span<ref_expr> args;
        };

        // used to coerse expresions to types
        struct as_t {
            ref_type type;
            ref_expr expr;
        };
        struct bitcast_t {
            ref_type type;
            ref_expr expr;
        };

        enum class op_e {
            opcall,
            opaccess,
            opadd,
            opsub,
            opmul,
            opdiv,
            opand,
            opor,
        };

        struct binary_op_t {
            op_e op;
            ref_expr lhs;
            ref_expr rhs;
        };

        struct unary_op_t {
            op_e op;
            ref_expr operand;

            union payload_t {
                std::span<ref_expr> args;
                ref_type type;
            };
            payload_t payload;
        };

        using variant = std::variant<unresolved_t,
                                     as_t,
                                     call_payload_t,
                                     int_literal_t,
                                     float_literal_t,
                                     bool_literal_t,
                                     name_t,
                                     rec_access_t,
                                     binary_op_t,
                                     unary_op_t>;
    } // namespace expr_var

    struct expr_t {
        ref_expr parent;
        ref_type type;
        expr_var::variant data;
    };

    namespace mutability {
        enum internal_e : std::int8_t {
            CONSTANT = 0,
            IMMUTABLE = 1,
            MUTABLE = 2,
            NONE = 3
        };

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

    namespace type_var {
        struct uint_t {
            size_t bit_size;
        };

        struct sint_t {
            size_t bit_size;
        };

        template <size_t S>
        struct fp_base {
            static const size_t bit_size = S;
        };

        using f16_t = fp_base<16>;
        using f32_t = fp_base<32>;
        using f64_t = fp_base<64>;
        using f128_t = fp_base<128>;

        struct integer_literal_t {};
        struct float_literal_t {};

        struct rec_t {
            ref_symbols symbols;
            std::span<ref_decl> members;
        };

        struct void_t {};
        struct optr_t {};
        struct ptr_t {
            ref_type type;
        };

        struct type_alias_t {
            ref_type type;
        };

        struct unresolved_t {
            util::unresolved data;
        };

        using variant = std::variant<unresolved_t,
                                     type_alias_t,
                                     integer_literal_t,
                                     float_literal_t,
                                     uint_t,
                                     sint_t,
                                     f16_t,
                                     f32_t,
                                     f64_t,
                                     f128_t,
                                     rec_t,
                                     void_t,
                                     optr_t,
                                     ptr_t>;

    }; // namespace type_var

    struct type_t {
        mutability::t mut;
        type_var::variant var;
    };

    struct decl_t {
        std::uint32_t name;
        decl_var::variant data;
    };

    struct type_eq {
        struct policy {
            static constexpr bool ignore_mutability = false;
        };

        using trivially_true = type_list<type_var::void_t,
                                         type_var::optr_t,
                                         type_var::integer_literal_t,
                                         type_var::float_literal_t,
                                         type_var::f16_t,
                                         type_var::f32_t,
                                         type_var::f64_t,
                                         type_var::f128_t>;

        using numeric_cat = type_list<type_var::uint_t, type_var::sint_t>;

        template <typename Policy = policy>
        static bool eq(const ref_type& a, const ref_type& b) {
            if (a == b)
                return true;
            if (!a || !b)
                return false;
            return eq<Policy>(a.deref(), b.deref());
        }

        template <typename Policy = policy>
        static bool eq(const type_t& a, const type_t& b) {
            return std::visit(
                [](const auto& x, const auto& y) { return eq_impl<Policy>(x, y); },
                a.var,
                b.var);
        }

      private:
        // default: different types
        template <typename Policy, typename A, typename B>
        static bool eq_impl(const A&, const B&) {
            return false;
        }

        // trivially true
        template <typename Policy, typename T>
            requires is_in_list<T, trivially_true>::value
        static constexpr bool eq_impl(const T&, const T&) {
            return true;
        }

        // numeric: compare size
        template <typename Policy, typename T>
            requires is_in_list<T, numeric_cat>::value
        static bool eq_impl(const T& a, const T& b) {
            return a.bit_size == b.bit_size;
        }

        // ptr: recurse
        template <typename Policy>
        static bool eq_impl(const type_var::ptr_t& a, const type_var::ptr_t& b) {
            return eq<Policy>(a.type, b.type);
        }

        // rec: structural
        template <typename Policy>
        static bool eq_impl(const type_var::rec_t& a, const type_var::rec_t& b) {
            if (a.members.size() != b.members.size())
                return false;
            for (size_t i = 0; i < a.members.size(); ++i) {
                const auto& ta =
                    std::get<decl_var::rec_member_t>(a.members[i].deref().data).type;
                const auto& tb =
                    std::get<decl_var::rec_member_t>(b.members[i].deref().data).type;
                if (!eq<Policy>(ta, tb))
                    return false;
            }
            return true;
        }
    };

    using allocator_t = llvm_allocator;

    struct env {
      public:
        const lexer::buffer& buffer;
        allocator_t& allocator;
        ref_symbols symbols;
        ref_ast parent;

        template <typename... Args>
        env with(Args&&... args) const noexcept {
            env copy = *this;
            (copy.apply(std::forward<Args>(args)), ...);
            return copy;
        }

      private:
        template <typename T>
        void apply(T&& value) noexcept {
            using U = std::decay_t<T>;
            if constexpr (std::is_same_v<U, allocator_t>) {
                allocator = value;
            } else if constexpr (std::is_same_v<U, ref_symbols>) {
                symbols = std::forward<T>(value);
            } else if constexpr (std::is_same_v<U, ref_ast>) {
                parent = std::forward<T>(value);
            } else {
                static_assert(always_false<U>::value,
                              "Unsupported type passed to env::with()");
            }
        }

        template <typename>
        struct always_false : std::false_type {};
    };

    template <median::e med>
    void consume_untill(cursor& c) {
        while (c.within() && !c.get().isa(med)) {
            c++;
        }
    }
    template <final::e tok>
    void consume_untill(cursor& c) {
        while (c.within() && !c.get().isa(tok)) {
            c++;
        }
    }

    namespace node2ast {
        template <auto... kind>
            requires((cmp_v<decltype(kind), final::e> ||
                      cmp_v<decltype(kind), median::e>) &&
                     ...)
        bool peek(cursor c) {
            return (((c.get().isa(kind) && (++c, true)) && ...));
        }

        template <typename Tuple, std::size_t... I>
        bool peek_tuple_impl(cursor c, Tuple const& t, std::index_sequence<I...>) {
            return peek<std::get<I>(t)...>(c);
        }

        template <typename Tuple>
        bool peek_tuple(cursor c, Tuple&& t) {
            return peek_tuple_impl(c,
                                   std::forward<Tuple>(t),
                                   std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }

        template <typename R>
        struct path_switch {
            cursor c;
            std::optional<R> result;

            explicit path_switch(cursor& c) : c(c) {}

            template <auto... kind, typename Fn, typename... Args>
            path_switch& path(Fn&& fn, Args&&... args) {
                if (!result) {
                    // auto cc = c;
                    if (peek<kind...>(c)) {
                        result = std::forward<Fn>(fn)(std::forward<Args>(args)...);
                    }
                }
                return *this;
            }

            template <typename Fn, typename... Args>
            R def(Fn&& fn, Args&&... args) {
                if (!result)
                    result = std::forward<Fn>(fn)(std::forward<Args>(args)...);
                return *result;
            }
        };

        template <final::e expected>
        const final& expect_node(const node& n) {
            if (auto f = n.as_final()) [[likely]] {
                if (f->get() == expected) [[likely]]
                    return f->get();
                throw_error(std::format("expected final '{}' but got final '{}'",
                                        lexer::str(expected),
                                        lexer::str(f->get().code)));
            } else if (auto m = n.as_median()) [[unlikely]] {
                throw_error(std::format("expected final '{}' but got median '{}'",
                                        lexer::str(expected),
                                        lexer::str(m->get().code)));
            }
            throw_error(std::format("expected final '{}' but got unknown node",
                                    lexer::str(expected)));
        }

        template <median::e expected>
        const median& expect_node(const node& n) {
            if (auto m = n.as_median()) {
                if (m->get() == expected)
                    return m->get();
                throw_error(std::format("expected median '{}' but got median '{}'",
                                        lexer::str(expected),
                                        lexer::str(m->get().code)));
            }
            throw_error(
                std::format("expected median '{}' but got final", lexer::str(expected)));
        }

        template <median::e c>
        auto expect(median::e in) {
            if (in != c)
                throw_error(std::format("expected '{}' but got '{}'",
                                        lexer::str(c),
                                        lexer::str(in)));
        }
        template <final::e c>
        auto expect(final::e in) {
            if (in != c)
                throw_error(std::format("expected '{}' but got '{}'",
                                        lexer::str(c),
                                        lexer::str(in)));
        }

        template <final::e expected>
        const final& expect_consume(cursor& c) {
            const auto& n = c++.get();
            return expect_node<expected>(n);
        }

        template <median::e expected>
        const median& expect_consume(cursor& c) {
            const auto& n = c++.get();
            return expect_node<expected>(n);
        }

        template <typename Fn>
        auto median_loop(env e, const median& m, Fn&& fn) {
            auto c = cursor(m.children());
            while (c.within()) {
                // run the function the user gave us
                {
                    std::forward<Fn>(fn)(e, c);
                }

                // expect terminator
                {
                    if (!c.within())
                        throw_error("Out of bounds");
                    auto& t = c++.get();
                    expect_node<final::TERMINATOR>(t);
                }
            }
        }

        ref_expr expr_fn(env e, cursor& c);

        namespace expr_patterns {
            namespace operands {
                expr_var::variant int_lit(env e, cursor& c) {
                    const auto index = e.buffer.to_index(&c.get());
                    const auto text = e.buffer.str(e.buffer.loc(index));
                    std::uint64_t val;
                    std::from_chars(text.data(), text.data() + text.size(), val);
                    c++;
                    return expr_var::int_literal_t{val};
                }

                expr_var::variant float_lit(env e, cursor& c) {
                    const auto index = e.buffer.to_index(&c.get());
                    const auto text = e.buffer.str(e.buffer.loc(index));
                    double val;
                    std::from_chars(text.data(), text.data() + text.size(), val);
                    c++;
                    return expr_var::float_literal_t{val};
                }

                expr_var::variant bool_true(env e, cursor& c) {
                    c++;
                    return expr_var::bool_literal_t{true};
                }

                expr_var::variant bool_false(env e, cursor& c) {
                    c++;
                    return expr_var::bool_literal_t{false};
                }

                expr_var::variant chain(env e, cursor& c) {
                    const auto begin = c;
                    expect_node<final::ID>(c++.get());
                    while (c.within() && c.get().isa(final::DCOLON)) {
                        c++;
                        expect_node<final::ID>(c++.get());
                    }
                    const auto end = c;
                    return expr_var::unresolved_t{{begin, end}};
                }
            } // namespace operands

            namespace operators {
                expr_var::variant call(env e, cursor& c, ref_expr callee) {
                    auto& parens = c++.get().unsafe_median();
                    expect<median::PARENS>(parens.code);

                    auto staging = staging_vec<ref_expr>(e.allocator);
                    median_loop(e, parens, [&staging](env e, cursor& c) {
                        staging.push_back(expr_fn(e, c));
                    });
                    auto args = staging.commit();

                    auto payload = expr_var::unary_op_t::payload_t{.args = args};
                    return expr_var::unary_op_t{
                        .op = expr_var::op_e::opcall,
                        .operand = callee,
                        .payload = payload,
                    };
                }
            } // namespace operators
        } // namespace expr_patterns

        ref_expr expr_fn(env e, cursor& c) {
            ref ptr = e.allocator.alloc_one<expr_t>();
            ptr.deref().parent = nullptr;

            ptr.deref().data =
                path_switch<expr_var::variant>{c}
                    .path<final::INT>(expr_patterns::operands::int_lit, e, c)
                    .path<final::FLOAT>(expr_patterns::operands::float_lit, e, c)
                    .path<final::TRUE>(expr_patterns::operands::bool_true, e, c)
                    .path<final::FALSE>(expr_patterns::operands::bool_false, e, c)
                    .path<final::ID>(expr_patterns::operands::chain, e, c)
                    .def(
                        [](env e, cursor& c) -> expr_var::variant {
                            throw_error(std::format("Unknown expression {}",
                                                    lexer::str(c.get())));
                        },
                        e,
                        c);

            return ptr;
        }

        ref_decl alloc_decl(allocator_t& allocator, std::uint32_t name) {
            return allocator.alloc_one<decl_t>(name, decl_var::variant{});
        }

        ref_decl alloc_and_insert_decl(env e, intern_id name) {
            const auto lookup = symbols_t::local_lookup(e.symbols, name);
            if (lookup) {
                throw_error("Found symbol with the same name");
            }
            auto mem = alloc_decl(e.allocator, name);
            const auto [_, inserted] = e.symbols.deref().table.emplace(name, mem);
            if (!inserted)
                throw_error("Unable to insert declaration");

            return mem;
        }
        ref_type type_fn(env e, cursor& c);

        ref_decl member_decl(env e, cursor& c, std::uint32_t i) {
            auto& name_node = c++.get();
            auto name = name_node.unsafe_final().data;

            auto ptr = alloc_and_insert_decl(e, name);
            expect_node<final::COLON>(c++.get());
            const auto type = type_fn(e, c);

            ptr.deref().data = decl_var::rec_member_t{.type = type, .index = i};

            return ptr;
        }

        namespace type_patterns {
            type_var::rec_t rec(env e, cursor& c) {
                c++;
                auto& members = c.get().as_median().value().get();
                expect<median::PARENS>(members.code);

                auto mr = e.allocator.memory_resource();

                ref_symbols symbols = e.allocator.alloc_one<symbols_t>();

                auto staging = staging_vec<ref_decl>(e.allocator);
                median_loop(
                    e.with(symbols),
                    members,
                    [&staging](env e, cursor& c) -> void {
                        const auto index = static_cast<std::uint32_t>(staging.size());
                        const auto mem =
                            path_switch<ref_decl>(c)
                                .path<final::ID, final::COLON>(member_decl, e, c, index)
                                .def(
                                    [](env e, cursor& c, std::uint32_t i) -> ref_decl {
                                        throw_error("Expected member declaration "
                                                    "found something else");
                                    },
                                    e,
                                    c,
                                    index);

                        staging.push_back(mem);
                    });
                c++;

                auto member_span = staging.commit();
                return {.symbols = symbols, .members = member_span};
            }
        } // namespace type_patterns

        ref_type type_fn(env e, cursor& c) {
            auto ptr = ref(e.allocator.alloc_one<type_t>());

            ptr.deref().mut = [](env e, cursor& c) -> mutability::t {
                return path_switch<mutability::t>{c}
                    .path<final::MUTABLE>(
                        [](cursor& c) -> mutability::t {
                            c++;
                            return mutability::mut();
                        },
                        c)
                    .path<final::IMMUTABLE>(
                        [](cursor& c) -> mutability::t {
                            c++;
                            return mutability::imut();
                        },
                        c)
                    .def([](cursor& c) -> mutability::t { return mutability::mut(); }, c);
            }(e, c);

            ptr.deref().var = [](env e, cursor& c) -> type_var::variant {
                return path_switch<type_var::variant>{c}
                    .path<final::ID>(
                        [](env e, cursor& c) -> type_var::variant {
                            const auto begin = c;
                            do {
                                expect_node<final::ID>(c++.get());
                            } while (c.get().isa(final::DCOLON) && c++.cursor);
                            return type_var::unresolved_t{{begin, c}};
                        },
                        e,
                        c)
                    .path<final::TYPE_USIZE>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::uint_t{64};
                        },
                        e,
                        c)
                    .path<final::TYPE_ISIZE>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::sint_t{64};
                        },
                        e,
                        c)
                    .path<final::UINT_TYPE>(
                        [](env e, cursor& c) -> type_var::variant {
                            const auto len = c++.get().unsafe_final().data;
                            return type_var::uint_t{len};
                        },
                        e,
                        c)
                    .path<final::SINT_TYPE>(
                        [](env e, cursor& c) -> type_var::variant {
                            const auto len = c++.get().unsafe_final().data;
                            return type_var::sint_t{len};
                        },
                        e,
                        c)
                    .path<final::F16>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::f16_t{};
                        },
                        e,
                        c)
                    .path<final::F32>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::f32_t{};
                        },
                        e,
                        c)
                    .path<final::F64>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::f64_t{};
                        },
                        e,
                        c)
                    .path<final::F128>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::f128_t{};
                        },
                        e,
                        c)
                    .path<final::REC>(
                        [](env e, cursor& c) -> type_var::variant {
                            return type_patterns::rec(e, c);
                        },
                        e,
                        c)
                    .path<final::VOID>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::void_t{};
                        },
                        e,
                        c)
                    .path<final::OPAQUE>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            bool nullable = true;
                            return type_var::optr_t{};
                        },
                        e,
                        c)
                    .path<final::MUL>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            bool nullable = true;
                            return type_var::ptr_t{
                                .type = type_fn(e, c),
                            };
                        },
                        e,
                        c)
                    .def(
                        [](env e, cursor& c) -> type_var::variant {
                            throw_error(
                                std::format("Unknown type {}", lexer::str(c.get())));
                        },
                        e,
                        c);
            }(e, c);

            return ptr;
        }

        template <auto fn>
        ref_decl make_decl(env e, cursor& c, intern_id name) {
            auto mem = alloc_and_insert_decl(e, name);
            mem.deref().data = fn(e, c, mem);

            // consume_untill<final::e::TERMINATOR>(c);
            return mem;
        }

        namespace decl_patterns {
            auto type_alias(env e, cursor& c, ref_decl ptr) {
                c++;
                const auto type = type_fn(e, c);
                return decl_var::type_alias_t{type};
            }

            auto fn(env e, cursor& c, ref_decl ptr) {
                c++;

                ref_symbols symbols = e.allocator.alloc_one<symbols_t>();
                auto staging = staging_vec<ref_decl>(e.allocator);

                auto& parens = c++.get().unsafe_median();
                expect<median::PARENS>(parens.code);

                std::uint32_t index = 0;
                median_loop(
                    e.with(symbols),
                    parens,
                    [&staging, &index](env e, cursor& c) {
                        auto& name_node = c++.get();
                        auto name = name_node.unsafe_final().data;

                        expect_node<final::COLON>(c++.get());

                        const auto type = type_fn(e, c);

                        auto param = alloc_and_insert_decl(e, name);
                        param.deref().data =
                            decl_var::fn_parameter_t{.type = type, .index = index++};
                        staging.push_back(param);
                    });

                auto args = staging.commit();

                const auto ret_type = type_fn(e, c);

                expect_node<final::ASIGN>(c++.get());

                auto body = expr_fn(e, c);
                consume_untill<final::TERMINATOR>(c);

                return decl_var::fn_t{
                    .symbols = symbols,
                    .args = args,
                    .ret_type = ret_type,
                    .body = body,
                };
            }

            // could be a variable or something elsei like a function
            auto var(env e, cursor& c, ref_decl ptr) {
                const auto type = type_fn(e, c);

                if (!c->isa(final::ASIGN)) {
                    throw_error("Declaration expects a value");
                }
                c++;

                auto init_expr = expr_fn(e, c);

                return decl_var::var_t{.type = type, .init_expr = init_expr};
            }
        } // namespace decl_patterns

        ref_decl decl_fn(env e, cursor& c) {
            auto& name_node = c++.get();
            auto name = name_node.unsafe_final().data;
            ++c;
            return path_switch<ref_decl>{c}
                .path<final::TYPE>(make_decl<decl_patterns::type_alias>, e, c, name)
                .path<final::FN>(make_decl<decl_patterns::fn>, e, c, name)
                .def(make_decl<decl_patterns::var>, e, c, name);
        }

        namespace stmt_patterns {
            stmt_var::variant loop_fn(env e, cursor& c) {
                return {};
            }

            stmt_var::variant break_fn(env e, cursor& c) {
                c++;
                if (c.get().isa(final::TERMINATOR))
                    return stmt_var::break_t{std::nullopt};
                else
                    return stmt_var::break_t{expr_fn(e, c)};
            }

            stmt_var::variant return_fn(env e, cursor& c) {
                c++;
                if (c.get().isa(final::TERMINATOR))
                    return stmt_var::return_t{std::nullopt};
                else
                    return stmt_var::return_t{expr_fn(e, c)};
            }

            stmt_var::variant decl_stmt_fn(env e, cursor& c) {
                auto decl = decl_fn(e, c);
                return {decl};
            }

            stmt_var::variant expr_stmt_fn(env e, cursor& c) {
                return expr_fn(e, c);
            }
        } // namespace stmt_patterns

        ref_stmt stmt_fn(env e, cursor& c) {
            auto stmt = e.allocator.alloc_one<stmt_t>();

            stmt->data =
                path_switch<stmt_var::variant>{c}
                    .path<final::ID, final::COLON>(stmt_patterns::decl_stmt_fn, e, c)
                    .path<final::BREAK>(stmt_patterns::break_fn, e, c)
                    .path<final::RETURN>(stmt_patterns::return_fn, e, c)
                    // .path<median::BRACES>(attribute_fn, e, c)
                    // .path<final::LOOP>(stmt_patterns::loop_fn, e, c)
                    // .path<final::BECOME>(stmt_patterns::become_fn, e, c)
                    .def(stmt_patterns::expr_stmt_fn, e, c);
            return stmt;
        }

        ref_stmts stmts_fn(env e, cursor& c) {
            auto staging = staging_vec<ref_stmt>(e.allocator);
            node2ast::median_loop(e, c.get().unsafe_median(), [](env e, cursor& c) {
                node2ast::stmt_fn(e, c);
            });
            const auto span = staging.commit();

            ref ptr = e.allocator.alloc_one<stmts_t>(span);
            return ptr;
        }

    } // namespace node2ast

    void test_type_eq(allocator_t& allocator) {
        // uint same size
        {
            std::println("test: uint same size");
            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::uint_t{32};
            b.deref().var = type_var::uint_t{32};
            assert(type_eq::eq(a, b));
            std::println("  pass");
        }
        // uint different size
        {
            std::println("test: uint different size");
            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::uint_t{32};
            b.deref().var = type_var::uint_t{64};
            assert(!type_eq::eq(a, b));
            std::println("  pass");
        }
        // uint vs sint
        {
            std::println("test: uint vs sint");
            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::uint_t{32};
            b.deref().var = type_var::sint_t{32};
            assert(!type_eq::eq(a, b));
            std::println("  pass");
        }
        // ptr to same type
        {
            std::println("test: ptr to same type");
            ref inner_a = allocator.alloc_one<type_t>();
            ref inner_b = allocator.alloc_one<type_t>();
            inner_a.deref().var = type_var::uint_t{32};
            inner_b.deref().var = type_var::uint_t{32};

            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::ptr_t{inner_a};
            b.deref().var = type_var::ptr_t{inner_b};
            assert(type_eq::eq(a, b));
            std::println("  pass");
        }
        // ptr to different types
        {
            std::println("test: ptr to different types");
            ref inner_a = allocator.alloc_one<type_t>();
            ref inner_b = allocator.alloc_one<type_t>();
            inner_a.deref().var = type_var::uint_t{32};
            inner_b.deref().var = type_var::uint_t{64};

            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::ptr_t{inner_a};
            b.deref().var = type_var::ptr_t{inner_b};
            assert(!type_eq::eq(a, b));
            std::println("  pass");
        }
        // rec same members
        {
            std::println("test: rec same members");
            ref symbols_a = allocator.alloc_one<symbols_t>();
            ref symbols_b = allocator.alloc_one<symbols_t>();

            ref field_type_a = allocator.alloc_one<type_t>();
            ref field_type_b = allocator.alloc_one<type_t>();
            field_type_a.deref().var = type_var::uint_t{32};
            field_type_b.deref().var = type_var::uint_t{32};

            ref mem_a =
                allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{field_type_a, 0});
            ref mem_b =
                allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{field_type_b, 0});

            auto span_a = allocator.alloc_many<ref_decl>(1);
            span_a[0] = mem_a;

            auto span_b = allocator.alloc_many<ref_decl>(1);
            span_b[0] = mem_b;

            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::rec_t{symbols_a, span_a};
            b.deref().var = type_var::rec_t{symbols_b, span_b};
            assert(type_eq::eq(a, b));
            std::println("  pass");
        }
        // rec different member types
        {
            std::println("test: rec different member types");
            ref symbols_a = allocator.alloc_one<symbols_t>();
            ref symbols_b = allocator.alloc_one<symbols_t>();

            ref field_type_a = allocator.alloc_one<type_t>();
            ref field_type_b = allocator.alloc_one<type_t>();
            field_type_a.deref().var = type_var::uint_t{32};
            field_type_b.deref().var = type_var::uint_t{64};

            ref mem_a =
                allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{field_type_a, 0});
            ref mem_b =
                allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{field_type_b, 0});

            auto span_a = allocator.alloc_many<ref_decl>(1);
            span_a[0] = mem_a;

            auto span_b = allocator.alloc_many<ref_decl>(1);
            span_b[0] = mem_b;

            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::rec_t{symbols_a, span_a};
            b.deref().var = type_var::rec_t{symbols_b, span_b};
            assert(!type_eq::eq(a, b));
            std::println("  pass");
        }
        // rec different member count
        {
            std::println("test: rec different member count");
            ref symbols_a = allocator.alloc_one<symbols_t>();
            ref symbols_b = allocator.alloc_one<symbols_t>();

            ref ft = allocator.alloc_one<type_t>();
            ft.deref().var = type_var::uint_t{32};

            ref mem_a1 = allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{ft, 0});
            ref mem_a2 = allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{ft, 1});
            ref mem_b = allocator.alloc_one<decl_t>(0, decl_var::rec_member_t{ft, 0});

            auto span_a = allocator.alloc_many<ref_decl>(2);
            span_a[0] = mem_a1;
            span_a[1] = mem_a2;

            auto span_b = allocator.alloc_many<ref_decl>(1);
            span_b[0] = mem_b;

            ref a = allocator.alloc_one<type_t>();
            ref b = allocator.alloc_one<type_t>();
            a.deref().var = type_var::rec_t{symbols_a, span_a};
            b.deref().var = type_var::rec_t{symbols_b, span_b};
            assert(!type_eq::eq(a, b));
            std::println("  pass");
        }

        std::println("all type_eq tests passed");
    }

    ref_stmts entry(allocator_t& allocator, const lexer::buffer& buffer) {
        auto& node = buffer.get_node(0);
        auto c = cursor{node.children()};

        auto symbols = allocator.alloc_one<symbols_t>();
        auto root = allocator.alloc_one<ast_t>();

        test_type_eq(allocator);

        auto e = env{buffer, allocator, symbols, root};

        auto file = node2ast::stmts_fn(e, c);
        return file;
        // auto staging = staging_vec<ref_stmt>(allocator);
        // node2ast::median_loop(e, node.unsafe_median(), [](env e, cursor& c) {
        //     node2ast::stmt_fn(e, c);
        // });
        // const auto stmts = staging.commit();
    }
} // namespace ast

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println("usage: {} <file>", argv[0]);
        return 1;
    }
    std::string_view filepath = argv[1];
    lexer::intern_table intern_table;
    auto text = load_file(filepath);
    if (!text) [[unlikely]] {
        throw std::runtime_error(text.error());
    }

    std::println("file=\"{}\"", filepath);

    source src{std::string(filepath), std::move(text.value())};
    const auto lexer_output = lexer::entry(src, intern_table);
    lexer::pretty_print(lexer_output, src);

    llvm_allocator arena;
    auto file = ast::entry(arena, lexer_output);
    (void)file;

    return 0;
}
