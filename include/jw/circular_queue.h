/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <jw/common.h>
#include <array>
#include <type_traits>
#include <atomic>
#include <stdexcept>
#include <execution>
#include <optional>

namespace jw
{
    enum class queue_sync
    {
        // No synchronization.
        none,

        // Reads may be interrupted by writes.
        write_irq,

        // Writes may be interrupted by reads.
        read_irq,

        // Both reads and writes may occur simultaneously.
        thread
    };
}

#include <jw/detail/circular_queue.h>

namespace jw
{
    template<typename Queue, bool Atomic>
    struct circular_queue_iterator
    {
        using container_type = Queue;
        using size_type = Queue::size_type;
        using difference_type = Queue::difference_type;
        using value_type = std::conditional_t<std::is_const_v<Queue>, const typename Queue::value_type, typename Queue::value_type>;
        using pointer = value_type*;
        using reference = value_type&;
        using iterator_category = std::random_access_iterator_tag;

        circular_queue_iterator() noexcept = default;

        circular_queue_iterator(container_type* queue, size_type pos) noexcept : c { queue }, i { pos } { }

        template<typename Queue2, bool Atomic2> requires (std::is_const_v<Queue> or not std::is_const_v<Queue2>)
        circular_queue_iterator(const circular_queue_iterator<Queue2, Atomic2>& other) noexcept : c { other.c }, i { other.load() } { }

        template<typename Queue2, bool Atomic2> requires (std::is_const_v<Queue> or not std::is_const_v<Queue2>)
        circular_queue_iterator& operator=(const circular_queue_iterator<Queue2, Atomic2>& other) noexcept { c = other.c; store(other.load()); }

        reference operator[](difference_type n) const noexcept { return *(c->get(c->wrap(load() + n))); }
        reference operator*() const noexcept { return *(c->get(position())); }
        pointer operator->() const noexcept { return c->get(position()); }

        circular_queue_iterator& operator+=(difference_type n) noexcept { fetch_add(n); return *this; }
        circular_queue_iterator& operator-=(difference_type n) noexcept { fetch_sub(n); return *this; }
        circular_queue_iterator<container_type, false> operator+(difference_type n) const noexcept { return { c, load() + n }; }
        circular_queue_iterator<container_type, false> operator-(difference_type n) const noexcept { return { c, load() - n }; }

        circular_queue_iterator& operator++() noexcept { return *this += 1; }
        circular_queue_iterator& operator--() noexcept { return *this -= 1; }
        circular_queue_iterator<container_type, false> operator++(int) noexcept { return { c, fetch_add(1) }; }
        circular_queue_iterator<container_type, false> operator--(int) noexcept { return { c, fetch_sub(1) }; }

        friend circular_queue_iterator<container_type, false> operator+(difference_type n, const circular_queue_iterator& it) noexcept
        {
            return { it.c, it.load() + n };
        }

        // Assumes both iterators are from the same container, since it
        // doesn't make any sense to compare them otherwise.  If you do need
        // to check if two iterators are from the same container, use
        // operator <=>.
        template<typename Q2, bool A2> requires (std::is_same_v<std::remove_const_t<Queue>, std::remove_const_t<Q2>>)
        difference_type operator-(const circular_queue_iterator<Q2, A2>& b) const noexcept
        {
            const auto h = c->load_head();
            return c->distance(h, position()) - c->distance(h, b.position());
        }

        template<typename Q2, bool A2> requires (std::is_same_v<std::remove_const_t<Queue>, std::remove_const_t<Q2>>)
        bool operator==(const circular_queue_iterator<Q2, A2>& b) const noexcept
        {
            return &**this == &*b;
        }

        template<typename Q2, bool A2> requires (std::is_same_v<std::remove_const_t<Queue>, std::remove_const_t<Q2>>)
        bool operator!=(const circular_queue_iterator<Q2, A2>& b) const noexcept
        {
            return not (*this == b);
        }

