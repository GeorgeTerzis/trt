// diagnostic.cpp
#pragma once
#include "source.hpp"
#include <cmath>
#include <cstdint>
#include <format>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <ostream>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace diagnostics {

    using severity = llvm::SourceMgr::DiagKind;
    struct type {
        severity severity;
        source_location loc;
        std::string msg;
    };

    struct unit {
        std::vector<type> diagnostics;

        template <typename StrT>
            requires(std::is_same_v<StrT, std::string> ||
                     std::is_same_v<StrT, std::string_view> ||
                     std::is_same_v<StrT, llvm::StringRef>)
        void emit(llvm::SourceMgr::DiagKind s, source_location l, StrT&& msg) {
            this->diagnostics.push_back(type{
                .severity = s,
                .loc = l,
                .msg = std::string(std::forward<StrT>(msg)),
            });
        }

        unit() = default;
        unit(const unit&) = delete;
        unit& operator=(const unit&) = delete;
        unit(unit&&) noexcept = default;
        unit& operator=(unit&&) noexcept = default;
    };

    inline void print(llvm::raw_ostream& os, llvm::SourceMgr& sm, const type& d) {
        auto range = llvm::SMRange(d.loc.begin, d.loc.end);
        sm.PrintMessage(os, range.Start, llvm::SourceMgr::DK_Error, d.msg, range);
    }

    inline void print_all(llvm::raw_ostream& os, const unit& u, llvm::SourceMgr& sm) {
        for (const auto& d : u.diagnostics)
            print(os, sm, d);
    }
} // namespace diagnostics
