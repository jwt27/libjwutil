/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2023 - 2024 J.W. Jagersma, see COPYING.txt for details    */

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
    // Synchronization mode for circular_queue.  This container can be made
    // thread- and interrupt-safe for a single "producer" adding elements to
    // the back, and a single "consumer" popping elements from the front.
    // The "interrupt" sync modes are meant for single-CPU systems only, where
    // either a read or a write will be an inherently atomic operation.
    enum class queue_sync
    {
        // No synchronization.
        none,

        // Reads may be interrupted by writes.
        producer_irq,

        // Writes may be interrupted by reads.
        consumer_irq,

        // Both reads and writes may occur simultaneously.
        thread
    };
}

#include <jw/detail/circular_queue.h>

namespace jw
{
    // Iterator for circular_queue.  Can be made atomic to synchronize between
    // threads.
    // As an optimization, most functions and operators in this class will
    // assume that both operands are from the same container.  Only
    // operator <=> doesn't make this assumption.
    template<typename Queue, bool Atomic>
    struct circular_queue_iterator
    {
        static constexpr bool is_const = std::is_const_v<Queue>;
        static constexpr bool is_atomic = Atomic;

        using container_type = Queue;
        using size_type = Queue::size_type;
        using difference_type = Queue::difference_type;
        using value_type = std::conditional_t<is_const, const typename Queue::value_type, typename Queue::value_type>;
        using pointer = std::conditional_t<is_const, typename Queue::const_pointer, typename Queue::pointer>;
        using reference = std::conditional_t<is_const, typename Queue::const_reference, typename Queue::reference>;
        using iterator_category = std::random_access_iterator_tag;

        circular_queue_iterator() noexcept = default;

        circular_queue_iterator(container_type* queue, size_type pos) noexcept : c { queue }, i { pos } { }

        circular_queue_iterator(const circular_queue_iterator& other) noexcept : c { other.c }, i { other.load() } { }

        template<typename Queue2, bool Atomic2> requires (std::is_const_v<Queue> or not std::is_const_v<Queue2>)
        circular_queue_iterator(const circular_queue_iterator<Queue2, Atomic2>& other) noexcept : c { other.c }, i { other.load() } { }

        circular_queue_iterator& operator=(const circular_queue_iterator& other) noexcept
        {
            c = other.c;
            store(other.load());
            return *this;
        }

        template<typename Queue2, bool Atomic2> requires (std::is_const_v<Queue> or not std::is_const_v<Queue2>)
        circular_queue_iterator& operator=(const circular_queue_iterator<Queue2, Atomic2>& other) noexcept
        {
            c = other.c;
            store(other.load());
            return *this;
        }

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

        difference_type operator-(const circular_queue_iterator<const Queue, false>& b) const noexcept
        {
            const auto h = c->load_head();
            assume(container() == b.container());
            return distance(h, position()) - distance(h, b.position());
        }

        bool operator==(const circular_queue_iterator<const Queue, false>& b) const noexcept
        {
            return c->wrap(load() xor b.load()) == 0;
        }

        bool operator!=(const circular_queue_iterator<const Queue, false>& b) const noexcept
        {
            return not (*this == b);
        }

        // Given an iterator i that lies between min and max, add an offset to
        // it, without going past either min or max.  Assumes all iterators
        // are from the same container.
        friend auto clamp_add(const circular_queue_iterator& i, difference_type delta,
                              const circular_queue_iterator<const Queue, false>& min,
                              const circular_queue_iterator<const Queue, false>& max) noexcept
        {
            using I = circular_queue_iterator<Queue, false>;
            const auto pos = i.position();
            auto* const queue = i.container();
            assume(min.container() == queue);
            assume(max.container() == queue);
            if (delta == 0) return I { i };
            if (delta > 0)  return I { queue, pos + std::min(static_cast<size_type>( delta), i.distance(pos, max.position())) };
            else            return I { queue, pos - std::min(static_cast<size_type>(-delta), i.distance(min.position(), pos)) };
        }