        template<typename Q2, bool A2> requires (std::is_same_v<std::remove_const_t<Queue>, std::remove_const_t<Q2>>)
        std::partial_ordering operator<=>(const circular_queue_iterator<Q2, A2>& b) const noexcept
        {
            if (container() != b.container()) return std::partial_ordering::unordered;
            const auto x = *this - b;
            if (x < 0) return std::partial_ordering::less;
            if (x > 0) return std::partial_ordering::greater;
            return std::partial_ordering::equivalent;
        }

        size_type position() const noexcept { return c->wrap(load()); }
        size_type index() const noexcept { return c->distance(c->load_head(), position()); }
        container_type* container() const noexcept { return c; }
        circular_queue_iterator<container_type, true> atomic() const noexcept { return { c, load() }; }

        friend void swap(circular_queue_iterator& a, circular_queue_iterator& b)
        {
            using std::swap;
            swap(a.c, b.c);
            swap(a.i, b.i);
        }

    private:
        size_type load() const noexcept
        {
            if constexpr (Atomic) return i.load(std::memory_order_acquire);
            else return i;
        }

        void store(size_type x) noexcept
        {
            if constexpr (Atomic) i.store(x, std::memory_order_release);
            else i = x;
        }

        size_type fetch_add(size_type x) noexcept
        {
            if constexpr (Atomic) return i.fetch_add(x, std::memory_order_acq_rel);
            else
            {
                const auto j = i;
                i += x;
                return j;
            }
        }

        size_type fetch_sub(size_type x) noexcept
        {
            if constexpr (Atomic) return i.fetch_sub(x, std::memory_order_acq_rel);
            else
            {
                const auto j = i;
                i -= x;
                return j;
            }
        }

        template<typename, bool> friend struct circular_queue_iterator;
        container_type* c;
        std::conditional_t<Atomic, std::atomic<size_type>, size_type> i;
    };

    // Given two iterators, returns the one closest to begin().  Assumes both
    // iterators are from the same container.
    template<typename Qa, bool Aa, typename Qb, bool Ab> requires (std::is_same_v<std::remove_const_t<Qa>, std::remove_const_t<Qb>>)
    auto min(const circular_queue_iterator<Qa, Aa>& ia, const circular_queue_iterator<Qb, Ab>& ib) noexcept
    {
        using Q = std::conditional_t<std::is_const_v<Qa> and std::is_const_v<Qb>, const Qa, std::remove_const_t<Qa>>;
        using I = circular_queue_iterator<Q, false>;
        Q* q = [&] { if constexpr (std::is_const_v<Qa>) return ib.container(); else return ia.container(); }();
        const I a { q, ia.position() }, b { q, ib.position() };
        return a - b < 0 ? a : b;
    }

    // Given two iterators, returns the one furthest removed from begin().
    // Assumes both iterators are from the same container.
    template<typename Qa, bool Aa, typename Qb, bool Ab> requires (std::is_same_v<std::remove_const_t<Qa>, std::remove_const_t<Qb>>)
    auto max(const circular_queue_iterator<Qa, Aa>& ia, const circular_queue_iterator<Qb, Ab>& ib) noexcept
    {
        using Q = std::conditional_t<std::is_const_v<Qa> and std::is_const_v<Qb>, const Qa, std::remove_const_t<Qa>>;
        using I = circular_queue_iterator<Q, false>;
        Q* q = [&] { if constexpr (std::is_const_v<Qa>) return ib.container(); else return ia.container(); }();
        const I a { q, ia.position() }, b { q, ib.position() };
        return a - b > 0 ? a : b;
    }

