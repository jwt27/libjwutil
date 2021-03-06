/* * * * * * * * * * * * * * libjwutil * * * * * * * * * * * * * */
/* Copyright (C) 2022 J.W. Jagersma, see COPYING.txt for details */
/* Copyright (C) 2021 J.W. Jagersma, see COPYING.txt for details */

#pragma once
#include <memory>
#include <memory_resource>
#include <algorithm>
#include <span>
#include <array>
#include <jw/common.h>

namespace jw
{
    template<typename Alloc>
    struct allocator_delete : public Alloc
    {
        template <typename... A>
        constexpr allocator_delete(A&&... args) : Alloc { std::forward<A>(args)... } { }
        using Alloc::operator=;
        using traits = std::allocator_traits<Alloc>;
        void operator()(auto* p)
        {
            if (p == nullptr) return;
            traits::destroy(*this, p);
            traits::deallocate(*this, p, 1);
        }
    };

    // Allocate a std::unique_ptr with a custom allocator
    template<typename T, typename Alloc, typename... Args>
    auto allocate_unique(const Alloc& alloc, Args&&... args)
    {
        using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using traits = std::allocator_traits<rebind>;
        using deleter = allocator_delete<rebind>;

        deleter d { rebind { alloc } };
        auto* p = traits::allocate(d, 1);
        try { traits::construct(d, p, std::forward<Args>(args)...); }
        catch (...) { traits::deallocate(d, p, 1); throw; }

        return std::unique_ptr<T, deleter> { p, std::move(d) };
    }

    // Initialize a non-owning std::unique_ptr with allocator_delete (saves typing)
    template<typename T, typename Alloc>
    auto init_unique(const Alloc& alloc)
    {
        using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
        using deleter = allocator_delete<rebind>;

        return std::unique_ptr<T, deleter> { nullptr, deleter { rebind { alloc } } };
    }

    // Behaves much like std::pmr::polymorphic_allocator, except that the
    // specific type of memory_resource can be narrowed down to eliminate
    // virtual function calls.
    template<typename Resource = std::pmr::memory_resource, typename T = std::byte>
    requires (std::is_base_of_v<std::pmr::memory_resource, Resource>)
    struct monomorphic_allocator
    {
        using value_type = T;
        using resource_type = Resource;

        using propagate_on_container_copy_assignment = std::false_type;
        using propagate_on_container_move_assignment = std::false_type;
        using propagate_on_container_swap = std::false_type;
        template <typename U> struct rebind { using other = monomorphic_allocator<Resource, U>; };

        constexpr monomorphic_allocator() = delete;
        constexpr monomorphic_allocator(const monomorphic_allocator&) noexcept = default;
        constexpr monomorphic_allocator(Resource* res) noexcept : r { res } { };

        template<typename R2, typename U>
        requires (std::is_base_of_v<Resource, R2>)
        constexpr monomorphic_allocator(const monomorphic_allocator<R2, U>& other) noexcept : r { other.resource() } { }

        template<typename U>
        requires (std::is_same_v<std::pmr::memory_resource, Resource>)
        constexpr monomorphic_allocator(const std::pmr::polymorphic_allocator<U>& other) noexcept : r { other.resource() } { }

        template<typename U>
        operator std::pmr::polymorphic_allocator<U>() { return std::pmr::polymorphic_allocator<U> { r }; }

        monomorphic_allocator& operator=(const monomorphic_allocator&) = delete;

        [[nodiscard]] T* allocate(std::size_t n)
        {
            return static_cast<T*>(allocate_bytes(n * sizeof(T), alignof(T)));
        }

        void deallocate(T* p, std::size_t n)
        {
            deallocate_bytes(p, n * sizeof(T), alignof(T));
        }

        [[nodiscard]] void* allocate_bytes(std::size_t n, std::size_t a = alignof(std::max_align_t))
        {
            return r->allocate(n, a);
        }

        void deallocate_bytes(void* p, std::size_t n, std::size_t a = alignof(std::max_align_t))
        {
            r->deallocate(p, n, a);
        }

        template <typename U>
        [[nodiscard]] U* allocate_object(std::size_t n = 1)
        {
            return static_cast<U*>(allocate_bytes(n * sizeof(U), alignof(U)));
        }

        template <typename U>
        void deallocate_object(U* p, std::size_t n = 1)
        {
            deallocate_bytes(p, n * sizeof(U), alignof(U));
        }

        template <typename U, typename... A>
        void construct(U* p, A&&... args)
        {
            std::uninitialized_construct_using_allocator(p, *this, std::forward<A>(args)...);
        }

