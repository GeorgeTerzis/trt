#pragma once

#include "llvm_allocator.hpp"

// because we need recursion and
// it would be prefereable to have
// everything in one contigious span
// we do this
// 5 elements is probably good enough?
// I could go further or even expose it

// DO NOT USE THIS AS A MEMBER
// THIS IS MEANT TO BE CONSUMED
template <typename T>
struct staging_vec {
    using Allocator = llvm_allocator;

    Allocator& allocator;
    llvm::SmallVector<T, 5> staging;

    explicit staging_vec(llvm_allocator& a) : allocator(a) {}

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
        std::span<T> result = allocator.alloc_many<T>(staging.size());
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
