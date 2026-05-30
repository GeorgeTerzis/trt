// Something very weird is going with the iterators
//  should just have the span generate them

// #pragma once

// #include <cstdint>
// #include <algorithm>
// #include <cstdlib>
// #include <cstring>
// #include <iterator>
// #include <optional>
// #include <span>
// #include <type_traits>
// #include <utility>

// /* Move only container
//  *  made for PODs
//  *  manualy allocated
//     manualy deallocated */
// template <typename T> struct podlist_t {
//   using len_t = std::uint32_t;
//   T *begin_;
//   len_t len_;
//   len_t cap_;

//   podlist_t(T *begin, len_t len, len_t cap)
//       : begin_(begin), len_(len), cap_(cap) {}
//   podlist_t() : begin_(nullptr), len_(0), cap_(0) {}

//   constexpr static podlist_t create(const len_t cap) {
//     return podlist_t((T *)std::aligned_alloc(alignof(T), cap * sizeof(T)), 0, cap);
//     // std::cout << __PRETTY_FUNCTION__ << " " << cap << std::endl;
//   }

//   auto empty() const -> bool { return len_ == 0; }

//   template <const len_t cap = 0> static podlist_t create() {
//     if constexpr (cap == 0) {
//       return podlist_t{nullptr, 0, 0};
//     } else {
//       return podlist_t((T *)std::aligned_alloc(alignof(T),cap * sizeof(T)), 0, cap);
//     }
//   }
//   constexpr static podlist_t make() { return podlist_t{nullptr, 0, 0}; }

//   template <typename... Args> static podlist_t<T> make(Args &&...args) {
//     constexpr len_t cap = sizeof...(Args);
//     auto p = podlist_t<T>::create(cap);
//     p.append(std::move(args)...);
//     return std::move(p);
//   }

//   template <typename... Args> static podlist_t<T> make_from_opt(Args &...args) {
//     constexpr len_t cap = sizeof...(Args) + 1;
//     // auto p = podlist_t((T *)std::malloc(cap * sizeof(T)), 0, cap);
//     auto p = podlist_t<T>::create<cap>();
//     (
//         [&](std::optional<T> &v) {
//           if (v.has_value())
//             p.push_back(std::move(v.value()));
//         }(args),
//         ...);
//     return std::move(p);
//   }

//   T &at(len_t index) { return begin_[index]; }
//   const T &at(len_t index) const { return begin_[index]; }

//   void resize(const len_t newcap) {
//     T *const re = (T *)realloc(begin_, newcap * sizeof(T));

//     // it is never gonna fail :)
//     // if(re){
//     begin_ = re;
//     cap_ = newcap;
//     // } else{
//     //  OwO fail OwO
//     // }
//   }

//   void release() {
//     if (begin_) {
//       free(begin_);
//     }
//   }

//   podlist_t<T> copy() {
//     if (len_ == 0)
//       return podlist_t<T>();

//     auto p = podlist_t<T>::make(len_);
//     std::memcpy(p.begin_, this->begin_, len_ * sizeof(T));
//     return std::move(p);
//   }

//   podlist_t(podlist_t &&other) noexcept {
//     this->begin_ = other.begin_;
//     this->len_ = other.len_;
//     this->cap_ = other.cap_;

//     // other.begin_ = nullptr;
//     // other.len_ = 0;
//     // other.cap_ = 0;
//   }
//   // Move assignment operator
//   podlist_t &operator=(podlist_t &&other) noexcept {
//     if (__builtin_expect((bool)(this != &other), false)) {
//       this->release(); // Free the current resources
//       begin_ = other.begin_;
//       len_ = other.len_;
//       cap_ = other.cap_;

//       // other.begin_ = nullptr;
//       // other.len_ = 0;
//       // other.cap_ = 0;
//     }
//     return *this;
//   }

//   // Delete copy constructor and copy assignment operator
//   podlist_t(const podlist_t &) = delete;
//   podlist_t &operator=(const podlist_t &) = delete;

