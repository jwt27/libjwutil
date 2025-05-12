/* * * * * * * * * * * * * * * * * * jwutil * * * * * * * * * * * * * * * * * */
/*    Copyright (C) 2022 - 2025 J.W. Jagersma, see COPYING.txt for details    */

#pragma once
#include <cstddef>
#include <cassert>
#include <memory>
#include <memory_resource>
#include <bit>
#include <execution>
#include <algorithm>
#include <vector>
#include <jw/common.h>
#include <jw/index.h>

namespace jw
{
    // This is a std::vector alternative with "short-string optimization":
    // small arrays are stored inside the class itself.
    // Yet... sizeof(sso_vector<T>) is equal to sizeof(std::vector<T>)!
    // This works by always allocating space for an even number of elements,
    // so that the first bit of the total capacity is always clear.  This bit
    // can then be used to signal whether the short-string optimization is in
    // effect.
    // Template parameter min_sso_size can be specified to set a minimum space
    // requirement.  If this is non-zero, SSO is always applied, but
    // sizeof(sso_vector) may increase.
    // For min_sso_size == 0, the available space is 11 bytes on i386, 23
    // bytes on amd64.  SSO is disabled if T can not fit in this space.
    template<typename T, std::size_t min_sso_size = 0, typename Alloc = std::allocator<T>>
    struct sso_vector
    {
        using value_type = T;
        using allocator_type = typename std::allocator_traits<Alloc>::rebind_alloc<T>;
        using allocator_traits = std::allocator_traits<allocator_type>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = allocator_traits::pointer;
        using const_pointer = allocator_traits::const_pointer;
        using iterator = T*;
        using const_iterator = const T*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr sso_vector() noexcept(noexcept(Alloc { })) : alloc { }
        {
            init_allocate(0);
        }

        constexpr explicit sso_vector(const Alloc& a) noexcept : alloc { a }
        {
            init_allocate(0);
        }

        constexpr sso_vector(size_type n, const T& value, const Alloc& a = Alloc { }) : alloc { a }
        {
            init_allocate(n);
            uninitialized_fill_n(begin(), n, value);
        }

        constexpr explicit sso_vector(size_type n, const Alloc& a = Alloc { }) : alloc { a }
        {
            init_allocate(n);
            uninitialized_default_construct_n(begin(), n);
        }

        template<std::input_iterator I>
        constexpr sso_vector(I first, I last, const Alloc& a = Alloc { }) : alloc { a }
        {
            init_allocate(last - first);
            uninitialized_copy(first, last, begin());
        }

        template<std::size_t N, typename A>
        constexpr sso_vector(sso_vector<T, N, A>&& other, const Alloc& a) : alloc { a }
        {
            using O = sso_vector<T, N, A>;
            if constexpr (std::equality_comparable_with<typename O::allocator_type, allocator_type>)
                if (not other.sso() and alloc == other.alloc)
            {
                new (&far) vector_data { std::move(other.far) };
                if constexpr (O::use_sso) other.near.bits = { true, 0 };
                else other.far = { };
                return;
            }

            const auto n = other.size();
            init_allocate(n);
            uninitialized_move_n(other.begin(), n, begin());
        }

        template<std::size_t N, typename A>
        requires (std::convertible_to<typename sso_vector<T, N, A>::allocator_type, allocator_type>)
        constexpr sso_vector(sso_vector<T, N, A>&& other) noexcept : sso_vector { std::move(other), other.alloc } { }

        template<std::size_t N, typename A>
        constexpr sso_vector(const sso_vector<T, N, A>& other, const Alloc& a) : sso_vector { other.cbegin(), other.cend(), a } { }

        template<std::size_t N, typename A>
        constexpr sso_vector(const sso_vector<T, N, A>& other) : sso_vector { other, std::allocator_traits<typename sso_vector<T, N, A>::allocator_type>::select_on_container_copy_construction(other.alloc) } { }