        template<typename U>
        void destroy(U* p)
        {
            std::destroy_at(p);
        }

        template <typename U, typename... A>
        [[nodiscard]] U* new_object(A&&... args)
        {
            auto* const p = allocate_object<U>();
            try { construct<U>(p, std::forward<A>(args)...); }
            catch (...) { deallocate_object(p); throw; }
            return p;
        }

        template <typename U>
        void delete_object(U* p)
        {
            destroy(p);
            deallocate_object(p);
        }

        template <typename R2, typename U>
        bool operator==(const monomorphic_allocator<R2, U>& b) const noexcept
        {
            return resource() == b.resource() or resource()->is_equal(*b.resource());
        }

        template <typename U>
        bool operator==(const std::pmr::polymorphic_allocator<U>& b) const noexcept
        {
            return resource() == b.resource() or resource()->is_equal(*b.resource());
        }

        constexpr Resource* resource() const noexcept { return r; }

    private:
        Resource* r;
    };

    // A std::pmr::memory_resource which allocates from one or multiple pools.  It is implemented
    // as a binary tree which is horizontally ordered by address, and vertically sorted by size.
    // The pool size can be increased dynamically by feeding pointers to grow().  Note that this
    // memory resource does not own (and thus free) the memory it allocates from.
    struct basic_pool_resource : public std::pmr::memory_resource
    {
        constexpr basic_pool_resource() noexcept = default;
        virtual ~basic_pool_resource() noexcept = default;

        basic_pool_resource(const std::span<std::byte>& ptr) { grow(ptr); }

        constexpr basic_pool_resource(basic_pool_resource&& o) noexcept
            : num_allocs { std::move(o.num_allocs) }, root { std::move(o.root) } { o.reset(); }

        constexpr basic_pool_resource& operator=(basic_pool_resource&& o) noexcept
        {
            using std::swap;
            swap(num_allocs, o.num_allocs);
            swap(root, o.root);
            return *this;
        }

        basic_pool_resource(const basic_pool_resource&) = delete;
        basic_pool_resource& operator=(const basic_pool_resource&) = delete;

        constexpr bool empty() const noexcept { return num_allocs == 0; }

        void grow(const std::span<std::byte>& ptr) noexcept { do_grow(ptr); }

        // Returns the size of the largest chunk.
        constexpr std::size_t max_chunk_size() const noexcept { return pool_node::size_or_zero(root); }

        // Returns maximum number of bytes that can be allocated at once, with the given alignment.
        constexpr std::size_t max_size(std::size_t alignment = alignof(std::max_align_t)) const noexcept
        {
            auto size = max_chunk_size();
            const auto overhead = alignment + sizeof(std::size_t) + sizeof(std::uint8_t);
            if (size < overhead) return 0;
            size -= overhead;
            if (size < sizeof(pool_node) + alignof(pool_node)) return 0;
            return size;
        }

        // Return size allocated to the given pointer.
        constexpr std::size_t size(void* p) const noexcept
        {
            auto offset = *(static_cast<std::uint8_t*>(p) - 1);
            auto size = *reinterpret_cast<std::size_t*>((static_cast<std::byte*>(p) - offset));
            return size - offset;
        }

    protected:
        struct pool_node
        {
            std::size_t size;
            std::array<pool_node*, 2> next { nullptr, nullptr };
            bool alloc_hi { false };

            constexpr auto* begin() noexcept { return static_cast<std::byte*>(static_cast<void*>(this)); }
            constexpr auto* end() noexcept { return begin() + size; }

            // Combine two sorted, non-overlapping trees into one.
            [[nodiscard, gnu::regparm(2), gnu::hot, gnu::nonnull]]
            constexpr pool_node* combine(pool_node* node) noexcept
            {
                auto* dst = this;
                if (node->size > dst->size) std::swap(dst, node);

                const auto higher = node > dst;

                if (dst->next[higher] != nullptr) node = dst->next[higher]->combine(node);

                dst->next[higher] = node;
                return dst;
            }