//   void shrink_to_fit() { resize(len_); }
//   auto size() const { return len_; }
//   auto size_in_bytes() const { return len_ * sizeof(T); }
//   auto length() const { return len_; }
//   auto cap() const { return cap_; }
//   auto cap_in_bytes() const { return cap_ * sizeof(T); }

//   auto data() { return begin_; }
//   const auto data() const { return begin_; }

//   auto &back() { return *(end() - 1); }
//   const auto &back() const { return *(end() - 1); }

//   auto &front() { return *(begin()); }
//   const auto &front() const { return *(begin()); }

// #define PODLIST_GROWTH_FACTOR ((cap_ > 50000) ? 2 : 30.0)
//   // #define PODLIST_GROWTH_FACTOR 1.1
//   void push_back(const T &val) {
//     if (len_ == cap_) [[unlikely]]
//       resize(cap_ * PODLIST_GROWTH_FACTOR + 1);
//     memcpy(begin_ + len_, &val, sizeof(T));
//     // *(begin_ + len_) = std::move(val);

//     ++len_;
//   }
//   void push_back(T &&val) {
//     if (len_ == cap_) [[unlikely]]
//       resize(cap_ * PODLIST_GROWTH_FACTOR + 1);
//     memcpy(begin_ + len_, &val, sizeof(T));
//     ++len_;
//   }

//   void push_back_assume_size(T &&val) {
//     memcpy(begin_ + len_, &val, sizeof(T));
//     ++len_;
//   }

//   void push_back_assume_size(T &val) {
//     memcpy(begin_ + len_, &val, sizeof(T));
//     ++len_;
//   }

//   template <typename... Args> void append(Args &&...args) {
//     constexpr auto arg_len = sizeof...(Args);

//     if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
//       resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

//     auto a = std::array<T, arg_len>{std::move(args)...};
//     memcpy(begin_ + len_, a.begin(), sizeof(T) * arg_len);
//     len_ += a.size();
//   }
//   template <typename... Args> void append(Args &...args) {
//     constexpr auto arg_len = sizeof...(Args);

//     if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
//       resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

//     constexpr auto a = std::array<T, sizeof...(Args)>{std::move(args)...};
//     memcpy(begin_ + len_, a.begin(), sizeof(T) * arg_len);
//     len_ += a.size();
//   }
//   void append(std::span<T> span) {
//     auto arg_len = span.size();

//     if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
//       resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

//     memcpy(begin_ + len_, span.begin(), sizeof(T) * arg_len);
//     len_ += span.size();
//   }

//   void clear() { this->len_ = 0; }

//   void insert(std::uint64_t index, std::span<T> span) {
//     if (index >= cap_)
//       append(span);

//     const auto leni = span.size();
//     const auto lenv = len_;
//     const auto newcap = lenv + leni;
//     const auto movelen = cap_ - index;
//     if (newcap > cap_)
//       this->resize(lenv + leni + lenv * 1.2);
//     // const auto endi = cap_ - 1;
//     const auto oldmovei = cap_ - movelen;

//     std::memmove(begin_ + index, begin_ + oldmovei, sizeof(T) * (movelen));
//     std::memcpy(begin_ + index, span.data(), sizeof(T) * leni);
//   }

//   void regress(const len_t rlen) {
//     if (len_ < rlen)
//       return;
//     len_ -= rlen;
//   }
//   void pop_back() { regress(1); }

//   struct podlist_iterator_t {
//     // using iterator_category = std::contiguous_iterator_tag;
//     // using difference_type = std::ptrdiff_t;
//     // using value_type = T;
//     // using pointer = typename std::conditional<is_const, const T*, T*>::type;
//     // using reference = typename std::conditional<is_const, const T&,
//     // T&>::type;
//     using iterator_category = std::contiguous_iterator_tag;
//     using difference_type = std::ptrdiff_t;
//     using value_type = T;
//     using pointer = T *;
//     using cpointer = const T *;
//     using reference = T &;

//     explicit podlist_iterator_t() : ptr_(nullptr) {}
//     explicit podlist_iterator_t(pointer ptr) : ptr_(ptr) {}
//     explicit podlist_iterator_t(cpointer ptr) : ptr_(const_cast<pointer>(ptr)) {}

