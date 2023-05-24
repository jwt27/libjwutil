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

    // Statically allocated storage backend for circular_queue.
    template<typename T, std::size_t N, queue_sync Sync>
    struct circular_queue_static_storage :
        detail::circular_queue_static_storage_base<T, N, Sync>
    {
        static_assert(std::has_single_bit(N));
        static_assert(N > 1);

        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;

    protected:
        template<typename, std::size_t, queue_sync> friend struct circular_queue_static_storage;

        circular_queue_static_storage() noexcept = default;
    };

    // Efficient circular FIFO with configurable storage backend.
    template<typename Storage>
    struct circular_queue : Storage
    {
        using value_type = Storage::value_type;
        using size_type = Storage::size_type;
        using difference_type = Storage::difference_type;
        using reference = Storage::reference;
        using const_reference = Storage::const_reference;
        using pointer = Storage::pointer;
        using const_pointer = Storage::const_pointer;
        using iterator = circular_queue_iterator<circular_queue<Storage>, false>;
        using const_iterator = circular_queue_iterator<const circular_queue<Storage>, false>;
        using atomic_iterator = circular_queue_iterator<circular_queue<Storage>, true>;
        using atomic_const_iterator = circular_queue_iterator<const circular_queue<Storage>, true>;

        template<typename... A>
        circular_queue(A&&... args) : Storage { std::forward<A>(args)... } { }

        ~circular_queue() noexcept { clear(); }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(const value_type& value)
        {
            if (not try_push_back(value)) this->overflow();
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(value_type&& value)
        {
            if (not try_push_back(std::move(value))) this->overflow();
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        template<typename... A>
        reference emplace_back(A&&... args)
        {
            const auto ref = try_emplace_back(std::forward<A>(args)...);
            if (not ref) this->overflow();
            return *ref;
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(const value_type& value)
        {
            return do_append(1, [&](auto t) { this->construct(t, value); return t; }).has_value();
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(value_type&& value)
        {
            return do_append(1, [&](auto t) { this->construct(t, std::move(value)); return t; }).has_value();
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // empty std::optional on overflow.
        template<typename... A>
        std::optional<std::reference_wrapper<value_type>> try_emplace_back(A&&... args)
            noexcept (noexcept(this->construct(0u, std::forward<A>(args)...)))
        {
            return do_append(1, [&](auto t) { this->construct(t, std::forward<A>(args)...); return std::ref(*this->get(t)); });
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Throws on overflow, and in that case, no elements are added.
        iterator append(std::initializer_list<value_type> list)
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
            if (not i) this->overflow();
            return *i;
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n, const_reference value)
        {
            const auto i = try_append(n, value);
            if (not i) this->overflow();
            return *i;
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n)
        {
            const auto i = try_append(n);
            if (not i) this->overflow();
            return *i;
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Returns an empty std::optional on overflow, and in that case, no
        // elements are added.
        std::optional<iterator> try_append(std::initializer_list<value_type> list)
            noexcept (noexcept(try_append(list.begin(), list.end())))
        {
            return try_append(list.begin(), list.end());
        }

        // Add multiple elements to the end and return an iterator to the
        // first inserted element.  Other iterators are not invalidated.
        // Returns an empty std::optional on overflow, and in that case, no
        // elements are added.
        template<std::forward_iterator I, std::sized_sentinel_for<I> S>
        std::optional<iterator> try_append(I first, S last)
            noexcept (noexcept(this->copy_n(0u, 0u, first)))
        {
            const auto n = last - first;
            return do_append(n, [&](auto t) { this->copy_n(t, n, first); return iterator { this, t }; });
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n, const value_type& value)
            noexcept (noexcept(this->fill_n(0u, n, value)))
        {
            return do_append(n, [&](auto t) { this->fill_n(t, n, value); return iterator { this, t }; });
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n)
            noexcept (noexcept(this->default_construct_n(0u, n)))
        {
            return do_append(n, [&](auto t) { this->default_construct_n(t, n); return iterator { this, t }; });
        }

        // Fill the queue with copy-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill(const value_type& value)
            noexcept (noexcept(try_append(0u, value)))
        {
            return *try_append(max_size() - size_for_write(), value);
        }

        // Fill the queue with default-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill()
            noexcept (noexcept(try_append(0u, 0u)))
        {
            return *try_append(max_size() - size_for_write());
        }

        // Remove the specified number of elements from the beginning.  Only
        // iterators to the removed elements are invalidated.  No bounds checks
        // are performed!
        void pop_front(size_type n = 1) noexcept
        {
            const auto h = this->load_head(access::read);
            this->destroy_n(h, n);
            this->store_head(this->add(h, n));
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
            return this->get(check_pos(i));
        }

        const_reference at(size_type i) const
        {
            return this->get(check_pos(i));
        }

        reference       operator[](size_type i)       noexcept { return this->get(this->add(this->load_head(access::read), i)); }
        const_reference operator[](size_type i) const noexcept { return this->get(this->add(this->load_head(access::read), i)); }

        reference       front()       noexcept { return *this->get(this->load_head(access::read)); }
        const_reference front() const noexcept { return *this->get(this->load_head(access::read)); }
        reference       back()        noexcept { return *this->get(this->load_tail(access::read) - 1); }
        const_reference back()  const noexcept { return *this->get(this->load_tail(access::read) - 1); }

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
            return this->distance(this->load_head(access::write), this->load_tail(access::write));
        }

        // Return number of elements currently in the queue.  This is meant
        // for use by the reader thread only.
        size_type size_for_read() const noexcept
        {
            return this->distance(this->load_head(access::read), this->load_tail(access::read));
        }

        // Returns maximum number of elements that the queue can store.  This
        // is one less than the allocated space, since otherwise it is
        // impossible to distinguish between a "full" and "empty" state.
        size_type max_size() const noexcept { return Storage::allocated_size() - 1; }

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

        // Invoke FUNC and bump tail pointer by N, if there is enough free
        // space.
        template<typename F>
        std::optional<std::invoke_result_t<F, size_type>> do_append(size_type n, F&& func) noexcept(noexcept(std::declval<F>()(std::declval<size_type>())))
        {
            const auto t = this->load_tail(access::write);
            if (this->distance(this->load_head(access::write), t) + n > max_size()) return std::nullopt;
            const auto result = std::forward<F>(func)(t);
            this->store_tail(this->add(t, n));
            return { result };
        }

        // Throw if index I is out of bounds.  Return absolute position of I.
        size_type check_pos(size_type i) const
        {
            const auto h = this->load_head(access::read);
            if (i >= this->distance(h, this->load_tail(access::read))) throw std::out_of_range { "index past end" };
            return this->add(h, i);
        }

        size_type find_contiguous_end(size_type i) const noexcept
        {
            const auto t = this->load_tail(access::read);
            return i > t ? max_size() + 1 : t;
        }
    };

    template<typename T, std::size_t N, queue_sync Sync = queue_sync::none>
    using static_circular_queue = circular_queue<circular_queue_static_storage<T, N, Sync>>;
}
