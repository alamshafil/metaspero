// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef KYTY_LOADER_FEX_HLE_BRIDGE_H_
#define KYTY_LOADER_FEX_HLE_BRIDGE_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Loader::Fex {

enum class ThunkSignature : uint8_t {
    Auto,                ///< Auto-detect based on function name, defaulting to IntOnly
    IntOnly,             ///< Standard System V AMD64 integer registers (RDI..R9, ret RAX)
    FloatRet,            ///< Integer arguments, float return in XMM0 (S0)
    DoubleRet,           ///< Integer arguments, double return in XMM0 (D0)
    FloatArg0,           ///< First arg in XMM0 (float), integer args for rest, integer/void return
    DoubleArg0,          ///< First arg in XMM0 (double), integer args for rest, integer/void return
    FloatArg0_FloatRet,  ///< Float arg in XMM0 (S0), float return in XMM0 (S0)
    DoubleArg0_DoubleRet,///< Double arg in XMM0 (D0), double return in XMM0 (D0)
    FloatArg01_FloatRet, ///< Float args in XMM0 (S0), XMM1 (S1), float return in XMM0 (S0)
    DoubleArg01_DoubleRet,///< Double args in XMM0 (D0), XMM1 (D1), double return in XMM0 (D0)
    Sincosf,             ///< float x in XMM0 (S0), float* s in RDI, float* c in RSI
    Sincos,              ///< double x in XMM0 (D0), double* s in RDI, double* c in RSI
    Difftime,            ///< int64_t t1 in RDI, int64_t t0 in RSI, double return in XMM0 (D0)
    FiberSwitch,         ///< Saves/restores x86-64 callee-saved GPRs + RSP/RIP for guest fiber context switch
};

struct GuestFiberContext {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
};
static_assert(sizeof(GuestFiberContext) == 80);

struct GuestFiberRestoreFrame {
    GuestFiberContext ctx;
    uint64_t          rax;
};
static_assert(sizeof(GuestFiberRestoreFrame) == 88);

using HleInvoker = uint64_t (*)(uint64_t host_fn, const uint64_t* int_args, const uint64_t* xmm_args, uint64_t guest_rsp);

struct HleDescriptor {
    HleInvoker invoker{nullptr};
    uint8_t    num_xmm_args{0};
    bool       float_ret{false};
    bool       has_rdx_ret{false};
};