//     // template <bool other_is_const>
//     podlist_iterator_t(const podlist_iterator_t &other): ptr_(other.ptr_) {}
//     // template <bool ic0, bool ic1>
//     // podlist_iterator_t<ic0>(podlist_iterator_t<ic1> ptr)
//     //     : ptr_(ptr.ptr_) {}
//     // explicit podlist_iterator_t<true>(podlist_iterator_t<false> ptr) :
//     // ptr_(ptr) {}

//     reference operator*() const { return *ptr_; }
//     pointer operator->() { return ptr_; }
//     const pointer operator->() const { return ptr_; }
//     podlist_iterator_t &operator++() {
//       ++ptr_;
//       return *this;
//     }
//     podlist_iterator_t operator++(int) {
//       podlist_iterator_t tmp = *this;
//       ++(*this);
//       return tmp;
//     }

//     podlist_iterator_t &operator--() {
//       --ptr_;
//       return *this;
//     }
//     podlist_iterator_t operator--(int) {
//       podlist_iterator_t tmp = *this;
//       --(*this);
//       return tmp;
//     }

//     podlist_iterator_t operator+(difference_type n) const {
//       return podlist_iterator_t(ptr_ + n);
//     }
//     podlist_iterator_t operator-(difference_type n) const {
//       return podlist_iterator_t(ptr_ - n);
//     }

//     difference_type operator-(const podlist_iterator_t &other) const {
//       return ptr_ - other.ptr_;
//     }

//     podlist_iterator_t &operator+=(difference_type n) {
//       ptr_ += n;
//       return *this;
//     }
//     podlist_iterator_t &operator-=(difference_type n) {
//       ptr_ -= n;
//       return *this;
//     }

//     // Comparison operators
//     friend bool operator==(const podlist_iterator_t &a,
//                            const podlist_iterator_t &b) {
//       return a.ptr_ == b.ptr_;
//     }

//     friend bool operator!=(const podlist_iterator_t &a,
//                            const podlist_iterator_t &b) {
//       return a.ptr_ != b.ptr_;
//     }

//     friend bool operator<(const podlist_iterator_t &a,
//                           const podlist_iterator_t &b) {
//       return a.ptr_ < b.ptr_;
//     }

//     friend bool operator<=(const podlist_iterator_t &a,
//                            const podlist_iterator_t &b) {
//       return a.ptr_ <= b.ptr_;
//     }

//     friend bool operator>(const podlist_iterator_t &a,
//                           const podlist_iterator_t &b) {
//       return a.ptr_ > b.ptr_;
//     }

//     friend bool operator>=(const podlist_iterator_t &a,
//                            const podlist_iterator_t &b) {
//       return a.ptr_ >= b.ptr_;
//     }

//     pointer base() const { return ptr_; }

//     inline podlist_iterator_t &advance(len_t off = 1) {
//       ptr_ += off;
//       return *this;
//     }

//   private:
//     pointer ptr_;
//   };

//   // Define types for both const and non-const iterators
//   using iterator = podlist_iterator_t;
//   using const_iterator = const podlist_iterator_t;

//   using it = iterator;
//   using c_it = const_iterator;

//   // Accessor functions for iterators
//   iterator begin() { return iterator(begin_); }
//   iterator end() { return iterator(begin_ + len_); }

//   const_iterator begin() const { return const_iterator(begin_); }
//   const_iterator end() const { return const_iterator(begin_ + len_); }

//   const_iterator cbegin() const { return const_iterator(begin_); }
//   const_iterator cend() const { return const_iterator(begin_ + len_); }

//   template <bool is_const = false> auto view() const {
//     return std::span{begin(), end()};
//   }

//   template <bool cit>
//   auto it2index(podlist_iterator_t it) const -> std::uint64_t {
//     return it.base() - begin_;
//   }
//   template <bool is_const, typename... Args>
//   void insert(podlist_iterator_t it, Args &...args) {
//     constexpr auto arg_len = sizeof...(Args);
//     auto index = it2index(it);
//     insert(index, args...);
//   }
// };
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

// change the allocator to somthing with alignment??

