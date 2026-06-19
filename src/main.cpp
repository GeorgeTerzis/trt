// !! check the compile.sh to see how to compile !!
//

// TODO: I need to move to a better error handling system rather rather than exploding on
// the first bump probably add an error state to each stage then lexer errors can be
// detected by the ast and skiped till the next terminator and so on then at the end of
// the AST stage we just  dump all the errors to the user.
// ofcourse not every error is the same and we can't just accumilate and continiue for
// every error but it is going to be a good start.
// * insert blog post from the D programming language guy here about error handling*
//

#include "./file_loader.cpp"

#include "../libs/llvm_allocator.hpp"
#include "../libs/meta.hpp"
#include "../libs/ref.hpp"
#include "../libs/staging_vec.hpp"
#include "../libs/variant_overload.hpp"
#include "./mutability.hpp"
#include "lexer.hpp"
#include "throw_error.hpp"
#include <boost/pfr/core.hpp>
#include <boost/type_index.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <flat_map>
#include <flat_set>
#include <format>
#include <functional>
#include <iostream>
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
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/TypeSize.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <wchar.h>

template <typename T>
auto type_name() {
    return boost::typeindex::type_id<T>().pretty_name();
}

namespace ast {
    // One of the biggest problems (not so much of a problem but there is a fix to it)
    // is the reallocation of stateless types, like uint, sint, floating, void, opaque...
    // even record types you can say that they have this problem but they are more complex
    // to implement it on. So I should keep a cache for these types since they are goingto
    // be reused everywhere
    // but I shouldn't expect the program to know this about these types because when I
    // decide to allow multiple files it is just going to complicate things.

    using cursor = const_cursor_t;
    using intern_id = lexer::intern_id;

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
        std::flat_map<intern_id, entry_t> table = {};

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

    void print_scope(const ref_scope& scope, int depth = 0) {
        if (!scope) {
            std::println("scope: null");
            return;
        }
        std::println("scope: {} parent: {} entries: {}",
                     scope.as_void(),
                     scope.deref().parent.as_void(),
                     scope.deref().table.size());
        for (const auto& [id, decl] : scope.deref().table) {
            std::println("  [{}] -> {}", id, decl.as_void());
        }
    }

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