        template<typename A>
        constexpr sso_vector(const std::vector<T, A>& other, const Alloc& a) : sso_vector { other.cbegin(), other.cend(), a } { }

        template<typename A>
        constexpr sso_vector(const std::vector<T, A>& other) : sso_vector { other, std::allocator_traits<typename std::vector<T, A>::allocator_type>::select_on_container_copy_construction(other.get_allocator()) } { }

        constexpr sso_vector(std::initializer_list<T> init, const Alloc& a = Alloc { }) : sso_vector { init.begin(), init.end(), a } { }

        constexpr ~sso_vector() noexcept
        {
            destroy_all();
            deallocate();
        }

        template<std::size_t N, typename A>
        constexpr sso_vector& operator=(const sso_vector<T, N, A>& other)
        {
            if (&other == this) return *this;
            return copy_assign(other.begin(), other.size(), other.alloc);
        }

        template<typename A>
        constexpr sso_vector& operator=(const std::vector<T, A>& other)
        {
            return copy_assign(other.begin(), other.size(), other.get_allocator());
        }

        template<std::size_t N, typename A> requires (std::convertible_to<typename sso_vector<T, N, A>::allocator_type, allocator_type>)
        constexpr sso_vector& operator=(sso_vector<T, N, A>&& other)
        {
            if (&other == this) return *this;

            using O = sso_vector<T, N, A>;
            constexpr bool propagate_alloc = std::allocator_traits<typename O::allocator_type>::propagate_on_container_move_assignment::value;

            const size_type n = other.size();

            std::conditional_t<propagate_alloc, allocator_type, allocator_type&> new_alloc = propagate_alloc ? other.alloc : alloc;

            if (not other.sso() and new_alloc == other.alloc)
            {
                replace_far(other.far);
                if constexpr (O::use_sso) other.near.bits = { true, 0 };
                else other.far = { };
            }
            else
            {
                if constexpr (use_sso) if (n <= sso_size)
                {
                    if (not near.bits.sso)
                    {
                        destroy_all();
                        deallocate();
                        near.bits = { true, 0 };
                        uninitialized_move_n(other.begin(), n, near_data());
                        near.bits = { true, n };
                    }
                    else move_assign_elements(other.begin(), n);
                    if constexpr (propagate_alloc) alloc = std::move(new_alloc);
                    return *this;
                }

                if (n > capacity())
                {
                    const std::size_t cap = calc_capacity(n + n / 2);
                    auto* const p = allocate(new_alloc, cap);
                    try
                    {
                        uninitialized_move_n(new_alloc, other.begin(), n, p);
                    }
                    catch (...)
                    {
                        deallocate(new_alloc, p, cap);
                        throw;
                    }
                    replace_far({ cap, n, p });
                }
                else move_assign_elements(other.begin(), n);
            }
            if constexpr (propagate_alloc) alloc = std::move(new_alloc);
            return *this;
        }

        constexpr sso_vector& operator=(std::initializer_list<T> ilist)
        {
            assign(ilist.begin(), ilist.end());
            return *this;
        }

        constexpr void assign(size_type n, const T& value)
        {
            if (n > capacity())
            {
                const std::size_t cap = calc_capacity(n + n / 2);
                auto* const p = allocate(cap);
                try
                {
                    uninitialized_fill_n(p, n, value);
                }
                catch (...)
                {
                    deallocate(p, cap);
                    throw;
                }
                replace_far({ cap, n, p });
            }
            else value_assign_elements(value, n);
        }

        constexpr void assign(std::initializer_list<T> ilist)
        {
            assign(ilist.begin(), ilist.end());
        }

        template<std::input_iterator I> requires (not std::is_rvalue_reference_v<std::iter_reference_t<I>>)
        constexpr void assign(I first, I last)
        {
            const size_type n = last - first;
            if (n > capacity())
            {
                const std::size_t cap = calc_capacity(n + n / 2);
                auto* const p = allocate(cap);
                try
                {
                    uninitialized_copy_n(first, n, p);
                }
                catch (...)
                {
                    deallocate(p, cap);
                    throw;
                }
                replace_far({ cap, n, p });
            }
            else copy_assign_elements(first, n);
        }