        // Given an iterator i that is ahead of this iterator, find the
        // distance between them.  That is: b == a + a.distance_to(b).
        // Assumes both iterators are from the same container.  Slightly
        // faster than subtraction.
        size_type distance_to(const circular_queue_iterator<const Queue, false>& i) const noexcept
        {
            assume(container() == i.container());
            return distance(position(), i.position());
        }

        // Given an iterator i that comes before this iterator, find the
        // distance between them.  That is: b == a - a.distance_from(b).
        // Assumes both iterators are from the same container.  Slightly
        // faster than subtraction.
        size_type distance_from(const circular_queue_iterator<const Queue, false>& other) const noexcept
        {
            assume(container() == i.container());
            return distance(i.position(), position());
        }

        size_type position() const noexcept { return c->wrap(load()); }
        size_type index() const noexcept { return distance(c->load_head(), position()); }
        container_type* container() const noexcept { return c; }
        circular_queue_iterator<container_type, true> atomic() const noexcept { return { c, load() }; }

        friend void swap(circular_queue_iterator& a, circular_queue_iterator& b)
        {
            using std::swap;
            swap(a.c, b.c);
            swap(a.i, b.i);
        }

    private:
        size_type distance(size_type h, size_type i) const noexcept
        {
            return c->distance(h, i);
        }

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

    template<typename Qa, typename Qb>
    std::partial_ordering operator<=>(const circular_queue_iterator<Qa, false>& a,
                                      const circular_queue_iterator<Qb, false>& b) noexcept
    {
        if (a.container() != b.container())
            return std::partial_ordering::unordered;
        const auto x = a - b;
        if (x < 0) return std::partial_ordering::less;
        if (x > 0) return std::partial_ordering::greater;
        return std::partial_ordering::equivalent;
    }

    // Given two iterators, returns the one closest to begin().  Assumes both
    // iterators are from the same container.
    template<typename Qa, bool Aa, typename Qb, bool Ab> requires (std::is_same_v<std::remove_const_t<Qa>, std::remove_const_t<Qb>>)
    auto min(const circular_queue_iterator<Qa, Aa>& ia, const circular_queue_iterator<Qb, Ab>& ib) noexcept
    {
        using Q = std::conditional_t<std::is_const_v<Qa> and std::is_const_v<Qb>, const Qa, std::remove_const_t<Qa>>;
        using I = circular_queue_iterator<Q, false>;
        assume(ia.container() == ib.container());
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
        assume(ia.container() == ib.container());
        Q* q = [&] { if constexpr (std::is_const_v<Qa>) return ib.container(); else return ia.container(); }();
        const I a { q, ia.position() }, b { q, ib.position() };
        return a - b > 0 ? a : b;
    }

    // Exception type thrown on overflow, from push_back() etc.
    struct circular_queue_overflow : std::length_error
    {
        circular_queue_overflow() : length_error { "circular_queue overflow" } { }
        circular_queue_overflow(const circular_queue_overflow&) noexcept = default;
        circular_queue_overflow& operator=(const circular_queue_overflow&) noexcept = default;
    };

    // Statically allocated storage backend for circular_queue.
    template<typename T, std::size_t N, queue_sync Sync>
    requires (std::has_single_bit(N))
    struct circular_queue_static_storage :
        detail::circular_queue_static_storage_base<T, N, Sync>
    {
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;

        circular_queue_static_storage() noexcept = default;
    };

    // Dynamically allocated storage backend for circular_queue.
    template<typename T, queue_sync Sync, typename Alloc>
    struct circular_queue_dynamic_storage :
        detail::circular_queue_dynamic_storage_base<T, Sync, Alloc>
    {
        using base = detail::circular_queue_dynamic_storage_base<T, Sync, Alloc>;

        using value_type = base::value_type;
        using size_type = base::size_type;
        using difference_type = base::difference_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = base::pointer;
        using const_pointer = base::const_pointer;

        // Resize allocated storage to at least the specified size.  All
        // iterators are invalidated.  Not thread-safe!
        void resize(size_type new_size) { base::resize(new_size); }

        // Create a new dynamic storage of at least the specified size.
        circular_queue_dynamic_storage(size_type size, const Alloc& allocator = { }) : base { size, allocator } { }

        // Move-construct from other.  Not thread-safe!
        template<queue_sync S2>
        circular_queue_dynamic_storage(circular_queue_dynamic_storage<T, S2, Alloc>&& other) : base { std::move(other) } { }

        // Move-assign from other.  Only available if the allocator allows
        // propagation on move-assignment.  Not thread-safe!
        template<queue_sync S2>
        circular_queue_dynamic_storage& operator=(circular_queue_dynamic_storage<T, S2, Alloc>&& other)
        {
            base::operator=(std::move(other));
            return *this;
        }
    };