namespace Detail {

template <typename T>
inline constexpr bool IsHleFloat =
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, float> ||
    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, double>;

template <typename T>
inline constexpr bool IsHleFloatVec3 =
    !IsHleFloat<T> && !std::is_pointer_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    !std::is_integral_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    !std::is_enum_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    std::is_trivially_copyable_v<std::remove_cv_t<std::remove_reference_t<T>>> &&
    sizeof(std::remove_cv_t<std::remove_reference_t<T>>) == 12 &&
    alignof(std::remove_cv_t<std::remove_reference_t<T>>) == 4;

template <typename T>
inline constexpr uint8_t HleArgXmmSlots =
    IsHleFloat<T> ? 1u : (IsHleFloatVec3<T> ? 2u : 0u);

template <typename R>
inline constexpr bool IsHleRdxRet =
    !IsHleFloat<R> && !std::is_pointer_v<R> && !std::is_integral_v<R> &&
    !std::is_enum_v<R> && std::is_trivially_copyable_v<R> &&
    sizeof(R) > sizeof(uint64_t) && sizeof(R) <= 2 * sizeof(uint64_t);

template <>
inline constexpr bool IsHleRdxRet<void> = false;

template <typename Arg>
std::remove_cv_t<std::remove_reference_t<Arg>> UnpackHleArg(
    const uint64_t* int_args, size_t& int_idx,
    const uint64_t* xmm_args, size_t& xmm_idx) noexcept {
    using CleanT = std::remove_cv_t<std::remove_reference_t<Arg>>;
    if constexpr (std::is_same_v<CleanT, float>) {
        float    val  = 0.0f;
        uint64_t bits = xmm_args[xmm_idx++];
        std::memcpy(&val, &bits, sizeof(float));
        return val;
    } else if constexpr (std::is_same_v<CleanT, double>) {
        double   val  = 0.0;
        uint64_t bits = xmm_args[xmm_idx++];
        std::memcpy(&val, &bits, sizeof(double));
        return val;
    } else if constexpr (IsHleFloatVec3<CleanT>) {
        CleanT   val{};
        uint64_t lo = xmm_args[xmm_idx++];
        uint64_t hi = xmm_args[xmm_idx++];
        std::memcpy(reinterpret_cast<uint8_t*>(&val), &lo, 8);
        std::memcpy(reinterpret_cast<uint8_t*>(&val) + 8, &hi, 4);
        return val;
    } else if constexpr (std::is_pointer_v<CleanT>) {
        return reinterpret_cast<CleanT>(static_cast<uintptr_t>(int_args[int_idx++]));
    } else if constexpr (std::is_enum_v<CleanT>) {
        return static_cast<CleanT>(
            static_cast<std::underlying_type_t<CleanT>>(int_args[int_idx++]));
    } else if constexpr (std::is_same_v<CleanT, bool>) {
        return (int_args[int_idx++] & 0xffu) != 0;
    } else if constexpr (std::is_integral_v<CleanT>) {
        return static_cast<CleanT>(int_args[int_idx++]);
    } else if constexpr (std::is_trivially_copyable_v<CleanT> && sizeof(CleanT) <= sizeof(uint64_t)) {
        CleanT   val{};
        uint64_t bits = int_args[int_idx++];
        std::memcpy(&val, &bits, sizeof(CleanT));
        return val;
    } else if constexpr (std::is_trivially_copyable_v<CleanT> && sizeof(CleanT) <= 2 * sizeof(uint64_t)) {
        CleanT   val{};
        uint64_t bits[2] = {int_args[int_idx], int_args[int_idx + 1]};
        int_idx += 2;
        std::memcpy(&val, bits, sizeof(CleanT));
        return val;
    } else {
        static_assert(sizeof(CleanT) == 0, "Unsupported HLE argument type");
        return CleanT{};
    }
}

template <typename F>
struct HleTraits;

template <typename R, typename... Args>
struct HleTraits<R (*)(Args...)> {
    static constexpr uint8_t kNumXmmArgs =
        static_cast<uint8_t>((0u + ... + HleArgXmmSlots<Args>));
    static constexpr bool kFloatRet  = IsHleFloat<R>;
    static constexpr bool kHasRdxRet = IsHleRdxRet<std::remove_cv_t<R>>;

    static uint64_t Invoke(uint64_t host_fn, const uint64_t* int_args,
                           const uint64_t* xmm_args, uint64_t guest_rsp) {
        [[maybe_unused]] size_t int_idx = 0;
        [[maybe_unused]] size_t xmm_idx = 0;
        std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...> unpacked{
            UnpackHleArg<Args>(int_args, int_idx, xmm_args, xmm_idx)...
        };
        auto fn = reinterpret_cast<R (*)(Args...)>(host_fn);
        if constexpr (std::is_void_v<R>) {
            std::apply(fn, unpacked);
            return 0;
        } else if constexpr (std::is_same_v<std::remove_cv_t<R>, float>) {
            float    res  = std::apply(fn, unpacked);
            uint32_t bits = 0;
            std::memcpy(&bits, &res, sizeof(float));
            return bits;
        } else if constexpr (std::is_same_v<std::remove_cv_t<R>, double>) {
            double   res  = std::apply(fn, unpacked);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(double));
            return bits;
        } else if constexpr (std::is_pointer_v<R>) {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(std::apply(fn, unpacked)));
        } else if constexpr (std::is_same_v<std::remove_cv_t<R>, bool>) {
            return std::apply(fn, unpacked) ? 1ULL : 0ULL;
        } else if constexpr (std::is_enum_v<R>) {
            using U = std::make_unsigned_t<std::underlying_type_t<R>>;
            return static_cast<uint64_t>(static_cast<U>(std::apply(fn, unpacked)));
        } else if constexpr (std::is_integral_v<R>) {
            using U = std::make_unsigned_t<std::remove_cv_t<R>>;
            return static_cast<uint64_t>(static_cast<U>(std::apply(fn, unpacked)));
        } else if constexpr (std::is_trivially_copyable_v<R> && sizeof(R) <= sizeof(uint64_t)) {
            R        res  = std::apply(fn, unpacked);
            uint64_t bits = 0;
            std::memcpy(&bits, &res, sizeof(R));
            return bits;
        } else if constexpr (kHasRdxRet) {
            R        res     = std::apply(fn, unpacked);
            uint64_t bits[2] = {0, 0};
            std::memcpy(bits, &res, sizeof(R));
            if (guest_rsp != 0) {
                *reinterpret_cast<uint64_t*>(guest_rsp - 8) = bits[1];
            }
            return bits[0];
        } else {
            static_assert(sizeof(R) == 0, "Unsupported HLE return type");
            return 0;
        }
    }
};

