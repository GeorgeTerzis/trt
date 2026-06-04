# trt a compiler (not really yet)

A simple compiled language built from scratch in C++23. The goal is to start small, get something working and iteratively improve uppon it.

> This project is a deliberate restart of a [more ambitious predecessor](https://github.com/GeorgeTerzis/language) that grew too large and tried to do too much at once. The aim here is to reach that level and then surpass it.

---

## Project files
```
libs/             — allocator, ref, map, vector, and other utilities
include/          — utf8 support, and whatever the project needs to vendor in
def               — token/keyword definitions (used via X-macros)
```

## Stages

**Lexer** 

(`namespace lexer`)
Converts source text into a flat buffer of `node`s either `final` (leaf tokens) or `median` (grouping nodes with children). Handles identifiers, keywords, numeric literals, string literals, symbol sequences, and symmetric delimiters `()`, `[]`, `{}`.

**AST builder** 

(`namespace ast::node2ast`)
Walks the node buffer and constructs a typed AST. Declarations, types, statements, and expressions are all represented as arena-allocated structs.

**Resolution**

`ast::walker::t` and `ast::Resolver`

I now am experimenting with a Walker CRPT to lower the boilerplate and make it a bit more managable.
Our only limitations come from `boost::pfr` that we are using to do reflection which is that we have to keep our AST as pods(plain old data).
which means no inheritance, private members, etc..

The Walker will allow  me to replace `SLOP::printer` in the future but it works for now so why change it


**Checking**

todo

**LLVM emit**

todo


---

## Language syntax (so far)

Everything that is a list uses `;` as the separator so it is used for function arguments, record fields, and so on. The trailing `;` is optional since it gets inserted virtually, but you can include it explicitly if you prefer. There are a few syntax tradeoffs due to this but I think they are worth it, since it makes the language easy to lex and parse,makes it more consistent, allows us to reserve te comma for another purpose.

**Variable declaration**
```
name: type = expression;
```

**Type alias**
```
name: @type = u32;
```

**Function declaration**
```
name: @fn(arg0: type0; arg1: type1; ...) return_type = expression;
```
**Builtin Keywords**
```
@sizeof{type}
@min{type}
@max(expr)
```
we have a few keywords that do not use the `@keyword` syntax
```
mut // explicit mutability
imut // explicity immutability
const // constant / compiletime known
ret    
break  // might be used to break out of expresion blocks
become // will take a while to implement
unreachable
_ (this one might become `ignore`)
```

**Built-in types**
```
u32, u64, s32, s64    — fixed width integers
usize, isize          — pointer-sized integers
f16, f32, f64, f128   — floating point
rec(field: type; ...) — anonymous record / struct
*type                 — pointer
```

---

## Building

Either use the provided script:
```bash
./compile.sh
```

Or compile manually:
```bash
clang++ ./src/main.cpp -std=c++23 $(llvm-config --libs) -o trt.out
./trt.out <file.trt>
```

**Tested on Linux with Clang only.** The lexer uses `[[clang::musttail]]` for its dispatch loop, whether other compilers support this attribute is unknown, so for now Clang is the safe choice.

Dependencies:
- clang++ with C++23 support
- LLVM (for the allocator and string utilities and the backend in the future)
---

## Usage
you can pass whaterver you want really
``` bash
trt.out file.trt
```

## Notes

**Integer types** are fixed-width. The user can define any bit width up to what fits in a `uint32`, so `u7`, `u24`, `s3` etc. are all valid.

**`usize` and `isize`** are currently hardcoded to 64-bit. The plan is to derive them from the target system at compile time.

---

## Status
- [x] UTF8
- [x] Lexer
- [x] Node buffer + pretty printer
- [x] Type parsing
- [x] Record type parsing
- [x] Variable and type alias declarations
- [x] Function declaration parsing (body stubbed)
- [x] Symbol table with scoped lookup
- [x] Type structural equality (`type_eq`)
- [ ] Expression parsing
- [ ] Statement bodies (loop, break, return)
- [ ] Recursive type aliasing
- [ ] Symbol resolution
- [ ] LLVM backend
