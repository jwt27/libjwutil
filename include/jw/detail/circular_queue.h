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
    struct circular_queue_base
    {
        using size_type = std::size_t;

    protected:
        constexpr circular_queue_base() noexcept = default;
        constexpr circular_queue_base(circular_queue_base&&) noexcept = default;
        constexpr circular_queue_base(const circular_queue_base&) noexcept = default;
        constexpr circular_queue_base& operator=(circular_queue_base&&) noexcept = default;
        constexpr circular_queue_base& operator=(const circular_queue_base&) noexcept = default;

        constexpr size_type load_head(queue_access access = queue_access::any) const noexcept
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

        constexpr size_type load_tail(queue_access access = queue_access::any) const noexcept
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

        constexpr void store_head(size_type h) noexcept
        {
            switch (Sync)
            {
            case queue_sync::none:
                head = h;
                return;

            case queue_sync::read_irq:
                return volatile_store(&head, h);

            case queue_sync::write_irq:
            case queue_sync::thread:
                return head_atomic().store(h, std::memory_order_release);
            }
        }

        constexpr void store_tail(size_type t) noexcept
        {
            switch (Sync)
            {
            case queue_sync::none:
                tail = t;
                return;

            case queue_sync::write_irq:
                return volatile_store(&tail, t);

            case queue_sync::read_irq:
            case queue_sync::thread:
                return tail_atomic().store(t, std::memory_order_release);
            }
        }

    private:
        auto head_atomic() noexcept { return std::atomic_ref<size_type> { head }; }
        auto tail_atomic() noexcept { return std::atomic_ref<size_type> { tail }; }
        auto head_atomic() const noexcept { return std::atomic_ref<const size_type> { head }; }
        auto tail_atomic() const noexcept { return std::atomic_ref<const size_type> { tail }; }

        alignas(std::atomic_ref<size_type>::required_alignment) size_type head { 0 };
        alignas(std::atomic_ref<size_type>::required_alignment) size_type tail { 0 };
    };
}
