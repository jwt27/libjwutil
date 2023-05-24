/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2023 J.W. Jagersma, see COPYING.txt for details */

#pragma once

namespace jw::detail
{
    enum class queue_access
    {
        any,
        read,
        write
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

            case queue_access::write:
                return tail;
            }
            __builtin_unreachable();
        }

        constexpr void store_head(std::size_t h) noexcept
        {
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
            assume(head == h);
        }

        constexpr void store_tail(std::size_t t) noexcept
        {
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
            static constexpr bool has_default_construct = requires (dummy a) { a.do_default_construct(std::declval<std::size_t>(), std::declval<std::size_t>()); };
            template<typename T> static constexpr bool has_copy = requires (dummy a) { a.do_copy(std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<T>()); };
            template<typename T> static constexpr bool has_fill = requires (dummy a) { a.do_fill(std::declval<std::size_t>(), std::declval<std::size_t>(), std::declval<T>()); };
        };

        auto* self()       noexcept { return static_cast<Storage*>(this); }
        auto* self() const noexcept { return static_cast<const Storage*>(this); }

    protected:
        static void overflow() { throw std::length_error { "circular_queue overflow" }; }

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
            requires (noexcept(construct(0u)))
        {
            for (unsigned i = 0; i < n; ++i)
                construct(add(p, i));
        }

        void default_construct_n(std::size_t p, std::size_t n)
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
            requires (noexcept(construct(0u, *it)))
        {
            for (unsigned i = 0; i < n; ++i, ++it)
                construct(add(p, i), *it);
        }

        template<std::forward_iterator I>
        void copy_n(std::size_t p, std::size_t n, I it)
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
            requires (noexcept(construct(0u, value)))
        {
            for (unsigned i = 0; i < n; ++i)
                construct(add(p, i), value);
        }

        template<typename T>
        void fill_n(std::size_t p, std::size_t n, const T& value)
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
            requires (std::is_nothrow_constructible_v<T, std::iter_reference_t<I>>)
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
}