            // Insert one new node into the tree, merging it with adjacent nodes where possible.
            [[nodiscard, gnu::regparm(2), gnu::hot, gnu::nonnull]]
            constexpr pool_node* insert(pool_node* node) noexcept
            {
                auto higher = node > this;
                auto lower = not higher;

                auto lo = node, hi = this;
                if (higher) std::swap(lo, hi);
                if (lo->end() == hi->begin())
                {
                    lo->size += hi->size;
                    auto tmp = next;
                    next.fill(nullptr);
                    node = lo;
                    std::destroy_at(hi);
                    if (tmp[higher] != nullptr) node = tmp[higher]->insert(node);
                    if (tmp[lower] != nullptr) node = node->combine(tmp[lower]);
                    return node;
                }

                if (next[higher] != nullptr) node = next[higher]->insert(node);

                if (node->size > size)
                {
                    next[higher] = node->next[lower];
                    node->next[lower] = this;
                    return node;
                }

                next[higher] = node;
                return this;
            }

            static constexpr std::size_t size_or_zero(const pool_node* node)
            {
                return node != nullptr ? node->size : 0;
            }

            constexpr auto minmax() noexcept
            {
                auto min = next[0], max = next[1];
                if (size_or_zero(min) > size_or_zero(max)) std::swap(min, max);
                return std::make_tuple(min, max);
            }

            [[nodiscard]] constexpr pool_node* erase() noexcept
            {
                pool_node* node;
                if (next[0] == nullptr) node = next[1];
                else if (next[1] == nullptr) node = next[0];
                else node = next[0]->combine(next[1]);
                next.fill(nullptr);
                return node;
            }

            [[nodiscard, gnu::nonnull]] constexpr pool_node* replace(pool_node* node) noexcept
            {
                auto max = std::max(size_or_zero(next[0]), size_or_zero(next[1]));
                if (node->size > max) [[likely]]
                {
                    node->next = next;
                    return node;
                }
                else return erase()->combine(node);
            }

            [[nodiscard]] constexpr pool_node* resize(std::size_t s) noexcept
            {
                size = s;
                auto [min, max] = minmax();
                if (max != nullptr and max->size > size) [[unlikely]]
                {
                    auto* node = max;
                    if(min != nullptr) node = max->combine(min);
                    next.fill(nullptr);
                    return node->combine(this);
                }
                else return this;
            }
        };

        constexpr void reset() noexcept
        {
            num_allocs = 0;
            root = nullptr;
        }

        template<typename Lock = jw::empty, typename... Args>
        [[gnu::noinline]] void grow_impl(const std::span<std::byte>& ptr, Args&&... lock_args) noexcept
        {
            auto* n = new(ptr.data()) pool_node { ptr.size_bytes() };
            if (root != nullptr) [[likely]]
            {
                [[maybe_unused]] Lock lock { std::forward<Args>(lock_args)... };
                root = root->insert(n);
            }
            else root = n;
        }

        template<typename Lock = jw::empty, typename... Args>
        [[nodiscard, gnu::noinline]] void* allocate_impl(std::size_t n, std::size_t a, Args&&... lock_args)
        {
            auto aligned_ptr = [](void* p, std::size_t align, bool down = false) noexcept
            {
                auto a = reinterpret_cast<std::uintptr_t>(p);
                auto b = a & -align;
                if (not down and b != a) b += align;
                return reinterpret_cast<std::byte*>(b);
            };

            a = std::max(a, alignof(void*));
            n += a + sizeof(std::size_t) + sizeof(std::uint8_t);
            n = std::max(n, sizeof(pool_node) + alignof(pool_node));

            std::size_t p_size;
            std::byte* p;
            {
                [[maybe_unused]] Lock lock { std::forward<Args>(lock_args)... };
            retry:
                if (root == nullptr) [[unlikely]]
                {
                    auto_grow(n);
                    goto retry;
                }
                p_size = root->size;
                p = root->begin();

                if (p_size > n + sizeof(pool_node) + alignof(pool_node))    // Split chunk
                {
                    // Alternate between allocating from the low and high end
                    // of each chunk, to keep the tree balanced.
                    auto split_at = root->alloc_hi ? root->end() - n : root->begin() + n;
                    auto* q = aligned_ptr(split_at, alignof(pool_node), root->alloc_hi);
                    std::size_t q_size = p_size - (q - p);
                    p_size -= q_size;
                    if (root->alloc_hi)
                    {
                        std::swap(p, q);
                        std::swap(p_size, q_size);
                        root = root->resize(q_size);
                    }
                    else root = root->replace(new(q) pool_node { static_cast<std::uintptr_t>(q_size) });
                    root->alloc_hi ^= true;
                }
                else if (p_size >= n) root = root->erase();     // Use entire chunk
                else [[unlikely]]
                {
                    auto_grow(n);
                    goto retry;
                }
                ++num_allocs;
            }
            *reinterpret_cast<std::size_t*>(p) = p_size;
            auto* p_aligned = aligned_ptr(p + sizeof(std::size_t) + sizeof(std::uint8_t), a);
            *reinterpret_cast<std::uint8_t*>(p_aligned - 1) = p_aligned - p;
            return p_aligned;
        }

