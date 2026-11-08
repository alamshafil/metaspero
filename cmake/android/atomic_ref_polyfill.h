// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal std::atomic_ref polyfill for the Android NDK libc++ (r28 ships clang 19's libc++, which
// predates atomic_ref). Force-included into every C++ translation unit of Android builds
// (see CMakeLists.txt), a no-op wherever the standard library provides std::atomic_ref.
#pragma once

#if defined(__cplusplus)
#include <atomic>
#include <cstddef>
#include <type_traits>

#if !defined(__cpp_lib_atomic_ref) && !defined(KYTY_ATOMIC_REF_POLYFILL)
#define KYTY_ATOMIC_REF_POLYFILL 1
namespace std {

template <class T>
struct atomic_ref {
    static_assert(is_trivially_copyable_v<T>, "atomic_ref requires a trivially copyable type");
    using value_type = T;
    using difference_type = conditional_t<is_pointer_v<T>, ptrdiff_t, T>;
    static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), nullptr);
    static constexpr size_t required_alignment = sizeof(T) > alignof(T) ? sizeof(T) : alignof(T);

    explicit atomic_ref(T& obj) noexcept : m_ptr(&obj) {}
    atomic_ref(const atomic_ref&) noexcept = default;
    atomic_ref& operator=(const atomic_ref&) = delete;

    bool is_lock_free() const noexcept { return __atomic_is_lock_free(sizeof(T), m_ptr); }

    void store(T v, memory_order o = memory_order_seq_cst) const noexcept { __atomic_store(m_ptr, &v, int(o)); }
    T load(memory_order o = memory_order_seq_cst) const noexcept {
        alignas(T) unsigned char buf[sizeof(T)];
        __atomic_load(m_ptr, reinterpret_cast<T*>(buf), int(o));
        return *reinterpret_cast<T*>(buf);
    }
    operator T() const noexcept { return load(); }
    T operator=(T v) const noexcept { store(v); return v; }

    T exchange(T v, memory_order o = memory_order_seq_cst) const noexcept {
        alignas(T) unsigned char buf[sizeof(T)];
        __atomic_exchange(m_ptr, &v, reinterpret_cast<T*>(buf), int(o));
        return *reinterpret_cast<T*>(buf);
    }

    bool compare_exchange_weak(T& e, T d, memory_order s, memory_order f) const noexcept {
        return __atomic_compare_exchange(m_ptr, &e, &d, true, int(s), int(_failure(f)));
    }
    bool compare_exchange_strong(T& e, T d, memory_order s, memory_order f) const noexcept {
        return __atomic_compare_exchange(m_ptr, &e, &d, false, int(s), int(_failure(f)));
    }
    bool compare_exchange_weak(T& e, T d, memory_order o = memory_order_seq_cst) const noexcept {
        return compare_exchange_weak(e, d, o, _failure(o));
    }
    bool compare_exchange_strong(T& e, T d, memory_order o = memory_order_seq_cst) const noexcept {
        return compare_exchange_strong(e, d, o, _failure(o));
    }

    T fetch_add(difference_type a, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_add(m_ptr, a, int(o)); }
    T fetch_sub(difference_type a, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_sub(m_ptr, a, int(o)); }
    T fetch_and(T a, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_and(m_ptr, a, int(o)); }
    T fetch_or(T a, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_or(m_ptr, a, int(o)); }
    T fetch_xor(T a, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_xor(m_ptr, a, int(o)); }
    T operator++() const noexcept { return fetch_add(1) + 1; }
    T operator++(int) const noexcept { return fetch_add(1); }
    T operator--() const noexcept { return fetch_sub(1) - 1; }
    T operator--(int) const noexcept { return fetch_sub(1); }
    T operator+=(difference_type a) const noexcept { return fetch_add(a) + a; }
    T operator-=(difference_type a) const noexcept { return fetch_sub(a) - a; }
    T operator&=(T a) const noexcept { return fetch_and(a) & a; }
    T operator|=(T a) const noexcept { return fetch_or(a) | a; }
    T operator^=(T a) const noexcept { return fetch_xor(a) ^ a; }

    // C++20 wait/notify: busy-wait fallback (notify is a no-op).
    void wait(T old, memory_order o = memory_order_seq_cst) const noexcept {
        for (;;) { T cur = load(o); if (__builtin_memcmp(&cur, &old, sizeof(T)) != 0) return; }
    }
    void notify_one() const noexcept {}
    void notify_all() const noexcept {}

private:
    static constexpr memory_order _failure(memory_order o) noexcept {
        return o == memory_order_acq_rel ? memory_order_acquire : (o == memory_order_release ? memory_order_relaxed : o);
    }
    T* m_ptr;
};

} // namespace std
#endif
#endif
