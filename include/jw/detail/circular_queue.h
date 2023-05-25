/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::detail
{
    enum class queue_access
    {
        any,
        read,
        write,
        unsynchronized
    };

    template<queue_sync Sync>
    struct circular_queue_storage_base
    {
        using size_type = std::size_t;

    protected:
        constexpr circular_queue_storage_base() noexcept = default;
        constexpr circular_queue_storage_base(circular_queue_storage_base&&) = delete;
        constexpr circular_queue_storage_base(const circular_queue_storage_base&) = delete;
        constexpr circular_queue_storage_base& operator=(circular_queue_storage_base&&) = delete;
        constexpr circular_queue_storage_base& operator=(const circular_queue_storage_base&) = delete;

        constexpr std::size_t load_head(queue_access access = queue_access::any) const noexcept
        {
            switch (access)
            {
            case queue_access::any:
                switch (Sync)
                {
                case queue_sync::none:
                    return head;

                case queue_sync::write_irq:
                case queue_sync::read_irq:
                case queue_sync::thread:
                    return head_atomic().load(std::memory_order_acquire);
                }
                __builtin_unreachable();

            case queue_access::unsynchronized:
            case queue_access::read:
                return head;

            case queue_access::write:
                switch (Sync)
                {
                case queue_sync::none:
                case queue_sync::write_irq:
                    return head;

                case queue_sync::read_irq:
                case queue_sync::thread:
                    return head_atomic().load(std::memory_order_acquire);
                }
            }
            __builtin_unreachable();
        }

        constexpr std::size_t load_tail(queue_access access = queue_access::any) const noexcept
        {
            switch (access)
            {
            case queue_access::any:
                switch (Sync)
                {
                case queue_sync::none:
                    return tail;

                case queue_sync::read_irq:
                case queue_sync::write_irq:
                case queue_sync::thread:
                    return tail_atomic().load(std::memory_order_acquire);
                }
                __builtin_unreachable();

            case queue_access::read:
                switch (Sync)
                {
                case queue_sync::none:
                case queue_sync::read_irq:
                    return tail;

                case queue_sync::write_irq:
                case queue_sync::thread:
                    return tail_atomic().load(std::memory_order_acquire);
                }
                __builtin_unreachable();

            case queue_access::unsynchronized:
            case queue_access::write:
                return tail;
            }
            __builtin_unreachable();
        }

        constexpr void store_head(std::size_t h, queue_access access = queue_access::read) noexcept
        {
            switch (access)
            {
            case queue_access::read:
                switch (Sync)
                {
                case queue_sync::none:
                    head = h;
                    break;

                case queue_sync::read_irq:
                    volatile_store(&head, h);
                    break;

                case queue_sync::write_irq:
                case queue_sync::thread:
                    head_atomic().store(h, std::memory_order_release);
                }
                break;

            case queue_access::unsynchronized:
                head = h;
                break;

            default:
                __builtin_unreachable();
            }
            assume(head == h);
        }

        constexpr void store_tail(std::size_t t, queue_access access = queue_access::write) noexcept
        {
            switch (access)
            {
            case queue_access::write:
                switch (Sync)
                {
                case queue_sync::none:
                    tail = t;
                    break;

                case queue_sync::write_irq:
                    volatile_store(&tail, t);
                    break;

                case queue_sync::read_irq:
                case queue_sync::thread:
                    tail_atomic().store(t, std::memory_order_release);
                }
                break;

            case queue_access::unsynchronized:
                tail = t;
                break;

            default:
                __builtin_unreachable();
            }
            assume(tail == t);
        }

    private:
        auto head_atomic() noexcept { return std::atomic_ref<size_type> { head }; }
        auto tail_atomic() noexcept { return std::atomic_ref<size_type> { tail }; }
        auto head_atomic() const noexcept { return std::atomic_ref<const size_type> { head }; }
        auto tail_atomic() const noexcept { return std::atomic_ref<const size_type> { tail }; }

        alignas(std::atomic_ref<size_type>::required_alignment) size_type head { 0 };
        alignas(std::atomic_ref<size_type>::required_alignment) size_type tail { 0 };
    };

    template<typename Storage>
    class circular_queue_utilities
    {
        struct dummy : Storage
        {
            static constexpr bool can_default_construct = requires (dummy a) { a.do_default_construct(std::declval<std::size_t>(), std::declval<std::size_t>()); };
            template<typename T> static constexpr bool can_copy = requires (dummy a) { a.do_copy(std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<T>()); };
            template<typename T> static constexpr bool can_fill = requires (dummy a) { a.do_fill(std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<T>()); };
        };

        auto* self()       noexcept { return static_cast<Storage*>(this); }
        auto* self() const noexcept { return static_cast<const Storage*>(this); }

    protected:
        // Find relative position (distance) of I from head position H.
        std::size_t distance(std::size_t h, std::size_t i) const noexcept
        {
            const std::ptrdiff_t n = i - h;
            if (n >= 0) return n;
            else return self()->allocated_size() + n;
        }

        // Find absolute position of index I from head position H.
        std::size_t add(std::size_t h, std::ptrdiff_t i) const noexcept
        {
            assume(self()->wrap(h) == h);
            return self()->wrap(h + i);
        }

        // Construct one element at position P.
        template<typename... A>
        void construct(std::size_t p, A&&... args)
            noexcept (noexcept(self()->do_construct(p, std::forward<A>(args)...)))
        {
            return self()->do_construct(p, std::forward<A>(args)...);
        }

        // Default-construct N elements at absolute position P.
        void default_construct_n(std::size_t p, std::size_t n)
            noexcept (noexcept(self()->do_default_construct(p, n)))
            requires (dummy::can_default_construct)
        {
            return self()->do_default_construct(p, n);
        }

        void default_construct_n(std::size_t p, std::size_t n) noexcept
            requires (not dummy::can_default_construct and noexcept(construct(0u)))
        {
            for (unsigned i = 0; i < n; ++i)
                construct(add(p, i));
        }

        void default_construct_n(std::size_t p, std::size_t n)
            requires (not dummy::can_default_construct and not noexcept(construct(0u)))
        {
            for (unsigned i = 0; i < n; ++i)
            {
                try { construct(add(p, i)); }
                catch (...)
                {
                    destroy_n(p, i);
                    throw;
                }
            }
        }

        // Copy N elements from IT starting at position P.
        template<std::forward_iterator I>
        void copy_n(std::size_t p, std::size_t n, I it)
            noexcept (noexcept(self()->do_copy(p, n, it)))
            requires (dummy::template can_copy<I>)
        {
            return self()->do_copy(p, n, it);
        }

        template<std::forward_iterator I>
        void copy_n(std::size_t p, std::size_t n, I it) noexcept
            requires (not dummy::template can_copy<I> and noexcept(construct(0u, *it)))
        {
            for (unsigned i = 0; i < n; ++i, ++it)
                construct(add(p, i), *it);
        }

        template<std::forward_iterator I>
        void copy_n(std::size_t p, std::size_t n, I it)
            requires (not dummy::template can_copy<I> and not noexcept(construct(0u, *it)))
        {
            for (unsigned i = 0; i < n; ++i, ++it)
            {
                try { construct(add(p, i), *it); }
                catch (...)
                {
                    destroy_n(p, i);
                    throw;
                }
            }
        }

        // Fill N elements with VALUE starting at position P.
        template<typename T>
        void fill_n(std::size_t p, std::size_t n, const T& value)
            noexcept (noexcept(self()->do_fill(p, n, value)))
            requires (dummy::template can_fill<const T&>)
        {
            return self()->do_fill(p, n, value);
        }

        template<typename T>
        void fill_n(std::size_t p, std::size_t n, const T& value) noexcept
            requires (not dummy::template can_fill<const T&> and noexcept(construct(0u, value)))
        {
            for (unsigned i = 0; i < n; ++i)
                construct(add(p, i), value);
        }

        template<typename T>
        void fill_n(std::size_t p, std::size_t n, const T& value)
            requires (not dummy::template can_fill<const T&> and not noexcept(construct(0u, value)))
        {
            for (unsigned i = 0; i < n; ++i)
            {
                try { construct(add(p, i), value); }
                catch (...)
                {
                    destroy_n(p, i);
                    throw;
                }
            }
        }

        // Destroy N elements starting at position P.
        void destroy_n(std::size_t p, std::size_t n) noexcept
        {
            return self()->do_destroy(p, n);
        }
    };

    template<typename T, std::size_t N, queue_sync Sync>
    struct circular_queue_static_storage_base :
        circular_queue_storage_base<Sync>,
        circular_queue_utilities<circular_queue_static_storage_base<T, N, Sync>>
    {
    protected:
        using utils = circular_queue_utilities<circular_queue_static_storage_base<T, N, Sync>>;
        friend utils;
        circular_queue_static_storage_base() noexcept = default;

        std::size_t allocated_size() const noexcept { return N; }
        std::size_t wrap(std::size_t i) const noexcept { return i & (N - 1); }

        template<typename... A>
        void do_construct(std::size_t p, A&&... args) noexcept (std::is_nothrow_constructible_v<T, A...>)
        {
            std::construct_at(get(p), std::forward<A>(args)...);
        }

        void do_default_construct(std::size_t p, std::size_t n) noexcept
            requires (std::is_nothrow_default_constructible_v<T>)
        {
            const auto max_n = std::min(n, N - p);
            std::uninitialized_default_construct_n(std::execution::unseq, get(p), max_n);
            if (max_n < n) std::uninitialized_default_construct_n(std::execution::unseq, get(0), n - max_n);
        }

        template<std::forward_iterator I>
        void do_copy(std::size_t p, std::size_t n, I it) noexcept
            requires (requires (I it, std::iter_difference_t<I> n) { { *(it + n) } -> std::same_as<std::iter_reference_t<I>>; }
                      and std::is_nothrow_constructible_v<T, std::iter_reference_t<I>>)
        {
            const auto max_n = std::min(n, N - p);
            std::uninitialized_copy_n(std::execution::unseq, it, max_n, get(p));
            if (max_n < n) std::uninitialized_copy_n(std::execution::unseq, it + max_n, n - max_n, get(0));
        }

        void do_fill(std::size_t p, std::size_t n, const T& value) noexcept
            requires (std::is_nothrow_copy_constructible_v<T>)
        {
            const auto max_n = std::min(n, N - p);
            std::uninitialized_fill_n(std::execution::unseq, get(p), max_n, value);
            if (max_n < n) std::uninitialized_fill_n(std::execution::unseq, get(0), n - max_n, value);
        }

        void do_destroy(std::size_t p, std::size_t n) noexcept
        {
            const auto max_n = std::min(n, N - p);
            std::destroy_n(std::execution::unseq, get(p), max_n);
            if (max_n < n) std::destroy_n(std::execution::unseq, get(0), n - max_n);
        }

        T* get(std::size_t i) noexcept { return std::launder(reinterpret_cast<T*>(&storage[i])); }
        const T* get(std::size_t i) const noexcept { return std::launder(reinterpret_cast<const T*>(&storage[i])); }

    private:
        std::array<std::aligned_storage_t<sizeof(T), alignof(T)>, N> storage;
    };

    template<typename T, queue_sync Sync, typename Alloc>
    struct circular_queue_dynamic_storage_base :
        circular_queue_storage_base<Sync>,
        circular_queue_utilities<circular_queue_dynamic_storage_base<T, Sync, Alloc>>
    {
    protected:
        using utils = circular_queue_utilities<circular_queue_dynamic_storage_base<T, Sync, Alloc>>;
        friend utils;

        using allocator_traits = std::allocator_traits<Alloc>;
        using allocator_type = allocator_traits::allocator_type;
        using value_type = allocator_traits::value_type;
        using pointer = allocator_traits::pointer;
        using const_pointer = allocator_traits::const_pointer;
        using size_type = allocator_traits::size_type;
        using difference_type = allocator_traits::difference_type;

        circular_queue_dynamic_storage_base() = delete;

        circular_queue_dynamic_storage_base(size_type size, const allocator_type& a)
            : alloc { a }
            , mask { std::bit_ceil(size) - 1 }
            , ptr { allocator_traits::allocate(alloc, allocated_size()) }
        { }

        template<queue_sync S2>
        circular_queue_dynamic_storage_base(circular_queue_dynamic_storage_base<T, S2, Alloc>&& other)
            : alloc { std::move(other.alloc) }
            , mask { other.mask }
            , ptr { other.ptr }
        {
            constexpr auto unsync = queue_access::unsynchronized;
            other.mask = -1;
            other.ptr = nullptr;
            this->store_head(other.load_head(unsync), unsync);
            this->store_tail(other.load_tail(unsync), unsync);
            other.store_head(0, unsync);
            other.store_tail(0, unsync);
        }

        template<queue_sync S2>
        circular_queue_dynamic_storage_base& operator=(circular_queue_dynamic_storage_base<T, S2, Alloc>&& other)
            requires (allocator_traits::propagate_on_container_move_assignment::value)
        {
            constexpr auto unsync = queue_access::unsynchronized;
            if (ptr)
            {
                const auto h = this->load_head(unsync);
                const auto t = this->load_tail(unsync);
                this->destroy_n(h, this->distance(h, t));
            }
            this->~circular_queue_dynamic_storage_base();
            return *new (this) circular_queue_dynamic_storage_base { std::move(other) };
        }

        ~circular_queue_dynamic_storage_base()
        {
            if (ptr) allocator_traits::deallocate(alloc, ptr, allocated_size());
        }

        void resize(size_type size)
        {
            size = std::bit_ceil(size);
            if (size <= allocated_size()) return;
            constexpr auto unsync = queue_access::unsynchronized;
            const auto h = this->load_head(unsync);
            const auto t = this->load_tail(unsync);
            const auto n = this->distance(h, t);
            const auto old_size = allocated_size();
            const pointer p = allocator_traits::allocate(alloc, n);
            try
            {
                for (unsigned i = 0; i < n; ++i)
                    allocator_traits::construct(alloc, p + h + i, std::move(*get(this->add(h, i))));
                for (unsigned i = 0; i < n; ++i)
                    allocator_traits::destroy(alloc, get(this->add(h, i)));
                mask = size - 1;
                ptr = p;
                this->store_tail(h + n, unsync);
            }
            catch (...)
            {
                allocator_traits::deallocate(alloc, p, size);
                throw;
            }
            allocator_traits::deallocate(alloc, ptr, old_size);
        }

        size_type allocated_size() const noexcept { return mask + 1; }
        size_type wrap(size_type i) const noexcept { return i & mask; }

        template<typename... A>
        void do_construct(size_type p, A&&... args)
            noexcept (noexcept(allocator_traits::construct(alloc, get(p), std::forward<A>(args)...)))
        {
            allocator_traits::construct(alloc, get(p), std::forward<A>(args)...);
        }

        void do_destroy(size_type p, size_type n) noexcept
        {
            for (unsigned i = 0; i < n; ++i)
                allocator_traits::destroy(alloc, get(p));
        }

        pointer get(size_type i) noexcept { return ptr + i; }
        const_pointer get(size_type i) const noexcept { return ptr + i; }

    private:
        [[no_unique_address]] allocator_type alloc;
        size_type mask;
        pointer ptr;
    };
}
