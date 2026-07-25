#include "lexer.hpp"
#include <llvm/Support/SMLoc.h>
#include <llvm/Support/SourceMgr.h>
#include <utility>

namespace lexer {
    std::string str(const source_location val) {
        return std::string(val.source());
    }

#define RET void
#define ARGS unit &u, source_view src, const uint32_t token_begin, uint32_t cursor
#define FNSIG (ARGS)->RET

    using dispatch_table = std::array<RET (*)(ARGS), 256>;
    using truth_table = std::array<bool, 256>;

    inline auto within_src(const std::string_view src, const std::uint32_t index) {
        return src.size() > index;
    }

    auto next FNSIG;

    auto peek(source_view src, std::uint32_t cursor) -> unsigned char {
        if (!within_src(src, cursor + 1))
            return 0;
        return static_cast<unsigned char>(src[cursor + 1]);
    }

    std::string_view str(final::e e) {
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

    std::string_view str(median::e e) {
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

    // std::string str(const source_location val) {
    //     return std::format("loc={{line={}, len={}, index={}}}",
    //                        val.line,
    //                        val.length(),
    //                        val.begin);
    // }
    std::string str(const final val) {
        return std::format("final={{code={}}}", str(val.code));
    }
    std::string str(const median val) {
        return std::format("median={{code={}, len={}}}", str(val.code), val.len);
    }
    std::string str(const node& val) {
        return std::format("node={{{}}}",
                           val.visit([&](const final& f) { return str(f); },
                                     [&](const median& f) { return str(f); }));
    }
    std::string str(const node& n, const lexer::buffer& buffer) {
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
            u.line_index = cursor;
            become next(u, src, token_begin + 1, cursor + 1);
        }
    } // namespace whitesapce

    node get_terminator() {
        return node{final::TERMINATOR};
    }

    std::uint32_t open_median(unit& u,
                              median::e code,
                              source_view src,
                              std::uint32_t token_begin,
                              std::uint32_t cursor);

    auto close_median(std::uint32_t index,
                      unit& u,
                      std::string_view src,
                      std::uint32_t cursor);

    void make_terminator(unit& u,
                         source_view src,
                         const uint32_t token_begin,
                         const uint32_t end) {
        u.last_terminator_depth = u.depth;
        const auto src_loc = u.make_srcloc(token_begin, end);
        u.buffer.push(get_terminator(), src_loc);
    }

    auto terminator FNSIG {
        auto end = cursor + 1;
        make_terminator(u, src, token_begin, end);

        become next(u, src, end, end);
    }

    node get_error() {
        return node{final::error};
    }

    template <typename... Args>
    [[clang::always_inline, nodiscard("Recovery point should be used")]] auto
    error_handling(unit& u,
                   source_view src,
                   const uint32_t token_begin,
                   uint32_t cursor,
                   std::format_string<Args...> fmt,
                   Args&&... args) -> std::uint32_t {
        const auto err_loc = u.make_srcloc(token_begin, cursor);
        u.buffer.push(get_error(), err_loc);
        make_terminator(u, src, token_begin, cursor);

        u.diagnostic_unit.emit(diagnostics::severity::DK_Error,
                               err_loc,
                               std::format(fmt, std::forward<Args>(args)...));

        return cursor;
    }

    std::uint32_t open_median(unit& u,
                              median::e code,
                              source_view src,
                              std::uint32_t token_begin,
                              std::uint32_t cursor) {

        const auto src_loc = u.make_srcloc(token_begin, token_begin + 1);
        auto index = u.buffer.push(node(code, 0), src_loc);
        u.prev_depth = u.depth;
        ++u.depth;
        return index;
    }

    auto close_median(std::uint32_t index,
                      unit& u,
                      std::string_view src,
                      std::uint32_t cursor) {
        {
            auto& open_node = u.buffer.out.get_node(index);
            auto& m = open_node.payload.median;
            m.len = u.buffer.out.nodes.size() - 1 - index;
        }

        {
            auto& loc = u.buffer.out.loc(index);
            loc.end = llvm::SMLoc::getFromPointer(u.base() + cursor);
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

            if (u.openstack.empty()) {
                const auto recovery = error_handling(u,
                                                     src,
                                                     token_begin,
                                                     cursor,
                                                     "unexpected symetrical close '{}'",
                                                     src[cursor - 1]);
                become next(u, src, recovery, recovery);
            }
            auto open_index = u.openstack.back();
            if (!symetrical_close_match(open_index.code, c)) [[unlikely]] {
                const auto recovery = error_handling(u,
                                                     src,
                                                     token_begin,
                                                     cursor,
                                                     "unexpected symbol '{}'",
                                                     src[cursor]);
                become next(u, src, recovery, recovery);
            }
            u.openstack.pop_back();
            auto prev_non_whitespace = [&]() -> unsigned char {
                auto i = cursor - 2;
                while (i > 0 && (whitesapce::is_horizontal(src[i]) ||
                                 whitesapce::is_vertical(src[i])))
                    --i;
                return static_cast<unsigned char>(src[i]);
            };

            if (prev_non_whitespace() != ';') {
                auto loc = u.buffer.out.locs.back();
                loc.end = llvm::SMLoc::getFromPointer(u.base() + cursor);
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
        const auto src_loc = u.make_srcloc(token_begin, cursor);
        u.buffer.push(node(final(final::STRING_LIT)), src_loc);

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

    inline std::string_view read_word(source_view src, const std::uint32_t begin_index) {
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

        const auto src_loc = u.make_srcloc(token_begin, static_cast<uint32_t>(end));
        u.buffer.push(
            node(type, ((type == final::ID) ? u.buffer.out.itable.intern(word) : 0)),
            src_loc);
        become next(u, src, end, end);
    }

    auto id FNSIG {
        auto word = read_word(src, token_begin);
        auto end = token_begin + word.size();
        auto intern_id = u.buffer.out.itable.intern(word);

        const auto src_loc = u.make_srcloc(token_begin, static_cast<uint32_t>(end));
        u.buffer.push(node(final::ID, intern_id), src_loc);
        become next(u, src, end, end);
    }

    inline bool all_digits(std::string_view v) {
        return !v.empty() &&
               std::ranges::all_of(v, [](unsigned char c) { return is_digit(c); });
    }

    auto prefixed_numeric_or_id FNSIG {
        auto text = read_word(src, token_begin);
        cursor += text.size();
        const auto src_loc = u.make_srcloc(token_begin, cursor);

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
            u.buffer.push(node(final{type, len}), src_loc);
            become next(u, src, cursor, cursor);
        }

        become keyword(u, src, token_begin, cursor);
    }

    auto num FNSIG {
        auto i = token_begin;
        final::e fintype = final::INT;

        while (i < src.size() && is_digit(static_cast<unsigned char>(src[i]))) {
            ++i;
        }

        if (i < src.size() && src[i] == '.') {
            fintype = final::FLOAT;
            ++i;

            while (i < src.size() && is_digit(src[i])) {
                ++i;
            }
        }
        if (i < src.size() && (src[i] == 'e')) {
            fintype = final::FLOAT;
            ++i;

            if (i < src.size() && (src[i] == '-'))
                ++i;

            while (i < src.size() && is_digit(src[i]))
                ++i;
        }

        const auto src_loc = u.make_srcloc(token_begin, i);
        u.buffer.push(node(final{fintype}), src_loc);

        become next(u, src, i, i);
    }

    auto unreachable_state FNSIG {
        auto recovery = error_handling(u, src, token_begin, cursor, "Unreachable state");
        become next(u, src, recovery, recovery);
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

    [[clang::always_inline]] inline auto scan_for_symbol(std::string_view src,
                                                         std::uint32_t cursor)
        -> std::string_view {
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
        if (text.size() == 0) [[unlikely]] {
            std::unreachable();
        }

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
                if (len == 0) [[unlikely]] {
                    std::unreachable();
                }
            } else {
                const auto src_loc = u.make_srcloc(token_begin, cursor);
                u.buffer.push(node(final(r)), src_loc);
                become next(u, src, cursor, cursor);
            }
        } while (true);
    }

    auto builtin FNSIG {
        cursor++;
        const auto word = scan_for<builtin_table>(src.substr(cursor), 0);
        auto end = cursor + word.size();

#define BUILTIN_KEYWORD(spelling, code) .Case(spelling, final::e::code)
        auto code = llvm::StringSwitch<final::e>(
                        std::string_view(word.begin() - 1, word.size() + 1))
#include "./def"
                        .Default(final::e::last);

        const auto src_loc = u.make_srcloc(token_begin, static_cast<uint32_t>(end));
        if (code == final::e::last) [[unlikely]] {
            auto recovery = error_handling(u,
                                           src,
                                           token_begin,
                                           cursor,
                                           "unknown builtin '@{}'",
                                           word);
            become next(u, src, recovery, recovery);
        }

        u.buffer.push(node(final(code)), src_loc);

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
        table['\"'] = strlit;
        return table;
    }();

    auto next FNSIG {
        if (!within_src(src, cursor))
            return;

        const auto c = static_cast<unsigned char>(src[cursor]);
        become next_table[c](u, src, token_begin, cursor);
    }

    auto close_file_median(unit& u, std::string_view src) {
        // cond_insert_invisible_separator(u);
        close_median(0, u, src, src.size());
    }

    std::string format_node_text(const buffer& b, const node& n) {
        const auto buffer_index = b.to_index(&n);
        const auto loc = b.loc(buffer_index);
        const auto text = loc.source();

        const auto fin_text =
            (n.as_final() && text.size()) ? "'" + std::string(text) + "'" : "";

        return std::format("[{}]{} {}", buffer_index, str(n), fin_text);
    }

    void internal_pretty_print(const buffer& b,
                               source_view src,
                               const_cursor_t i,
                               std::uint32_t&& depth) {
        constexpr int depth_mul = 4;
        // constexpr int value_width = 30;

        const std::string indent_str(depth * depth_mul, ' ');

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

    buffer entry(source& src,
                 llvm::SourceMgr& sm,
                 std::uint32_t src_id,
                 diagnostics::unit& dunit,
                 intern_table& itable) {
        buffer buffer{
            .itable = itable,
            .src = src,
            .nodes = {},
            .locs = {},
        };
        buffer_builder builder{buffer};
        unit u{sm, src_id, dunit, builder};

        open_median(u, median::e::FILE, src, 0, 0);
        next(u, src, 0, 0);
        close_median(0, u, src, src.size());

        if (u.openstack.size()) {
            for (const auto& elm : u.openstack) {
                const auto loc = buffer.loc(elm.index);
                (void)lexer::error_handling(u,
                                            src,
                                            0,
                                            0,
                                            "Did not close symetrical `{}`",
                                            *loc.begin.getPointer());
            }
        } // assert(!u.openstack.size());

        return buffer;
    }

#undef FNSIG
#undef ARGS
#undef RET

} // namespace lexer
