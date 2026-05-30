#pragma once

#include <flat_set>
#include <initializer_list>
#include <string>
#include <utility>

template <typename Key>
struct set {
  using key_type = Key;
  using value_type = Key;
  using impl = std::flat_set<Key>;
  using size_type = typename impl::size_type;
  using iterator = typename impl::iterator;
  using const_iterator = typename impl::const_iterator;

  static constexpr std::string metadata = "set";

  impl data;

  // constructors
  set() = default;
  explicit set(const impl& other) : data(other) {}
  explicit set(impl&& other) noexcept : data(std::move(other)) {}
  set(std::initializer_list<value_type> init) : data(init) {}

  // assignment
  set& operator=(const impl& other) {
    data = other;
    return *this;
  }
  set& operator=(impl&& other) noexcept {
    data = std::move(other);
    return *this;
  }
  set& operator=(std::initializer_list<value_type> init) {
    data = init;
    return *this;
  }

  // iterators
  iterator begin() noexcept { return data.begin(); }
  const_iterator begin() const noexcept { return data.begin(); }
  const_iterator cbegin() const noexcept { return data.cbegin(); }
  iterator end() noexcept { return data.end(); }
  const_iterator end() const noexcept { return data.end(); }
  const_iterator cend() const noexcept { return data.cend(); }

  // capacity
  bool empty() const noexcept { return data.empty(); }
  size_type size() const noexcept { return data.size(); }

  // modifiers
  void clear() noexcept { data.clear(); }
  std::pair<iterator, bool> insert(const value_type& v) {
    return data.insert(v);
  }
  std::pair<iterator, bool> insert(value_type&& v) {
    return data.insert(std::move(v));
  }
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return data.emplace(std::forward<Args>(args)...);
  }
  void erase(iterator pos) { data.erase(pos); }
  size_type erase(const Key& key) { return data.erase(key); }

  void swap(set& other) noexcept { data.swap(other.data); }

  // lookup
  size_type count(const Key& key) const { return data.count(key); }
  iterator find(const Key& key) { return data.find(key); }
  const_iterator find(const Key& key) const { return data.find(key); }
  bool contains(const Key& key) const { return data.contains(key); }
};