template <typename R, typename... Args>
struct HleTraits<R (*)(Args...) noexcept> : HleTraits<R (*)(Args...)> {};

template <typename R, typename... Args>
struct HleTraits<R (*)(Args..., ...)> {
    static constexpr uint8_t kNumXmmArgs = 6;
    static constexpr bool    kFloatRet   = IsHleFloat<R>;
    static constexpr bool    kHasRdxRet  = false;

    static uint64_t Invoke(uint64_t host_fn, const uint64_t* int_args,
                           const uint64_t* xmm_args, uint64_t guest_rsp) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        using VaVec = __m128;
#else
        using VaVec = float __attribute__((__vector_size__(16), __aligned__(16)));
#endif
        auto make_xmm = [&](size_t i) noexcept -> VaVec {
            VaVec v{};
            if (i < kNumXmmArgs) {
                std::memcpy(&v, &xmm_args[i], sizeof(uint64_t));
            }
            return v;
        };
        using VaFn = R (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                           uint64_t, VaVec, VaVec, VaVec, VaVec, VaVec, VaVec, VaVec, VaVec, ...);
        auto     fn       = reinterpret_cast<VaFn>(host_fn);
        uint64_t overflow = (guest_rsp != 0) ? (guest_rsp + 8) : 0;
        if constexpr (std::is_void_v<R>) {
            fn(int_args[0], int_args[1], int_args[2], int_args[3], int_args[4], int_args[5],
               overflow,
               make_xmm(0), make_xmm(1), make_xmm(2), make_xmm(3),
               make_xmm(4), make_xmm(5), make_xmm(6), make_xmm(7));
            return 0;
        } else {
            using U = std::make_unsigned_t<std::remove_cv_t<R>>;
            return static_cast<uint64_t>(static_cast<U>(
                fn(int_args[0], int_args[1], int_args[2], int_args[3], int_args[4], int_args[5],
                   overflow,
                   make_xmm(0), make_xmm(1), make_xmm(2), make_xmm(3),
                   make_xmm(4), make_xmm(5), make_xmm(6), make_xmm(7))));
        }
    }
};

template <typename R, typename... Args>
struct HleTraits<R (*)(Args..., ...) noexcept> : HleTraits<R (*)(Args..., ...)> {};

} // namespace Detail

template <typename F>
constexpr HleDescriptor MakeHleDescriptor(F /*fn*/) noexcept {
    using PtrT = std::decay_t<F>;
    return HleDescriptor{
        &Detail::HleTraits<PtrT>::Invoke,
        Detail::HleTraits<PtrT>::kNumXmmArgs,
        Detail::HleTraits<PtrT>::kFloatRet,
        Detail::HleTraits<PtrT>::kHasRdxRet,
    };
}

struct HleFunction {
    uint64_t        id{0};
    uint64_t        host_fn{0};
    std::string     name;
    ThunkSignature  sig{ThunkSignature::IntOnly};
    HleDescriptor   desc{};
};

class FexHleBridge {
public:
    static FexHleBridge& Instance();

    /// Reset all registered thunks and state when a guest session ends
    void Reset();

