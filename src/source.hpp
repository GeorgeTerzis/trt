#pragma once

#include <llvm/Support/SMLoc.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <print>
#include <string_view>

struct source_location {
    llvm::SMLoc begin;
    llvm::SMLoc end;

    source_location(llvm::SMLoc b, llvm::SMLoc e) : begin(b), end(e) {}

    auto length() const {
        return end.getPointer() - begin.getPointer();
    }

    std::string_view source() const {
        return (this->length()) ? std::string_view{begin.getPointer(), end.getPointer()}
                                : std::string_view{};
    }
};

struct source {
    std::string_view filename;
    std::string_view text;

    source(auto f, auto s) : filename(f), text(s) {}

    source(const source&) = delete;
    source& operator=(const source&) = delete;

    source(source&&) noexcept = default;
    source& operator=(source&&) noexcept = default;

    operator std::string_view() const noexcept {
        return text;
    }
    size_t size() const noexcept {
        return text.size();
    }
    char operator[](size_t i) const {
        return text[i];
    }
    auto substr(size_t pos = 0, size_t count = std::string_view::npos) const {
        size_t len = std::min(count, text.size() - pos);
        return std::string_view(text.data() + pos, len);
    }
    auto begin() const noexcept {
        return text.begin();
    }
    auto end() const noexcept {
        return text.end();
    }
};

using source_view = source&;