        template<std::input_iterator I> requires (std::is_rvalue_reference_v<std::iter_reference_t<I>>)
        constexpr void assign(I first, I last)
        {
            const size_type n = last - first;
            if (n > capacity())
            {
                const std::size_t cap = calc_capacity(n + n / 2);
                auto* const p = allocate(cap);
                try
                {
                    uninitialized_move_n(first, n, p);
                }
                catch (...)
                {
                    deallocate(p, cap);
                    throw;
                }
                replace_far({ cap, n, p });
            }
            else move_assign_elements(first, n);
        }

        constexpr allocator_type get_allocator() const noexcept { return alloc; }

        constexpr bool sso() const noexcept
        {
            if constexpr (use_sso) return near.bits.sso;
            else return false;
        }

        constexpr bool empty() const noexcept
        {
            return size() == 0;
        }

        constexpr size_type size() const noexcept
        {
            if constexpr (use_sso) if (near.bits.sso)
                return near.bits.size;
            return far.size;
        }

        constexpr size_type capacity() const noexcept
        {
            if constexpr (use_sso) if (near.bits.sso)
                return sso_size;
            return far.capacity;
        }

        constexpr size_type sso_capacity() const noexcept
        {
            return sso_size;
        }

        constexpr size_type max_size() const noexcept
        {
            return std::numeric_limits<difference_type>::max() / sizeof(T);
        }

        constexpr T* data() noexcept
        {
            if constexpr (use_sso) if (near.bits.sso)
                return reinterpret_cast<T*>(&near.storage);
            return far.ptr;
        }

        constexpr const T* data() const noexcept
        {
            if constexpr (use_sso) if (near.bits.sso)
                return reinterpret_cast<const T*>(&near.storage);
            return far.ptr;
        }

        constexpr iterator begin() noexcept { return data(); }
        constexpr const_iterator begin() const noexcept { return data(); };
        constexpr const_iterator cbegin() const noexcept { return data(); };

        constexpr iterator end() noexcept { return data() + size(); }
        constexpr const_iterator end() const noexcept { return data() + size(); };
        constexpr const_iterator cend() const noexcept { return data() + size(); };

        constexpr reverse_iterator rbegin() noexcept { return { end() }; }
        constexpr const_reverse_iterator rbegin() const noexcept { return { end() }; }
        constexpr const_reverse_iterator crbegin() const noexcept { return { end() }; }

        constexpr reverse_iterator rend() noexcept { return { begin() }; }
        constexpr const_reverse_iterator rend() const noexcept { return { begin() }; }
        constexpr const_reverse_iterator crend() const noexcept { return { begin() }; }

        constexpr reference front() { return *begin(); }
        constexpr const_reference front() const { return *begin(); }

        constexpr reference back() { return end()[-1]; }
        constexpr const_reference back() const { return end()[-1]; }

        constexpr reference operator[](size_type pos) { return begin()[pos]; }
        constexpr const_reference operator[](size_type pos) const { return begin()[pos]; }

        constexpr reference at(size_type pos) { check_pos(pos); return begin()[pos]; }
        constexpr const_reference at(size_type pos) const { check_pos(pos); return begin()[pos]; }

        constexpr void reserve(size_type cap)
        {
            if (cap <= capacity()) return;
            cap = calc_capacity(cap);
            const auto n = size();
            auto* const p = allocate(cap);
            try
            {
                uninitialized_move_n(begin(), n, p);
            }
            catch (...)
            {
                deallocate(p, cap);
                throw;
            }
            replace_far({ cap, n, p });
        }

