// !! check the compile.sh to see how to compile !!

#include "./file_loader.cpp"

#include "../include/utf8.h"
#include "../include/utf8/checked.h"

#include "../libs/llvm_allocator.hpp"
#include "../libs/map.hpp"
#include "../libs/meta.hpp"
#include "../libs/ref.hpp"
#include "../libs/vector.hpp"

#include <boost/pfr/core.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <flat_map>
#include <flat_set>
#include <format>
#include <functional>
#include <iostream>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringMapEntry.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSwitch.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Analysis/TypeBasedAliasAnalysis.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/TypeSize.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
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

template <typename... Func>
struct overload : Func... {
    using Func::operator()...;
};

template <typename... Func>
overload(Func...) -> overload<Func...>;

// I hate this
// I could eliminate this by having 2 allocators
// 1 for temporary allocations like vectors and once they are done I copy paste them to
// the main one and 1 for long living allocations refrences
template <typename T>
struct staging_vec {
    using Allocator = llvm_allocator;

    Allocator& arena;
    llvm::SmallVector<T, 5> staging;

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

using intern_id = std::uint32_t;

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
    uint32_t data = 0; // IDs use it for names

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
        uint32_t last_terminator_depth = 0;
        uint32_t prev_depth = 0; // tracks per open and close
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

    auto peek(source_view src, std::uint32_t cursor) -> unsigned char {
        if (!within_src(src, cursor + 1))
            return 0;
        return static_cast<unsigned char>(src[cursor + 1]);
    }

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

    // void cond_insert_invisible_separator(unit& u) {
    //     auto& last_node = u.buffer.out.nodes.back();
    //     auto& last_loc = u.buffer.out.locs.back();

    //     last_node.visit(
    //         [&](const final& f) {
    //             auto loc = last_loc;
    //             loc.end_index = 0;

    //             bool depth_equal = (u.last_terminator_depth == u.depth - 1);
    //             bool is_terminator = (f == final::TERMINATOR);

    //             u.buffer.push(get_terminator(), loc);
    //             // if (u.prev_depth != u.last_terminator_depth) {
    //             //     u.last_terminator_depth = u.depth;

    //             //     u.buffer.push(get_terminator(), loc);
    //             // } else if (skip) {
    //             // } else if (depth_equal && !is_terminator) {
    //             //     u.last_terminator_depth = u.depth;

    //             //     u.buffer.push(get_terminator(), loc);
    //             // } else if (!depth_equal) {
    //             //     u.last_terminator_depth = u.depth;

    //             // }
    //         },
    //         [](const median&) {});
    // }