    // Common interface to circular_queue for both producer and consumer
    // threads.
    template<typename Queue, typename Storage, detail::queue_access Access>
    struct circular_queue_common_interface
    {
        using value_type = Storage::value_type;
        using size_type = Storage::size_type;
        using difference_type = Storage::difference_type;
        using reference = Storage::reference;
        using const_reference = Storage::const_reference;
        using pointer = Storage::pointer;
        using const_pointer = Storage::const_pointer;
        using iterator = circular_queue_iterator<Queue, false>;
        using const_iterator = circular_queue_iterator<const Queue, false>;

        reference       at(size_type i)       { return self()->get(check_pos(i)); }
        const_reference at(size_type i) const { return self()->get(check_pos(i)); }

        reference       operator[](size_type i)       noexcept { return self()->get(self()->add(self()->load_head(Access), i)); }
        const_reference operator[](size_type i) const noexcept { return self()->get(self()->add(self()->load_head(Access), i)); }

        reference       front()       noexcept { return *self()->get(self()->load_head(Access)); }
        const_reference front() const noexcept { return *self()->get(self()->load_head(Access)); }
        reference       back()        noexcept { return *self()->get(self()->load_tail(Access) - 1); }
        const_reference back()  const noexcept { return *self()->get(self()->load_tail(Access) - 1); }

        iterator        begin()       noexcept { return { self(), self()->load_head(Access) }; }
        const_iterator  begin() const noexcept { return { self(), self()->load_head(Access) }; }
        const_iterator cbegin() const noexcept { return { self(), self()->load_head(Access) }; }

        iterator        end()       noexcept { return { self(), self()->load_tail(Access) }; }
        const_iterator  end() const noexcept { return { self(), self()->load_tail(Access) }; }
        const_iterator cend() const noexcept { return { self(), self()->load_tail(Access) }; }

        // Given a valid iterator to an element in this queue, produce the
        // pointer that is logically closest to begin(), while keeping the
        // closed range [ptr, &*i] contiguous in memory.
        pointer       contiguous_begin(const_iterator i)       noexcept { return self()->get(find_contiguous_begin(i.position())); }
        const_pointer contiguous_begin(const_iterator i) const noexcept { return self()->get(find_contiguous_begin(i.position())); }

        // Returns the pointer that is logically closest to end(), while
        // keeping the range [&*i, ptr) contiguous in memory.
        pointer       contiguous_end(const_iterator i)       noexcept { return self()->get(find_contiguous_end(i.position())); }
        const_pointer contiguous_end(const_iterator i) const noexcept { return self()->get(find_contiguous_end(i.position())); }

        // Given a pointer to an element in this queue, produce an iterator
        // that points to the same element.  Given a pointer that is one
        // position past the end of the allocated storage, such as one
        // previously returned from contiguous_end(), this returns an iterator
        // that points to the beginning of the next contiguous memory range.
        iterator       iterator_from_pointer(const_pointer p)       noexcept { return { self(), static_cast<size_type>(p - self()->get(0)) }; }
        const_iterator iterator_from_pointer(const_pointer p) const noexcept { return { self(), static_cast<size_type>(p - self()->get(0)) }; }

        // Check if the queue is empty.
        bool empty() const noexcept
        { return self()->load_head(Access) == self()->load_tail(Access); }

        // Check if the queue is full.
        bool full() const noexcept
        { return self()->load_head(Access) == self()->add(self()->load_tail(Access), 1); }

        // Return number of elements currently in the queue.
        size_type size() const noexcept
        { return self()->distance(self()->load_head(Access), self()->load_tail(Access)); }