        constexpr void shrink_to_fit()
        {
            if constexpr (use_sso)
            {
                if (near.bits.sso) return;
                if (far.size <= sso_size)
                {
                    auto src = far;
                    new (&near) sso_data;
                    uninitialized_move_n(src.ptr, src.size, near_data());
                    destroy_n(src.ptr, src.size);
                    near.bits = { true, src.size };
                    deallocate(src.ptr, src.capacity);
                    return;
                }
            }
            const auto n = far.size;
            const auto cap = calc_capacity(n);
            if (cap >= far.capacity) return;
            auto* const p = allocate(cap);
            try
            {
                uninitialized_move_n(far.ptr, n, p);
            }
            catch (...)
            {
                deallocate(p, cap);
                throw;
            }
            replace_far({ cap, n, p });
        }

        constexpr void clear() noexcept
        {
            destroy_all();
            set_size(0);
        }

        constexpr iterator insert(const_iterator pos, const T& value)
        {
            const size_type i = pos - begin();
            const size_type a = make_gap(i, 1);
            T* const p = begin() + i;
            [[assume(a < 2)]];
            try
            {
                if constexpr (uses_allocator)
                {
                    destroy_n(p, a);
                    construct(p, value);
                }
                else
                {
                    if (a != 0) *p = value;
                    else construct(p, value);
                }
            }
            catch (...)
            {
                if (p == end() - 1) set_size(size() - 1);
                throw;
            }
            return p;
        }

        constexpr iterator insert(const_iterator pos, T&& value)
        {
            const size_type i = pos - begin();
            const size_type a = make_gap(i, 1);
            T* const p = begin() + i;
            [[assume(a < 2)]];
            try
            {
                if constexpr (uses_allocator)
                {
                    destroy_n(p, a);
                    construct(p, std::move(value));
                }
                else
                {
                    if (a != 0) *p = std::move(value);
                    else construct(p, std::move(value));
                }
            }
            catch (...)
            {
                if (p == end() - 1) set_size(size() - 1);
                throw;
            }
            return p;
        }

        constexpr iterator insert(const_iterator pos, size_type n, const T& value)
        {
            const size_type i = pos - begin();
            const size_type a = make_gap(i, n);
            T* const p = begin() + i;
            try
            {
                if constexpr (uses_allocator or not std::is_nothrow_copy_assignable_v<T>)
                {
                    destroy_n(p, a);
                    uninitialized_fill_n(p, n, value);
                }
                else
                {
                    std::fill_n(par_unseq(), p, a, value);
                    uninitialized_fill_n(p + a, n - a, value);
                }
            }
            catch (...)
            {
                if (p == end() - 1) set_size(size() - 1);
                throw;
            }
            return p;
        }

        template <std::input_iterator I>
        constexpr iterator insert(const_iterator pos, I first, I last)
        {
            const size_type n = last - first;
            const size_type i = pos - begin();
            const size_type a = make_gap(i, n);
            T* const p = begin() + i;
            try
            {
                if constexpr (uses_allocator or not std::is_nothrow_assignable_v<T, std::iter_reference_t<I>>)
                {
                    destroy_n(p, a);
                    uninitialized_copy(first, last, p);
                }
                else
                {
                    std::copy(par_unseq(), first, first + a, p);
                    uninitialized_copy(first + a, last, p + a);
                }
            }
            catch (...)
            {
                if (p == end() - 1) set_size(size() - 1);
                throw;
            }
            return p;
        }

        constexpr iterator insert(const_iterator pos, std::initializer_list<T> ilist)
        {
            return insert(pos, ilist.begin(), ilist.end());
        }

        template <typename... A>
        constexpr iterator emplace(const_iterator pos, A&&... args)
        {
            const size_type i = pos - begin();
            const size_type a = make_gap(i, 1);
            T* const p = begin() + i;
            [[assume(a < 2)]];
            destroy_n(p, a);
            try { construct(p, std::forward<A>(args)...); }
            catch (...)
            {
                if (p == end() - 1) set_size(size() - 1);
                throw;
            }
            return p;
        }