    // Simple and efficient fixed-size circular FIFO.  Can be made thread-safe
    // for a single reader and single writer via template parameter Sync.
    template<typename T, std::size_t N, queue_sync Sync = queue_sync::none>
    requires (std::has_single_bit(N))
    struct circular_queue : detail::circular_queue_base<Sync>
    {
        using value_type = T;
        using size_type = detail::circular_queue_base<Sync>::size_type;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using iterator = circular_queue_iterator<circular_queue<T, N, Sync>, false>;
        using const_iterator = circular_queue_iterator<const circular_queue<T, N, Sync>, false>;
        using atomic_iterator = circular_queue_iterator<circular_queue<T, N, Sync>, true>;
        using atomic_const_iterator = circular_queue_iterator<const circular_queue<T, N, Sync>, true>;

        circular_queue() noexcept = default;

        ~circular_queue() { clear(); }

        // Copy-construct from other circular_queue.  Throws on overflow.
        template<std::size_t N2, queue_sync S2>
        explicit circular_queue(const circular_queue<T, N2, S2>& other)
            : circular_queue { other.begin(), other.end() } { }

        // Move-construct from other circular_queue.  Throws on overflow.
        template<std::size_t N2, queue_sync S2>
        explicit circular_queue(circular_queue<T, N2, S2>&& other)
            : circular_queue { std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()) }
        {
            other.clear();
        }

        // Construct from iterators.  Throws on overflow.
        template<std::forward_iterator I, std::sized_sentinel_for<I> S>
        circular_queue(I first, S last)
        {
            append(first, last);
        }

        // Clear and replace with contents from other circular_queue. Throws
        // on overflow, and in that case, the queue is emptied.  Not
        // thread-safe!
        template<std::size_t N2, queue_sync S2>
        circular_queue& operator=(const circular_queue<T, N2, S2>& other)
        {
            clear();
            append(other.cbegin(), other.cend());
            return *this;
        }