        // Returns maximum number of elements that the queue can store.  This
        // is one less than the allocated space, since otherwise it is
        // impossible to distinguish between a "full" and "empty" state.
        size_type max_size() const noexcept { return self()->allocated_size() - 1; }

    protected:
        circular_queue_common_interface() noexcept = default;
        circular_queue_common_interface(circular_queue_common_interface&&) = delete;
        circular_queue_common_interface(const circular_queue_common_interface&) = delete;
        circular_queue_common_interface& operator=(circular_queue_common_interface&&) = delete;
        circular_queue_common_interface& operator=(const circular_queue_common_interface&) = delete;

    private:
        auto* self()       noexcept { return static_cast<      Queue*>(this); }
        auto* self() const noexcept { return static_cast<const Queue*>(this); }

        // Throw if index I is out of bounds.  Return absolute position of I.
        size_type check_pos(size_type i) const
        {
            const auto h = self()->load_head(Access);
            if (i >= self()->distance(h, self()->load_tail(Access))) throw std::out_of_range { "index past end" };
            return self()->add(h, i);
        }

        size_type find_contiguous_begin(size_type i) const noexcept
        {
            const auto h = self()->load_head(Access);
            return i > h ? h : 0;
        }

        size_type find_contiguous_end(size_type i) const noexcept
        {
            const auto t = self()->load_tail(Access);
            return i > t ? self()->allocated_size() : t;
        }
    };

    // Interface to circular_queue for use by the consumer thread.
    template<typename Queue, typename Storage>
    struct circular_queue_consumer : circular_queue_common_interface<Queue, Storage, detail::queue_access::consume>
    {
        using common = circular_queue_common_interface<Queue, Storage, detail::queue_access::consume>;

        using size_type = common::size_type;
        using const_iterator = common::const_iterator;

        // Remove the specified number of elements from the beginning.  Only
        // iterators to the removed elements are invalidated.  No bounds checks
        // are performed!
        void pop_front(size_type n = 1) noexcept
        {
            const auto h = self()->load_head(access::consume);
            self()->destroy_n(h, n);
            self()->store_head(self()->add(h, n));
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
            return pop_front_to(this->cend());
        }

    protected:
        circular_queue_consumer() noexcept = default;

    private:
        using access = detail::queue_access;

        auto* self()       noexcept { return static_cast<      Queue*>(this); }
        auto* self() const noexcept { return static_cast<const Queue*>(this); }
    };

    // Interface to circular_queue for use by the writer thread.
    template<typename Queue, typename Storage>
    struct circular_queue_producer : circular_queue_common_interface<Queue, Storage, detail::queue_access::produce>
    {
        using common = circular_queue_common_interface<Queue, Storage, detail::queue_access::produce>;