        constexpr iterator erase(const_iterator pos)
        {
            return erase(pos, pos + 1);
        }

        constexpr iterator erase(const_iterator first, const_iterator last)
        {
            T* const p = begin();
            const size_type i = first - p;
            const size_type n = last - first;
            const size_type old_size = size();
            const size_type new_size = old_size - n;
            std::move(std::execution::seq, p + i + n, p + old_size, p + i);
            destroy_n(p + new_size, n);
            set_size(new_size);
            return p + i;
        }

        constexpr void push_back(const T& value)
        {
            emplace_back(value);
        }

        constexpr void push_back(T&& value)
        {
            emplace_back(std::move(value));
        }

        template <typename... A>
        constexpr reference emplace_back(A&&... args)
        {
            const size_type n = size();
            do_resize(n + 1);
            try
            {
                construct(end() - 1, std::forward<A>(args)...);
            }
            catch (...)
            {
                set_size(n);
                throw;
            }
            return back();
        }

        constexpr void pop_back()
        {
            do_resize(size() - 1);
        }

        constexpr void resize(size_type n)
        {
            const auto a = do_resize(n);
            uninitialized_default_construct_n(end() - a, a);
        }

        constexpr void resize(size_type n, const value_type& value)
        {
            const auto a = do_resize(n);
            uninitialized_fill_n(end() - a, a, value);
        }

        template<typename A> requires (not sso and std::is_same_v<typename sso_vector<T, min_sso_size, A>::allocator_type, allocator_type>)
        constexpr void swap(sso_vector<T, min_sso_size, A>& other)
        {
            using std::swap;
            if constexpr (allocator_traits::propagate_on_container_swap::value)
                swap(alloc, other.alloc);
            swap(far.capacity, other.far.capacity);
            swap(far.size, other.far.size);
            swap(far.ptr, other.far.ptr);
        }

    private:
        template<typename, std::size_t, typename>
        friend struct sso_vector;

        struct vector_data
        {
            size_type capacity;
            size_type size;
            pointer ptr;
        };

        static constexpr size_type default_sso_size = (sizeof(vector_data) - alignof(T)) / sizeof(T);
        static constexpr size_type sso_size = std::max(min_sso_size, default_sso_size);
        static constexpr size_type min_alloc_size = std::max(std::bit_ceil(sso_size), 8ul);
        static constexpr bool use_sso = sso_size > 0;
        static constexpr bool oversized = min_sso_size > default_sso_size;
        static constexpr bool uses_allocator = std::uses_allocator_v<T, allocator_type>;

        struct [[gnu::packed]] sso_bits
        {
            bool sso : 1;
            unsigned size : 7;
        };
        struct sso_data
        {
            sso_bits bits;
            std::aligned_storage_t<sizeof(T) * sso_size, alignof(T)> storage;
        };

        static_assert(sso_size < 1 << 7);
        static_assert(std::endian::native == std::endian::little);
        static_assert(sizeof(sso_bits) == 1);
        static_assert(alignof(sso_bits) == 1);
        static_assert(not use_sso or oversized or sizeof(sso_data) <= sizeof(vector_data));
        static_assert(not use_sso or not oversized or sizeof(sso_data) > sizeof(vector_data));

        static consteval auto& par_unseq() noexcept
        {
            return std::execution::par_unseq;
        }

        static constexpr size_type calc_capacity(size_type n) noexcept
        {
            return std::max(std::bit_ceil(n), min_alloc_size);
        }

        template <typename I>
        constexpr void uninitialized_move(I first, I last, T* dst)
        {
            uninitialized_move_n(first, last - first, dst);
        }

        template <typename I>
        constexpr void uninitialized_move_n(I src, size_type n, T* dst)
        {
            uninitialized_move_n(alloc, src, n, dst);
        }

