// #include <boost/container/vector.hpp>
#include <llvm/ADT/SmallVector.h>
#include <string>
#include <vector>

template <typename T>
struct vector {
    static constexpr std::string metadata = "vector";

    using type = T;

    template <typename Y>
    using impl = llvm::SmallVector<Y, 0>;

    using vectype = impl<type>;
    vectype data;

    vector() = default;
    vector(std::initializer_list<T> init) : data(init) {}

    bool empty() const noexcept { return data.empty(); }
    std::size_t size() const noexcept { return data.size(); }

    T& operator[](std::size_t i) { return data[i]; }
    const T& operator[](std::size_t i) const { return data[i]; }

    T& front() { return data.front(); }
    const T& front() const { return data.front(); }

    T& back() { return data.back(); }
    const T& back() const { return data.back(); }

    void push_back(const T& value) { data.push_back(value); }
    void push_back(T&& value) { data.push_back(std::move(value)); }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        data.emplace_back(std::forward<Args>(args)...);
    }

    void pop_back() { data.pop_back(); }
    void clear() noexcept { data.clear(); }

    auto begin() noexcept { return data.begin(); }
    auto end() noexcept { return data.end(); }
    auto begin() const noexcept { return data.begin(); }
    auto end() const noexcept { return data.end(); }

    auto erase(auto it) { return data.erase(it); }
};