        // Clear and replace with contents from other circular_queue. Throws
        // on overflow, and in that case, the queue is emptied.  Not
        // thread-safe!
        template<std::size_t N2, queue_sync S2>
        circular_queue& operator=(circular_queue<T, N2, S2>&& other)
        {
            clear();
            append(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
            other.clear();
            return *this;
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(const T& value)
        {
            if (not try_push_back(value)) overflow();
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(T&& value)
        {
            if (not try_push_back(std::move(value))) overflow();
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        template<typename... A>
        reference emplace_back(A&&... args)
        {
            const auto ref = try_emplace_back(std::forward<A>(args)...);
            if (not ref) overflow();
            return ref;
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            const auto x = bump(1);
            if (not x) return false;
            std::construct_at(get(add(*x, -1)), value);
            this->store_tail(*x);
            return true;
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            const auto x = bump(1);
            if (not x) return false;
            std::construct_at(get(add(*x, -1)), std::move(value));
            this->store_tail(*x);
            return true;
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // empty std::optional on overflow.
        template<typename... A>
        std::optional<reference> try_emplace_back(A&&... args) noexcept(std::is_nothrow_constructible_v<T, A...>)
        {
            const auto x = bump(1);
            if (not x) return std::nullopt;
            const auto i = add(*x, -1);
            std::construct_at(get(i), std::forward<A>(args)...);
            this->store_tail(*x);
            return { *get(i) };
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Throws on overflow, and in that case, no elements are added.
        iterator append(std::initializer_list<T> list)
        {
            return append(list.begin(), list.end());
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Throws on overflow, and in that case, no elements are added.
        template<std::forward_iterator I, std::sized_sentinel_for<I> S>
        iterator append(I first, S last)
        {
            const auto i = try_append(first, last);
            if (not i) overflow();
            return *i;
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n, const_reference value)
        {
            const auto i = try_append(n, value);
            if (not i) overflow();
            return *i;
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n)
        {
            const auto i = try_append(n);
            if (not i) overflow();
            return *i;
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Returns an empty std::optional on overflow, and in that case, no
        // elements are added.
        std::optional<iterator> try_append(std::initializer_list<T> list) noexcept
        {
            return try_append(list.begin(), list.end());
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Returns an empty std::optional on overflow, and in that case, no
        // elements are added.
        template<std::forward_iterator I, std::sized_sentinel_for<I> S>
        requires (std::is_nothrow_constructible_v<T, std::iter_reference_t<I>>)
        std::optional<iterator> try_append(I first, S last) noexcept
        {
            const auto x = bump(last - first);
            if (not x) return std::nullopt;
            const auto it = iterator { this, this->load_tail(access::write) };
            std::uninitialized_copy(std::execution::par_unseq, first, last, it);
            this->store_tail(*x);
            return { it };
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Returns an empty std::optional on overflow, and in that case, no
        // elements are added.
        template<std::forward_iterator I, std::sized_sentinel_for<I> S>
        std::optional<iterator> try_append(I it, S last)
        {
            const auto n = last - it;
            const auto x = bump(n);
            if (not x) return std::nullopt;
            const auto t = add(*x, -n);
            for (unsigned i = 0; i < n; ++i, ++it)
            {
                try { std::construct_at(get(add(t, i)), *it); }
                catch (...)
                {
                    destroy(t, i);
                    throw;
                }
            }
            this->store_tail(*x);
            return { iterator { this, t } };
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n, const_reference value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            const auto x = bump(n);
            if (not x) return std::nullopt;
            const auto t = add(*x, -n);
            if constexpr (std::is_nothrow_copy_constructible_v<T>)
            {
                std::uninitialized_fill_n(std::execution::par_unseq, iterator { this, t }, n, value);
            }
            else
            {
                for (unsigned i = 0; i < n; ++i)
                {
                    try { std::construct_at(get(add(t, i)), value); }
                    catch (...)
                    {
                        destroy(t, i);
                        throw;
                    }
                }
            }
            this->store_tail(*x);
            return { iterator { this, t } };
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n) noexcept(std::is_nothrow_default_constructible_v<T>)
        {
            const auto x = bump(n);
            if (not x) return std::nullopt;
            const auto t = add(*x, -n);
            if constexpr (std::is_nothrow_default_constructible_v<T>)
            {
                std::uninitialized_default_construct_n(std::execution::par_unseq, iterator { this, t }, n);
            }
            else
            {
                for (unsigned i = 0; i < n; ++i)
                {
                    try { new (get(add(t, i))) T; }
                    catch (...)
                    {
                        destroy(t, i);
                        throw;
                    }
                }
            }
            this->store_tail(*x);
            return { iterator { this, t } };
        }

        // Fill the queue with copy-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill(const_reference value) noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            return *try_append(max_size() - size_for_write(), value);
        }

        // Fill the queue with default-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill() noexcept(std::is_nothrow_default_constructible_v<T>)
        {
            return *try_append(max_size() - size_for_write());
        }

        // Remove the specified number of elements from the beginning.  Only
        // iterators to the removed elements are invalidated.  No bounds checks
        // are performed!
        void pop_front(size_type n = 1) noexcept
        {
            const auto h = this->load_head(access::read);
            destroy(h, n);
            this->store_head(add(h, n));
        }

        // Remove elements from the beginning, so that the given iterator
        // becomes the new head position.  Only iterators to the removed
        // elements are invalidated.  No bounds checks are performed!
        void pop_front_to(const_iterator it) noexcept
        {
            return pop_front(it.index());
        }

        // Remove all elements.
        void clear() noexcept
        {
            return pop_front_to(cend());
        }

        reference at(size_type i)
        {
            const auto h = check_pos(i);
            return get(add(h, i));
        }

        const_reference at(size_type i) const
        {
            const auto h = check_pos(i);
            return get(add(h, i));
        }

        reference       operator[](size_type i)       noexcept { return get(add(this->load_head(access::read), i)); }
        const_reference operator[](size_type i) const noexcept { return get(add(this->load_head(access::read), i)); }

        reference       front()       noexcept { return *get(this->load_head(access::read)); }
        const_reference front() const noexcept { return *get(this->load_head(access::read)); }
        reference       back()        noexcept { return *get(this->load_tail(access::read) - 1); }
        const_reference back()  const noexcept { return *get(this->load_tail(access::read) - 1); }

        iterator        begin()       noexcept { return { this, this->load_head(access::read) }; }
        const_iterator  begin() const noexcept { return { this, this->load_head(access::read) }; }
        const_iterator cbegin() const noexcept { return { this, this->load_head(access::read) }; }

        iterator        end()       noexcept { return { this, this->load_tail(access::read) }; }
        const_iterator  end() const noexcept { return { this, this->load_tail(access::read) }; }
        const_iterator cend() const noexcept { return { this, this->load_tail(access::read) }; }

        // Returns the end iterator that is furthest removed from i, while
        // keeping the range [i, end) contiguous in memory.
        iterator       contiguous_end(const_iterator i)       noexcept { return { this, find_contiguous_end(i.position()) }; }
        const_iterator contiguous_end(const_iterator i) const noexcept { return { this, find_contiguous_end(i.position()) }; }

        // Return number of elements currently in the queue.  This is meant
        // for use by the writer thread only.
        size_type size_for_write() const noexcept
        {
            return distance(this->load_head(access::write), this->load_tail(access::write));
        }

        // Return number of elements currently in the queue.  This is meant
        // for use by the reader thread only.
        size_type size_for_read() const noexcept
        {
            return distance(this->load_head(access::read), this->load_tail(access::read));
        }

        // Returns maximum number of elements that the queue can store.  This
        // is one less than N, since otherwise it is impossible to distinguish
        // between a "full" and "empty" state.
        size_type max_size() const noexcept { return N - 1; }

        // Check if the queue is empty.  This is meant for use by the reader
        // thread only.
        bool empty() const noexcept
        {
            return this->load_head(access::read) == this->load_tail(access::read);
        }

        static_assert(std::random_access_iterator<iterator>);

    private:
        template<typename, bool> friend struct circular_queue_iterator;
        using access = detail::queue_access;

        static void overflow() { throw std::length_error { "circular_queue overflow" }; }

        static size_type wrap(size_type i) noexcept{ return i & (N - 1); }

        // Find relative position (distance) of I from head position H.
        static size_type distance(size_type h, size_type i) noexcept
        {
            const difference_type n = i - h;
            if (n >= 0) return n;
            else return N + n;
        }

        // Find absolute position of index I from head position H.
        static size_type add(size_type h, difference_type i) noexcept
        {
            assume(h < N);
            return wrap(h + i);
        }

        // Get pointer to element at absolute position.
        T* get(size_type i) noexcept { return std::launder(reinterpret_cast<T*>(&storage[i])); }
        const T* get(size_type i) const noexcept { return std::launder(reinterpret_cast<const T*>(&storage[i])); }

        // Return new tail position for adding N elements, if enough space is
        // available.
        std::optional<size_type> bump(size_type n)
        {
            const auto t = this->load_tail(access::write);
            if (distance(this->load_head(access::write), t) + n >= N) return std::nullopt;
            return { add(t, n) };
        }

        // Destroy N elements starting at absolute position I.
        void destroy(size_type i, size_type n)
        {
            const auto max_n = std::min(n, N - i);
            std::destroy_n(std::execution::par_unseq, get(i), max_n);
            if (max_n < n) std::destroy_n(std::execution::par_unseq, get(0), n - max_n);
        }

        // Throw if position is out of bounds.  Return current head.
        size_type check_pos(size_type i) const
        {
            const auto h = this->load_head(access::read);
            if (i >= distance(h, this->load_tail(access::read))) throw std::out_of_range { "index past end" };
            return h;
        }

        size_type find_contiguous_end(size_type i) const noexcept
        {
            const auto t = this->load_tail(access::read);
            return i > t ? N : t;
        }

        std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, N> storage;
    };
}