        template <typename I>
        constexpr void uninitialized_move_n(allocator_type& alloc, I src, size_type n, T* dst)
        {
            uninitialized_copy_n(alloc, std::make_move_iterator(src), n, dst);
        }

        template <typename I>
        constexpr void uninitialized_copy(I first, I last, T* dst)
        {
            uninitialized_copy_n(first, last - first, dst);
        }

        template <typename I>
        constexpr void uninitialized_copy_n(I src, size_type n, T* dst)
        {
            uninitialized_copy_n(alloc, src, n, dst);
        }

        template <std::same_as<T> U, typename I>
        requires (noexcept(construct(std::declval<U*>(), std::declval<std::iter_reference_t<I>>())))
        constexpr void uninitialized_copy_n(allocator_type& alloc, I src, size_type n, U* dst) noexcept
        {
            std::for_each_n(par_unseq(), index { 0 }, n, [&alloc, src, dst](auto i) { allocator_traits::construct(alloc, dst + i, src[i]); });
        }

        template <typename I>
        constexpr void uninitialized_copy_n(allocator_type& alloc, I src, size_type n, T* dst)
        {
            for (unsigned i = 0; i < n; ++i)
            {
                try { allocator_traits::construct(alloc, dst + i, src[i]); }
                catch (...)
                {
                    destroy_n(dst, i);
                    throw;
                }
            }
        }

        template<std::same_as<T> U>
        requires (noexcept(construct(std::declval<U*>(), std::declval<const U&>())))
        constexpr void uninitialized_fill_n(U* dst, size_type n, const U& value) noexcept
        {
            std::for_each_n(par_unseq(), index { 0 }, n, [this, dst, &value](auto i) { construct(dst + i, value); });
        }

        constexpr void uninitialized_fill_n(T* dst, size_type n, const T& value)
        {
            for (unsigned i = 0; i < n; ++i)
            {
                try { construct(dst + i, value); }
                catch (...)
                {
                    destroy_n(dst, i);
                    throw;
                }
            }
        }

        template<std::same_as<T> U>
        requires (noexcept(construct(std::declval<U*>())))
        constexpr void uninitialized_default_construct_n(U* dst, size_type n) noexcept
        {
            std::for_each_n(par_unseq(), index { 0 }, n, [this, dst](auto i) { construct(dst + i); });
        }

        constexpr void uninitialized_default_construct_n(T* dst, size_type n)
        {
            for (unsigned i = 0; i < n; ++i)
            {
                try { construct(dst + i); }
                catch (...)
                {
                    destroy_n(dst, i);
                    throw;
                }
            }
        }

        constexpr void destroy_n(T* p, size_type n) noexcept
        {
            std::for_each_n(par_unseq(), index { 0 }, n, [this, p](auto i) { destroy(p + i); });
        }

        template<typename... A>
        constexpr void construct(T* p, A&&... args)
            noexcept(noexcept(allocator_traits::construct(std::declval<Alloc&>(), std::declval<T*>(), std::declval<A>()...)))
        {
            allocator_traits::construct(alloc, p, std::forward<A>(args)...);
        }

        constexpr void destroy(T* p) noexcept
        {
            allocator_traits::destroy(alloc, p);
        }

        [[nodiscard]] constexpr pointer allocate(allocator_type& a, size_type cap)
        {
            assert(std::has_single_bit(cap));
            assert(cap > min_alloc_size);
            return allocator_traits::allocate(a, cap);
        }

        [[nodiscard]] constexpr pointer allocate(size_type cap)
        {
            return allocate(alloc, cap);
        }

        constexpr void deallocate(allocator_type& a, pointer p, size_type cap) noexcept
        {
            if (p == nullptr) return;
            allocator_traits::deallocate(a, p, cap);
        }

        constexpr void deallocate(pointer p, size_type cap) noexcept
        {
            deallocate(alloc, p, cap);
        }

        constexpr void deallocate() noexcept
        {
            if (not sso()) deallocate(far.ptr, far.capacity);
        }

