#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>

template <typename T>
using cxx_ref = std::reference_wrapper<T>;
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseMapInfo.h>

template <typename T>
struct ref {
    using value = T;
    using pointer = T*;
    using cpointer = const T*;
    using reference = T&;
    using creference = const T&;

    static constexpr std::string metadata = "ref";

    pointer data;

    struct access_error : std::bad_optional_access {
        std::source_location where;
        std::string message;
        access_error(std::source_location loc = std::source_location::current()) :
            where(loc) {
            message = std::string("ref<T>: null access at ") + where.file_name() + ":" +
                      std::to_string(where.line()) + " in " + where.function_name();
        }

        const char* what() const noexcept override {
            return message.c_str();
        }
    };

    ref() : data(nullptr) {}
    ref(std::nullptr_t) : data(nullptr) {}
    ref(T* d) : data(d) {}

    inline pointer
    checked_ptr(const std::source_location loc = std::source_location::current()) const {
        if (!data) [[unlikely]]
            throw access_error(loc);
        return data;
    }

    inline reference deref(std::source_location loc = std::source_location::current()) {
        return *checked_ptr(loc);
    }

    inline creference
    deref(std::source_location loc = std::source_location::current()) const {
        return *checked_ptr(loc);
    }
    using rw = std::reference_wrapper<T>;
    using crw = std::reference_wrapper<const T>;

    std::optional<rw> safe_deref() {
        return data ? std::optional<rw>{*data} : std::nullopt;
    }

    std::optional<crw> safe_deref() const {
        return data ? std::optional<crw>{*data} : std::nullopt;
    }

    // // without these can force the deref() function
    // // to be used instead
    // // and have accurate diagnostics
    // reference operator*() { return *checked_ptr(); }
    // creference operator*() const { return *checked_ptr(); }

    // pointer operator->() { return checked_ptr(); }
    // cpointer operator->() const { return checked_ptr(); }

    bool is_null() const {
        return data == nullptr;
    }
    bool is_valid() const {
        return data != nullptr;
    }

    explicit operator bool() const {
        return is_valid();
    }

    void* as_void() const {
        return data;
    }
    uintptr_t as_uint() const {
        return reinterpret_cast<uintptr_t>(data);
    }

    ref& replace(void* other) noexcept {
        data = static_cast<T*>(other);
        return *this;
    }

    ref& replace(uintptr_t other) noexcept {
        data = reinterpret_cast<T*>(other);
        return *this;
    }

    ref& replace(ref<T> other) noexcept {
        data = other.data;
        return *this;
    }

    ref& replace(T* other) noexcept {
        data = other;
        return *this;
    }

    decltype(auto) ptr(this auto&& self) {
        return self.data;
    }

    void reset() {
        data = nullptr;
    }

    auto strong();
};

template <typename T>
struct strong_ref {
    using pointer = T*;
    using cpointer = const T*;
    using reference = T&;
    using creference = const T&;
    static constexpr std::string metadata = "ref";

    pointer data;

    struct null_init_error : std::runtime_error {
        std::source_location where;
        null_init_error(std::source_location loc = std::source_location::current()) :
            std::runtime_error("strong_ref<T>: initialized with nullptr"),
            where(loc) {}
    };

    strong_ref(T* init, std::source_location loc = std::source_location::current()) :
        data(init) {
        if (!init)
            throw null_init_error(loc);
    }

    strong_ref(T& init) : data(&init) {}
    strong_ref(const ref<T> init) : data(init.data) {}

    strong_ref(const strong_ref&) = default;
    strong_ref& operator=(const strong_ref&) = default;

    strong_ref(strong_ref&&) noexcept = default;
    strong_ref& operator=(strong_ref&&) noexcept = default;

    reference get(std::source_location loc = std::source_location::current()) {
        if (!data)
            throw null_init_error(loc);
        return *data;
    }

    reference operator*() {
        return get();
    }
    pointer operator->() {
        return data;
    }

    creference operator*() const {
        return get();
    }
    cpointer operator->() const {
        return data;
    }

    creference get(std::source_location loc = std::source_location::current()) const {
        if (!data)
            throw null_init_error(loc);
        return *data;
    }

    // creference operator*() const { return get(); }
    // const pointer operator->() const { return data; }

    void reset() = delete;

    void replace(T* other, std::source_location loc = std::source_location::current()) {
        if (!other)
            throw null_init_error(loc);
        data = other;
    }

    void replace(T& other) {
        data = &other;
    }

    pointer ptr() {
        return data;
    }
    const pointer ptr() const {
        return data;
    }

    reference deref() {
        return *data;
    }
    creference deref() const {
        return *data;
    }
    void* as_void() const {
        return data;
    }
    uintptr_t as_uint() const {
        return reinterpret_cast<uintptr_t>(data);
    }
    ref<T> as_ref() const {
        return ref<T>{data};
    }
    operator ref<T>() const {
        if (!data)
            throw null_init_error();
        return ref<T>(data);
    }
};

template <typename T>
auto operator<=>(const strong_ref<T>& lhs, const strong_ref<T>& rhs) {
    return lhs.ptr() <=> rhs.ptr();
}

template <typename T>
bool operator!=(const strong_ref<T>& lhs, const strong_ref<T>& rhs) {
    return !(lhs == rhs);
}

template <typename T>
bool operator==(const strong_ref<T>& lhs, const strong_ref<T>& rhs) {
    return lhs.ptr() == rhs.ptr();
}
template <typename T>
auto operator<=>(const ref<T>& lhs, const ref<T>& rhs) {
    return lhs.ptr() <=> rhs.ptr();
}

template <typename T>
bool operator!=(const ref<T>& lhs, const ref<T>& rhs) {
    return !(lhs == rhs);
}

template <typename T>
bool operator==(const ref<T>& lhs, const ref<T>& rhs) {
    return lhs.ptr() == rhs.ptr();
}

namespace std {
    template <typename T>
    struct hash<::ref<T>> {
        size_t operator()(const ::ref<T>& r) const noexcept {
            using ptr_t = typename ::ref<T>::pointer;
            return std::hash<ptr_t>{}(const_cast<ptr_t>(r.ptr()));
        }
    };
} // namespace std

template <typename T>
auto ref<T>::strong() {
    return strong_ref<T>{this->data};
}