        using value_type = common::value_type;
        using size_type = common::size_type;
        using reference = common::reference;
        using iterator = common::iterator;

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(const value_type& value)
        {
            if (not try_push_back(value)) throw circular_queue_overflow { };
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        void push_back(value_type&& value)
        {
            if (not try_push_back(std::move(value))) throw circular_queue_overflow { };
        }

        // Add an element to the end.  No iterators are invalidated.  Throws
        // on overflow.
        template<typename... A>
        reference emplace_back(A&&... args)
        {
            const auto ref = try_emplace_back(std::forward<A>(args)...);
            if (not ref) throw circular_queue_overflow { };
            return *ref;
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(const value_type& value)
        {
            return do_append(1, [&](auto t) { self()->construct(t, value); return t; }).has_value();
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // false on overflow.
        bool try_push_back(value_type&& value)
        {
            return do_append(1, [&](auto t) { self()->construct(t, std::move(value)); return t; }).has_value();
        }

        // Add an element to the end.  No iterators are invalidated.  Returns
        // empty std::optional on overflow.
        template<typename... A>
        std::optional<std::reference_wrapper<value_type>> try_emplace_back(A&&... args)
            noexcept (noexcept(self()->construct(0u, std::forward<A>(args)...)))
        {
            return do_append(1, [&](auto t) { self()->construct(t, std::forward<A>(args)...); return std::ref(*self()->get(t)); });
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
            if (not i) throw circular_queue_overflow { };
            return *i;
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n, const value_type& value)
        {
            const auto i = try_append(n, value);
            if (not i) throw circular_queue_overflow { };
            return *i;
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Throws on overflow, and in that case, no elements are
        // added.
        iterator append(size_type n)
        {
            const auto i = try_append(n);
            if (not i) throw circular_queue_overflow { };
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
            noexcept (noexcept(self()->copy_n(0u, 0u, first)))
        {
            const auto n = last - first;
            return do_append(n, [&](auto t) { self()->copy_n(t, n, first); return iterator { self(), t }; });
        }

        // Add multiple copy-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n, const value_type& value)
            noexcept (noexcept(self()->fill_n(0u, n, value)))
        {
            return do_append(n, [&](auto t) { self()->fill_n(t, n, value); return iterator { self(), t }; });
        }

        // Add multiple default-constructed elements to the end and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.  Returns an empty std::optional on overflow, and in
        // that case, no elements are added.
        std::optional<iterator> try_append(size_type n)
            noexcept (noexcept(self()->default_construct_n(0u, n)))
        {
            return do_append(n, [&](auto t) { self()->default_construct_n(t, n); return iterator { self(), t }; });
        }

        // Fill the queue with copy-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill(const value_type& value)
            noexcept (noexcept(try_append(0u, value)))
        {
            return *try_append(this->max_size() - this->size(), value);
        }

        // Fill the queue with default-constructed elements and return an
        // iterator to the first inserted element.  Other iterators are not
        // invalidated.
        iterator fill()
            noexcept (noexcept(try_append(0u)))
        {
            return *try_append(this->max_size() - this->size());
        }

    protected:
        circular_queue_producer() noexcept = default;

    private:
        using access = detail::queue_access;

        auto* self()       noexcept { return static_cast<      Queue*>(this); }
        auto* self() const noexcept { return static_cast<const Queue*>(this); }

        // Invoke FUNC and bump tail pointer by N, if there is enough free
        // space.
        template<typename F>
        std::optional<std::invoke_result_t<F, size_type>> do_append(size_type n, F&& func)
            noexcept (noexcept(std::declval<F>()(std::declval<size_type>())))
        {
            const auto t = self()->load_tail(access::produce);
            if (self()->distance(self()->load_head(access::produce), t) + n > this->max_size()) [[unlikely]]
                return std::nullopt;

            finally store { [&] { self()->store_tail(self()->add(t, n)); } };
            return { std::forward<F>(func)(t) };
        }
    };

    // Efficient circular FIFO with configurable storage backend.
    template<typename Storage>
    struct circular_queue :
        Storage,
        private circular_queue_consumer<circular_queue<Storage>, Storage>,
        private circular_queue_producer<circular_queue<Storage>, Storage>
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

        using consumer_type = circular_queue_consumer<circular_queue<Storage>, Storage>;
        using producer_type = circular_queue_producer<circular_queue<Storage>, Storage>;

        static_assert(std::random_access_iterator<iterator>);

        using Storage::Storage;
        using Storage::operator=;

        ~circular_queue() noexcept { consumer()->clear(); }

        consumer_type* consumer() noexcept { return this; }
        const consumer_type* consumer() const noexcept { return this; }

        producer_type* producer() noexcept { return this; }
        const producer_type* producer() const noexcept { return this; }

    private:
        template<typename, bool> friend struct circular_queue_iterator;
        template<typename, typename, detail::queue_access> friend struct circular_queue_common_interface;
        friend consumer_type;
        friend producer_type;
    };

    // Circular queue using statically allocated storage.  May use
    // optimizations such as vectorized copy-construction.
    template<typename T, std::size_t N, queue_sync Sync = queue_sync::none>
    using static_circular_queue = circular_queue<circular_queue_static_storage<T, N, Sync>>;

    // Circular queue using dynamically allocated storage.  Elements are
    // constructed via the allocator.
    template<typename T, queue_sync Sync = queue_sync::none, typename Alloc = std::allocator<T>>
    using dynamic_circular_queue = circular_queue<circular_queue_dynamic_storage<T, Sync, Alloc>>;
}