        constexpr void destroy_all() noexcept
        {
            destroy_n(begin(), size());
        }

        constexpr void init_allocate(size_type n)
        {
            if constexpr (use_sso) if (n <= sso_size)
            {
                new (&near) sso_data;
                near.bits = { true, n };
                return;
            }

            const size_type cap = calc_capacity(n + n / 2);
            if (n == 0) new (&far) vector_data { };
            else new (&far) vector_data { cap, n, allocate(cap) };
            if constexpr (use_sso)
                [[assume(not near.bits.sso)]];
        }

        constexpr void value_assign_elements(const T& v, size_type n)
        {
            assert(n < capacity());
            const auto sz = size();
            auto* const dst = data();
            if constexpr (uses_allocator or not std::is_nothrow_copy_assignable_v<T>)
            {
                destroy_n(dst, sz);
                uninitialized_fill_n(dst, n, v);
            }
            else
            {
                if (n < sz)
                    destroy_n(dst + n, sz - n);
                if (n > sz)
                    uninitialized_fill_n(dst + sz, n - sz, v);
                std::fill_n(par_unseq(), dst, std::min(n, sz), v);
            }
            set_size(n);
        }

        template<typename I>
        constexpr void move_assign_elements(I src, size_type n)
        {
            assert(n < capacity());
            const auto sz = size();
            auto* const dst = data();
            if constexpr (uses_allocator or not std::is_nothrow_move_assignable_v<T>)
            {
                destroy_n(dst, sz);
                uninitialized_move_n(src, n, dst);
            }
            else
            {
                if (n < sz)
                    destroy_n(dst + n, sz - n);
                if (n > sz)
                    uninitialized_move_n(src + sz, n - sz, dst + sz);
                std::move(par_unseq(), src, src + std::min(n, sz), dst);
            }
            set_size(n);
        }

        template<typename I>
        constexpr void copy_assign_elements(I src, size_type n)
        {
            assert(n < capacity());
            const auto sz = size();
            auto* const dst = data();
            if constexpr (uses_allocator or not std::is_nothrow_copy_assignable_v<T>)
            {
                destroy_n(dst, sz);
                uninitialized_copy_n(src, n, dst);
            }
            else
            {
                if (n < sz)
                    destroy_n(dst + n, sz - n);
                if (n > sz)
                    uninitialized_copy_n(src + sz, n - sz, dst + sz);
                std::copy_n(par_unseq(), src, std::min(n, sz), dst);
            }
            set_size(n);
        }

        template<typename I, typename A> requires(std::convertible_to<A, allocator_type>)
        constexpr sso_vector& copy_assign(I src, size_type n, const A& a)
        {
            constexpr bool propagate_alloc = std::allocator_traits<A>::propagate_on_container_copy_assignment::value;

            std::conditional_t<propagate_alloc, allocator_type, allocator_type&> new_alloc = propagate_alloc ? a : alloc;

            if (n > capacity() or (propagate_alloc and new_alloc != alloc))
            {
                if constexpr (use_sso) if (n <= sso_size)
                {
                    if (not near.bits.sso)
                    {
                        destroy_all();
                        deallocate();
                        near.bits = { true, 0 };
                        uninitialized_copy_n(src, n, near_data());
                        near.bits = { true, n };
                    }
                    else copy_assign_elements(src, n);
                    if constexpr (propagate_alloc) alloc = std::move(new_alloc);
                    return *this;
                }

                const std::size_t cap = calc_capacity(n + n / 2);
                auto* const p = allocate(new_alloc, cap);
                try
                {
                    uninitialized_copy_n(new_alloc, src, n, p);
                }
                catch (...)
                {
                    deallocate(new_alloc, p, cap);
                    throw;
                }
                replace_far({ cap, n, p });
            }
            else copy_assign_elements(src, n);
            if constexpr (propagate_alloc) alloc = std::move(new_alloc);
            return *this;
        }