    /// Register a host function and get its synthetic guest thunk address
    uint64_t RegisterFunction(uint64_t host_func_ptr, const std::string& name,
                              ThunkSignature sig = ThunkSignature::Auto,
                              HleDescriptor desc = {});

    /// Find function by synthetic thunk address
    const HleFunction* FindByThunk(uint64_t guest_thunk_addr) const;

    /// Find function by ID
    const HleFunction* FindById(uint64_t id) const;

    /// Get the HLT trampoline address (used as return address for guest entry/module funcs)
    uint64_t GetHltAddress();

    /// Dispatch an intercepted HLE call from guest registers
    /// gpr: [0]=RDI, [1]=RSI, [2]=RDX, [3]=R10 (preserved RCX), [4]=R8, [5]=R9
    uint64_t DispatchHle(uint64_t thunk_id, const uint64_t* gpr, uint64_t guest_rsp);

    /// Record an HLE call (increments counter, logs first calls, stores in recent calls buffer)
    void RecordHleCall(uint64_t thunk_id, const uint64_t* args, uint64_t guest_rsp, uint64_t result);

    /// Get call count for a function ID
    uint64_t GetCallCount(uint64_t id) const;

    /// Get current guest caller RIP on this thread
    uint64_t GetCurrentCallerRip() const;

    /// Read the caller's x86-64 fiber context saved by the FiberSwitch thunk prior to syscall
    bool ReadCurrentGuestFiberContext(GuestFiberContext* out_ctx) const;

    /// Schedule a guest x86-64 fiber context restore upon returning from the current FiberSwitch syscall
    void ScheduleGuestFiberRestore(const GuestFiberContext& target_ctx, uint64_t target_rax);

    /// Check whether an address is within the synthetic thunk memory range
    bool IsThunkAddress(uint64_t addr) const {
        return m_thunk_base != 0 && addr >= m_thunk_base && addr < (m_thunk_base + m_thunk_capacity);
    }

    struct RecentCall {
        uint64_t thunk_id{0};
        uint64_t caller_rip{0};
        uint64_t args[3]{0, 0, 0};
        uint64_t ret{0};
        char name[64]{0};
    };

    static constexpr size_t RECENT_CALLS_CAPACITY = 32;
    uint64_t GetRecentCallCount() const;
    RecentCall GetRecentCall(uint64_t index) const;

    struct InFlightCall {
        std::atomic<uint64_t> thunk_id{UINT64_MAX};
        std::atomic<uint64_t> caller_rip{0};
        std::atomic<uint64_t> arg0{0};
        std::atomic<uint64_t> arg1{0};
        std::atomic<uint64_t> start_ms{0};
        std::atomic<int32_t>  tid{-1};
    };

    static constexpr size_t MAX_IN_FLIGHT = 256;
    const InFlightCall* GetInFlightCalls() const { return m_in_flight; }
    void SetInFlight(size_t slot, int32_t tid, uint64_t thunk_id, uint64_t caller_rip, uint64_t a0, uint64_t a1);
    void ClearInFlight(size_t slot);

    static constexpr uint64_t THUNK_STRIDE = 64; // 64 bytes per stub (supports up to 6 XMM args + float return)

private:
    FexHleBridge();

    void EnsureInitialized();

    mutable std::shared_mutex m_mutex;
    std::vector<std::unique_ptr<HleFunction>> m_functions;
    std::unordered_map<uint64_t, uint64_t> m_host_to_id;
    std::unique_ptr<std::atomic<uint64_t>[]> m_call_counts;
    uint64_t m_thunk_base{0};
    uint64_t m_thunk_capacity{0};
    uint64_t m_custom_code_offset{0};
    uint64_t m_fiber_trampoline_addr{0};

    std::atomic<uint64_t> m_recent_calls_total{0};
    RecentCall m_recent_calls[RECENT_CALLS_CAPACITY];
    InFlightCall m_in_flight[MAX_IN_FLIGHT];
};

} // namespace Loader::Fex

#endif // KYTY_LOADER_FEX_HLE_BRIDGE_H_
