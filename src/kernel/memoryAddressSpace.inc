// Internal memory address-space helpers included by memory.cpp.
// Kept in the same translation unit because they depend on memory.cpp-local helpers.

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
// iOS address space limitation: 36-bit VA max (64 GB)
static constexpr uint64_t HOST_SYSTEM_MANAGED_MIN  = 0x0200000000ull; // 8 GB (above __PAGEZERO & frameworks)
static constexpr uint64_t HOST_SYSTEM_MANAGED_MAX  = 0x03ffffffffull; // 16 GB - 1
static constexpr uint64_t HOST_SYSTEM_RESERVED_MAX = 0x07ffffffffull; // 32 GB - 1
static constexpr uint64_t HOST_USER_MIN            = 0x0800000000ull; // 32 GB
static constexpr uint64_t HOST_USER_MAX            = 0x0fffffffe0ull; // 64 GB - 32
#elif (defined(KYTY_ARM64_APPLE) && !TARGET_OS_IPHONE) || (defined(__APPLE__) && (defined(__x86_64__) || defined(_M_X64)))
static constexpr uint64_t HOST_SYSTEM_MANAGED_MIN  = 0x0000040000ull;
static constexpr uint64_t HOST_SYSTEM_MANAGED_MAX  = 0x07ffffbfffull;
static constexpr uint64_t HOST_SYSTEM_RESERVED_MAX = 0x0fbfffffffull;
static constexpr uint64_t HOST_USER_MIN            = 0x7000000000ull;
static constexpr uint64_t HOST_USER_MAX            = 0xfbffffffffull;
#elif defined(__ANDROID__)
// Android arm64 kernels commonly expose a 39-bit (512 GiB) user address space, but 48-bit kernels
// provide full VA space. Probe whether addresses >= 512 GiB can be mapped with MAP_FIXED_NOREPLACE;
// if so, expand the guest ceiling to 1 TiB (0xfbffffffff), otherwise fall back to 256 GiB (0x3fffffffff).
static inline uint64_t ProbeAndroidUserMax() {
#if defined(MAP_FIXED_NOREPLACE)
	void* probe = ::mmap(reinterpret_cast<void*>(0x8000000000ull), 4096, PROT_NONE,
	                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (probe != MAP_FAILED) {
		::munmap(probe, 4096);
		return 0xfbffffffffull; // 48-bit VA kernel
	}
#endif
	return 0x3fffffffffull; // 39-bit VA kernel
}
static constexpr uint64_t HOST_SYSTEM_MANAGED_MIN  = 0x0000040000ull;
static constexpr uint64_t HOST_SYSTEM_MANAGED_MAX  = 0x07ffffbfffull;
static constexpr uint64_t HOST_SYSTEM_RESERVED_MAX = 0x0fffffffffull;
static constexpr uint64_t HOST_USER_MIN            = 0x1000000000ull;
static const uint64_t     HOST_USER_MAX            = ProbeAndroidUserMax();
#else
static constexpr uint64_t HOST_SYSTEM_MANAGED_MIN  = 0x0000040000ull;
static constexpr uint64_t HOST_SYSTEM_MANAGED_MAX  = 0x07ffffbfffull;
static constexpr uint64_t HOST_SYSTEM_RESERVED_MAX = 0x0fffffffffull;
static constexpr uint64_t HOST_USER_MIN            = 0x1000000000ull;
static constexpr uint64_t HOST_USER_MAX            = 0xfbffffffffull;
#endif

static bool GuestFreeRangeContains(uint64_t base, uint64_t range_size, uint64_t candidate,
                                   uint64_t size) {
	if (UINT64_MAX - base < range_size || candidate < base) {
		return false;
	}
	const auto range_end = base + range_size;
	return candidate <= range_end && size <= range_end - candidate;
}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
static bool CoalescePlaceholders(uint64_t vaddr, uint64_t size) {
	return VirtualFree(reinterpret_cast<void*>(vaddr), size,
	                   MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS) != 0;
}
#endif

class GuestBackingStore {
public:
	enum class FailureReason {
		None,
		FixedMapViewFailed,
		PlaceholderMapFailed,
		ProtectFailed,
		AddressRangeUnavailable,
	};

	explicit GuestBackingStore(uint64_t size): m_size(size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		m_handle = CreateFileMappingA(
		    INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE | SEC_COMMIT,
		    static_cast<DWORD>(m_size >> 32u), static_cast<DWORD>(m_size & 0xffffffffu), nullptr);
		if (m_handle == nullptr) {
			EXIT("could not reserve %" PRIu64 " MB for guest direct memory: 0x%08" PRIx32
			     ". Windows commits\n"
			     "this up front, so the paging file is usually what needs to be larger.\n",
			     m_size / (1024U * 1024U), static_cast<uint32_t>(GetLastError()));
		}
		m_backing_base = static_cast<uint8_t*>(
		    MapViewOfFile(m_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, m_size));
		if (m_backing_base == nullptr) {
			EXIT("direct-memory backing: writable alias failed: 0x%08" PRIx32 "\n",
			     static_cast<uint32_t>(GetLastError()));
		}
#elif defined(__APPLE__)
		auto* backing = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
		if (backing != MAP_FAILED) {
			m_backing_base = static_cast<uint8_t*>(backing);
		} else {
#if TARGET_OS_IPHONE
			m_backing_base = nullptr;
			m_phys_vaddr.resize(m_size >> PageBits, 0);
#else
			EXIT("direct-memory backing: anonymous shared memory allocation failed (size=%" PRIu64
			     " MB): %d\n",
			     m_size / (1024U * 1024U), errno);
#endif
		}
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
		m_fd = static_cast<int>(syscall(SYS_memfd_create, "KytyDirectMemory", 0));
		if (m_fd < 0) {
			EXIT("direct-memory backing: shared fd creation failed: %d\n", errno);
		}
		if (ftruncate(m_fd, static_cast<off_t>(m_size)) != 0) {
			EXIT("direct-memory backing: ftruncate to %" PRIu64 " MB failed: %d\n",
			     m_size / (1024U * 1024U), errno);
		}
		auto* backing = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
		if (backing == MAP_FAILED) {
			EXIT("direct-memory backing: writable alias failed: %d\n", errno);
		}
		m_backing_base = static_cast<uint8_t*>(backing);
#endif
	}

	// Guest views outlive the alias and the handle; the address-space owner releases them.
	~GuestBackingStore() {
		for (auto& bucket: m_pages) {
			delete[] bucket.load(std::memory_order_relaxed);
		}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		UnmapViewOfFile(m_backing_base);
		CloseHandle(m_handle);
#elif defined(__APPLE__)
		if (m_backing_base != nullptr) {
			munmap(m_backing_base, m_size);
		}
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
		munmap(m_backing_base, m_size);
		if (m_fd >= 0) {
			close(m_fd);
		}
#endif
	}

	// Drop the physical pages behind a released direct-memory range while keeping the address space
	// reserved for process-lifetime guest views.
	void DiscardPhysicalBacking(uint64_t offset, uint64_t size) {
		if (m_backing_base == nullptr || offset >= m_size || size == 0) {
			return;
		}
		const auto safe_size = std::min(size, m_size - offset);
#if defined(__APPLE__)
#if defined(MADV_FREE_REUSABLE)
		madvise(m_backing_base + offset, safe_size, MADV_FREE_REUSABLE);
#elif defined(MADV_DONTNEED)
		madvise(m_backing_base + offset, safe_size, MADV_DONTNEED);
#endif
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
#if defined(MADV_DONTNEED)
		madvise(m_backing_base + offset, safe_size, MADV_DONTNEED);
#endif
#endif
	}

	KYTY_CLASS_NO_COPY(GuestBackingStore);

	static const char* GetFailureReasonName(FailureReason reason) {
		switch (reason) {
			case FailureReason::None: return "n/a";
			case FailureReason::FixedMapViewFailed: return "fixed-map-view-failed";
			case FailureReason::PlaceholderMapFailed: return "placeholder-map-failed";
			case FailureReason::ProtectFailed: return "protect-failed";
			case FailureReason::AddressRangeUnavailable: return "address-range-unavailable";
			default: return "unknown";
		}
	}

	[[nodiscard]] uint64_t GetBackingBase() const {
		return reinterpret_cast<uint64_t>(m_backing_base);
	}

	[[nodiscard]] uint64_t GetBackingSize() const { return m_size; }

	void ZeroRange(uint64_t backing_offset, uint64_t size) {
		if (m_backing_base != nullptr) {
			std::memset(m_backing_base + backing_offset, 0, size);
		}
#if defined(__APPLE__) && TARGET_OS_IPHONE
		else {
			const auto first_page = backing_offset >> PageBits;
			const auto end_page   = (backing_offset + size) >> PageBits;
			for (auto phys_page = first_page;
			     phys_page < end_page && phys_page < m_phys_vaddr.size(); ++phys_page) {
				const auto vpage = m_phys_vaddr[phys_page];
				if (vpage != 0) {
					AccessMappedRun(uint64_t {vpage} << PageBits, uint64_t {1} << PageBits,
					                [](uint8_t* ptr) { std::memset(ptr, 0, uint64_t {1} << PageBits); });
				}
			}
		}
#endif
#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
		g_test_backing_bytes_zeroed += size;
#endif
	}

	// Callers hold the state lock, so no view changes under the write. Unbacked gaps are skipped,
	// mirroring sparse reads that zero-fill them.
	void WriteBacking(uint64_t vaddr, const void* data, uint64_t size) {
		const auto* source = static_cast<const uint8_t*>(data);
		ForEachRun(
		    vaddr, size,
		    [&](uint64_t offset, uint64_t start, uint64_t bytes) {
			    WithBackingRun(offset, start, bytes, [&](uint8_t* backing) {
				    std::memcpy(backing, source + (start - vaddr), bytes);
			    });
		    },
		    [](uint64_t, uint64_t) {});
	}

	// Needs no lock: page entries are atomic and buckets live as long as the store.
	[[nodiscard]] bool IsBacked(uint64_t vaddr, uint64_t size) const {
		if (size == 0 || UINT64_MAX - vaddr < size) {
			return false;
		}
		for (auto page = vaddr >> PageBits; page <= (vaddr + size - 1u) >> PageBits; ++page) {
			if (LoadPage(page) == 0) {
				return false;
			}
		}
		return true;
	}

	// Needs no lock either, as the alias stays mapped. A read racing an unmap copies the backing's
	// current bytes, or zeros for a page unmapped between the check and the copy.
	bool TryReadBacking(uint64_t vaddr, void* data, uint64_t size) const {
		if (data == nullptr || !IsBacked(vaddr, size)) {
			return false;
		}
		ReadSparseBacking(vaddr, data, size, [](uint64_t, uint8_t* destination, uint64_t bytes) {
			std::memset(destination, 0, bytes);
		});
		return true;
	}

	template <typename Gap>
	void ReadSparseBacking(uint64_t vaddr, void* data, uint64_t size, Gap&& gap) const {
		auto* destination = static_cast<uint8_t*>(data);
		ForEachRun(
		    vaddr, size,
		    [&](uint64_t offset, uint64_t start, uint64_t bytes) {
			    WithBackingRun(offset, start, bytes, [&](const uint8_t* backing) {
				    std::memcpy(destination + (start - vaddr), backing, bytes);
			    });
		    },
		    [&](uint64_t start, uint64_t bytes) {
			    gap(start, destination + (start - vaddr), bytes);
		    });
	}

	// Mapping and unmapping hold the state lock exclusively.
	bool MapFixed(uint64_t vaddr, uint64_t size, uint64_t backing_offset,
	              VirtualMemory::Mode mode, FailureReason* failure_reason = nullptr) {
#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
		if (const auto callback = g_test_before_backing_map) {
			g_test_before_backing_map = nullptr;
			callback(vaddr, size);
		}
#endif
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		if (!ReplacePlaceholderView(vaddr, size, backing_offset, mode, failure_reason)) {
			return false;
		}
#else
		if (!MapView(vaddr, size, backing_offset, mode, failure_reason)) {
			return false;
		}
#endif
		StorePages(vaddr, size, backing_offset);
		return true;
	}

	// The caller validated the span; failing here is an emulator fault.
	void Unmap(uint64_t vaddr, uint64_t size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		const auto end   = vaddr + size;
		uint64_t   views = 0;
		for (auto current = vaddr; current < end; ++views) {
			const auto view     = QueryView(current);
			const auto part_end = std::min(end, view.base + view.size);
			UnmapOne(view, current, part_end - current);
			current = part_end;
		}
		// Each unmapped file view becomes a separate Windows placeholder. The address-space owner
		// records this contiguous guest span as one free range, so normalize fragmented backing
		// views into one placeholder before publishing it there.
		EXIT_IF(views > 1 && !CoalescePlaceholders(vaddr, size));
#else
		// Replace only the requested range so concurrent accesses and GPU watch
		// protections on the surviving mappings remain intact.
		EXIT_IF(!UnmapViewPreservePlaceholder(reinterpret_cast<void*>(vaddr), size));
		ClearPages(vaddr, size);
#endif
	}

private:
	static constexpr uint64_t PageBits    = 14;
	static constexpr uint64_t PageMask    = (uint64_t {1} << PageBits) - 1u;
	static constexpr uint64_t BucketPages = uint64_t {1} << 16u;
	static constexpr uint64_t BucketCount =
	    ((kExtendedMemoryBase + kExtendedMemorySize) >> PageBits) / BucketPages;

	// Zero for an unbacked page, otherwise the backing page index plus one.
	[[nodiscard]] uint32_t LoadPage(uint64_t page) const {
		if (page / BucketPages >= BucketCount) {
			return 0;
		}
		const auto* entries = m_pages[page / BucketPages].load(std::memory_order_acquire);
		return entries != nullptr ? entries[page % BucketPages].load(std::memory_order_relaxed) : 0;
	}

	// Walks the span in runs of contiguous backing, as (backing offset, start, bytes), and runs of
	// unbacked pages, as (start, bytes).
	template <typename Region, typename Gap>
	void ForEachRun(uint64_t vaddr, uint64_t size, Region&& region, Gap&& gap) const {
		const auto end = vaddr + size;
		for (auto start = vaddr; start < end;) {
			const auto     entry    = LoadPage(start >> PageBits);
			const uint32_t step     = entry != 0 ? 1 : 0;
			auto           expected = entry + step;
			auto           stop     = (start | PageMask) + 1u;
			while (stop < end && LoadPage(stop >> PageBits) == expected) {
				stop += PageMask + 1u;
				expected += step;
			}
			stop = std::min(stop, end);
			if (entry != 0) {
				region((uint64_t {entry - 1u} << PageBits) + (start & PageMask), start,
				       stop - start);
			} else {
				gap(start, stop - start);
			}
			start = stop;
		}
	}

	void StorePages(uint64_t vaddr, uint64_t size, uint64_t backing_offset) {
		const auto end = vaddr + size;
		EXIT_IF(((vaddr | size | backing_offset) & PageMask) != 0 ||
		        (end >> PageBits) > BucketCount * BucketPages || backing_offset + size > m_size);
		auto entry = static_cast<uint32_t>(backing_offset >> PageBits) + 1u;
		for (auto page = vaddr >> PageBits; page < end >> PageBits; ++page, ++entry) {
			auto& bucket  = m_pages[page / BucketPages];
			auto* entries = bucket.load(std::memory_order_relaxed);
			if (entries == nullptr) {
				entries = new std::atomic<uint32_t>[BucketPages] {};
				bucket.store(entries, std::memory_order_release);
			}
			entries[page % BucketPages].store(entry, std::memory_order_relaxed);
#if defined(__APPLE__) && TARGET_OS_IPHONE
			if (m_backing_base == nullptr && (entry - 1u) < m_phys_vaddr.size()) {
				m_phys_vaddr[entry - 1u] = static_cast<uint32_t>(page);
			}
#endif
		}
	}

	void ClearPages(uint64_t vaddr, uint64_t size) {
		for (auto page = vaddr >> PageBits; page < (vaddr + size) >> PageBits; ++page) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
			if (m_backing_base == nullptr) {
				const auto entry = LoadPage(page);
				if (entry != 0 && (entry - 1u) < m_phys_vaddr.size() &&
				    m_phys_vaddr[entry - 1u] == static_cast<uint32_t>(page)) {
					m_phys_vaddr[entry - 1u] = 0;
				}
			}
#endif
			m_pages[page / BucketPages].load(std::memory_order_relaxed)[page % BucketPages].store(
			    0, std::memory_order_relaxed);
		}
	}

	template <typename Fn>
	void WithBackingRun(uint64_t offset, uint64_t start, uint64_t bytes, Fn&& fn) const {
		if (m_backing_base != nullptr) {
			fn(m_backing_base + offset);
			return;
		}
#if defined(__APPLE__) && TARGET_OS_IPHONE
		AccessMappedRun(start, bytes, [&](uint8_t* ptr) { fn(ptr); });
#else
		(void)start;
		(void)bytes;
#endif
	}

	static void SetFailure(FailureReason* out, FailureReason reason) {
		if (out != nullptr) {
			*out = reason;
		}
	}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static DWORD GetProtect(VirtualMemory::Mode mode) {
		switch (mode) {
			case VirtualMemory::Mode::Read: return PAGE_READONLY;
			case VirtualMemory::Mode::Write:
			case VirtualMemory::Mode::ReadWrite: return PAGE_READWRITE;
			case VirtualMemory::Mode::Execute: return PAGE_EXECUTE;
			case VirtualMemory::Mode::ExecuteRead: return PAGE_EXECUTE_READ;
			case VirtualMemory::Mode::ExecuteWrite:
			case VirtualMemory::Mode::ExecuteReadWrite: return PAGE_EXECUTE_READWRITE;
			case VirtualMemory::Mode::NoAccess:
			default: return PAGE_NOACCESS;
		}
	}

	// Collects the page runs of the span whose protection differs from the view's mode.
	static void QueryProtections(uint64_t vaddr, uint64_t size, VirtualMemory::Mode mode,
	                             std::vector<MEMORY_BASIC_INFORMATION>* out) {
		const auto end = vaddr + size;
		for (auto addr = vaddr; addr < end;) {
			MEMORY_BASIC_INFORMATION info {};
			EXIT_IF(VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info)) == 0);
			info.RegionSize = std::min<SIZE_T>(info.RegionSize, end - addr);
			if (info.Protect != GetProtect(mode)) {
				out->push_back(info);
			}
			addr += info.RegionSize;
		}
	}

	static bool ApplyProtection(void* ptr, uint64_t size, DWORD protect) {
		DWORD old_protect = 0;
		return VirtualProtect(ptr, size, protect, &old_protect) != 0;
	}

	static bool SplitPlaceholder(uint64_t vaddr, uint64_t size) {
		return VirtualFree(reinterpret_cast<void*>(vaddr), size,
		                   MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) != 0;
	}

	struct View {
		uint64_t base    = 0;
		uint64_t size    = 0;
		DWORD    protect = 0;
	};

	// The OS records a view as the regions sharing its allocation base; guest and tracker
	// protections split it into several.
	static View QueryView(uint64_t vaddr) {
		MEMORY_BASIC_INFORMATION info {};
		EXIT_IF(VirtualQuery(reinterpret_cast<void*>(vaddr), &info, sizeof(info)) == 0 ||
		        info.Type != MEM_MAPPED);
		View view {reinterpret_cast<uint64_t>(info.AllocationBase), 0, info.AllocationProtect};
		auto end = reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
		while (VirtualQuery(reinterpret_cast<void*>(end), &info, sizeof(info)) != 0 &&
		       reinterpret_cast<uint64_t>(info.AllocationBase) == view.base) {
			end += info.RegionSize;
		}
		view.size = end - view.base;
		return view;
	}

	void UnmapOne(const View& view, uint64_t vaddr, uint64_t size) {
		const uint64_t offset     = uint64_t {LoadPage(view.base >> PageBits) - 1u} << PageBits;
		const uint64_t left_size  = vaddr - view.base;
		const uint64_t right_addr = vaddr + size;
		const uint64_t right_size = view.base + view.size - right_addr;
		// Survivors are re-mapped read-write, executable if the view was, and the protections guest
		// mprotect and write tracking set on them are re-applied afterwards.
		const auto mode =
		    (view.protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0
		        ? VirtualMemory::Mode::ExecuteReadWrite
		        : VirtualMemory::Mode::ReadWrite;
		std::vector<MEMORY_BASIC_INFORMATION> protections;
		QueryProtections(view.base, left_size, mode, &protections);
		QueryProtections(right_addr, right_size, mode, &protections);

		EXIT_IF(!UnmapViewPreservePlaceholder(reinterpret_cast<void*>(view.base), view.size));
		ClearPages(vaddr, size);
		EXIT_IF(left_size != 0 && !SplitPlaceholder(view.base, left_size));
		EXIT_IF(right_size != 0 && !SplitPlaceholder(vaddr, size));
		EXIT_IF(left_size != 0 && !MapFixed(view.base, left_size, offset, mode));
		EXIT_IF(right_size != 0 &&
		        !MapFixed(right_addr, right_size, offset + (right_addr - view.base), mode));
		for (const auto& info: protections) {
			EXIT_IF(!ApplyProtection(info.BaseAddress, info.RegionSize, info.Protect));
		}
	}

	bool ReplacePlaceholderView(uint64_t vaddr, uint64_t size, uint64_t backing_offset,
	                            VirtualMemory::Mode mode, FailureReason* failure_reason) {
		const DWORD map_protect =
		    mode == VirtualMemory::Mode::NoAccess ? PAGE_READWRITE : GetProtect(mode);
		void* ptr =
		    MapViewOfFile3(m_handle, GetCurrentProcess(), reinterpret_cast<void*>(vaddr),
		                   backing_offset, size, MEM_REPLACE_PLACEHOLDER, map_protect, nullptr, 0);
		if (ptr == nullptr || reinterpret_cast<uint64_t>(ptr) != vaddr) {
			if (ptr != nullptr) {
				UnmapViewOfFile2(GetCurrentProcess(), ptr, MEM_PRESERVE_PLACEHOLDER);
			}
			SetFailure(failure_reason, FailureReason::PlaceholderMapFailed);
			return false;
		}

		if (mode == VirtualMemory::Mode::NoAccess && !ApplyProtection(ptr, size, PAGE_NOACCESS)) {
			UnmapViewOfFile2(GetCurrentProcess(), ptr, MEM_PRESERVE_PLACEHOLDER);
			SetFailure(failure_reason, FailureReason::ProtectFailed);
			return false;
		}
		return true;
	}

	static bool UnmapViewPreservePlaceholder(void* vaddr, uint64_t /*size*/) {
		return UnmapViewOfFile2(GetCurrentProcess(), vaddr, MEM_PRESERVE_PLACEHOLDER) != 0;
	}

	HANDLE m_handle = nullptr;
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
	static int GetProtect(VirtualMemory::Mode mode) {
		switch (mode) {
			case VirtualMemory::Mode::Read: return PROT_READ;
			case VirtualMemory::Mode::Write:
			case VirtualMemory::Mode::ReadWrite: return PROT_READ | PROT_WRITE;
			case VirtualMemory::Mode::Execute: return PROT_EXEC;
			case VirtualMemory::Mode::ExecuteRead: return PROT_EXEC | PROT_READ;
			case VirtualMemory::Mode::ExecuteWrite:
			case VirtualMemory::Mode::ExecuteReadWrite: return PROT_EXEC | PROT_READ | PROT_WRITE;
			case VirtualMemory::Mode::NoAccess:
			default: return PROT_NONE;
		}
	}

	bool MapView(uint64_t vaddr, uint64_t size, uint64_t backing_offset, VirtualMemory::Mode mode,
	             FailureReason* failure_reason) {
#if defined(__APPLE__)
		if (m_backing_base != nullptr) {
			vm_address_t target   = static_cast<vm_address_t>(vaddr);
			vm_prot_t    cur_prot = 0;
			vm_prot_t    max_prot = 0;
			kern_return_t kr = vm_remap(
			    mach_task_self(), &target, size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
			    mach_task_self(),
			    static_cast<vm_address_t>(
			        reinterpret_cast<uintptr_t>(m_backing_base + backing_offset)),
			    FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE);
			if (kr != KERN_SUCCESS) {
				SetFailure(failure_reason, FailureReason::FixedMapViewFailed);
				return false;
			}
			if (mode != VirtualMemory::Mode::NoAccess &&
			    mprotect(reinterpret_cast<void*>(vaddr), size, GetProtect(mode)) != 0) {
				SetFailure(failure_reason, FailureReason::ProtectFailed);
				return false;
			}
			return true;
		}
#if TARGET_OS_IPHONE
		constexpr uint64_t page_size = uint64_t {1} << PageBits;
		const auto         end       = vaddr + size;
		auto               cur_vaddr = vaddr;
		auto               cur_phys  = backing_offset >> PageBits;
		while (cur_vaddr < end) {
			const auto existing_vpage =
			    cur_phys < m_phys_vaddr.size() ? m_phys_vaddr[cur_phys] : 0u;
			auto run_end  = cur_vaddr + page_size;
			auto run_phys = cur_phys + 1u;
			if (existing_vpage == 0) {
				while (run_end < end &&
				       (run_phys >= m_phys_vaddr.size() || m_phys_vaddr[run_phys] == 0)) {
					run_end += page_size;
					++run_phys;
				}
				const auto run_size = run_end - cur_vaddr;
				if (mmap(reinterpret_cast<void*>(cur_vaddr), run_size, PROT_READ | PROT_WRITE,
				         MAP_SHARED | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) {
					SetFailure(failure_reason, FailureReason::FixedMapViewFailed);
					return false;
				}
			} else {
				auto expected_vpage = existing_vpage + 1u;
				while (run_end < end && run_phys < m_phys_vaddr.size() &&
				       m_phys_vaddr[run_phys] == expected_vpage) {
					run_end += page_size;
					++run_phys;
					++expected_vpage;
				}
				const auto   run_size = run_end - cur_vaddr;
				vm_address_t target   = static_cast<vm_address_t>(cur_vaddr);
				vm_prot_t    cur_prot = 0;
				vm_prot_t    max_prot = 0;
				kern_return_t kr = vm_remap(
				    mach_task_self(), &target, run_size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
				    mach_task_self(), static_cast<vm_address_t>(uint64_t {existing_vpage} << PageBits),
				    FALSE, &cur_prot, &max_prot, VM_INHERIT_NONE);
				if (kr != KERN_SUCCESS) {
					SetFailure(failure_reason, FailureReason::FixedMapViewFailed);
					return false;
				}
			}
			cur_vaddr = run_end;
			cur_phys  = run_phys;
		}
		if (GetProtect(mode) != (PROT_READ | PROT_WRITE) &&
		    mprotect(reinterpret_cast<void*>(vaddr), size, GetProtect(mode)) != 0) {
			SetFailure(failure_reason, FailureReason::ProtectFailed);
			return false;
		}
		return true;
#endif
#else
		if (mmap(reinterpret_cast<void*>(vaddr), size, GetProtect(mode), MAP_SHARED | MAP_FIXED,
		         m_fd, static_cast<off_t>(backing_offset)) == MAP_FAILED) {
			SetFailure(failure_reason, FailureReason::FixedMapViewFailed);
			return false;
		}
		return true;
#endif
	}

#if defined(__APPLE__) && TARGET_OS_IPHONE
	template <typename Fn>
	static void AccessMappedRun(uint64_t start, uint64_t bytes, Fn&& fn) {
		const auto   page_base = start & ~PageMask;
		const auto   page_off  = start - page_base;
		const auto   map_size  = ((page_off + bytes + PageMask) & ~PageMask);
		vm_address_t tmp       = 0;
		vm_prot_t    cur_prot  = 0;
		vm_prot_t    max_prot  = 0;
		if (vm_remap(mach_task_self(), &tmp, static_cast<vm_size_t>(map_size), 0, VM_FLAGS_ANYWHERE,
		             mach_task_self(), static_cast<vm_address_t>(page_base), FALSE, &cur_prot,
		             &max_prot, VM_INHERIT_NONE) == KERN_SUCCESS) {
			vm_protect(mach_task_self(), tmp, static_cast<vm_size_t>(map_size), FALSE,
			           VM_PROT_READ | VM_PROT_WRITE);
			fn(reinterpret_cast<uint8_t*>(tmp) + page_off);
			vm_deallocate(mach_task_self(), tmp, static_cast<vm_size_t>(map_size));
		} else {
			fn(reinterpret_cast<uint8_t*>(start));
		}
	}
#endif

	// Replace the file view atomically instead of exposing an unmapped hole in the
	// process-lifetime guest reservation.
	static bool UnmapViewPreservePlaceholder(void* vaddr, uint64_t size) {
		return mmap(vaddr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) !=
		       MAP_FAILED;
	}

	int m_fd = -1;
#endif

	uint8_t*                                                     m_backing_base = nullptr;
	uint64_t                                                     m_size         = 0;
#if defined(__APPLE__) && TARGET_OS_IPHONE
	std::vector<uint32_t>                                        m_phys_vaddr;
#endif
	std::array<std::atomic<std::atomic<uint32_t>*>, BucketCount> m_pages {};
};

// Callers change it with the state lock held exclusively and query it under the operation lock or
// the state lock. TryWriteBacking, CopySparseMemory and ProtectTransient take the state lock shared
// themselves.
class GuestAddressSpace {
public:
	explicit GuestAddressSpace(uint64_t backing_size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		SYSTEM_INFO info {};
		GetSystemInfo(&info);
		m_granularity = info.dwAllocationGranularity;
#endif
		ReserveGuestRegions();
		m_backing = new GuestBackingStore(backing_size);
	}

	~GuestAddressSpace() {
		delete m_backing;
		m_backing = nullptr;
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		for (const auto& [start, size]: m_owned) {
			const auto end     = start + size;
			auto       current = start;
			while (current < end) {
				MEMORY_BASIC_INFORMATION info {};
				EXIT_IF(VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) ==
				        0);
				const auto region_start = reinterpret_cast<uint64_t>(info.BaseAddress);
				const auto region_end   = region_start + static_cast<uint64_t>(info.RegionSize);
				if (info.Type == MEM_MAPPED) {
					EXIT_IF(UnmapViewOfFile2(GetCurrentProcess(), info.AllocationBase,
					                         MEM_PRESERVE_PLACEHOLDER) == 0);
					continue;
				}
				if (info.State == MEM_COMMIT) {
					EXIT_IF(VirtualFree(reinterpret_cast<void*>(region_start), info.RegionSize,
					                    MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) == 0);
					continue;
				}
				if (info.State == MEM_RESERVE) {
					EXIT_IF(VirtualFree(info.AllocationBase, 0, MEM_RELEASE) == 0);
				}
				current = region_end;
			}
		}
#elif defined(KYTY_ARM64_APPLE)
		for (const auto& [start, size]: m_owned) {
			vm_deallocate(mach_task_self(), static_cast<vm_address_t>(start),
			              static_cast<vm_size_t>(size));
		}
#else
		for (const auto& [start, size]: m_owned) {
			munmap(reinterpret_cast<void*>(start), size);
		}
#endif
	}

	KYTY_CLASS_NO_COPY(GuestAddressSpace);

	[[nodiscard]] uint64_t GetGranularity() const { return m_granularity; }
	[[nodiscard]] uint64_t GetBackingBase() const { return m_backing->GetBackingBase(); }
	[[nodiscard]] uint64_t GetBackingSize() const { return m_backing->GetBackingSize(); }

	void ZeroBacking(uint64_t backing_offset, uint64_t size) {
		m_backing->ZeroRange(backing_offset, size);
	}

	// The owned ranges are fixed at construction.
	[[nodiscard]] bool Owns(uint64_t vaddr, uint64_t size) const {
		if (size == 0 || UINT64_MAX - vaddr < size) {
			return false;
		}
		return std::any_of(m_owned.begin(), m_owned.end(), [vaddr, size](const auto& range) {
			return vaddr >= range.first && vaddr + size <= range.first + range.second;
		});
	}

#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
	[[nodiscard]] bool OverlapsOwned(uint64_t vaddr, uint64_t size) const {
		return std::any_of(m_owned.begin(), m_owned.end(), [vaddr, size](const auto& range) {
			return VirtualRangesOverlap(vaddr, size, range.first, range.second);
		});
	}
#endif

	// Writes the backed bytes of a range that holds no private mappings.
	bool TryWriteBacking(uint64_t vaddr, const void* data, uint64_t size) {
		if (!Owns(vaddr, size)) {
			return false;
		}
		std::shared_lock state(g_memory_state_mutex);
		if (OverlapsPrivateUnlocked(vaddr, size)) {
			return false;
		}
		m_backing->WriteBacking(vaddr, data, size);
		return true;
	}

	bool TryReadBacking(uint64_t vaddr, void* data, uint64_t size) {
		return m_backing->TryReadBacking(vaddr, data, size);
	}

	[[nodiscard]] bool IsBacked(uint64_t vaddr, uint64_t size) const {
		return m_backing->IsBacked(vaddr, size);
	}

	template <bool copy_private>
	bool CopySparseMemory(uint64_t vaddr, void* data, uint64_t size) {
		if (m_backing->TryReadBacking(vaddr, data, size)) {
			return true;
		}
		if (data == nullptr || !Owns(vaddr, size)) {
			return false;
		}
		std::shared_lock state(g_memory_state_mutex);
#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
		if (g_test_backing_read != nullptr) {
			g_test_backing_read(vaddr, size);
		}
#endif
		if constexpr (!copy_private) {
			if (OverlapsPrivateUnlocked(vaddr, size)) {
				return false;
			}
		}
		m_backing->ReadSparseBacking(
		    vaddr, data, size, [&](uint64_t start, uint8_t* destination, uint64_t bytes) {
			    if constexpr (copy_private) {
				    // Backing gaps can contain private mappings or reservations only.
				    CopyPrivateUnlocked(start, destination, bytes);
			    } else {
				    std::memset(destination, 0, bytes);
			    }
		    });
		return true;
	}

	bool Protect(uint64_t vaddr, uint64_t size, VirtualMemory::Mode mode) {
		return ProtectMappedUnlocked(vaddr, size, mode);
	}

	// Tracker protections only read m_mapped; the page tracker orders them per page.
	bool ProtectTransient(uint64_t vaddr, uint64_t size, VirtualMemory::Mode mode) {
		std::shared_lock state(g_memory_state_mutex);
		return ProtectMappedUnlocked(vaddr, size, mode);
	}

	bool MapBacking(uint64_t vaddr, uint64_t size, uint64_t backing_offset,
	                VirtualMemory::Mode               mode,
	                GuestBackingStore::FailureReason* failure_reason = nullptr) {
#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX
		const bool replacing = ContainsMappedUnlocked(vaddr, size, MappingType::Backing);
#else
		constexpr bool replacing = false;
#endif
		if (!replacing && !ConsumeUnlocked(vaddr, size)) {
			if (failure_reason != nullptr) {
				*failure_reason = GuestBackingStore::FailureReason::AddressRangeUnavailable;
			}
			return false;
		}
		const auto host_mode = VirtualMemory::IsExecute(mode)
		                           ? VirtualMemory::Mode::ExecuteReadWrite
		                           : VirtualMemory::Mode::ReadWrite;
		if (m_backing->MapFixed(vaddr, size, backing_offset, host_mode, failure_reason)) {
			if (!replacing) {
				AddMappedUnlocked(vaddr, size, MappingType::Backing);
			}
			return true;
		}
		if (!replacing) {
			AddFreeUnlocked(vaddr, size);
		}
		return false;
	}

	bool UnmapBacking(uint64_t vaddr, uint64_t size) {
		if (!ContainsMappedUnlocked(vaddr, size, MappingType::Backing)) {
			return false;
		}
		m_backing->Unmap(vaddr, size);
		RemoveMappedUnlocked(vaddr, size);
		AddFreeUnlocked(vaddr, size);
		return true;
	}

	// Callers hold g_memory_state_mutex exclusively.
	void DiscardPhysicalBacking(uint64_t offset, uint64_t size) {
		if (m_backing != nullptr) {
			m_backing->DiscardPhysicalBacking(offset, size);
		}
	}

	uint64_t FindFreeAligned(uint64_t search_start, uint64_t search_end, uint64_t size,
	                         uint64_t alignment) {
		alignment = std::max<uint64_t>(alignment, PageSize());
		return FindFreeAlignedUnlocked(search_start, search_end, size, alignment);
	}

	bool ContainsFree(uint64_t vaddr, uint64_t size) {
		return FindContaining(vaddr, size) != m_free.end();
	}

	bool Commit(uint64_t vaddr, uint64_t size, VirtualMemory::Mode mode) {
		if (!ConsumeUnlocked(vaddr, size)) {
			return false;
		}
		const auto host_mode = VirtualMemory::IsExecute(mode)
		                           ? VirtualMemory::Mode::ExecuteReadWrite
		                           : VirtualMemory::Mode::ReadWrite;

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		auto* ptr = VirtualAlloc2(GetCurrentProcess(), reinterpret_cast<void*>(vaddr), size,
		                          MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
		                          GetProtect(host_mode), nullptr, 0);
		if (ptr == nullptr || reinterpret_cast<uint64_t>(ptr) != vaddr) {
			if (ptr != nullptr) {
				VirtualFree(ptr, 0, MEM_RELEASE);
			}
			AddFreeUnlocked(vaddr, size);
			return false;
		}
		AddMappedUnlocked(vaddr, size, MappingType::Private);
		return true;
#else
		if (VirtualMemory::Protect(vaddr, size, host_mode)) {
			AddMappedUnlocked(vaddr, size, MappingType::Private);
			return true;
		}
		AddFreeUnlocked(vaddr, size);
		return false;
#endif
	}

	bool ReleaseCommitted(uint64_t vaddr, uint64_t size) {
		if (!IsPageAligned(vaddr) || !IsPageAligned(size)) {
			return false;
		}
		if (!ContainsMappedUnlocked(vaddr, size, MappingType::Private)) {
			return false;
		}
		const auto end     = vaddr + size;
		auto       current = vaddr;
		std::vector<std::pair<uint64_t, uint64_t>> regions;
		while (current < end) {
			const auto it       = FindMappedContainingUnlocked(current);
			const auto part_end = std::min(end, it->second.vaddr + it->second.size);
			regions.emplace_back(current, part_end - current);
			current = part_end;
		}
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		for (const auto& [region_vaddr, region_size]: regions) {
			EXIT_IF(VirtualFree(reinterpret_cast<void*>(region_vaddr), region_size,
			                    MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) == 0);
		}
		// ReleaseCommitted also publishes the aggregate span as one logical free range.
		EXIT_IF(regions.size() > 1 && !CoalescePlaceholders(vaddr, size));
#else
		for (const auto& [region_vaddr, region_size]: regions) {
			RestorePlaceholder(region_vaddr, region_size);
		}
#endif
		RemoveMappedUnlocked(vaddr, size);
		AddFreeUnlocked(vaddr, size);
		return true;
	}

	bool SelfTest() {
		auto addr = FindFreeAligned(HOST_SYSTEM_MANAGED_MIN, HOST_SYSTEM_MANAGED_MAX + 1u,
		                            PageSize(), PageSize());
		if (addr == 0) {
			addr = FindFreeAligned(HOST_USER_MIN, HOST_USER_MAX + 1u, PageSize(), PageSize());
		}
		if (addr == 0) {
			addr = FindFreeAligned(HOST_SYSTEM_MANAGED_MIN, HOST_USER_MAX + 1u, PageSize(),
			                       PageSize());
		}
		if (addr == 0) {
			addr = FindFreeAligned(0, UINT64_MAX, PageSize(), PageSize());
		}
		if (addr == 0) {
			LOGF_COLOR(Log::Color::Red,
			           "\t guest address-space self-test: 16 KiB range unavailable\n");
			return false;
		}
		if (!Commit(addr, PageSize(), VirtualMemory::Mode::ReadWrite)) {
			LOGF_COLOR(Log::Color::Red, "\t guest address-space self-test: commit failed\n");
			return false;
		}

		auto* ptr = reinterpret_cast<uint64_t*>(addr);
		*ptr      = 0x4b59545950484f4cull; // "KYTYPHOL"

		if (!ReleaseCommitted(addr, PageSize())) {
			LOGF_COLOR(Log::Color::Red,
			           "\t guest address-space self-test: release-committed failed\n");
			return false;
		}

		LOGF_COLOR(Log::Color::Green, "\t guest address-space self-test: ok\n");
		return true;
	}

private:
	enum class MappingType { Backing, Private };

	struct MappedRegion {
		uint64_t    vaddr = 0;
		uint64_t    size  = 0;
		MappingType type  = MappingType::Private;
	};

	static constexpr uint64_t PageSize() { return 0x4000; }

	static uint64_t AlignUp(uint64_t value, uint64_t alignment) {
		if (alignment == 0) {
			return value;
		}
		const auto remainder = value % alignment;
		if (remainder == 0) {
			return value;
		}
		const auto addend = alignment - remainder;
		return UINT64_MAX - value < addend ? UINT64_MAX : value + addend;
	}

	static uint64_t AlignDown(uint64_t value, uint64_t alignment) {
		return value & ~(alignment - 1u);
	}

	[[nodiscard]] static bool IsPageAligned(uint64_t value) {
		return (value & (PageSize() - 1u)) == 0;
	}

	bool ProtectMappedUnlocked(uint64_t vaddr, uint64_t size, VirtualMemory::Mode mode) {
		const auto end = vaddr + size;
		auto       it  = m_mapped.upper_bound(vaddr);
		if (it != m_mapped.begin()) {
			--it;
		}
		for (; it != m_mapped.end() && it->second.vaddr < end; ++it) {
			const auto& region      = it->second;
			const auto  range_start = std::max(vaddr, region.vaddr);
			const auto  range_end   = std::min(end, region.vaddr + region.size);
			if (range_start >= range_end) {
				continue;
			}
			if (!VirtualMemory::Protect(range_start, range_end - range_start, mode)) {
				return false;
			}
		}
		return true;
	}

	bool ConsumeUnlocked(uint64_t vaddr, uint64_t size) {
		const auto it = FindContaining(vaddr, size);
		if (it == m_free.end() || !SplitUnlocked(it, vaddr, size)) {
			return false;
		}
		m_free.erase(vaddr);
		return true;
	}

	std::map<uint64_t, MappedRegion>::iterator FindMappedContainingUnlocked(uint64_t vaddr) {
		auto next = m_mapped.upper_bound(vaddr);
		if (next == m_mapped.begin()) {
			return m_mapped.end();
		}
		auto it = std::prev(next);
		return vaddr < it->second.vaddr + it->second.size ? it : m_mapped.end();
	}

	bool ContainsMappedUnlocked(uint64_t vaddr, uint64_t size, MappingType type) {
		const auto end     = vaddr + size;
		auto       current = vaddr;
		while (current < end) {
			const auto it = FindMappedContainingUnlocked(current);
			if (it == m_mapped.end() || it->second.type != type) {
				return false;
			}
			current = std::min(end, it->second.vaddr + it->second.size);
		}
		return true;
	}

	[[nodiscard]] bool OverlapsPrivateUnlocked(uint64_t vaddr, uint64_t size) const {
		auto mapping = m_mapped.upper_bound(vaddr);
		if (mapping != m_mapped.begin()) {
			--mapping;
		}
		for (; mapping != m_mapped.end() && mapping->second.vaddr < vaddr + size; ++mapping) {
			const auto& region = mapping->second;
			if (region.type == MappingType::Private && region.vaddr + region.size > vaddr) {
				return true;
			}
		}
		return false;
	}

	// Copies the private mappings of a span and zero-fills the rest.
	void CopyPrivateUnlocked(uint64_t vaddr, uint8_t* destination, uint64_t size) const {
		const auto end    = vaddr + size;
		auto       cursor = vaddr;
		auto       it     = m_mapped.upper_bound(vaddr);
		if (it != m_mapped.begin()) {
			--it;
		}
		for (; it != m_mapped.end() && it->second.vaddr < end; ++it) {
			const auto start = std::max(cursor, it->second.vaddr);
			const auto stop  = std::min(end, it->second.vaddr + it->second.size);
			if (start < stop) {
				std::memset(destination + (cursor - vaddr), 0, start - cursor);
				std::memcpy(destination + (start - vaddr), reinterpret_cast<const void*>(start),
				            stop - start);
				cursor = stop;
			}
		}
		std::memset(destination + (cursor - vaddr), 0, end - cursor);
	}

	void AddMappedUnlocked(uint64_t vaddr, uint64_t size, MappingType type) {
		m_mapped.emplace(vaddr, MappedRegion {vaddr, size, type});
	}

	void RemoveMappedUnlocked(uint64_t vaddr, uint64_t size) {
		const auto end     = vaddr + size;
		auto       current = vaddr;
		while (current < end) {
			const auto it           = FindMappedContainingUnlocked(current);
			const auto original     = it->second;
			const auto original_end = original.vaddr + original.size;
			const auto part_end     = std::min(end, original_end);
			m_mapped.erase(it);
			if (original.vaddr < current) {
				AddMappedUnlocked(original.vaddr, current - original.vaddr, original.type);
			}
			if (part_end < original_end) {
				AddMappedUnlocked(part_end, original_end - part_end, original.type);
			}
			current = part_end;
		}
	}

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static DWORD GetProtect(VirtualMemory::Mode mode) {
		switch (mode) {
			case VirtualMemory::Mode::Read: return PAGE_READONLY;
			case VirtualMemory::Mode::Write:
			case VirtualMemory::Mode::ReadWrite: return PAGE_READWRITE;
			case VirtualMemory::Mode::Execute: return PAGE_EXECUTE;
			case VirtualMemory::Mode::ExecuteRead: return PAGE_EXECUTE_READ;
			case VirtualMemory::Mode::ExecuteWrite:
			case VirtualMemory::Mode::ExecuteReadWrite: return PAGE_EXECUTE_READWRITE;
			case VirtualMemory::Mode::NoAccess:
			default: return PAGE_NOACCESS;
		}
	}
#endif

	void ReserveGuestRegions() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
		const std::array<std::pair<uint64_t, uint64_t>, 2> regions {{
		    {HOST_SYSTEM_MANAGED_MIN, HOST_SYSTEM_RESERVED_MAX + 1u},
		    {HOST_USER_MIN, HOST_USER_MAX + 1u},
		}};
#else
		const std::array<std::pair<uint64_t, uint64_t>, 3> regions {{
		    {HOST_SYSTEM_MANAGED_MIN, HOST_SYSTEM_RESERVED_MAX + 1u},
		    {HOST_USER_MIN, HOST_USER_MAX + 1u},
		    {kExtendedMemoryBase, kExtendedMemoryBase + kExtendedMemorySize},
		}};
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		constexpr uint64_t MinimumRegionSize = 0x1000000;
		const auto         process           = GetCurrentProcess();
		for (const auto& [guest_start, guest_end]: regions) {
			auto next_addr = guest_start;
			while (next_addr < guest_end) {
				MEMORY_BASIC_INFORMATION info {};
				if (VirtualQuery(reinterpret_cast<const void*>(next_addr), &info, sizeof(info)) == 0) {
					EXIT("failed to inspect guest address space at 0x%016" PRIx64 "\n", next_addr);
				}

				const auto region_base = reinterpret_cast<uint64_t>(info.BaseAddress);
				const auto region_end = std::min<uint64_t>(
				    guest_end, region_base + static_cast<uint64_t>(info.RegionSize));
				const auto reserve_start =
				    AlignUp(std::max(region_base, guest_start), m_granularity);
				const auto reserve_end = AlignDown(region_end, m_granularity);

				if (info.State == MEM_FREE && reserve_end > reserve_start &&
				    reserve_end - reserve_start > MinimumRegionSize) {
					auto* ptr = VirtualAlloc2(
					    process, reinterpret_cast<void*>(reserve_start), reserve_end - reserve_start,
					    MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
					if (reinterpret_cast<uint64_t>(ptr) != reserve_start) {
						EXIT("failed to reserve guest address space at 0x%016" PRIx64
						     ", size 0x%016" PRIx64 "\n",
						     reserve_start, reserve_end - reserve_start);
					}
					AddFreeUnlocked(reserve_start, reserve_end - reserve_start);
					m_owned.emplace_back(reserve_start, reserve_end - reserve_start);
				}

				next_addr = region_end;
			}
		}
#else
#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX && !defined(__APPLE__)
		std::vector<std::pair<uint64_t, uint64_t>> occupied;
		FILE*                                      maps = fopen("/proc/self/maps", "r");
		EXIT_IF(maps == nullptr);

		char line[512];
		while (fgets(line, sizeof(line), maps) != nullptr) {
			unsigned long long mapping_start = 0;
			unsigned long long mapping_end   = 0;
			if (sscanf(line, "%llx-%llx", &mapping_start, &mapping_end) == 2) {
				occupied.emplace_back(static_cast<uint64_t>(mapping_start),
				                      static_cast<uint64_t>(mapping_end));
			}
		}
		fclose(maps);

		auto reserve_range = [this](uint64_t start, uint64_t end) {
			start = AlignUp(start, PageSize());
			end   = AlignDown(end, PageSize());
			if (end <= start) {
				return;
			}

			const auto size = end - start;
			int        flags = MAP_PRIVATE | MAP_ANON | MAP_NORESERVE;
#if defined(KYTY_LINKED_GUEST_ADDRESS_SPACE)
			flags |= MAP_FIXED;
#elif defined(MAP_FIXED_NOREPLACE)
			flags |= MAP_FIXED_NOREPLACE;
#endif

			void* ptr = mmap(reinterpret_cast<void*>(start), size, PROT_NONE, flags, -1, 0);
			if (ptr == MAP_FAILED || reinterpret_cast<uint64_t>(ptr) != start) {
				if (ptr != MAP_FAILED) {
					munmap(ptr, size);
				}
				return;
			}

			AddFreeUnlocked(start, size);
			// Free ranges merge across region boundaries, so owned ranges do as well.
			if (!m_owned.empty() && m_owned.back().first + m_owned.back().second == start) {
				m_owned.back().second += size;
			} else {
				m_owned.emplace_back(start, size);
			}
		};

		for (const auto& [region_start, region_end]: regions) {
			auto current = region_start;

			for (const auto& [mapping_start, mapping_end]: occupied) {
				if (mapping_end <= current) {
					continue;
				}
				if (mapping_start >= region_end) {
					break;
				}

				if (mapping_start > current) {
					reserve_range(current, std::min(mapping_start, region_end));
				}

				current = std::max(current, mapping_end);
				if (current >= region_end) {
					break;
				}
			}

			if (current < region_end) {
				reserve_range(current, region_end);
			}
		}
#elif defined(__APPLE__) && !defined(KYTY_LINKED_GUEST_ADDRESS_SPACE)
		std::vector<std::pair<uint64_t, uint64_t>> occupied;
		// 0 to 4GB is __PAGEZERO on Darwin 64-bit
		occupied.emplace_back(0, 0x100000000ULL);

		vm_address_t address = 0x100000000ULL;
		vm_size_t    size    = 0;
		vm_region_basic_info_data_64_t info {};
		mach_msg_type_number_t count       = VM_REGION_BASIC_INFO_COUNT_64;
		mach_port_t            object_name = MACH_PORT_NULL;
		while (true) {
			count = VM_REGION_BASIC_INFO_COUNT_64;
			if (vm_region_64(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
			                 reinterpret_cast<vm_region_info_t>(&info), &count, &object_name) != KERN_SUCCESS) {
				break;
			}
			if (MACH_PORT_VALID(object_name)) {
				mach_port_deallocate(mach_task_self(), object_name);
			}
			occupied.emplace_back(static_cast<uint64_t>(address),
			                      static_cast<uint64_t>(address + size));
			address += size;
		}
		std::sort(occupied.begin(), occupied.end());

		auto reserve_chunk = [this](uint64_t start, uint64_t chunk_size) -> bool {
#if defined(KYTY_ARM64_APPLE)
			vm_address_t target = static_cast<vm_address_t>(start);
			if (vm_allocate(mach_task_self(), &target, static_cast<vm_size_t>(chunk_size), VM_FLAGS_FIXED) != KERN_SUCCESS) {
				return false;
			}
			vm_protect(mach_task_self(), target, static_cast<vm_size_t>(chunk_size), FALSE, VM_PROT_NONE);
			AddFreeUnlocked(static_cast<uint64_t>(target), chunk_size);
			m_owned.emplace_back(static_cast<uint64_t>(target), chunk_size);
			return true;
#else
			void* ptr = mmap(reinterpret_cast<void*>(start), chunk_size, PROT_NONE,
			                 MAP_PRIVATE | MAP_ANON, -1, 0);
			if (ptr == MAP_FAILED || reinterpret_cast<uint64_t>(ptr) != start) {
				if (ptr != MAP_FAILED) {
					munmap(ptr, chunk_size);
				}
				return false;
			}
			AddFreeUnlocked(start, chunk_size);
			m_owned.emplace_back(start, chunk_size);
			return true;
#endif
		};

		auto reserve_range = [&](uint64_t start, uint64_t end) {
			start = AlignUp(start, PageSize());
			end   = AlignDown(end, PageSize());
			if (start == 0 || end <= start) {
				return;
			}

			constexpr uint64_t CHUNK_SIZE  = 256ULL * 1024ULL * 1024ULL;
			constexpr uint64_t SMALL_CHUNK = 32ULL * 1024ULL * 1024ULL;
			uint64_t           curr        = start;
			while (curr < end) {
				uint64_t sz = std::min(end - curr, CHUNK_SIZE);
				if (!reserve_chunk(curr, sz)) {
					if (sz > SMALL_CHUNK && reserve_chunk(curr, SMALL_CHUNK)) {
						curr += SMALL_CHUNK;
						continue;
					}
					curr += sz;
					continue;
				}
				curr += sz;
			}
		};

		for (const auto& [region_start, region_end]: regions) {
			auto current = region_start;
			for (const auto& [mapping_start, mapping_end]: occupied) {
				if (mapping_end <= current) {
					continue;
				}
				if (mapping_start >= region_end) {
					break;
				}
				if (mapping_start > current) {
					reserve_range(current, std::min(mapping_start, region_end));
				}
				current = std::max(current, mapping_end);
				if (current >= region_end) {
					break;
				}
			}
			if (current < region_end) {
				reserve_range(current, region_end);
			}
		}

		// Fallback: reserve gaps between host mappings if the predefined regions were unusable.
		if (m_free.empty()) {
			for (size_t i = 0; i + 1 < occupied.size(); ++i) {
				uint64_t gap_start = occupied[i].second;
				uint64_t gap_end   = occupied[i + 1].first;
				if (gap_end > gap_start && (gap_end - gap_start) >= (32ULL * 1024ULL * 1024ULL)) {
					reserve_range(gap_start, gap_end);
				}
			}
			if (!occupied.empty()) {
				uint64_t gap_start = occupied.back().second;
				uint64_t gap_end   = 0x0200000000ULL;
				if (gap_end > gap_start && (gap_end - gap_start) >= (32ULL * 1024ULL * 1024ULL)) {
					reserve_range(gap_start, gap_end);
				}
			}
		}

		uint64_t max_free_contiguous = 0;
		for (const auto& [base, rsz]: m_free) {
			max_free_contiguous = std::max(max_free_contiguous, rsz);
		}

		// Large direct-memory heaps need one large contiguous run even when the predefined ranges
		// are fragmented by host mappings.
		constexpr uint64_t REQUIRED_CONTIGUOUS = 3584ULL * 1024ULL * 1024ULL;
		if (max_free_contiguous < REQUIRED_CONTIGUOUS) {
			constexpr uint64_t POOL_SIZES[] = {
			    3584ULL * 1024ULL * 1024ULL,
			    3200ULL * 1024ULL * 1024ULL,
			    3072ULL * 1024ULL * 1024ULL,
			};
			for (uint64_t try_size: POOL_SIZES) {
				if (try_size <= max_free_contiguous) {
					break;
				}
				vm_address_t pool_addr = 0;
				if (vm_allocate(mach_task_self(), &pool_addr, static_cast<vm_size_t>(try_size),
				                VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
					vm_protect(mach_task_self(), pool_addr, static_cast<vm_size_t>(try_size), FALSE,
					           VM_PROT_NONE);
					AddFreeUnlocked(static_cast<uint64_t>(pool_addr), try_size);
					m_owned.emplace_back(static_cast<uint64_t>(pool_addr), try_size);
					break;
				}
			}
		}
#else
		for (const auto& [start, end]: regions) {
			int flags = MAP_PRIVATE | MAP_ANON | MAP_NORESERVE;
#if defined(KYTY_LINKED_GUEST_ADDRESS_SPACE)
			flags |= MAP_FIXED;
#elif defined(MAP_FIXED_NOREPLACE)
			flags |= MAP_FIXED_NOREPLACE;
#endif
			void* ptr = mmap(reinterpret_cast<void*>(start), end - start, PROT_NONE, flags, -1, 0);
			if (ptr == MAP_FAILED || reinterpret_cast<uint64_t>(ptr) != start) {
				if (ptr != MAP_FAILED) {
					munmap(ptr, end - start);
				}
				EXIT("failed to reserve guest address space at 0x%016" PRIx64
				     ", size 0x%016" PRIx64 "\n",
				     start, end - start);
			}
			AddFreeUnlocked(start, end - start);
			m_owned.emplace_back(start, end - start);
		}
#endif
#endif
	}

	uint64_t FindFreeAlignedUnlocked(uint64_t search_start, uint64_t search_end, uint64_t size,
	                                 uint64_t alignment) const {
		// Include the predecessor because search_start can lie inside a free range.
		auto it = m_free.upper_bound(search_start);
		if (it != m_free.begin()) {
			--it;
		}
		for (; it != m_free.end() && it->first < search_end; ++it) {
#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
			g_test_free_range_visits.fetch_add(1, std::memory_order_relaxed);
#endif
			const auto& [base, range_size] = *it;
			const auto candidate           = AlignUp(std::max(base, search_start), alignment);
			if (candidate < search_end && size <= search_end - candidate &&
			    GuestFreeRangeContains(base, range_size, candidate, size)) {
				return candidate;
			}
		}
		return 0;
	}

	std::map<uint64_t, uint64_t>::iterator FindContaining(uint64_t vaddr, uint64_t size) {
		auto next = m_free.upper_bound(vaddr);
		if (next == m_free.begin()) {
			return m_free.end();
		}
		auto it = std::prev(next);
		return GuestFreeRangeContains(it->first, it->second, vaddr, size) ? it : m_free.end();
	}

	bool SplitUnlocked(std::map<uint64_t, uint64_t>::iterator it, uint64_t vaddr, uint64_t size) {
		const auto base = it->first;
		const auto end  = it->first + it->second;
		if (base != vaddr) {
			const auto left_size = vaddr - base;
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
			if (VirtualFree(reinterpret_cast<void*>(base), left_size,
			                MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) == 0) {
				LOGF_COLOR(Log::Color::Red,
				           "\t guest placeholder split failed: base=0x%016" PRIx64
				           " size=0x%016" PRIx64 " requested=0x%016" PRIx64
				           " request_size=0x%016" PRIx64 " error=0x%08" PRIx32 "\n",
				           base, end - base, vaddr, size, static_cast<uint32_t>(GetLastError()));
				return false;
			}
#endif
			it->second = left_size;
			it         = m_free.emplace(vaddr, end - vaddr).first;
		}

		if (it->second != size) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
			if (VirtualFree(reinterpret_cast<void*>(it->first), size,
			                MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) == 0) {
				LOGF_COLOR(Log::Color::Red,
				           "\t guest placeholder split failed: base=0x%016" PRIx64
				           " size=0x%016" PRIx64 " requested=0x%016" PRIx64
				           " request_size=0x%016" PRIx64 " error=0x%08" PRIx32 "\n",
				           it->first, it->second, vaddr, size,
				           static_cast<uint32_t>(GetLastError()));
				return false;
			}
#endif
			const auto right_start = it->first + size;
			const auto right_size  = it->second - size;
			it->second             = size;
			m_free.emplace(right_start, right_size);
		}

		return true;
	}

	void AddFreeUnlocked(uint64_t vaddr, uint64_t size) {
		auto [it, inserted] = m_free.emplace(vaddr, size);
		if (!inserted) {
			it->second = size;
		}

		auto base = it->first;
		auto end  = it->first + it->second;

		auto first = it;
		while (first != m_free.begin()) {
			auto prev = std::prev(first);
			if (prev->first + prev->second != base) {
				break;
			}
			base  = prev->first;
			first = prev;
		}

		auto last = std::next(it);
		while (last != m_free.end() && end == last->first) {
			end  = last->first + last->second;
			last = std::next(last);
		}

		if (base != it->first || end != it->first + it->second) {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
			if (CoalescePlaceholders(base, end - base)) {
				m_free.erase(first, last);
				m_free.emplace(base, end - base);
			}
#else
			m_free.erase(first, last);
			m_free.emplace(base, end - base);
#endif
		}
	}

	void RestorePlaceholder(uint64_t vaddr, uint64_t size) {
#if KYTY_PLATFORM != KYTY_PLATFORM_WINDOWS
		// The address remains owned for the process lifetime; MAP_FIXED only replaces one of our
		// mappings and never acquires a new guest address.
		void* ptr = mmap(reinterpret_cast<void*>(vaddr), size, PROT_NONE,
		                 MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		EXIT_IF(ptr == MAP_FAILED);
#else
		(void)vaddr;
		(void)size;
#endif
	}

	std::map<uint64_t, uint64_t>               m_free;
	std::map<uint64_t, MappedRegion>            m_mapped;
	std::vector<std::pair<uint64_t, uint64_t>> m_owned;
	uint64_t                                   m_granularity = 0x10000;
	GuestBackingStore*                         m_backing     = nullptr;
};