        [[nodiscard]] virtual void* do_allocate(std::size_t n, std::size_t a) override
        {
            return allocate_impl(n, a);
        }

        virtual void do_deallocate(void* ptr, std::size_t, std::size_t) noexcept override
        {
            auto* p = static_cast<std::uint8_t*>(ptr);
            p -= *(p - 1);
            grow({ reinterpret_cast<std::byte*>(p), *reinterpret_cast<std::size_t*>(p) });
            --num_allocs;
        }

        virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
        {
            return dynamic_cast<const basic_pool_resource*>(&other) == this;
        }

        virtual void do_grow(const std::span<std::byte>& ptr) noexcept { grow_impl(ptr); }

        virtual void auto_grow(std::size_t) { throw std::bad_alloc { }; }

        std::size_t num_allocs { 0 };
        pool_node* root { nullptr };
    };

    // A specialization of basic_pool_resource, this pool type manages memory
    // allocated from an upstream memory_resource.  It grows automatically
    // when exhausted.
    struct pool_resource : public basic_pool_resource
    {
        using base = basic_pool_resource;

        constexpr pool_resource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource()) noexcept
            : res { upstream } { };

        pool_resource(std::size_t size_bytes, std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
            : pool_resource { upstream } { grow(size_bytes); }

        virtual ~pool_resource() noexcept { release(); }

        constexpr pool_resource(pool_resource&& o) noexcept
            : base { std::move(o) }, pools { std::move(o.pools) } { o.reset(); }

        constexpr pool_resource& operator=(pool_resource&& o) noexcept
        {
            base::operator=(std::move(o));
            using std::swap;
            swap(res, o.res);
            swap(pools, o.pools);
            return *this;
        }

        pool_resource(const pool_resource&) = delete;
        pool_resource& operator=(const pool_resource&) = delete;

        using base::size;
        constexpr std::size_t size() const noexcept
        {
            std::size_t size = 0;
            for (auto&& i : pools)
                size += i.size_bytes();
            return size;
        }

        // Deallocate the memory pool
        void release() noexcept
        {
            if (pools.data() != nullptr)
            {
                for (auto&& i : pools)
                    res->deallocate(i.data(), i.size_bytes(), alignof(pool_node));
                res->deallocate(pools.data(), pools.size_bytes(), alignof(pool_type));
            }
            reset();
        }

        void grow(std::size_t bytes) { do_grow(bytes); }

        bool in_pool(const void* ptr) const noexcept
        {
            if (pools.data() == nullptr) return false;
            auto p = static_cast<const std::byte*>(ptr);
            for (auto&& i : pools)
                if (p < (&*i.end()) and p >= &*i.begin()) return true;
            return false;
        }

    protected:
        using base::grow;

        constexpr void reset() noexcept
        {
            base::reset();
            pools = { };
        }

        template<typename Lock = jw::empty, typename... Args>
        void grow_alloc(std::size_t bytes, Args&&... lock_args)
        {
            bytes = std::max(bytes, sizeof(pool_node));
            auto* p = res->allocate(bytes, alignof(pool_node));
            pool_type* new_pools;
            try
            {
                new_pools = static_cast<pool_type*>(res->allocate(sizeof(pool_type) * (pools.size() + 1), alignof(pool_type)));
            }
            catch (...)
            {
                res->deallocate(p, bytes, alignof(pool_node));
                throw;
            }
            [[maybe_unused]] Lock lock { std::forward<Args>(lock_args)... };
            if (pools.data() != nullptr)
            {
                std::uninitialized_move(pools.begin(), pools.end(), new_pools);
                std::destroy(pools.begin(), pools.end());
                res->deallocate(pools.data(), pools.size_bytes(), alignof(pool_type));
            }
            pools = { new_pools, pools.size() + 1 };
            grow_impl(pools.back() = pool_type { static_cast<std::byte*>(p), bytes });
        }

        virtual void do_grow(std::size_t needed)
        {
            grow_alloc(needed);
        }

        virtual void auto_grow(std::size_t needed) override
        {
            grow(std::max(needed * 2, size() / 2));
        }

        using pool_type = std::span<std::byte>;

        std::pmr::memory_resource* res { };
        std::span<pool_type> pools { };
    };
}