    std::uint32_t open_median(unit& u,
                              median::e code,
                              string_view src,
                              std::uint32_t token_begin,
                              std::uint32_t cursor) {

        auto loc = source_location{u.line, token_begin, token_begin + 1};
        auto index = u.buffer.push(node(code, 0), loc);
        u.prev_depth = u.depth;
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
        u.prev_depth = u.depth;
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

            if (!symetrical_close_match(open_index.code, c)) [[unlikely]]
                std::abort();

            auto prev_non_whitespace = [&]() -> unsigned char {
                auto i = cursor - 2;
                while (i > 0 && (whitesapce::is_horizontal(src[i]) ||
                                 whitesapce::is_vertical(src[i])))
                    --i;
                return static_cast<unsigned char>(src[i]);
            };

            if (prev_non_whitespace() != ';') {
                auto loc = u.buffer.out.locs.back();
                loc.end_index = 0;
                u.buffer.push(get_terminator(), loc);
                u.last_terminator_depth = u.depth;
            }

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
            throw_error(std::format("Unknown builtin '@{}'", std::string_view(word)));

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
        // cond_insert_invisible_separator(u);
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
        internal_pretty_print(buf, src, i, 0);
        std::println("\n");
        return;
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

namespace ast {
    using cursor = const_cursor_t;

    struct decl_t;
    struct type_t;
    struct expr_t;
    struct stmt_t;
    struct stmts_t;
    struct scope_t;

    using ref_type = ref<type_t>;
    using ref_expr = ref<expr_t>;
    using ref_decl = ref<decl_t>;
    using ref_stmts = ref<stmts_t>;
    using ref_stmt = ref<stmt_t>;
    using ref_scope = ref<scope_t>;

    struct scope_t {
        using entry_t = ref_decl;

        struct lookup_result {
            ref_scope where;
            entry_t symbol;
        };

        ref_scope parent;
        map<intern_id, entry_t> table = {};

        template <bool is_local>
        static std::optional<lookup_result> lookup_impl(ref_scope& self, intern_id key) {
            assert(self.is_valid());
            auto it = self.deref().table.find(key);
            if (it != self.deref().table.end())
                return lookup_result{self, it->second};

            if constexpr (!is_local)
                if (self.deref().parent)
                    return lookup_impl<false>(self.deref().parent, key);
            return std::nullopt;
        }

        static std::optional<lookup_result> local_lookup(ref_scope& self,
                                                         intern_id name) {
            return lookup_impl<true>(self, name);
        }

        static std::optional<lookup_result> ancestor_lookup(ref_scope& self,
                                                            intern_id name) {
            return lookup_impl<false>(self, name);
        }

        static ref_scope get_root(ref_scope current) {
            if (!current.deref().parent)
                return current;
            [[clang::musttail]] return get_root(current.deref().parent);
        }
    };

    namespace util {
        struct frame {
            ref_scope scope;
            ref_stmts stmts;
        };

        struct unresolved_range {
            cursor begin;
            cursor end;
        };
        struct unresolved_name {
            intern_id name;
        };
    } // namespace util

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
            ref_scope scope;
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

        struct decl {
            ref_decl ref;
        };
        struct expr {
            ref_expr ref;
        };

        struct loop_t {
            ref_expr cond;
            std::optional<ref_expr> incr;
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
            util::unresolved_name name;
        };
        struct block_t {
            util::frame frame;
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

        struct rec_init_t {
            ref_type type;
            std::span<ref_expr> args;
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

        struct access_t {
            ref_expr lhs;
            ref_expr rhs;
        };

        enum class op_e {
            opcall,
            // opaccess,
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

        enum class op_pos : std::uint8_t { prefix, infix, postfix };

        struct op_meta_t {
            op_pos pos;
            std::uint8_t prec;
            bool right_assoc = false;

            constexpr bool is_prefix() const {
                return pos == op_pos::prefix;
            }
            constexpr bool is_infix() const {
                return pos == op_pos::infix;
            }
            constexpr bool is_postfix() const {
                return pos == op_pos::postfix;
            }
            constexpr bool is_right_assoc() const {
                return right_assoc;
            }
            constexpr bool is_left_assoc() const {
                return !right_assoc;
            }
        };

        constexpr op_meta_t op_meta(const op_e op) {
            switch (op) {
            // postfix
            case op_e::opcall:
                return {op_pos::postfix, 7};
            // infix
            // case op_e::opaccess:
            //     return {op_pos::infix, 7};
            case op_e::opmul:
                return {op_pos::infix, 6};
            case op_e::opdiv:
                return {op_pos::infix, 6};
            case op_e::opadd:
                return {op_pos::infix, 5};
            case op_e::opsub:
                return {op_pos::infix, 5};
            case op_e::opand:
                return {op_pos::infix, 3};
            case op_e::opor:
                return {op_pos::infix, 2};
            }
        }
        constexpr int left_bp(op_meta_t meta) {
            return meta.prec;
        }
        constexpr int right_bp(op_meta_t meta) {
            return meta.right_assoc ? meta.prec : meta.prec + 1;
        }

        using variant = std::variant<unresolved_t,
                                     access_t,
                                     block_t,
                                     rec_init_t,
                                     as_t,
                                     call_payload_t,
                                     int_literal_t,
                                     float_literal_t,
                                     bool_literal_t,
                                     name_t,
                                     // rec_access_t,
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
            ref_scope scope;
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
            util::unresolved_range data;
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
        type_var::variant data;
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
                a.data,
                b.data);
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
        ref_scope scope;
        // ref_ast parent;

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
            } else if constexpr (std::is_same_v<U, ref_scope>) {
                scope = std::forward<T>(value);
                // } else if constexpr (std::is_same_v<U, ref_ast>) {
                //     parent = std::forward<T>(value);
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

    ref_scope make_scope(allocator_t& allocator, ref_scope parent = {}) {
        ref_scope scope = allocator.alloc_one<scope_t>();
        scope.deref().parent = parent;
        return scope;
    }

    ref_scope make_scope(const env& e) {
        return make_scope(e.allocator, e.scope);
    }

    ref_expr make_expr(env e, expr_var::variant&& data) {
        ref ptr = e.allocator.alloc_one<expr_t>();
        ptr.deref().parent = nullptr;
        ptr.deref().data = std::move(data);
        return ptr;
    }
    ref_decl alloc_decl(allocator_t& allocator, std::uint32_t name) {
        return allocator.alloc_one<decl_t>(name, decl_var::variant{});
    }

    ref_decl alloc_and_insert_decl(env e, intern_id name) {
        const auto lookup = scope_t::local_lookup(e.scope, name);
        if (lookup) {
            throw_error("Found symbol with the same name");
        }
        auto mem = alloc_decl(e.allocator, name);
        const auto [_, inserted] = e.scope.deref().table.emplace(name, mem);
        if (!inserted)
            throw_error("Unable to insert declaration");

        return mem;
    }

    ref_scope scope_of_type(const ref_type& t);
    ref_scope scope_of_decl(const ref_decl& d);

    ref_scope scope_of(const type_var::rec_t& v) {
        return v.scope;
    }

    ref_scope scope_of(const type_var::type_alias_t& v) {
        return scope_of_type(v.type);
    }

    ref_scope scope_of(const type_var::ptr_t& v) {
        return scope_of_type(v.type);
    }

    template <typename T>
    ref_scope scope_of(const T&) {
        throw_error(std::format("Cannot get scope of type {}", type_str<T>()));
    }

    ref_scope scope_of_type(const ref_type& t) {
        return std::visit([](const auto& v) { return scope_of(v); }, t.deref().data);
    }

    ref_scope scope_of_decl(const ref_decl& d) {
        return std::visit(
            overload{
                [](const decl_var::type_alias_t& v) { return scope_of_type(v.type); },
                [](const auto&) -> ref_scope {
                    throw_error("Cannot get scope of non-type decl");
                },
            },
            d.deref().data);
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
            const cursor c;
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
        auto ch_loop(env e, cursor c, Fn&& fn) {
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
        template <typename Fn>
        auto median_loop(env e, const median& m, Fn&& fn) {
            const auto ch = m.children();
            auto c = cursor(ch);
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

        ref_type type_fn(env e, cursor& c);
        ref_expr expr_fn(env e, cursor& c, int min_prec = 0);
        ref_stmts stmts_fn(env e, cursor& c);

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

                expr_var::variant rec_init(env e, cursor& c) {
                    c++;
                    auto& type_parens = c++.get().unsafe_median();
                    expect<median::PARENS>(type_parens.code);
                    auto type_c = cursor(type_parens.children());
                    const auto type = type_fn(e, type_c);

                    auto& args_parens = c++.get().unsafe_median();
                    expect<median::PARENS>(args_parens.code);
                    auto staging = staging_vec<ref_expr>(e.allocator);
                    median_loop(e, args_parens, [&staging](env e, cursor& c) {
                        staging.push_back(expr_fn(e, c));
                    });
                    const auto args = staging.commit();

                    return expr_var::rec_init_t{
                        .type = type,
                        .args = args,
                    };
                }

                expr_var::variant single_id(env e, cursor& c) {
                    const auto name = expect_node<final::ID>(c++.get()).data;
                    return expr_var::unresolved_t{name};
                }

                expr_var::variant block(env e, cursor& c) {
                    const auto& node = c++.get();

                    ref scope = make_scope(e);
                    auto stmts_cursor = cursor(node.unsafe_median().children());
                    const auto stmts = stmts_fn(e.with(scope), stmts_cursor);
                    return expr_var::block_t{util::frame(scope, stmts)};
                }

                ref_expr parse(env e, cursor& c);

                template <auto fn>
                expr_var::variant can_chain(env e, cursor& c) {
                    auto lhs = fn(e, c);
                    if (!c->isa(final::DCOLON)) {
                        return lhs;
                    }
                    c++;

                    auto rhs_ptr = parse(e, c);
                    auto lhs_ptr = make_expr(e, std::move(lhs));

                    return expr_var::access_t{lhs_ptr, rhs_ptr};
                }

                ref_expr parse(env e, cursor& c) {
                    return make_expr(
                        e,
                        path_switch<expr_var::variant>{c}
                            .path<final::DUCKLING>(rec_init, e, c)
                            .path<final::INT>(int_lit, e, c)
                            .path<final::FLOAT>(float_lit, e, c)
                            .path<final::TRUE>(bool_true, e, c)
                            .path<final::FALSE>(bool_false, e, c)
                            .path<final::ID>(can_chain<single_id>, e, c)
                            .path<median::PARENS>(can_chain<block>, e, c)
                            .def(
                                [](env e, cursor& c) -> expr_var::variant {
                                    throw_error(std::format("Expected operand, got {}",
                                                            lexer::str(c.get())));
                                },
                                e,
                                c));
                }
            } // namespace operands

            namespace operators {
                std::optional<expr_var::op_e> peek_op(const cursor& c) {
                    const auto& v = c.get();
                    if (v.isa(median::PARENS))
                        return expr_var::op_e::opcall;
                    // else if (v.isa(final::DCOLON))
                    //         return expr_var::op_e::opaccess;
                    else if (v.isa(final::PLUS))
                        return expr_var::op_e::opadd;
                    else if (v.isa(final::MINUS))
                        return expr_var::op_e::opsub;
                    else if (v.isa(final::MUL))
                        return expr_var::op_e::opmul;
                    else if (v.isa(final::DIV))
                        return expr_var::op_e::opdiv;
                    else if (v.isa(final::LOGICAL_AND))
                        return expr_var::op_e::opand;
                    else if (v.isa(final::LOGICAL_OR))
                        return expr_var::op_e::opor;
                    return std::nullopt;
                }

                ref_expr call(env e, cursor& c, ref_expr callee) {
                    auto& parens = c++.get().unsafe_median();
                    expect<median::PARENS>(parens.code);

                    auto staging = staging_vec<ref_expr>(e.allocator);
                    median_loop(e, parens, [&staging](env e, cursor& c) {
                        staging.push_back(expr_fn(e, c));
                    });
                    auto args = staging.commit();

                    return make_expr(
                        e,
                        expr_var::unary_op_t{
                            .op = expr_var::op_e::opcall,
                            .operand = callee,
                            .payload = expr_var::unary_op_t::payload_t{.args = args},
                        });
                }

                ref_expr infix(env e, cursor& c, ref_expr lhs, expr_var::op_e op) {
                    c++; // consume operator token
                    const auto meta = expr_var::op_meta(op);
                    ref_expr rhs = expr_fn(e, c, expr_var::right_bp(meta));
                    return make_expr(e,
                                     expr_var::binary_op_t{
                                         .op = op,
                                         .lhs = lhs,
                                         .rhs = rhs,
                                     });
                }

                ref_expr
                parse_postfix(env e, cursor& c, ref_expr lhs, expr_var::op_e op) {
                    switch (op) {
                    case expr_var::op_e::opcall:
                        return call(e, c, lhs);
                    default:
                        throw_error(std::format("Unknown postfix operator"));
                    }
                }

            } // namespace operators
        } // namespace expr_patterns

        ref_expr expr_fn(env e, cursor& c, int min_prec) {
            ref_expr lhs = expr_patterns::operands::parse(e, c);

            while (c.within() && !c.get().isa(final::TERMINATOR)) {
                const auto maybe_op = expr_patterns::operators::peek_op(c);
                if (!maybe_op)
                    break;

                const auto meta = expr_var::op_meta(*maybe_op);
                if (expr_var::left_bp(meta) < min_prec)
                    break;

                if (meta.is_postfix()) {
                    lhs = expr_patterns::operators::parse_postfix(e, c, lhs, *maybe_op);
                } else if (meta.is_infix()) {
                    lhs = expr_patterns::operators::infix(e, c, lhs, *maybe_op);
                } else {
                    throw_error(std::format("Unexpected prefix op in infix position"));
                }
            }

            return lhs;
        }

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

                ref scope = make_scope(e);

                auto staging = staging_vec<ref_decl>(e.allocator);
                median_loop(e.with(scope), members, [&staging](env e, cursor& c) -> void {
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
                return {.scope = scope, .members = member_span};
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
                    .def([](cursor& c) -> mutability::t { return mutability::none(); },
                         c);
            }(e, c);

            ptr.deref().data = [](env e, cursor& c) -> type_var::variant {
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

                auto scope = make_scope(e);
                auto staging = staging_vec<ref_decl>(e.allocator);

                auto& parens = c++.get().unsafe_median();
                expect<median::PARENS>(parens.code);

                std::uint32_t index = 0;
                median_loop(e.with(scope), parens, [&staging, &index](env e, cursor& c) {
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
                    .scope = scope,
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
                if (c.get().isa(final::TERMINATOR)) {
                    return stmt_var::return_t{std::nullopt};
                } else {
                    return stmt_var::return_t{expr_fn(e, c)};
                }
            }

            stmt_var::variant decl_stmt_fn(env e, cursor& c) {
                auto decl = decl_fn(e, c);
                return stmt_var::decl{decl};
            }

            stmt_var::variant expr_stmt_fn(env e, cursor& c) {
                return stmt_var::expr{expr_fn(e, c)};
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

            ch_loop(e, c, [&staging](env e, cursor& c) {
                staging.push_back(node2ast::stmt_fn(e, c));
            });
            const auto span = staging.commit();

            ref ptr = e.allocator.alloc_one<stmts_t>(span);
            return ptr;
        }

    } // namespace node2ast

    // Devilish
    //  I kinda like it but at the same time I hate it
    //  WHY USE STD::FUNCTION TO RESOLVE AND NOT PASS THE Unit or whatever other context?
    //  it works though so it is what it is who gives a cares about the printer
    // maybe not what i had hopped from the MachineGod gemmini to give me back
    // hard coding the operators is diaboloical
    // this thing will be a nihtmare to update
    namespace SLOP {

        static std::string default_name_resolver(std::uint32_t id) {
            return "id_" + std::to_string(id);
        }

        template <typename NameResolver = decltype(&default_name_resolver)>
        struct printer {
            std::ostream& os;
            NameResolver resolve;
            int indent = 0;

            explicit printer(std::ostream& os,
                             NameResolver resolve = &default_name_resolver) :
                os(os),
                resolve(std::move(resolve)) {}

            // --- ref overloads ---

            void print(ref_type t) {
                t ? print(t.deref()) : (void)(os << "null");
            }
            void print(ref_expr e) {
                e ? print(e.deref()) : (void)(os << "null");
            }
            void print(ref_decl d) {
                d ? print(d.deref()) : (void)(os << "null");
            }
            void print(ref_stmt s) {
                s ? print(s.deref()) : (void)(os << "null");
            }
            void print(ref_stmts s) {
                s ? print(s.deref()) : (void)(os << "null");
            }

            // --- concrete overloads ---

            void print(const type_t& t) {
                if (mutability::has(t.mut))
                    os << mutability::str(t.mut) << ' ';
                std::visit([this](const auto& v) { print_type(v); }, t.data);
            }

            void print(const expr_t& e) {
                std::visit([this](const auto& v) { print_expr(v); }, e.data);
            }

            void print(const stmt_t& s) {
                std::visit([this](const auto& v) { print_stmt(v); }, s.data);
            }

            void print(const stmts_t& s) {
                for (const auto& stmt : s.span) {
                    indent_line();
                    print(stmt);
                    os << '\n';
                }
            }

            void print(const decl_t& d) {
                std::visit([this, &d](const auto& v) { print_decl(d, v); }, d.data);
            }

          private:
            void indent_line() {
                for (int i = 0; i < indent; ++i)
                    os << "    ";
            }

            template <typename Fn>
            void indented(Fn&& fn) {
                ++indent;
                fn();
                --indent;
            }

            void print_args(std::span<ref_expr> args) {
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i)
                        os << ", ";
                    print(args[i]);
                }
            }

            static constexpr std::string_view op_str(expr_var::op_e op) {
                switch (op) {
                case expr_var::op_e::opadd:
                    return "+";
                case expr_var::op_e::opsub:
                    return "-";
                case expr_var::op_e::opmul:
                    return "*";
                case expr_var::op_e::opdiv:
                    return "/";
                case expr_var::op_e::opand:
                    return "&&";
                case expr_var::op_e::opor:
                    return "||";
                case expr_var::op_e::opcall:
                    return "<call>";
                }
            }

            // --- type variants ---

            void print_type(const type_var::void_t&) {
                os << "void";
            }
            void print_type(const type_var::optr_t&) {
                os << "optr";
            }
            void print_type(const type_var::integer_literal_t&) {
                os << "int_literal";
            }
            void print_type(const type_var::float_literal_t&) {
                os << "float_literal";
            }
            void print_type(const type_var::uint_t& v) {
                os << 'u' << v.bit_size;
            }
            void print_type(const type_var::sint_t& v) {
                os << 'i' << v.bit_size;
            }
            void print_type(const type_var::unresolved_t&) {
                os << "<unresolved>";
            }
            void print_type(const type_var::type_alias_t& v) {
                print(v.type);
            }

            template <size_t S>
            void print_type(const type_var::fp_base<S>&) {
                os << 'f' << S;
            }

            void print_type(const type_var::ptr_t& v) {
                os << '*';
                print(v.type);
            }

            void print_type(const type_var::rec_t& v) {
                os << "rec {\n";
                indented([&] {
                    for (const auto& m : v.members) {
                        indent_line();
                        print(m);
                        os << ";\n";
                    }
                });
                indent_line();
                os << '}';
            }

            // --- expr variants ---

            void print_expr(const expr_var::int_literal_t& v) {
                os << v.value;
            }
            void print_expr(const expr_var::float_literal_t& v) {
                os << v.value;
            }
            void print_expr(const expr_var::bool_literal_t& v) {
                os << (v.value ? "true" : "false");
            }
            void print_expr(const expr_var::unresolved_t&) {
                os << "<unresolved>";
            }

            void print_expr(const expr_var::name_t& v) {
                os << (v.decl ? resolve(v.decl.deref().name) : "<null>");
            }

            void print_expr(const expr_var::access_t& v) {
                print(v.lhs);
                os << "::";
                print(v.rhs);
            }

            void print_expr(const expr_var::as_t& v) {
                print(v.expr);
                os << " as ";
                print(v.type);
            }

            void print_expr(const expr_var::bitcast_t& v) {
                os << "bitcast<";
                print(v.type);
                os << ">(";
                print(v.expr);
                os << ')';
            }

            void print_expr(const expr_var::rec_init_t& v) {
                print(v.type);
                os << "{ ";
                print_args(v.args);
                os << " }";
            }

            void print_expr(const expr_var::call_payload_t& v) {
                os << '(';
                print_args(v.args);
                os << ')';
            }

            void print_expr(const expr_var::binary_op_t& v) {
                os << '(';
                print(v.lhs);
                os << ' ' << op_str(v.op) << ' ';
                print(v.rhs);
                os << ')';
            }

            void print_expr(const expr_var::unary_op_t& v) {
                if (v.op == expr_var::op_e::opcall) {
                    print(v.operand);
                    os << '(';
                    print_args(v.payload.args);
                    os << ')';
                } else {
                    os << op_str(v.op);
                    print(v.operand);
                }
            }

            void print_expr(const expr_var::block_t& v) {
                os << "{\n";
                indented([&] { print(v.frame.stmts); });
                indent_line();
                os << '}';
            }

            // --- stmt variants ---

            void print_stmt(const stmt_var::decl& v) {
                print(v.ref);
                os << ';';
            }

            void print_stmt(const stmt_var::expr& v) {
                print(v.ref);
                os << ';';
            }

            void print_stmt(const stmt_var::return_t& v) {
                os << "return";
                if (v.val) {
                    os << ' ';
                    print(*v.val);
                }
                os << ';';
            }

            void print_stmt(const stmt_var::break_t& v) {
                os << "break";
                if (v.val) {
                    os << ' ';
                    print(*v.val);
                }
                os << ';';
            }

            // --- decl variants ---

            void print_decl(const decl_t& d, const decl_var::var_t& v) {
                print(v.type);
                os << ' ' << resolve(d.name);
                if (v.init_expr) {
                    os << " = ";
                    print(v.init_expr);
                }
            }

            void print_decl(const decl_t& d, const decl_var::rec_member_t& v) {
                os << resolve(d.name) << " : ";
                print(v.type);
            }

            void print_decl(const decl_t& d, const decl_var::tagged_union_member_t& v) {
                os << "variant " << resolve(d.name) << " : ";
                print(v.type);
            }

            void print_decl(const decl_t& d, const decl_var::fn_parameter_t& v) {
                os << resolve(d.name) << " : ";
                print(v.type);
            }

            void print_decl(const decl_t& d, const decl_var::type_alias_t& v) {
                os << "type " << resolve(d.name) << " = ";
                print(v.type);
            }

            void print_decl(const decl_t& d, const decl_var::fn_t& v) {
                os << "fn " << resolve(d.name) << '(';
                for (size_t i = 0; i < v.args.size(); ++i) {
                    if (i)
                        os << ", ";
                    print(v.args[i]);
                }
                os << ") -> ";
                print(v.ret_type);
                os << ' ';
                print(v.body);
            }
        };

        template <typename NameResolver>
        printer(std::ostream&, NameResolver) -> printer<NameResolver>;

    } // namespace SLOP
    template <typename T>
    struct is_ref : std::false_type {};
    template <typename T>
    struct is_ref<ref<T>> : std::true_type {};
    template <typename T>
    inline constexpr bool is_ref_v = is_ref<T>::value;

    template <typename T>
    struct is_variant : std::false_type {};
    template <typename... Ts>
    struct is_variant<std::variant<Ts...>> : std::true_type {};
    template <typename T>
    inline constexpr bool is_variant_v = is_variant<T>::value;

    template <typename T>
    struct is_span_of_ref : std::false_type {};
    template <typename T>
    struct is_span_of_ref<std::span<ref<T>>> : std::true_type {};
    template <typename T>
    inline constexpr bool is_span_of_ref_v = is_span_of_ref<T>::value;

    template <typename T>
    struct is_optional_of_ref : std::false_type {};
    template <typename T>
    struct is_optional_of_ref<std::optional<ref<T>>> : std::true_type {};
    template <typename T>
    inline constexpr bool is_optional_of_ref_v = is_optional_of_ref<T>::value;

    namespace walker {
        template <typename Derived>
        struct t {

            constexpr Derived* derived() {
                return static_cast<Derived*>(this);
            }

            template <typename Base, typename T>
            void for_each_member(ref<Base> ptr, T& v) {
                boost::pfr::for_each_field(v, [&]<typename F>(F& field) {
                    if constexpr (is_ref_v<F> && !std::is_same_v<ref_scope, F>) {
                        visit(field);
                    } else if constexpr (is_optional_of_ref_v<F>) {
                        if (field) {
                            visit(*field);
                        }
                    } else if constexpr (is_span_of_ref_v<F>) {
                        for (auto& elm : field) {
                            visit(elm);
                        }
                    } else if constexpr (requires(T t) {
                                             { t.frame } -> std::same_as<util::frame&>;
                                         }) {
                        visit(field.stmts);
                    }
                });
            }

            template <typename Ref>
                requires is_ref_v<Ref>
            void visit(Ref ptr) {
                if constexpr (requires { ptr.deref().data; }) {
                    std::visit(overload{[&](auto& v) {
                                   derived()->entry(ptr, v);
                                   for_each_member(ptr, v);
                                   derived()->exit(ptr, v);
                               }},
                               ptr.deref().data);
                } else if constexpr (requires { ptr.deref().span; }) {
                    for (auto& elm : ptr.deref().span) {
                        visit(elm);
                    }
                }
            }
        };
    } // namespace walker

    struct Resolver : walker::t<Resolver> {
        std::flat_set<void*> resolving;
        std::vector<ref_scope> scopes;

        void resolve_name_expr(ref_scope scope, ref_expr expr, expr_var::unresolved_t u) {
            const auto lookup = scope_t::ancestor_lookup(scope, u.name.name);

            if (!lookup) {
                throw std::runtime_error("Failed to find symbol");
            }
            const auto& result = lookup.value();
            std::println("found symbol {}", result.symbol.as_void());
        }

        void insert(void* ptr) {
            if (resolving.contains(ptr))
                throw std::runtime_error("Recursive resolution attempted");

            resolving.insert(ptr);
        }

        void remove(void* ptr) {
            if (!resolving.contains(ptr))
                throw std::runtime_error("Attempted to erase non existant entry");

            resolving.erase(ptr);
        }

        void insert_scope(ref_scope scope) {
            std::println("scope::{} with parent::{}",
                         scope.as_void(),
                         scope.deref().parent.as_void());
            scopes.push_back(scope);
        }
        void pop_scope() {
            scopes.pop_back();
        }
        ref_scope back_scope() {
            return scopes.back();
        }

        void entry(ref_expr ptr, expr_var::access_t& v) {
            visit(v.lhs);
        }

        void entry(ref_expr ptr, expr_var::unresolved_t& v) {
            insert(ptr.as_void());
            std::println("unresolved expr :: id_{}", v.name.name);
            remove(ptr.as_void());
        }

        template <bool local = false>
        ref_decl resolve_type_chain(cursor& c, const cursor& end, ref_scope scope) {
            if (c.cursor == end.cursor)
                throw_error("Empty type chain");

            const auto& n = c++.get();

            if (!n.isa(final::ID)) [[unlikely]]
                throw_error(
                    std::format("Expected ID in type chain, got {}", lexer::str(n)));

            const auto id = n.unsafe_final().data;

            constexpr auto lookup_fn =
                local ? scope_t::local_lookup : scope_t::ancestor_lookup;
            auto lookup = lookup_fn(scope, id);

            if (!lookup)
                throw_error(std::format("Unknown type symbol"));

            const auto result = lookup->symbol;

            // consume :: if present
            if (c.cursor != end.cursor) {
                if (!c.get().isa(final::DCOLON)) [[unlikely]] {
                    throw_error(
                        std::format("Expected '::' after ID in type chain, got {}",
                                    lexer::str(c.get())));
                }
                c++;

                if (c.cursor == end.cursor) [[unlikely]]
                    throw_error("Trailing '::' in type chain");

                // recurse into the scope of the resolved decl, local from here on
                return resolve_type_chain<true>(c, end, scope_of_decl(result));
            }

            return result;
        }

        void entry(ref_type ptr, type_var::unresolved_t& v) {
            insert(ptr.as_void());

            auto c = v.data.begin;
            const auto result = resolve_type_chain(c, v.data.end, scopes.back());

            if (!result)
                throw_error("Empty type chain");

            auto resolved_type = [&]() {
                const auto* alias =
                    std::get_if<decl_var::type_alias_t>(&result.deref().data);
                if (alias) {
                    return std::optional{std::cref(alias->type)};
                }
                return decltype(std::optional{std::cref(alias->type)}){};
            }();

            if (const auto& rt = resolved_type) {
                ptr.deref().data = type_var::type_alias_t{rt.value()};
            } else {
                throw_error("Expected a type declaration");
            }

            remove(ptr.as_void());
        }

        void entry(ref_decl ptr, decl_var::fn_t& v) {
            insert_scope(v.scope);
        }
        void exit(ref_decl ptr, decl_var::fn_t& v) {
            pop_scope();
        }
        void entry(ref_expr ptr, expr_var::block_t& v) {
            insert_scope(v.frame.scope);
        }
        void exit(ref_expr ptr, expr_var::block_t& v) {
            pop_scope();
        }
        void entry(ref_type ptr, type_var::rec_t& v) {
            insert_scope(v.scope);
        }
        void exit(ref_type ptr, type_var::rec_t& v) {
            pop_scope();
        }

        void entry(auto ptr, auto& v) {}
        void exit(auto ptr, auto& v) {}
    };

    ref_stmts entry(allocator_t& allocator, const lexer::buffer& buffer) {
        auto& node = buffer.get_node(0);
        auto c = cursor{node.children()};

        ref scope = make_scope(allocator);
        // auto root = allocator.alloc_one<ast_t>();

        // test_type_eq(allocator);

        auto e = env{buffer, allocator, scope, /* root */};

        auto file = node2ast::stmts_fn(e, c);

        Resolver v;
        v.scopes.push_back(scope);
        v.visit(file);

        return file;
    }
} // namespace ast

namespace codegen {
    // copy pasted from language (in other words OLD,TOUCH NEEDED)
    // It will need some kind of cache for sure
    struct unit {
      private:
        llvm_allocator a;

        std::unique_ptr<llvm::LLVMContext> c;
        std::unique_ptr<llvm::Module> m;
        std::unique_ptr<llvm::IRBuilder<>> b;
        std::unique_ptr<llvm::TargetMachine> tm;
        llvm::StringMap<bool> features;

        template <typename KeyT, typename ValueT>
        struct cache_map : std::flat_map<KeyT, ValueT> {
            auto insert(KeyT key, ValueT val) {
                return this->try_emplace(key, val);
            }
            [[nodiscard]] std::optional<ValueT> retrieve(KeyT key) const {
                if (auto it = this->find(key); it != this->end())
                    return it->second;
                return std::nullopt;
            }
        };

        // template <typename ValueT>
        // using cache_set = std::flat_set<ValueT>;
        // struct {
        //     cache_map<semantics::ref_type, llvm::Type*> types;
        //     cache_map<semantics::ref_type, llvm::FunctionType*> function_types;
        //     cache_map<semantics::ref_expr, llvm::Value*> exprs;
        //     // cache_set<semantics::ref_type> signed_int;
        // } ca;

      public:
        unit(const unit&) = delete;
        unit& operator=(const unit&) = delete;

        unit(unit&&) noexcept = default;
        unit& operator=(unit&&) noexcept = default;

        explicit unit(std::string_view module_name) {
            this->c = std::make_unique<llvm::LLVMContext>();
            this->m = std::make_unique<llvm::Module>(module_name, *c);
            this->b = std::make_unique<llvm::IRBuilder<>>(*c);

            {
                auto target_triple = llvm::sys::getDefaultTargetTriple();
                llvm::Triple triple(target_triple);
                triple.normalize();
                m->setTargetTriple(triple);

                std::string error;
                const llvm::Target* target =
                    llvm::TargetRegistry::lookupTarget(triple, error);
                if (!target) {
                    llvm::errs() << error << "\n";
                    throw std::runtime_error("Failed to lookup LLVM target");
                }

                llvm::TargetOptions opt;
                this->tm = std::unique_ptr<llvm::TargetMachine>(
                    target
                        ->createTargetMachine(triple, "generic", "", opt, std::nullopt));

                m->setDataLayout(tm->createDataLayout());
            }
            this->features = llvm::sys::getHostCPUFeatures();
            /////////////////////////////////////////////////////////////
            {
                auto cpu = llvm::sys::getHostCPUName();
                std::string feature_str = std::format("CPU={}\n", cpu.str());
                int i = 0;
                for (auto& f : features) {
                    if (f.second)
                        feature_str += "+";
                    else
                        feature_str += "-";
                    feature_str += f.first().str();

                    if (((i + 1) % 10) == 0)
                        feature_str += "\n";
                    else
                        feature_str += ", ";

                    ++i;
                }
                // std::println("{}", feature_str);
            }
        }

        // auto& cache() {
        //     return ca;
        // }
        // const auto& cache() const {
        //     return ca;
        // }

        [[gnu::always_inline]] inline auto& allocator() noexcept {
            return a;
        }
        [[gnu::always_inline]] inline auto& context() const noexcept {
            return *c;
        }
        [[gnu::always_inline]] inline auto& module() const noexcept {
            return *m;
        }
        [[gnu::always_inline]] inline auto& builder() const noexcept {
            return *b;
        }
        [[gnu::always_inline]] inline auto& data_layout() const noexcept {
            return m->getDataLayout();
        }

        void verify() const {
            if (llvm::verifyModule(module(), &llvm::errs())) {
                throw std::runtime_error("Invalid LLVM module");
            }
        }
        void print() const {
            module().print(llvm::outs(), nullptr);
        }
    };
} // namespace codegen

int main(int argc, char* argv[]) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    if (argc < 2) {
        std::println("usage: {} <file>\n", argv[0]);
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

    {
        // ast::ASTInspector inspector;
        // inspector.visit_stmt(file.deref().span[0]);
        // I should hook up the inter_table
        //
        std::println("\n");
        auto slpp = ast::SLOP::printer(std::cerr);
        slpp.print(file);
    }
    auto llvm_unit = codegen::unit(filepath);
    return 0;
}