        // Make a gap of N elements starting at position I.  Returns the
        // number of moved-from elements that can be reassigned.  The
        // remaining elements are uninitialized.
        constexpr size_type make_gap(size_type i, size_type n)
        {
            const size_type old_size = size();
            const size_type new_size = old_size + n;
            const size_type j = old_size - i;
            if (new_size > capacity())
            {
                const std::size_t cap = calc_capacity(new_size + new_size / 2);
                auto* const p = allocate(cap);
                try
                {
                    auto* const src = begin();
                    uninitialized_move_n(src, i, p);
                    uninitialized_move_n(src + i, j, p + i + n);
                }
                catch (...)
                {
                    deallocate(p, cap);
                    throw;
                }
                replace_far({ cap, new_size, p });
                return 0;
            }
            else
            {
                set_size(new_size);
                const size_type a = std::min(n, j);
                const size_type b = j - a;
                T* const src = begin() + i;
                T* const dst = src + n;
                const auto rsrc = std::make_reverse_iterator(src + j);
                const auto rdst = std::make_reverse_iterator(dst + j);
                uninitialized_move_n(src + b, a, dst + b);
                std::move(std::execution::seq, rsrc, rsrc + b, rdst);
                return a;
            }
        }

        // Resize without creating new objects.  Returns the number of
        // uninitialized elements at the end.
        constexpr size_type do_resize(size_type n)
        {
            const auto old_size = size();
            if (n > old_size)
            {
                if (n > capacity()) reserve(n + n / 2);
                set_size(n);
                return n - old_size;
            }
            else
            {
                destroy_n(begin() + n, old_size - n);
                set_size(n);
                return 0;
            }
        }

        constexpr void set_size(size_type n) noexcept
        {
            if constexpr (use_sso) if (near.bits.sso)
            {
                near.bits = { true, n };
                return;
            }
            far.size = n;
        }

        constexpr void check_pos(size_type i)
        {
            if (i >= size()) throw std::out_of_range { "sso_vector: position >= size()" };
        }

        constexpr T* near_data() noexcept
        {
            if constexpr (use_sso) return reinterpret_cast<T*>(&near.storage);
            else return nullptr;
        }

        constexpr T* far_data() noexcept
        {
            return far.ptr;
        }

        // Destroy all elements, then deallocate and replace vector_data.
        constexpr void replace_far(const vector_data& vdata) noexcept
        {
            destroy_all();
            if constexpr (use_sso) if (near.bits.sso)
            {
                new (&far) vector_data { vdata };
                return;
            }
            deallocate();
            far = vdata;
        }

        [[no_unique_address]] allocator_type alloc;
        union
        {
            vector_data far;
            std::conditional_t<use_sso, sso_data, jw::empty> near;
        };
    };

    template <typename T, std::size_t N, typename A1, typename A2>
    constexpr void swap(sso_vector<T, N, A1>& a, sso_vector<T, N, A2>& b)
    {
        a.swap(b);
    }

    template <typename T, std::size_t N, typename A, typename U>
    constexpr typename sso_vector<T, N, A>::size_type erase(sso_vector<T, N, A>& c, const U& value)
    {
        const auto begin = c.begin();
        const auto end = c.end();
        auto i = std::remove(begin, end, value);
        c.erase(i, end);
        return end - i;
    }

    template <typename T, std::size_t N, typename A, typename F>
    constexpr typename sso_vector<T, N, A>::size_type erase_if(sso_vector<T, N, A>& c, F pred)
    {
        const auto begin = c.begin();
        const auto end = c.end();
        auto i = std::remove_if(begin, end, pred);
        c.erase(i, end);
        return end - i;
    }
}

namespace jw::pmr
{
    template<typename T, std::size_t min_sso_size = 0> using sso_vector = jw::sso_vector<T, min_sso_size, std::pmr::polymorphic_allocator<T>>;
}
