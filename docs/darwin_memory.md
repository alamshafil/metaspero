# Darwin Virtual and Direct Memory Architecture

This doc describes the design and runtime memory architecture of Metaspero on Apple Silicon (macOS and iOS), detailing direct memory aliasing, host memory footprint considerations, graphics cache lifecycles, and platform differences between macOS and iOS.

## 1. Architecture Overview

The PS5 uses a unified memory architecture where the CPU and GPU share a single 16 GB physical address space (with ~13.8 GB available to userland games). Games allocate "direct memory" (`sceKernelAllocateDirectMemory`) and map it into arbitrary virtual addresses (`sceKernelMapDirectMemory`). Multiple virtual ranges can alias the exact same physical memory block simultaneously.

On Darwin (macOS and iOS), Metaspero models this unified memory using Mach VM primitives:

- Backing store: Single shared anonymous Mach VM allocation (`MAP_ANON | MAP_SHARED`) representing the 13.8 GB guest physical address space.
- Guest mappings: Views remapped into guest virtual address space using Mach `vm_remap` (`VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE`).
- GPU access: Metal/Vulkan textures and buffers track guest CPU addresses via `PageManager` and synchronize dirty pages across frame boundaries.

## 2. Direct Memory Backing (`src/kernel/memoryAddressSpace.inc`)

### Anonymous Mach VM (`MAP_ANON | MAP_SHARED`)

Direct memory mappings require memory aliasing across multiple virtual addresses:

```cpp
auto* backing = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
m_backing_base = static_cast<uint8_t*>(backing);
```

When a game calls `sceKernelMapDirectMemory`, Kyty calls Mach `vm_remap` with `VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE` to alias the physical pages from `m_backing_base` into the guest virtual destination.

Note: Earlier builds experimented with sparse APFS temporary files. It is not recommended to use this approach.

### Dynamic Page Discarding (`madvise`)

When a game releases direct memory in `src/kernel/memory.cpp`:

```cpp
static int ReleaseDirectMemoryInternal(int64_t start, size_t len) {
    ...
    for (const auto& block: allocated_span) {
        ...
        EXIT_IF(!g_physical_memory->Release(block.start_addr, block.size, ...));
        if (g_guest_address_space != nullptr) {
            g_guest_address_space->DiscardPhysicalBacking(block.start_addr, block.size);
        }
    }
```

#### Why is this code needed

1. Direct memory is backed by a single 13.8 GB block (`m_backing_base`).
2. When a game allocates direct memory and writes to it, Darwin maps physical RAM pages to those addresses (demand-faulting them into the process footprint).
3. When the game finishes level 1, it calls `sceKernelReleaseDirectMemory` to free that 2 GB of memory so it can load level 2.
4. The Solution: `DiscardPhysicalBacking` invokes Darwin's `madvise`:

```cpp
void DiscardPhysicalBacking(uint64_t offset, uint64_t size) {
    const auto safe_size = std::min(size, m_size - offset);
#if defined(__APPLE__) && defined(MADV_FREE_REUSABLE)
    madvise(m_backing_base + offset, safe_size, MADV_FREE_REUSABLE);
#elif defined(MADV_DONTNEED)
    madvise(m_backing_base + offset, safe_size, MADV_DONTNEED);
#endif
}
```

`MADV_FREE_REUSABLE` marks the pages as reusable/purgeable by Darwin without modifying the virtual address space layout. If the game later re-allocates and accesses those addresses, the Darwin kernel transparently provides clean, zeroed physical pages on the next page fault.

### Clean Teardown

On shutdown, `GuestBackingStore::~GuestBackingStore` calls:

```cpp
if (m_backing_base != nullptr) {
    munmap(m_backing_base, m_size);
    m_backing_base = nullptr;
}
```

## 3. Vulkan & Metal Memory Allocator (`src/graphics/host_gpu/vma.cpp`)

Darwin Solution:

Metaspero dynamically scales the system reserve proportionally to actual installed unified memory:

```cpp
#if defined(KYTY_ARM64_APPLE) || (defined(__APPLE__) && defined(__arm64__))
    // Dynamic reserve scaled to memory: 2 GB on 8 GB Mac, 4 GB on 16 GB Mac, up to 8 GB max
    const uint64_t system_reserve = std::min<uint64_t>(budget / 4, 8ull * 1024 * 1024 * 1024);
    const auto available = budget > usage ? budget - usage : uint64_t {0};
    return available > system_reserve ? available - system_reserve : std::max<uint64_t>(budget / 4, 1024ull * 1024 * 1024);
#endif
```

This ensures 8 GB and 16 GB Macs, as well as high-end 64 GB+ Macs, automatically receive an optimal Vulkan budget.

## 4. Metal Resource Lifecycle & MoltenVK Settings

### Swapchain Autorelease Pool (`swapchain.cpp`)

MoltenVK presentation (`vkQueuePresentKHR`) creates Objective-C/Metal `CAMetalDrawable` instances. On Darwin, presenting outside an autorelease pool leaks drawables until the thread runloop drains:

```cpp
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    result = m_window.graphic_ctx.queue.presentKHR(&present);
    objc_autoreleasePoolPop(pool);
#else
    result = m_window.graphic_ctx.queue.presentKHR(&present);
#endif
```

Wrapping each presentation call ensures drawables are immediately recycled every frame.

### MoltenVK Environment Variables

In `KytyBridge.mm`:

```objc
setenv("MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS", "0", 1);
setenv("MVK_CONFIG_SHADER_COMPRESSION_ALGORITHM", "1", 1);
```

1. `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=0`
   - MoltenVK's default is `0` (on-demand encoding).
   - Explicitly locking this to `0` prevents accidental overrides to `1` or `2` (prefilling), which causes dereferencing crashes (`Signal 11` in `MVKCommandEncoder::encodeBarrier`) with Kyty's pipeline barrier recording.
2. `MVK_CONFIG_SHADER_COMPRESSION_ALGORITHM=1`
   - `0` = None (default in MoltenVK), `1` = Apple LZFSE compression.
   - Compressing the converted Metal Shading Language (MSL) pipeline cache in memory saves resident RAM on complex games with thousands of pipeline variants.

---

## 5. Resolution, 4K Output & Research Considerations (`videoOut.cpp`)

In `src/graphics/presentation/videoOut.cpp`:

```cpp
status->resolution = ((attribute3 & 4) != 0 && ctx->width < 3840 && ctx->height < 2160 ? 1u : 2u);
```

## 7. Future Research

For mobile optimization, consider:

1. Forced 1080p (`1u`) Profile for Mobile / Low-Spec GPUs
   - On an iPhone or iPad, the physical screen is small
   - Forcing `status->resolution = 1u` on mobile devices would save resident Metal render target memory and cut GPU usage theoretically
   - For macOS, native 4K output should remain the default to leverage high-resolution and external displays.
2. Host-Side Dynamic Texture Demotion
   - Rather than artificially capping guest direct memory, intercept host Vulkan `vk::Image` creations in `TextureCache` and downscale non-render-target textures (e.g. mip 0 clamping) when host memory is pressured.
3. Sparse Host Backing Eviction
   - Explore Mach VM `vm_deallocate` / `vm_allocate` on idle guest direct memory regions that have already been uploaded to GPU buffers, reloading from guest ELF or package data on fault.
