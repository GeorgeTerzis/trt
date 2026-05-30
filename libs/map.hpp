#pragma once

#include <boost/container/flat_map.hpp>
#include <flat_map>
#include <initializer_list>
#include <llvm/ADT/StringMap.h>
#include <string>
#include <unordered_map>
#include <utility>

template <typename Val>
struct string_map {
    using key_type = llvm::StringRef;
    using type = Val;
    using impl = llvm::StringMap<Val>;
    using size_type = typename impl::size_type;
    using iterator = typename impl::iterator;
    using const_iterator = typename impl::const_iterator;

    static constexpr std::string metadata = "map";

    impl data;

    // constructors
    string_map() = default;

    string_map(std::initializer_list<std::pair<llvm::StringRef, Val>> init) {
        for (const auto& [k, v] : init)
            data.try_emplace(k, v);
    }

    // element access
    Val& operator[](llvm::StringRef k) {
        return data[k];
    }

    Val& at(llvm::StringRef k) {
        auto it = data.find(k);
        assert(it != data.end());
        return it->second;
    }

    const Val& at(llvm::StringRef k) const {
        auto it = data.find(k);
        assert(it != data.end());
        return it->second;
    }

    // iterators
    iterator begin() noexcept {
        return data.begin();
    }
    const_iterator begin() const noexcept {
        return data.begin();
    }
    const_iterator cbegin() const noexcept {
        return data.cbegin();
    }
    iterator end() noexcept {
        return data.end();
    }
    const_iterator end() const noexcept {
        return data.end();
    }
    const_iterator cend() const noexcept {
        return data.cend();
    }

    // capacity
    bool empty() const noexcept {
        return data.empty();
    }
    size_type size() const noexcept {
        return data.size();
    }

    // modifiers
    void clear() noexcept {
        data.clear();
    }

    std::pair<iterator, bool> insert(llvm::StringRef key, const Val& value) {
        return data.try_emplace(key, value);
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(llvm::StringRef key, Args&&... args) {
        return data.try_emplace(key, std::forward<Args>(args)...);
    }

    void erase(iterator pos) {
        data.erase(pos);
    }

    size_type erase(llvm::StringRef key) {
        return data.erase(key);
    }

    void swap(string_map& other) noexcept {
        data.swap(other.data);
    }

    // lookup
    size_type count(llvm::StringRef key) const {
        return data.count(key);
    }

    iterator find(llvm::StringRef key) {
        return data.find(key);
    }

    const_iterator find(llvm::StringRef key) const {
        return data.find(key);
    }

    bool contains(llvm::StringRef key) const {
        return data.contains(key);
    }
};

template <typename Key, typename Val>
struct map {
    using key_type = Key;
    using type = Val;
    using value_type = std::pair<const Key, Val>;
    using impl = boost::container::flat_map<Key, Val>;
    using size_type = typename impl::size_type;
    using iterator = typename impl::iterator;
    using const_iterator = typename impl::const_iterator;

    static constexpr std::string metadata = "map";

    impl data;

    // constructors
    map() = default;
    explicit map(const impl& other) : data(other) {}
    explicit map(impl&& other) noexcept : data(std::move(other)) {}
    map(std::initializer_list<value_type> init) : data(init) {}

    // assignment
    map& operator=(const impl& other) {
        data = other;
        return *this;
    }
    map& operator=(impl&& other) noexcept {
        data = std::move(other);
        return *this;
    }
    map& operator=(std::initializer_list<value_type> init) {
        data = init;
        return *this;
    }

    // element access
    Val& operator[](const Key& k) {
        return data[k];
    }
    Val& operator[](Key&& k) {
        return data[std::move(k)];
    }
    Val& at(const Key& k) {
        return data.at(k);
    }
    const Val& at(const Key& k) const {
        return data.at(k);
    }

    // iterators
    iterator begin() noexcept {
        return data.begin();
    }
    const_iterator begin() const noexcept {
        return data.begin();
    }
    const_iterator cbegin() const noexcept {
        return data.cbegin();
    }
    iterator end() noexcept {
        return data.end();
    }
    const_iterator end() const noexcept {
        return data.end();
    }
    const_iterator cend() const noexcept {
        return data.cend();
    }

    // capacity
    bool empty() const noexcept {
        return data.empty();
    }
    size_type size() const noexcept {
        return data.size();
    }

    // modifiers
    void clear() noexcept {
        data.clear();
    }
    std::pair<iterator, bool> insert(const value_type& v) {
        return data.insert(v);
    }
    std::pair<iterator, bool> insert(value_type&& v) {
        return data.insert(std::move(v));
    }
    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return data.insert_or_assign(std::forward<Args>(args)...);
    }
    void erase(iterator pos) {
        data.erase(pos);
    }
    size_type erase(const Key& key) {
        return data.erase(key);
    }

    void swap(map& other) noexcept {
        data.swap(other.data);
    }

    // lookup
    size_type count(const Key& key) const {
        return data.count(key);
    }
    iterator find(const Key& key) {
        return data.find(key);
    }
    const_iterator find(const Key& key) const {
        return data.find(key);
    }
    bool contains(const Key& key) const {
        return data.contains(key);
    }
};

template <typename Key, typename Val, typename Compare>
bool equals(const map<Key, Val>& lhs, const map<Key, Val>& rhs, Compare cmp) {
    if (lhs.size() != rhs.size())
        return false;

    auto it1 = lhs.begin();
    auto it2 = rhs.begin();

    while (it1 != lhs.end()) {
        if (it1->first != it2->first || !cmp(it1->second, it2->second)) {
            return false;
        }
        ++it1;
        ++it2;
    }

    return true;
}
