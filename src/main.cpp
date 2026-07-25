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
#include "diagnostic.cpp"
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

#include "ast.hpp"

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
    auto file = ast::entry(arena, diagnostic_unit, lexer_output, llvm_unit.data_layout());
    if (!diagnostic_unit.diagnostics.empty()) {
        diagnostics::print_all(llvm::errs(), diagnostic_unit, sm);
        std::exit(EXIT_FAILURE);
    }

    {
        auto sloprint = ast::SLOP::printer<>(std::cerr);
        sloprint.print(file);
    }

    {
        codegen::unit u("my_module");
        codegen::entry(u, file);
    }
    return 0;
}