/* Move only container
 *  made for PODs
 *  manualy allocated
    manualy deallocated */
template <typename T> struct podlist_t {
    using len_t = std::uint32_t;
    T *begin_;
    len_t len_;
    len_t cap_;

    podlist_t(T *begin, len_t len, len_t cap) : begin_(begin), len_(len), cap_(cap) {}
    podlist_t() : begin_(nullptr), len_(0), cap_(0) {}

    constexpr static podlist_t create(const len_t cap) {
        return podlist_t((T *)std::aligned_alloc(alignof(T), cap * sizeof(T)), 0, cap);
        // std::cout << __PRETTY_FUNCTION__ << " " << cap << std::endl;
    }

    auto empty() const -> bool { return len_ == 0; }

    template <const len_t cap = 0> static podlist_t create() {
        if constexpr (cap == 0) {
            return podlist_t{nullptr, 0, 0};
        } else {
            return podlist_t(
                (T *)std::aligned_alloc(alignof(T), cap * sizeof(T)), 0, cap);
        }
    }
    constexpr static podlist_t make() { return podlist_t{nullptr, 0, 0}; }

    template <typename... Args> static podlist_t<T> make(Args &&...args) {
        constexpr len_t cap = sizeof...(Args);
        auto p = podlist_t<T>::create(cap);
        p.append(std::move(args)...);
        return std::move(p);
    }

    template <typename... Args> static podlist_t<T> make_from_opt(Args &...args) {
        constexpr len_t cap = sizeof...(Args) + 1;
        // auto p = podlist_t((T *)std::malloc(cap * sizeof(T)), 0, cap);
        auto p = podlist_t<T>::create<cap>();
        (
            [&](std::optional<T> &v) {
                if (v.has_value())
                    p.push_back(std::move(v.value()));
            }(args),
            ...);
        return std::move(p);
    }

    T &at(len_t index) { return begin_[index]; }
    const T &at(len_t index) const { return begin_[index]; }

    void resize(const len_t newcap) {
        T *const re = (T *)realloc(begin_, newcap * sizeof(T));

        // it is never gonna fail :)
        // if(re){
        begin_ = re;
        cap_ = newcap;
        // } else{
        //  OwO fail OwO
        // }
    }

    void release() {
        if (begin_) {
            free(begin_);
        }
    }

    podlist_t<T> copy() {
        if (len_ == 0)
            return podlist_t<T>();

        auto p = podlist_t<T>::make(len_);
        std::memcpy(p.begin_, this->begin_, len_ * sizeof(T));
        return std::move(p);
    }

    podlist_t(podlist_t &&other) noexcept {
        this->begin_ = other.begin_;
        this->len_ = other.len_;
        this->cap_ = other.cap_;

        // other.begin_ = nullptr;
        // other.len_ = 0;
        // other.cap_ = 0;
    }
    // Move assignment operator
    podlist_t &operator=(podlist_t &&other) noexcept {
        if (__builtin_expect((bool)(this != &other), false)) {
            this->release(); // Free the current resources
            begin_ = other.begin_;
            len_ = other.len_;
            cap_ = other.cap_;

            // other.begin_ = nullptr;
            // other.len_ = 0;
            // other.cap_ = 0;
        }
        return *this;
    }

    // Delete copy constructor and copy assignment operator
    podlist_t(const podlist_t &) = delete;
    podlist_t &operator=(const podlist_t &) = delete;

    void shrink_to_fit() { resize(len_); }
    auto size() const { return len_; }
    auto size_in_bytes() const { return len_ * sizeof(T); }
    auto length() const { return len_; }
    auto cap() const { return cap_; }
    auto cap_in_bytes() const { return cap_ * sizeof(T); }

    auto data() { return begin_; }
    const auto data() const { return begin_; }

    auto &back() { return *(end() - 1); }
    const auto &back() const { return *(end() - 1); }

    auto &front() { return *(begin()); }
    const auto &front() const { return *(begin()); }

#define PODLIST_GROWTH_FACTOR ((cap_ > 50000) ? 2 : 30.0)
    // #define PODLIST_GROWTH_FACTOR 1.1
    void push_back(const T &val) {
        if (len_ == cap_) [[unlikely]]
            resize(cap_ * PODLIST_GROWTH_FACTOR + 1);
        memcpy(begin_ + len_, &val, sizeof(T));
        // *(begin_ + len_) = std::move(val);

        ++len_;
    }
    void push_back(T &&val) {
        if (len_ == cap_) [[unlikely]]
            resize(cap_ * PODLIST_GROWTH_FACTOR + 1);
        memcpy(begin_ + len_, &val, sizeof(T));
        ++len_;
    }

    void push_back_assume_size(T &&val) {
        memcpy(begin_ + len_, &val, sizeof(T));
        ++len_;
    }
    void push_back_assume_size(T &val) {
        memcpy(begin_ + len_, &val, sizeof(T));
        ++len_;
    }

    template <typename... Args> void append(Args &&...args) {
        constexpr auto arg_len = sizeof...(Args);

        if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
            resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

        auto a = std::array<T, arg_len>{std::move(args)...};
        memcpy(begin_ + len_, a.begin(), sizeof(T) * arg_len);
        len_ += a.size();
    }
    template <typename... Args> void append(Args &...args) {
        constexpr auto arg_len = sizeof...(Args);

        if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
            resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

        constexpr auto a = std::array<T, sizeof...(Args)>{std::move(args)...};
        memcpy(begin_ + len_, a.begin(), sizeof(T) * arg_len);
        len_ += a.size();
    }
    void append(std::span<T> span) {
        auto arg_len = span.size();

        if (__builtin_expect((bool)(len_ + arg_len >= cap_), false))
            resize(cap_ * PODLIST_GROWTH_FACTOR + arg_len);

        memcpy(begin_ + len_, span.begin(), sizeof(T) * arg_len);
        len_ += span.size();
    }

    void clear() { this->len_ = 0; }

    void insert(std::uint64_t index, std::span<T> span) {
        if (index >= cap_)
            append(span);

        const auto leni = span.size();
        const auto lenv = len_;
        const auto newcap = lenv + leni;
        const auto movelen = cap_ - index;
        if (newcap > cap_)
            this->resize(lenv + leni + lenv * 1.2);
        // const auto endi = cap_ - 1;
        const auto oldmovei = cap_ - movelen;

        std::memmove(begin_ + index, begin_ + oldmovei, sizeof(T) * (movelen));
        std::memcpy(begin_ + index, span.data(), sizeof(T) * leni);
    }

    void regress(const len_t rlen) {
        if (len_ < rlen)
            return;
        len_ -= rlen;
    }
    void pop_back() { regress(1); }

    template <typename O, bool is_const> struct podlist_iterator_t {
        // using iterator_category = std::contiguous_iterator_tag;
        // using difference_type = std::ptrdiff_t;
        // using value_type = T;
        // using pointer = typename std::conditional<is_const, const T*, T*>::type;
        // using reference = typename std::conditional<is_const, const T&,
        // T&>::type;
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = typename std::conditional<is_const, const T *, T *>::type;
        using reference = typename std::conditional<is_const, const T &, T &>::type;

        explicit podlist_iterator_t() : ptr_(nullptr) {}
        explicit podlist_iterator_t(pointer ptr) : ptr_(ptr) {}

        template <bool other_is_const>
        podlist_iterator_t(const podlist_iterator_t<O, other_is_const> &other) :
            ptr_(other.ptr_) {}
        // template <bool ic0, bool ic1>
        // podlist_iterator_t<ic0>(podlist_iterator_t<ic1> ptr)
        //     : ptr_(ptr.ptr_) {}
        // explicit podlist_iterator_t<true>(podlist_iterator_t<false> ptr) :
        // ptr_(ptr) {}

        reference operator*() const { return *ptr_; }
        pointer operator->() const { return ptr_; }
        podlist_iterator_t &operator++() {
            ++ptr_;
            return *this;
        }
        podlist_iterator_t operator++(int) {
            podlist_iterator_t tmp = *this;
            ++(*this);
            return tmp;
        }

        podlist_iterator_t &operator--() {
            --ptr_;
            return *this;
        }
        podlist_iterator_t operator--(int) {
            podlist_iterator_t tmp = *this;
            --(*this);
            return tmp;
        }

        podlist_iterator_t operator+(difference_type n) const {
            return podlist_iterator_t(ptr_ + n);
        }
        podlist_iterator_t operator-(difference_type n) const {
            return podlist_iterator_t(ptr_ - n);
        }

        difference_type operator-(const podlist_iterator_t &other) const {
            return ptr_ - other.ptr_;
        }

        podlist_iterator_t &operator+=(difference_type n) {
            ptr_ += n;
            return *this;
        }
        podlist_iterator_t &operator-=(difference_type n) {
            ptr_ -= n;
            return *this;
        }

        // Comparison operators
        friend bool operator==(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ == b.ptr_;
        }

        friend bool operator!=(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ != b.ptr_;
        }

        friend bool operator<(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ < b.ptr_;
        }

        friend bool operator<=(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ <= b.ptr_;
        }

        friend bool operator>(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ > b.ptr_;
        }

        friend bool operator>=(const podlist_iterator_t &a, const podlist_iterator_t &b) {
            return a.ptr_ >= b.ptr_;
        }

        pointer base() const { return ptr_; }

        inline podlist_iterator_t &advance(len_t off = 1) {
            ptr_ += off;
            return *this;
        }
        inline podlist_iterator_t next(len_t off = 1) {
            auto n = *this;
            n.advance();
            return n;
        }

        // private:
        pointer ptr_;
    };

    // template <bool is_const = false> struct span {
    //   using iterator = podlist_iterator_t<T, is_const>;

    //   iterator begin_;
    //   iterator end_;

    //   span(iterator begin, iterator end) : begin_(begin), end_(end) {}
    //   span(iterator begin, size_t len) : begin_(begin), end_(begin + len) {}

    //   span(T *bptr, T *eptr) : begin_(bptr), end_(eptr) {}
    //   span(T *bptr, size_t len) : begin_(bptr), end_(bptr + len) {}

    //   iterator begin() const { return begin_; }
    //   iterator end() const { return end_; }

    //   auto operator[](size_t index) -> T & { return *(begin_ + index); }
    //   auto operator[](size_t index) const -> const T & {
    //     return *(begin_ + index);
    //   }

    //   span slice(std::size_t start, std::size_t count) const {
    //     if (start + count > size())
    //       throw std::out_of_range("Span slice out of range");
    //     return span(begin_ + start, count);
    //   }

    //   bool empty() const { return begin_ == end_; }

    //   bool operator==(const span &other) const {
    //     return size() == other.size() && std::equal(begin_, end_, other.begin_);
    //   }

    //   bool operator!=(const span &other) const { return !(*this == other); }

    //   bool operator<(const span &other) const {
    //     return std::lexicographical_compare(begin_, end_, other.begin_,
    //                                         other.end_);
    //   }
    //   std::ptrdiff_t size() const { return end_ - begin_; }
    // };

    // Define types for both const and non-const iterators
    using iterator = podlist_iterator_t<T, false>;
    using const_iterator = podlist_iterator_t<T, true>;

    using it = iterator;
    using c_it = const_iterator;

    // Accessor functions for iterators
    iterator begin() { return iterator(begin_); }
    iterator end() { return iterator(begin_ + len_); }

    const_iterator begin() const { return const_iterator(begin_); }
    const_iterator end() const { return const_iterator(begin_ + len_); }

    const_iterator cbegin() const { return const_iterator(begin_); }
    const_iterator cend() const { return const_iterator(begin_ + len_); }

    auto view() const { return std::span{begin(), end()}; }

    template <bool cit>
    auto it2index(podlist_iterator_t<T, cit> it) const -> std::uint64_t {
        return it.base() - begin_;
    }
    template <bool is_const, typename... Args>
    void insert(podlist_iterator_t<T, is_const> it, Args &...args) {
        constexpr auto arg_len = sizeof...(Args);
        auto index = it2index(it);
        insert(index, args...);
    }
};