        // consumable
        // a record , tuple or variant builder injests it to it's type
        // rec_field, var_field, tup_field
        struct field_token {
            intern_id name;
            ref_type type;
        };
    } // namespace util

    namespace decl_var {
        struct var_t {
            ref_type type;
            ref_expr init_expr;
        };

        struct var_infer_t {
            ref_expr init_expr;
        };

        struct rec_member_t {
            ref_type type;
            std::uint32_t index;
        };
        struct var_member_t {
            ref_type type;
        };
        struct tup_member_t {
            ref_type type;
        };

        struct tagged_union_member_t {
            ref_type type;
        };

        struct fn_parameter_t {
            ref_type type;
            std::uint32_t index;
        };

        struct fn_t {
            ref_type type;

            ref_scope scope;
            std::span<ref_decl> args;
            ref_type ret_type;
            ref_expr body;
        };

        struct efn_t {
            ref_type type;
            ref_scope scope;
            std::span<ref_decl> args;
            ref_type ret_type;
            std::string_view external_symbol_name;
        };

        struct type_alias_t {
            ref_type type;
        };

        using addressable = type_list<var_t, rec_member_t, fn_parameter_t>;
        using callable = type_list<fn_t, efn_t>;

        using variant = std::variant<var_t,
                                     var_infer_t,
                                     rec_member_t,
                                     tagged_union_member_t,
                                     fn_parameter_t,
                                     fn_t,
                                     efn_t,
                                     type_alias_t>;
    }; // namespace decl_var

    namespace stmt_var {

        struct decl_wrap {
            ref_decl ref;
        };
        struct expr_wrap {
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

        using variant = std::variant<decl_wrap, expr_wrap, return_t, break_t>;
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

        struct match_t {
            struct arm_t {
                ref_expr value;
                ref_expr body;
            };

            ref_expr value;
            std::span<arm_t> arms;
        };

        struct call_t {
            ref_expr callee;
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
                                     call_t,
                                     int_literal_t,
                                     float_literal_t,
                                     bool_literal_t,
                                     name_t,
                                     binary_op_t,
                                     unary_op_t>;
    } // namespace expr_var

    struct expr_t {
        ref_expr parent;
        ref_type type;
        expr_var::variant data;
    };

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

        struct callable_t {
            std::span<ref_type> args;
            ref_type ret_type;
        };

        struct enum_t {};
        struct variant_t {
            ref_scope scope;
        };
        struct tagged_variant_t {
            ref_type enum_tag;
            ref_scope scope;
        };

        using variant = std::variant<unresolved_t,
                                     callable_t,
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

    struct type_eq_default_policy {
        static constexpr bool ignore_mutability = false;
    };
    template <typename Policy = type_eq_default_policy>
    struct type_eq {

        using trivially_true = type_list<type_var::void_t,
                                         type_var::optr_t,
                                         type_var::integer_literal_t,
                                         type_var::float_literal_t,
                                         type_var::f16_t,
                                         type_var::f32_t,
                                         type_var::f64_t,
                                         type_var::f128_t>;

        using numeric_cat = type_list<type_var::uint_t, type_var::sint_t>;

        static bool eq(const ref_type& a, const ref_type& b) {
            if (a == b)
                return true;
            if (!a || !b)
                return false;
            return eq<Policy>(a.deref(), b.deref());
        }

        static bool eq(const type_t& a, const type_t& b) {
            return std::visit(
                [](const auto& x, const auto& y) { return eq_impl<Policy>(x, y); },
                a.data,
                b.data);
        }

      private:
        // default: different types
        template <typename A, typename B>
        static bool eq_impl(const A&, const B&) {
            return false;
        }

        // trivially true
        template <typename T>
            requires is_in_list<T, trivially_true>::value
        static constexpr bool eq_impl(const T&, const T&) {
            return true;
        }

        // numeric: compare size
        template <typename T>
            requires is_in_list<T, numeric_cat>::value
        static bool eq_impl(const T& a, const T& b) {
            return a.bit_size == b.bit_size;
        }

        static bool eq_impl(const type_var::ptr_t& a, const type_var::ptr_t& b) {
            return eq<Policy>(a.type, b.type);
        }

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
        const llvm::DataLayout& target_data_layout;
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
    using maybe_ref_scope = std::optional<ref_scope>;
    maybe_ref_scope scope_of(const ref_type& ptr);
    maybe_ref_scope scope_of(const ref_decl& ptr);
    maybe_ref_scope scope_of(const ref_expr& ptr);
    ref_scope scope_of(const type_var::rec_t& v) {
        return v.scope;
    }

    maybe_ref_scope scope_of(const type_var::type_alias_t& v) {
        return scope_of(v.type);
    }
    template <typename T>
    maybe_ref_scope scope_of(const T&) {
        return std::nullopt;
    }

    maybe_ref_scope scope_of(const type_var::ptr_t& v) {
        return scope_of(v.type);
    }

    maybe_ref_scope scope_of(const ref_expr& ptr) {
        return std::visit(
            overload{[](const expr_var::name_t& v) { return scope_of(v.decl); },
                     [](auto& v) -> maybe_ref_scope { return std::nullopt; }},
            ptr.deref().data);
    }
    maybe_ref_scope scope_of(const ref_type& ptr) {
        return std::visit([](const auto& v) -> maybe_ref_scope { return scope_of(v); },
                          ptr.deref().data);
    }

    maybe_ref_scope scope_of(const ref_decl& ptr) {
        return std::visit(
            overload{
                [](const decl_var::type_alias_t& v) { return scope_of(v.type); },
                [](const auto&) -> maybe_ref_scope {
                    throw_error("Cannot get scope of non-type decl");
                },
            },
            ptr.deref().data);
    }

    decl_var::rec_member_t rec_field_val(const auto index, const util::field_token f) {
        return {
            .type = f.type,
            .index = index,
        };
    }

    ref_decl rec_field(env e, const auto index, const util::field_token f) {
        auto ptr = alloc_and_insert_decl(e, f.name);
        ptr.deref().data = rec_field_val(index, f);
        return ptr;
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
        ref_expr expr_fn(env e, cursor& c);
        ref_expr expr_fn_(env e, cursor& c, int min_prec);
        ref_stmts stmts_fn(env e, cursor& c);

        util::field_token field_token(env e, cursor& c) {
            const auto& name_fin = c++.get().as_final().value().get();
            const auto name = name_fin.data;
            expect_node<final::COLON>(c++.get());
            const auto type = type_fn(e, c);
            return {name, type};
        }

        namespace expr_patterns {

            namespace operands {
                expr_var::variant int_lit(env e, cursor& c) {
                    const auto index = e.buffer.to_index(&c.get());
                    const auto text = e.buffer.loc(index).source();
                    std::uint64_t val;
                    std::from_chars(text.data(), text.data() + text.size(), val);
                    c++;
                    return expr_var::int_literal_t{val};
                }

                expr_var::variant float_lit(env e, cursor& c) {
                    const auto index = e.buffer.to_index(&c.get());
                    const auto text = e.buffer.loc(index).source();
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
                            .path<median::PARENS>(
                                block,
                                e,
                                c) // solution to the problems that come from chaining, I
                                   // might bring it back in the future
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

                    return make_expr(e,
                                     expr_var::call_t{
                                         .callee = callee,
                                         .args = args,
                                     });
                    // expr_var::unary_op_t{
                    //     .op = expr_var::op_e::opcall,
                    //     .operand = callee,
                    //     .payload = expr_var::unary_op_t::payload_t{.args = args},
                    // });
                }

                ref_expr infix(env e, cursor& c, ref_expr lhs, expr_var::op_e op) {
                    c++;
                    const auto meta = expr_var::op_meta(op);
                    ref_expr rhs = expr_fn_(e, c, expr_var::right_bp(meta));
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

        ref_expr expr_fn_(env e, cursor& c, int min_prec) {
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

        ref_expr expr_fn(env e, cursor& c) {
            auto ptr = expr_fn_(e, c, 0);
            expect_node<final::TERMINATOR>(c.get());
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
                    const auto token = field_token(e, c);
                    const auto m = rec_field(e, index, token);

                    staging.push_back(m);
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
                            return type_var::uint_t{
                                e.target_data_layout.getPointerSizeInBits()};
                        },
                        e,
                        c)
                    .path<final::TYPE_ISIZE>(
                        [](env e, cursor& c) -> type_var::variant {
                            c++;
                            return type_var::sint_t{
                                e.target_data_layout.getPointerSizeInBits()};
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
                return [](env e, cursor& c, ref_decl ptr) -> auto {
                    auto staging = staging_vec<ref_decl>(e.allocator);
                    auto& parens = c++.get().unsafe_median();
                    expect<median::PARENS>(parens.code);
                    std::uint32_t index = 0;
                    median_loop(e, parens, [&staging, &index](env e, cursor& c) {
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

                    auto fn_type = [&] -> ref_type {
                        auto type_staging = staging_vec<ref_type>(e.allocator);
                        for (const auto& elm : args)
                            type_staging.push_back(
                                std::get<decl_var::fn_parameter_t>(elm.deref().data)
                                    .type);
                        auto types = type_staging.commit();
                        ref type_ptr = e.allocator.alloc_one<type_t>();
                        type_ptr.deref().mut = mutability::imut();
                        type_ptr.deref().data = type_var::callable_t{
                            .args = types,
                            .ret_type = ret_type,
                        };
                        return type_ptr;
                    }();

                    // @external "symbol_name" path
                    if (c.get().isa(final::EXTERNAL)) {
                        c++;
                        const auto& sym_node = c++.get();
                        expect_node<final::STRING_LIT>(sym_node);
                        consume_untill<final::TERMINATOR>(c);
                        return decl_var::variant{decl_var::efn_t{
                            .type = fn_type,
                            .scope = e.scope,
                            .args = args,
                            .ret_type = ret_type,
                            .external_symbol_name =
                                e.buffer.loc(e.buffer.to_index(&sym_node)).source(),
                        }};
                    }

                    // regular fn path
                    auto body = expr_fn(e, c);
                    consume_untill<final::TERMINATOR>(c);
                    return decl_var::variant{decl_var::fn_t{
                        .type = fn_type,
                        .scope = e.scope,
                        .args = args,
                        .ret_type = ret_type,
                        .body = body,
                    }};
                }(e.with(scope), c, ptr);
            }
            // could be a variable or something elsei like a function
            auto var_infer(env e, cursor& c, ref_decl ptr) {
                c++;
                auto init_expr = expr_fn(e, c);
                return decl_var::var_infer_t{.init_expr = init_expr};
            }
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
                .path<final::ASIGN>(make_decl<decl_patterns::var_infer>, e, c, name)
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
                return stmt_var::decl_wrap{decl};
            }

            stmt_var::variant expr_stmt_fn(env e, cursor& c) {
                return stmt_var::expr_wrap{expr_fn(e, c)};
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
    //  WHY USE STD::FUNCTION TO RESOLVE AND NOT PASS THE Unit or whatever other
    //  context? it works though so it is what it is who gives a cares about the
    //  printer
    // maybe not what i had hopped from the MachineGod gemmini to give me back
    // hard coding the operators is diaboloical
    // this thing will be a nihtmare to update

    template <typename T>
        requires(is_in_list<std::remove_cvref_t<T>, decl_var::addressable>::value)
    ref_type type_of_decl(const T& v) {
        return v.type;
    }

    template <typename T>
        requires(is_in_list<std::remove_cvref_t<T>, decl_var::callable>::value)
    ref_type type_of_decl(const T& v) {
        return v.type;
    }

    std::optional<ref_type> type_of_decl(const auto& v) {
        return std::nullopt;
    }

    std::optional<ref_type> type_of_decl(const ref_decl ptr) {
        const auto& val = ptr.deref();
        return std::visit(
            overload{
                [](const auto& v) -> std::optional<ref_type> { return type_of_decl(v); },
            },
            val.data);
    }

    static ref_type resolve_expr_type(allocator_t& allocator,
                                      const expr_var::variant& var) {
        return std::visit(
            overload{
                // these 3  require allocation
                // so I guess I have to bring in some kind of allocator maybe even
                // enviroment
                // I could also have some kind of cache where I resue the same pointer
                // for these stateless types like void, lit, etc..
                // It would save a lot of memory, less allocations
                [&](const expr_var::int_literal_t& v) -> ref_type {
                    auto ptr = allocator.alloc_one<type_t>();
                    ptr->data = type_var::integer_literal_t{};
                    return ptr;
                },
                [&](const expr_var::float_literal_t& v) -> ref_type {
                    auto ptr = allocator.alloc_one<type_t>();
                    ptr->data = type_var::float_literal_t{};
                    return ptr;
                },
                [&](const expr_var::bool_literal_t& v) -> ref_type {
                    auto ptr = allocator.alloc_one<type_t>();
                    ptr->data = type_var::integer_literal_t{};
                    return ptr;
                },
                [&](const expr_var::binary_op_t& v) -> ref_type {
                    switch (v.op) {
                    case expr_var::op_e::opadd:
                    case expr_var::op_e::opdiv:
                    case expr_var::op_e::opmul:
                    case expr_var::op_e::opsub: {
                        return v.lhs.deref().type;
                    }
                    case expr_var::op_e::opor:
                    case expr_var::op_e::opand: {
                        auto ptr = allocator.alloc_one<type_t>();
                        ptr->data = type_var::integer_literal_t{};
                        return ptr;
                    }
                    default:
                        throw_error("TODO");
                    }
                },
                // these 2 are basicaly operators
                [&](const expr_var::access_t& v) { return v.rhs.deref().type; },
                [&](const expr_var::call_t& v) {
                    const auto callee_type = v.callee.deref().type;
                    // DO NOT JUST LET IT THROW
                    // FIX later i guess
                    const auto& fntype =
                        std::get<type_var::callable_t>(callee_type.deref().data);
                    return fntype.ret_type;
                },

                [&](const expr_var::rec_init_t& v) -> ref_type { return v.type; },
                [&](const expr_var::name_t& v) -> ref_type {
                    auto decl_type = type_of_decl(v.decl);
                    if (!decl_type)
                        throw_error("Declaration can't have a type");
                    return decl_type.value();
                },
                [&](const expr_var::as_t& v) -> ref_type { return v.type; },
                [&](const expr_var::block_t& v) -> ref_type {
                    const auto scope = v.frame.scope;
                    const auto& stmts = v.frame.stmts.deref();

                    if (stmts.span.empty()) {
                        auto ptr = allocator.alloc_one<type_t>();
                        ptr->data = type_var::void_t{};
                        return ptr;
                    }

                    const auto& last_stmt = stmts.span.back().deref();
                    return std::visit(
                        overload{[&](const stmt_var::break_t& v) -> ref_type {
                                     if (const auto& opt = v.val; opt.has_value()) {
                                         return opt->deref().type;
                                     } else {
                                         auto ptr = allocator.alloc_one<type_t>();
                                         ptr->data = type_var::void_t{};
                                         return ptr;
                                     }
                                 },
                                 [&](const stmt_var::expr_wrap& v) -> ref_type {
                                     return v.ref.deref().type;
                                 },
                                 [&](const auto& v) -> ref_type {
                                     auto ptr = allocator.alloc_one<type_t>();
                                     ptr->data = type_var::void_t{};
                                     return ptr;
                                 }},
                        last_stmt.data);
                },
                [](auto& v) -> ref_type { return nullptr; },
            },
            var);
    }

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
        enum signal {
            cont,
            skip,
        };

        template <typename Derived>
        struct type {
            std::flat_set<void*> visited;

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
                    } else if constexpr (std::is_same_v<util::frame, F>) {
                        visit(field.stmts);
                    }
                });
            }

            template <typename Ref>
                requires is_ref_v<Ref>
            void visit(Ref ptr) {
                if (this->visited.contains(ptr.as_void())) {
                    return;
                }

                auto result = derived()->ptr_entry(ptr);
                if (result == cont) {
                    if constexpr (requires { ptr.deref().data; }) {

                        std::visit(overload{[&](auto& v) {
                                       auto result = derived()->entry(ptr, v);
                                       if (result == cont) {
                                           for_each_member(ptr, v);
                                       }
                                       derived()->exit(ptr, v);
                                   }},
                                   ptr.deref().data);
                    } else if constexpr (requires { ptr.deref().span; }) {
                        for (auto& elm : ptr.deref().span) {
                            visit(elm);
                        }
                    }
                }
                derived()->ptr_exit(ptr);
                this->visited.insert(ptr.as_void());
            }
        };
    } // namespace walker

    struct Resolver : walker::type<Resolver> {
        allocator_t& allocator;

        walker::signal ptr_entry(auto ptr) {
            return walker::cont;
        }
        void ptr_exit(auto ptr) {}

        walker::signal entry(auto ptr, auto& v) {
            return walker::cont;
        }
        void exit(auto ptr, auto& v) {}

        std::flat_set<void*> resolving;
        std::vector<ref_scope> scopes;

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
            scopes.push_back(scope);
        }

        void pop_scope() {
            scopes.pop_back();
        }
        ref_scope back_scope() {
            return scopes.back();
        }

        walker::signal entry(ref_expr ptr, expr_var::access_t& v) {
            visit(v.lhs);
            auto scope = scope_of(v.lhs.deref().type);
            if (!scope) {
                throw_error("Expected symbol to have a scope");
            }
            this->insert_scope(scope.value());
            visit(v.rhs);
            return walker::skip;
        }

        void exit(ref_expr ptr, expr_var::access_t& v) {
            pop_scope();
        }

        walker::signal entry(ref_expr ptr, expr_var::unresolved_t& v) {
            insert(ptr.as_void());

            auto scope = this->back_scope();

            // needs some kind of scoping we can't really go around just resolving like
            // this
            //  if this get's hit by something that is part of an access then  we use the
            //  right scope but the wrong lookup function
            auto lookup = scope_t::ancestor_lookup(scope, v.name.name);
            // print_scope(scope);
            // print_scope(scope.deref().parent);
            if (!lookup) [[unlikely]] {
                throw_error("Expected expresion name lookup to resolve");
            }
            auto& lv = lookup.value();
            ptr.deref().data = expr_var::name_t{lv.symbol};

            // this probably needs to be expanded upon
            // I might try to automate this slightly so I do not have to manually write
            // every visitor.
            // But it works for <name>::<something> not for ()::<something>
            // since we need to deduce it's type

            // this will be moved to the exrp_type_resolve function

            {
                auto type = std::visit(
                    overload{[&](decl_var::efn_t& var) -> ref_type { return var.type; },
                             [&](decl_var::fn_t& var) -> ref_type { return var.type; },
                             [&]<typename T>(T& var) -> ref_type
                                 requires(is_in_list<std::remove_cvref_t<T>,
                                                     decl_var::addressable>::value)
                             { return var.type; },
                             [](auto& val) -> ref_type {
                                 throw_error("Expected this to resolve to a type");
                             }},
                             lv.symbol.deref().data);
                visit(type);
                ptr.deref().type = type;
            }

            remove(ptr.as_void());
            return walker::cont;

        } // namespace ast

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

                auto next_scope = scope_of(result);
                if (!next_scope) [[unlikely]]
                    throw_error("Expected symbol to have scope");

                return resolve_type_chain<true>(c, end, next_scope.value());
            }

            return result;
        }

        walker::signal entry(ref_type ptr, type_var::unresolved_t& v) {
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
            return walker::cont;
        }

        walker::signal entry(ref_decl ptr, decl_var::fn_t& v) {
            insert_scope(v.scope);
            return walker::cont;
        }
        void exit(ref_decl ptr, decl_var::fn_t& v) {
            pop_scope();
        }

        walker::signal entry(ref_expr ptr, expr_var::block_t& v) {
            insert_scope(v.frame.scope);
            return walker::cont;
        }

        void exit(ref_expr ptr, expr_var::block_t& v) {
            pop_scope();
        }

        walker::signal entry(ref_type ptr, type_var::rec_t& v) {
            insert_scope(v.scope);

            return walker::cont;
        }

        void exit(ref_type ptr, type_var::rec_t& v) {
            pop_scope();
        }

        void exit(ref_decl ptr, decl_var::var_infer_t& v) {
            ptr.deref().data = decl_var::var_t{
                .type = v.init_expr.deref().type,
                .init_expr = v.init_expr,
            };
        }

        void ptr_exit(ref_expr ptr) {
            auto& val = ptr.deref();
            if (!val.type) {
                auto t = resolve_expr_type(this->allocator, val.data);
                val.type = t;
            }
        }
    };

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

            void print_type(const type_var::callable_t& v) {
                os << "@fn";
                print(v.ret_type);
                os << "::args::";
                for (const auto& elm : v.args)
                    print(elm);
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

            void print_expr(const expr_var::call_t& v) {
                print(v.callee);
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
                os << op_str(v.op);
                print(v.operand);
            }

            void print_expr(const expr_var::block_t& v) {
                os << "{\n";
                indented([&] { print(v.frame.stmts); });
                indent_line();
                os << '}';
            }

            // --- stmt variants ---

            void print_stmt(const stmt_var::decl_wrap& v) {
                print(v.ref);
                os << ';';
            }

            void print_stmt(const stmt_var::expr_wrap& v) {
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

            void print_decl(const decl_t& d, const decl_var::var_infer_t& v) {
                // print(v.type);
                os << ' ' << resolve(d.name);
                if (v.init_expr) {
                    os << " = ";
                    print(v.init_expr);
                }
            }
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
            void print_decl(const decl_t& d, const decl_var::efn_t& v) {
                os << "external fn " << resolve(d.name) << '(';
                for (size_t i = 0; i < v.args.size(); ++i) {
                    if (i)
                        os << ", ";
                    print(v.args[i]);
                }
                os << ") -> ";
                print(v.ret_type);
                os << ' ';
                os << v.external_symbol_name;
            }
        };

        template <typename NameResolver>
        printer(std::ostream&, NameResolver) -> printer<NameResolver>;

        struct ExprTreePrinter : walker::type<ExprTreePrinter> {
            std::ostream& os;
            std::vector<bool> last_child_stack;

            explicit ExprTreePrinter(std::ostream& os) : os(os) {}

            walker::signal ptr_entry(auto ptr) {
                return walker::cont;
            }
            void ptr_exit(auto ptr) {}
            walker::signal entry(auto ptr, auto& v) {
                return walker::cont;
            }
            void exit(auto ptr, auto& v) {}

          private:
            void print_prefix() {
                for (size_t i = 0; i + 1 < last_child_stack.size(); ++i)
                    os << (last_child_stack[i] ? "    " : "│   ");
                if (!last_child_stack.empty())
                    os << (last_child_stack.back() ? "└── " : "├── ");
            }

            void visit_children(std::span<ref_expr> children) {
                for (size_t i = 0; i < children.size(); ++i) {
                    last_child_stack.push_back(i + 1 == children.size());
                    visit(children[i]);
                    last_child_stack.pop_back();
                }
            }

            void visit_child(ref_expr child, bool is_last) {
                last_child_stack.push_back(is_last);
                visit(child);
                last_child_stack.pop_back();
            }

            static std::string type_label(ref_type t) {
                if (!t)
                    return "<no type>";
                return std::visit(
                    overload{[](const type_var::uint_t& v) -> std::string {
                                 return std::format("u{}", v.bit_size);
                             },
                             [](const type_var::sint_t& v) -> std::string {
                                 return std::format("i{}", v.bit_size);
                             },
                             [](const type_var::type_alias_t& v) -> std::string {
                                 return type_label(v.type);
                             },
                             []<typename T>(const T& v) -> std::string {
                                 return std::string(type_name<T>());
                             }},
                    t.deref().data);
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
                default:
                    return "?";
                }
            }

            void node(ref_expr ptr, std::string_view label) {
                print_prefix();
                os << label << " :: " << type_label(ptr.deref().type) << "\n";
            }

          public:
            walker::signal entry(ref_expr ptr, expr_var::int_literal_t& v) {
                node(ptr, std::format("IntLit({})", v.value));
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::float_literal_t& v) {
                node(ptr, std::format("FloatLit({})", v.value));
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::bool_literal_t& v) {
                node(ptr, std::format("BoolLit({})", v.value ? "true" : "false"));
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::unresolved_t& v) {
                node(ptr, "Unresolved");
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::name_t& v) {
                node(
                    ptr,
                    std::format("Name({})",
                                v.decl ? std::to_string(v.decl.deref().name) : "<null>"));
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::binary_op_t& v) {
                node(ptr, std::format("BinOp({})", op_str(v.op)));
                visit_child(v.lhs, false);
                visit_child(v.rhs, true);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::unary_op_t& v) {
                node(ptr, std::format("UnaryOp({})", op_str(v.op)));
                visit_child(v.operand, true);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::call_t& v) {
                node(ptr, "Call");
                visit_child(v.callee, v.args.empty());
                visit_children(v.args);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::access_t& v) {
                node(ptr, "Access");
                visit_child(v.lhs, false);
                visit_child(v.rhs, true);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::rec_init_t& v) {
                node(ptr, "RecInit");
                visit_children(v.args);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::as_t& v) {
                node(ptr, "As");
                visit_child(v.expr, true);
                return walker::skip;
            }
            walker::signal entry(ref_expr ptr, expr_var::block_t& v) {
                node(ptr, "Block");
                visit(v.frame.stmts);
                return walker::skip;
            }
        };
    } // namespace SLOP

    ref_stmts entry(allocator_t& allocator,
                    const lexer::buffer& buffer,
                    const llvm::DataLayout& data_layout) {
        auto& node = buffer.get_node(0);
        auto c = cursor{node.children()};

        ref scope = make_scope(allocator);

        // test_type_eq(allocator);

        auto e = env{buffer, data_layout, allocator, scope};

        auto file = node2ast::stmts_fn(e, c);

        Resolver v{
            .allocator = e.allocator,
        };
        v.scopes.push_back(scope);
        v.visit(file);

        SLOP::ExprTreePrinter ep(std::cerr);
        ep.visit(file);

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

    using value_cache = std::flat_map<void*, llvm::Value*>;

    llvm::Type* gen_type(unit& u, const ast::ref_type& ref);
    llvm::Value* gen_expr(unit& u, value_cache& cache, const ast::ref_expr& ref);
    void gen_stmt(unit& u, value_cache& cache, const ast::ref_stmt& ref);
    void gen_decl(unit& u, value_cache& cache, const ast::ref_decl& ref);

    llvm::Type* gen_type(unit& u, const ast::type_var::void_t&) {
        return llvm::Type::getVoidTy(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::uint_t& t) {
        return llvm::IntegerType::get(u.context(), t.bit_size);
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::sint_t& t) {
        return llvm::IntegerType::get(u.context(), t.bit_size);
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::f16_t&) {
        return llvm::Type::getHalfTy(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::f32_t&) {
        return llvm::Type::getFloatTy(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::f64_t&) {
        return llvm::Type::getDoubleTy(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::f128_t&) {
        return llvm::Type::getFP128Ty(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::optr_t&) {
        return llvm::PointerType::getUnqual(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::ptr_t&) {
        return llvm::PointerType::getUnqual(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::integer_literal_t&) {
        return llvm::IntegerType::get(u.context(), 64);
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::float_literal_t&) {
        return llvm::Type::getDoubleTy(u.context());
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::type_alias_t& t) {
        return gen_type(u, t.type);
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::unresolved_t&) {
        throw_error("unresolved type in codegen");
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::rec_t& t) {
        auto fields = u.allocator().alloc_many<llvm::Type*>(t.members.size());
        for (size_t i = 0; i < t.members.size(); ++i) {
            const auto& m =
                std::get<ast::decl_var::rec_member_t>(t.members[i].deref().data);
            fields[i] = gen_type(u, m.type);
        }
        return llvm::StructType::get(
            u.context(),
            llvm::ArrayRef<llvm::Type*>(fields.data(), fields.size()));
    }
    llvm::Type* gen_type(unit& u, const ast::type_var::callable_t& t) {
        return nullptr;
    }

    llvm::Type* gen_type(unit& u, const ast::type_t& t) {
        return std::visit([&](const auto& v) { return gen_type(u, v); }, t.data);
    }
    llvm::Type* gen_type(unit& u, const ast::ref_type& ref) {
        return gen_type(u, ref.deref());
    }

    llvm::Value* gen_expr(unit& u, value_cache& cache, const auto& v) {
        return nullptr;
    }
    llvm::Value*
    gen_expr(unit& u, value_cache& cache, const ast::expr_var::unresolved_t&) {
        throw_error("unresolved expr in codegen");
    }
    llvm::Value* gen_expr(unit& u, value_cache& cache, const ast::expr_t& e) {
        return std::visit([&](const auto& v) { return gen_expr(u, cache, v); }, e.data);
    }
    llvm::Value* gen_expr(unit& u, value_cache& cache, const ast::ref_expr& ref) {
        return gen_expr(u, cache, ref.deref());
    }

    void gen_stmt(unit& u, value_cache& cache, const auto& v) {}
    void gen_stmt(unit& u, value_cache& cache, const ast::stmt_t& s) {
        std::visit([&](const auto& v) { gen_stmt(u, cache, v); }, s.data);
    }
    void gen_stmt(unit& u, value_cache& cache, const ast::ref_stmt& ref) {
        gen_stmt(u, cache, ref.deref());
    }

    void gen_decl(unit& u, value_cache& cache, const ast::decl_t&, const auto&) {}
    void gen_decl(unit& u, value_cache& cache, const ast::decl_t& d) {
        std::visit([&](const auto& v) { gen_decl(u, cache, d, v); }, d.data);
    }
    void gen_decl(unit& u, value_cache& cache, const ast::ref_decl& ref) {
        gen_decl(u, cache, ref.deref());
    }

    void entry(unit& u, const ast::ref_stmts& stmts) {
        value_cache cache;
        for (const auto& stmt : stmts.deref().span)
            gen_stmt(u, cache, stmt);
    }
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

    diagnostics::unit diagnostic_unit;
    lexer::intern_table intern_table;

    auto llvm_unit = codegen::unit(filepath);

    llvm::SourceMgr sm;
    auto main_file = llvm::MemoryBuffer::getFile(filepath);
    std::println("file=\"{}\"", filepath);
    auto src_id = sm.AddNewSourceBuffer(std::move(*main_file), llvm::SMLoc());

    source src{filepath, sm.getBufferInfo(src_id).Buffer.get()->getBuffer()};
    const auto lexer_output =
        lexer::entry(src, sm, src_id, diagnostic_unit, intern_table);
    lexer::pretty_print(lexer_output, src);

    if (!diagnostic_unit.diagnostics.empty()) {
        diagnostics::print_all(llvm::errs(), diagnostic_unit, sm);
        std::exit(EXIT_FAILURE);
    }

    llvm_allocator arena;
    auto file = ast::entry(arena, lexer_output, llvm_unit.data_layout());

    {
        // ast::ASTInspector inspector;
        // inspector.visit_stmt(file.deref().span[0]);
        // I should hook up the inter_table
        //
        std::println("\n");
        auto slpp = ast::SLOP::printer(std::cerr);
        slpp.print(file);
    }
    {

        // codegen::gen_type(llvm_unit, );
    }

    return 0;
}
