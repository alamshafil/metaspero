#include "kernel/syncOnAddress.h"

#include "common/hostException.h"
#include "common/threads.h"
#include "libs/errno.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <list>
#include <mutex>

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX && !defined(__APPLE__)
#include <cerrno>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Libs::LibKernel::SyncOnAddress {

namespace {

constexpr uint32_t SIGNAL_POLL_MICROS = 10000;

using Clock = std::chrono::steady_clock;

template <typename T>
[[nodiscard]] bool IsValidWaitAddress(const volatile T* address) {
	return address != nullptr && (reinterpret_cast<uintptr_t>(address) & (alignof(T) - 1u)) == 0;
}

[[nodiscard]] bool IsValidWakeAddress(const volatile void* address) {
	return address != nullptr &&
	       (reinterpret_cast<uintptr_t>(address) & (alignof(uint32_t) - 1u)) == 0;
}

template <typename T>
[[nodiscard]] T ReadWord(const volatile T* address) {
	return __atomic_load_n(address, __ATOMIC_ACQUIRE);
}

struct WaitDeadline {
	bool              finite = false;
	Clock::time_point end {};
};

[[nodiscard]] WaitDeadline MakeDeadline(std::chrono::nanoseconds timeout) {
	const auto now = Clock::now();
	const auto remaining = Clock::time_point::max() - now;
	return {true, timeout >= remaining ? Clock::time_point::max() : now + timeout};
}

[[nodiscard]] WaitDeadline MakeDeadline(const uint32_t* timeout_micros) {
	if (timeout_micros == nullptr) {
		return {};
	}
	return MakeDeadline(std::chrono::microseconds(*timeout_micros));
}

[[nodiscard]] uint32_t GetWaitSliceMicros(const WaitDeadline& deadline, bool first_wait) {
	if (!deadline.finite) {
		return SIGNAL_POLL_MICROS;
	}

	const auto now = Clock::now();
	if (now >= deadline.end) {
		return first_wait ? 0u : UINT32_MAX;
	}

	const auto remaining =
	    std::chrono::ceil<std::chrono::microseconds>(deadline.end - now).count();
	return static_cast<uint32_t>(std::min<int64_t>(remaining, SIGNAL_POLL_MICROS));
}

void PollSignals(signal_poll_func_t signal_poll) {
	if (signal_poll != nullptr) {
		signal_poll();
	}
}

#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX && !defined(__APPLE__)

template <typename T>
int WaitLinux(volatile T* address, T expected, const WaitDeadline& deadline,
              signal_poll_func_t signal_poll) {
	bool       first_wait = true;

	for (;;) {
		if (ReadWord(address) != expected) {
			return OK;
		}
		const auto slice_micros = GetWaitSliceMicros(deadline, first_wait);
		if (slice_micros == UINT32_MAX) {
			return ReadWord(address) == expected ? KERNEL_ERROR_ETIMEDOUT : OK;
		}
		const timespec timeout = {
		    .tv_sec  = static_cast<time_t>(slice_micros / 1000000u),
		    .tv_nsec = static_cast<long>(slice_micros % 1000000u) * 1000L,
		};

		long result     = 0;
		int  wait_error = 0;
		result          = syscall(SYS_futex, const_cast<T*>(address), FUTEX_WAIT_PRIVATE,
		                          static_cast<uint32_t>(expected), &timeout, nullptr, 0);
		if (result != 0) {
			wait_error = errno;
		}
		if (result == 0 || wait_error == EAGAIN) {
			return OK;
		}
		if (wait_error != ETIMEDOUT && wait_error != EINTR) {
			return KERNEL_ERROR_EINVAL;
		}

		PollSignals(signal_poll);
		if (deadline.finite && Clock::now() >= deadline.end) {
			return ReadWord(address) == expected ? KERNEL_ERROR_ETIMEDOUT : OK;
		}
		first_wait = false;
	}
}

int WakeLinux(volatile void* address, int32_t count) {
	// The legacy FUTEX_WAKE path may wake one waiter when count is zero.
	if (count == 0) {
		return OK;
	}

	const auto result = syscall(SYS_futex, const_cast<void*>(address), FUTEX_WAKE_PRIVATE, count,
	                            nullptr, nullptr, 0);
	return result < 0 ? KERNEL_ERROR_EINVAL : OK;
}

#endif

struct ParkingLotWaiter {
	const volatile void*    address = nullptr;
	std::condition_variable cv;
	bool                    unparked = false;
};

struct ParkingLotBucket {
	std::mutex                   mutex;
	std::list<ParkingLotWaiter*> waiters;
};

struct ParkingLotTable {
	static constexpr size_t NUM_BUCKETS = 1024;
	ParkingLotBucket        buckets[NUM_BUCKETS];
};

static ParkingLotBucket& GetParkingLotBucket(const volatile void* addr) {
	static auto* table = new ParkingLotTable();
	uintptr_t val = reinterpret_cast<uintptr_t>(addr) >> 3;
	val ^= val >> 16;
	val *= 0x85ebca6b72d2ae35ULL;
	val ^= val >> 13;
	return table->buckets[val % ParkingLotTable::NUM_BUCKETS];
}

template <typename T>
int WaitParkingLot(volatile T* address, T expected, const WaitDeadline& deadline,
                   signal_poll_func_t signal_poll) {
	auto& bucket = GetParkingLotBucket(address);

	ParkingLotWaiter waiter;
	waiter.address  = const_cast<const volatile void*>(reinterpret_cast<volatile void*>(address));
	waiter.unparked = false;

	std::unique_lock<std::mutex> lock(bucket.mutex);

	// CRITICAL: Check value WHILE HOLDING THE BUCKET MUTEX
	// If the value was already modified by a waker, we must not sleep.
	if (ReadWord(address) != expected) {
		return OK;
	}

	bucket.waiters.push_back(&waiter);

	bool timed_out  = false;
	bool first_wait = true;

	while (!waiter.unparked) {
		const auto slice_micros = GetWaitSliceMicros(deadline, first_wait);
		if (slice_micros == UINT32_MAX || slice_micros == 0) {
			timed_out = true;
			break;
		}

		(void)waiter.cv.wait_for(lock, std::chrono::microseconds(slice_micros));

		if (Common::HostException::IsEmulationShuttingDown()) {
			bucket.waiters.remove(&waiter);
			lock.unlock();
			return KERNEL_ERROR_ETIMEDOUT;
		}

		if (waiter.unparked) {
			break;
		}

		if (ReadWord(address) != expected) {
			waiter.unparked = true;
			break;
		}

		if (deadline.finite && Clock::now() >= deadline.end) {
			timed_out = true;
			break;
		}

		if (signal_poll != nullptr) {
			lock.unlock();
			PollSignals(signal_poll);
			lock.lock();

			if (waiter.unparked || ReadWord(address) != expected) {
				waiter.unparked = true;
				break;
			}
		}

		first_wait = false;
	}

	bucket.waiters.remove(&waiter);
	lock.unlock();

	if (timed_out && !waiter.unparked) {
		return ReadWord(address) == expected ? KERNEL_ERROR_ETIMEDOUT : OK;
	}

	return OK;
}

int WakeParkingLot(volatile void* address, int32_t count) {
	if (count == 0) {
		return OK;
	}

	auto& bucket = GetParkingLotBucket(address);
	std::unique_lock<std::mutex> lock(bucket.mutex);

	int32_t         woken       = 0;
	const uintptr_t target_addr = reinterpret_cast<uintptr_t>(address);

	for (auto* waiter: bucket.waiters) {
		if (!waiter->unparked && reinterpret_cast<uintptr_t>(waiter->address) == target_addr) {
			waiter->unparked = true;
			waiter->cv.notify_one();
			woken++;
			if (count != INT_MAX && woken >= count) {
				break;
			}
		}
	}

	return OK;
}

template <typename T>
int WaitImpl(volatile T* address, T expected, const WaitDeadline& deadline,
             signal_poll_func_t signal_poll) {
	if (!IsValidWaitAddress(address)) {
		return KERNEL_ERROR_EINVAL;
	}

	// Full memory barrier before wait
	std::atomic_thread_fence(std::memory_order_seq_cst);

	int result = OK;
#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX && !defined(__APPLE__)
	result = WaitLinux(address, expected, deadline, signal_poll);
#else
	result = WaitParkingLot(address, expected, deadline, signal_poll);
#endif
	PollSignals(signal_poll);

	// Full memory barrier after wait: ensures ARM64 flushes speculative loads
	// and subsequent guest instructions observe all writes by the waker core.
	std::atomic_thread_fence(std::memory_order_seq_cst);

	return result;
}

} // namespace

int Wait32(volatile uint32_t* address, uint32_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll) {
	return WaitImpl(address, expected, MakeDeadline(timeout_micros), signal_poll);
}

int Wait64(volatile uint64_t* address, uint64_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll) {
	return WaitImpl(address, expected, MakeDeadline(timeout_micros), signal_poll);
}

int Wait64(volatile uint64_t* address, uint64_t expected, std::chrono::nanoseconds timeout,
           signal_poll_func_t signal_poll) {
	return WaitImpl(address, expected, MakeDeadline(timeout), signal_poll);
}

int Wake(volatile void* address, int32_t count) {
	if (!IsValidWakeAddress(address) || count < 0) {
		return KERNEL_ERROR_EINVAL;
	}

	// Full memory barrier before wake: ensure preceding stores are globally visible.
	std::atomic_thread_fence(std::memory_order_seq_cst);

	int result = OK;
#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX && !defined(__APPLE__)
	result = WakeLinux(address, count);
#else
	result = WakeParkingLot(address, count);
#endif

	// Full memory barrier after wake
	std::atomic_thread_fence(std::memory_order_seq_cst);

	return result;
}

} // namespace Libs::LibKernel::SyncOnAddress
