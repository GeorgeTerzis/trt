#pragma once
#include "llvm/Support/Allocator.h"
#include <span>

struct llvm_memory_resource : std::pmr::memory_resource {
    llvm::BumpPtrAllocator& alloc_impl;

    explicit llvm_memory_resource(llvm::BumpPtrAllocator& a) : alloc_impl(a) {}

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        return alloc_impl.Allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        // BumpPtrAllocator doesn't free individually - VERY SPECIAL
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

// I am pretty sure this is move only
struct llvm_allocator {
    llvm::BumpPtrAllocator alloc_impl;

    struct dealloc_obj {
        void* ptr;
        void (*destructor)(void*);
    };

    std::vector<dealloc_obj> registry;
    llvm_allocator() = default;
    llvm_allocator(const llvm_allocator&) = delete;
    llvm_allocator& operator=(const llvm_allocator&) = delete;

    llvm_allocator(llvm_allocator&& other) noexcept :
        alloc_impl(std::move(other.alloc_impl)),
        registry(std::move(other.registry)) {}

    llvm_allocator& operator=(llvm_allocator&& other) noexcept {
        if (this != &other) {
            alloc_impl = std::move(other.alloc_impl);
            registry = std::move(other.registry);
        }
        return *this;
    }

    template <typename T, typename... Args>
    [[nodiscard]] T* alloc_one(Args&&... args) {

        void* mem = alloc_impl.Allocate(sizeof(T), alignof(T));
        T* obj = new (mem) T(std::forward<Args>(args)...);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            registry.push_back({obj, [](void* p) { static_cast<T*>(p)->~T(); }});
        }
        return obj;
    }

    template <typename T, typename... Args>
    [[nodiscard]] std::span<T> alloc_many(std::size_t n, Args&&... args) {
        if (n == 0)
            return {};

        constexpr auto alignment = alignof(T);
        constexpr auto elm_size = sizeof(T);

        void* mem = alloc_impl.Allocate(elm_size * n, alignment);
        T* arr = static_cast<T*>(mem);

        for (std::size_t i = 0; i < n; ++i)
            new (&arr[i]) T(std::forward<Args>(args)...);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::size_t i = 0; i < n; ++i)
                registry.push_back({&arr[i], [](void* p) { static_cast<T*>(p)->~T(); }});
        }
        return {arr, n};
    }

    template <typename T>
    void destroy(T* obj) {
        if constexpr (!std::is_trivially_destructible_v<T>)
            obj->~T();
    }

    void destroy_all() {
        for (auto it = registry.rbegin(); it != registry.rend(); ++it)
            it->destructor(it->ptr);
        registry.clear();
    }

    [[nodiscard]] std::size_t bytes_allocated() const {
        return alloc_impl.getBytesAllocated();
    }
    [[nodiscard]] std::size_t total_memory() const {
        return alloc_impl.getTotalMemory();
    }
    [[nodiscard]] std::size_t num_slabs() const {
        return alloc_impl.GetNumSlabs();
    }

    llvm_memory_resource memory_resource() {
        return llvm_memory_resource{alloc_impl};
    }
};
