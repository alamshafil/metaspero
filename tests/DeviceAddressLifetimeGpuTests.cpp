// Regression for the MoltenVK DeviceLost ("Invalid Resource", code 9) seen in the game.
//
// MoltenVK makes every live device-address buffer resident in each command buffer that binds a shader
// using physical storage buffer addresses, without retaining it. A buffer destroyed while such command
// buffers are still queued aborts them, although none of them uses it. The emulator therefore waits for
// all submitted work before destroying a device-address buffer on MoltenVK
// (DeviceAddressBufferDestructionWaitsForQueue, BufferCache::DeleteBuffer).
//
// Default run: applies that rule with 60 queued command buffers and must not lose the device (exit 0,
// 1 on failure).
// --expect-driver-hazard: destroys the buffer once its only user has provably finished and expects exactly
// VK_ERROR_DEVICE_LOST, documenting why the rule exists (exit 0 reproduced, 1 not reproduced).
// Exit 2: inconclusive (the wait for the user of the buffer failed, or another error occurred).
//
//   device_address_lifetime_gpu_tests [--expect-driver-hazard]   (driver: SDL_VULKAN_LIBRARY)
#include "graphics/host_gpu/vulkanCommon.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static_assert(Libs::Graphics::DeviceAddressBufferDestructionWaitsForQueue(vk::DriverId::eMoltenvk),
              "MoltenVK must keep device-address buffers alive until queued work completes");
static_assert(!Libs::Graphics::DeviceAddressBufferDestructionWaitsForQueue(vk::DriverId::eAmdProprietary),
              "other drivers keep the per-use retire tick");

namespace {

constexpr uint32_t kHeavySpirv[] = {
	0x07230203,0x00010500,0x0008000b,0x00000050,0x00000000,0x00020011,0x00000001,0x00020011,
	0x0000000b,0x00020011,0x000014e3,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,
	0x00000000,0x0003000e,0x000014e4,0x00000001,0x0007000f,0x00000005,0x00000004,0x6e69616d,
	0x00000000,0x0000000b,0x00000014,0x00060010,0x00000004,0x00000011,0x00000040,0x00000001,
	0x00000001,0x00030003,0x00000002,0x000001cc,0x00070004,0x455f4c47,0x625f5458,0x65666675,
	0x65725f72,0x65726566,0x0065636e,0x000d0004,0x455f4c47,0x735f5458,0x65646168,0x78655f72,
	0x63696c70,0x615f7469,0x68746972,0x6974656d,0x79745f63,0x5f736570,0x36746e69,0x00000034,
	0x00040005,0x00000004,0x6e69616d,0x00000000,0x00030005,0x00000008,0x00000069,0x00080005,
	0x0000000b,0x475f6c67,0x61626f6c,0x766e496c,0x7461636f,0x496e6f69,0x00000044,0x00040005,
	0x00000012,0x68737550,0x00000000,0x00040006,0x00000012,0x00000000,0x00637273,0x00040006,
	0x00000012,0x00000001,0x00747364,0x00060006,0x00000012,0x00000002,0x72657469,0x6f697461,
	0x0000736e,0x00050006,0x00000012,0x00000003,0x6e756f63,0x00000074,0x00030005,0x00000014,
	0x00006370,0x00040005,0x00000021,0x64726f57,0x00000073,0x00040006,0x00000021,0x00000000,
	0x00000076,0x00030005,0x00000023,0x00637273,0x00030005,0x00000029,0x00636361,0x00030005,
	0x0000002b,0x0000006b,0x00040047,0x0000000b,0x0000000b,0x0000001c,0x00030047,0x00000012,
	0x00000002,0x00050048,0x00000012,0x00000000,0x00000023,0x00000000,0x00050048,0x00000012,
	0x00000001,0x00000023,0x00000008,0x00050048,0x00000012,0x00000002,0x00000023,0x00000010,
	0x00050048,0x00000012,0x00000003,0x00000023,0x00000014,0x00040047,0x00000020,0x00000006,
	0x00000004,0x00030047,0x00000021,0x00000002,0x00050048,0x00000021,0x00000000,0x00000023,
	0x00000000,0x00030047,0x00000023,0x000014ec,0x00040047,0x0000004f,0x0000000b,0x00000019,
	0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00040015,0x00000006,0x00000020,
	0x00000000,0x00040020,0x00000007,0x00000007,0x00000006,0x00040017,0x00000009,0x00000006,
	0x00000003,0x00040020,0x0000000a,0x00000001,0x00000009,0x0004003b,0x0000000a,0x0000000b,
	0x00000001,0x0004002b,0x00000006,0x0000000c,0x00000000,0x00040020,0x0000000d,0x00000001,
	0x00000006,0x00040015,0x00000011,0x00000040,0x00000000,0x0006001e,0x00000012,0x00000011,
	0x00000011,0x00000006,0x00000006,0x00040020,0x00000013,0x00000009,0x00000012,0x0004003b,
	0x00000013,0x00000014,0x00000009,0x00040015,0x00000015,0x00000020,0x00000001,0x0004002b,
	0x00000015,0x00000016,0x00000003,0x00040020,0x00000017,0x00000009,0x00000006,0x00020014,
	0x00000018,0x0003001e,0x00000021,0x00000018,0x00040020,0x00000022,0x000014e2,0x00000021,
	0x00040020,0x00000024,0x00000007,0x00000022,0x0004002b,0x00000006,0x0000002c,0x0019660d,
	0x00040020,0x00000031,0x000014e2,0x00000006,0x0004002b,0x00000015,0x00000034,0x00000001,
	0x0004002b,0x00000006,0x00000039,0x00000001,0x0004002b,0x00000015,0x00000043,0x00000002,
	0x00040020,0x00000044,0x00000009,0x00000011,0x0003001d,0x0000001f,0x00000006,0x00040020,
	0x0000004b,0x00000001,0x00000006,0x0004002b,0x00000006,0x0000004c,0x00000002,0x00040020,
	0x0000004d,0x00000007,0x00000011,0x0004002b,0x00000006,0x0000004e,0x00000040,0x00050036,
	0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003b,0x00000007,
	0x00000008,0x00000007,0x0004003b,0x00000024,0x00000023,0x00000007,0x0004003b,0x00000007,
	0x00000029,0x00000007,0x0004003b,0x00000007,0x0000002b,0x00000007,0x00050041,0x0000000d,
	0x0000000e,0x0000000b,0x0000000c,0x0004003d,0x00000006,0x0000000f,0x0000000e,0x0003003e,
	0x00000008,0x0000000f,0x00050041,0x00000017,0x00000018,0x00000014,0x00000016,0x0004003d,
	0x00000006,0x00000019,0x00000018,0x0004003d,0x00000006,0x0000001a,0x00000008,0x000500b0,
	0x00000018,0x0000001b,0x0000001a,0x00000019,0x00040012,0x0000001b,0x0000001c,0x0000001d,
	0x000200f8,0x0000001c,0x000100fd,0x000200f8,0x0000001d,0x0004003d,0x00000011,0x00000020,
	0x00000014,0x00040079,0x00000022,0x00000025,0x00000020,0x0003003e,0x00000023,0x00000025,
	0x0004003d,0x00000006,0x0000002a,0x00000008,0x0003003e,0x00000029,0x0000002a,0x0003003e,
	0x0000002b,0x0000000c,0x000200f8,0x0000002d,0x0004003d,0x00000006,0x0000002e,0x0000002b,
	0x00050041,0x00000017,0x00000030,0x00000014,0x00000034,0x0004003d,0x00000006,0x00000035,
	0x00000030,0x000500b1,0x00000018,0x00000036,0x0000002e,0x00000035,0x000500f7,0x0000002d,
	0x0000002f,0x00000036,0x00000000,0x000200f8,0x0000002e,0x0004003d,0x00000006,0x00000037,
	0x00000029,0x00050080,0x00000006,0x00000038,0x00000037,0x0000002c,0x0004003d,0x00000006,
	0x0000003a,0x00000029,0x0004003d,0x00000006,0x0000003b,0x0000002b,0x00050080,0x00000006,
	0x0000003c,0x0000003a,0x0000003b,0x00050041,0x00000017,0x0000003d,0x00000014,0x00000016,
	0x0004003d,0x00000006,0x0000003e,0x0000003d,0x00050089,0x00000006,0x0000003f,0x0000003c,
	0x0000003e,0x0004003d,0x00000022,0x00000040,0x00000023,0x00050041,0x00000031,0x00000041,
	0x00000040,0x0000003f,0x0004003d,0x00000006,0x00000042,0x00000041,0x00050080,0x00000006,
	0x00000043,0x00000038,0x00000042,0x0003003e,0x00000029,0x00000043,0x0004003d,0x00000006,
	0x00000044,0x0000002b,0x00050080,0x00000006,0x00000045,0x00000044,0x00000039,0x0003003e,
	0x0000002b,0x00000045,0x000200f8,0x0000002f,0x0004003d,0x00000006,0x00000046,0x00000029,
	0x00050041,0x00000044,0x00000047,0x00000014,0x00000043,0x0004003d,0x00000011,0x00000048,
	0x00000047,0x00040079,0x00000022,0x00000049,0x00000048,0x0004003d,0x00000006,0x0000004a,
	0x00000008,0x00050041,0x00000031,0x0000004c,0x00000049,0x0000004a,0x0003003e,0x0000004c,
	0x00000046,0x000100fd,0x00010038
};

void Check(VkResult result, const char* message) {
	if (result != VK_SUCCESS) {
		std::fprintf(stderr, "DeviceAddressLifetimeGpuTests: failed: %s (result=%d)\n", message, result);
		std::abort();
	}
}

#define DL(name) name = (PFN_##name)vkGetDeviceProcAddr(g_dev, #name)

VkDevice g_dev = VK_NULL_HANDLE;
VkPhysicalDeviceMemoryProperties g_mp {};

uint32_t FindMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) {
	for (uint32_t i = 0; i < g_mp.memoryTypeCount; ++i) {
		if ((filter & (1u << i)) && (g_mp.memoryTypes[i].propertyFlags & flags) == flags) {
			return i;
		}
	}
	return UINT32_MAX;
}

struct Mem {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint64_t address = 0;
};

#define FN(name) PFN_##name name
FN(vkGetInstanceProcAddr); FN(vkEnumerateInstanceExtensionProperties); FN(vkCreateInstance); FN(vkEnumeratePhysicalDevices); FN(vkGetPhysicalDeviceMemoryProperties);
FN(vkCreateDevice); FN(vkGetDeviceProcAddr); FN(vkGetDeviceQueue); FN(vkCreateBuffer); FN(vkDestroyBuffer);
FN(vkGetBufferMemoryRequirements); FN(vkAllocateMemory); FN(vkFreeMemory); FN(vkBindBufferMemory);
FN(vkGetBufferDeviceAddress); FN(vkCreateShaderModule); FN(vkCreatePipelineLayout); FN(vkCreateComputePipelines);
FN(vkCreateCommandPool); FN(vkAllocateCommandBuffers); FN(vkBeginCommandBuffer); FN(vkEndCommandBuffer);
FN(vkCmdBindPipeline); FN(vkCmdPushConstants); FN(vkCmdDispatch); FN(vkQueueSubmit);
FN(vkCreateSemaphore); FN(vkWaitSemaphores); FN(vkQueueWaitIdle); FN(vkResetCommandPool);

Mem MakeBuffer(VkDeviceSize size) {
	Mem m;
	VkBufferCreateInfo bi {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bi.size = size;
	bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	Check(vkCreateBuffer(g_dev, &bi, nullptr, &m.buffer), "vkCreateBuffer");
	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(g_dev, m.buffer, &mr);
	VkMemoryAllocateFlagsInfo fi {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
	fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo ai {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	ai.pNext = &fi;
	ai.allocationSize = mr.size;
	ai.memoryTypeIndex = FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Check(vkAllocateMemory(g_dev, &ai, nullptr, &m.memory), "vkAllocateMemory");
	Check(vkBindBufferMemory(g_dev, m.buffer, m.memory, 0), "vkBindBufferMemory");
	VkBufferDeviceAddressInfo di {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
	di.buffer = m.buffer;
	m.address = vkGetBufferDeviceAddress(g_dev, &di);
	return m;
}

} // namespace

int main(int argc, char** argv) {
	const char* const library = std::getenv("SDL_VULKAN_LIBRARY");
	const bool        early   = argc > 1 && std::strcmp(argv[1], "--expect-driver-hazard") == 0;
	const int         trials  = early ? 5 : 1;
	const int         queued  = 60;
	void*       lib     = dlopen(library != nullptr ? library : "libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
	if (!lib) { std::printf("dlopen failed\n"); return 2; }
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	if (!vkGetInstanceProcAddr) { std::printf("vkGetInstanceProcAddr missing; inconclusive\n"); return 2; }
#define IL(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name)
	VkInstance instance = VK_NULL_HANDLE;
	IL(vkCreateInstance); IL(vkEnumerateInstanceExtensionProperties);
	VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ii {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ii.pApplicationInfo = &app;
	uint32_t extension_count = 0;
	Check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr), "vkEnumerateInstanceExtensionProperties count");
	std::vector<VkExtensionProperties> extensions(extension_count);
	Check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()), "vkEnumerateInstanceExtensionProperties");
	const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
	for (uint32_t i = 0; i < extension_count; ++i) {
		if (std::strcmp(extensions[i].extensionName, portability) == 0) {
			ii.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
			ii.enabledExtensionCount = 1;
			ii.ppEnabledExtensionNames = &portability;
			break;
		}
	}


	Check(vkCreateInstance(&ii, nullptr, &instance), "vkCreateInstance");
	IL(vkEnumeratePhysicalDevices); IL(vkGetPhysicalDeviceMemoryProperties); IL(vkCreateDevice); IL(vkGetDeviceProcAddr);
	uint32_t n = 0;
	Check(vkEnumeratePhysicalDevices(instance, &n, nullptr), "vkEnumeratePhysicalDevices count");
	if (n == 0) { std::printf("No Vulkan physical device; inconclusive\n"); return 2; }
	std::vector<VkPhysicalDevice> devices(n, VK_NULL_HANDLE);
	Check(vkEnumeratePhysicalDevices(instance, &n, devices.data()), "vkEnumeratePhysicalDevices");
	if (n == 0 || devices[0] == VK_NULL_HANDLE) { std::printf("No Vulkan physical device; inconclusive\n"); return 2; }
	VkPhysicalDevice phys = devices[0];
	vkGetPhysicalDeviceMemoryProperties(phys, &g_mp);
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qi {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueCount = 1; qi.pQueuePriorities = &prio;
	VkPhysicalDeviceBufferDeviceAddressFeatures bda {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
	bda.bufferDeviceAddress = VK_TRUE;
	VkPhysicalDeviceTimelineSemaphoreFeatures tsf {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
	tsf.timelineSemaphore = VK_TRUE; tsf.pNext = &bda;
	VkDeviceCreateInfo di {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	di.pNext = &tsf;
	di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
	Check(vkCreateDevice(phys, &di, nullptr, &g_dev), "vkCreateDevice");
	DL(vkGetDeviceQueue); DL(vkCreateBuffer); DL(vkDestroyBuffer); DL(vkGetBufferMemoryRequirements);
	DL(vkAllocateMemory); DL(vkFreeMemory); DL(vkBindBufferMemory); DL(vkGetBufferDeviceAddress);
	DL(vkCreateShaderModule); DL(vkCreatePipelineLayout); DL(vkCreateComputePipelines);
	DL(vkCreateCommandPool); DL(vkAllocateCommandBuffers); DL(vkBeginCommandBuffer); DL(vkEndCommandBuffer);
	DL(vkCmdBindPipeline); DL(vkCmdPushConstants); DL(vkCmdDispatch); DL(vkQueueSubmit);
	DL(vkCreateSemaphore); DL(vkWaitSemaphores); DL(vkQueueWaitIdle); DL(vkResetCommandPool);
	VkQueue queue; vkGetDeviceQueue(g_dev, 0, 0, &queue);

	VkShaderModuleCreateInfo si {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; si.codeSize = sizeof(kHeavySpirv); si.pCode = kHeavySpirv;
	VkShaderModule module; Check(vkCreateShaderModule(g_dev, &si, nullptr, &module), "vkCreateShaderModule");
	VkPushConstantRange pr {VK_SHADER_STAGE_COMPUTE_BIT, 0, 24};
	VkPipelineLayoutCreateInfo li {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; li.pushConstantRangeCount = 1; li.pPushConstantRanges = &pr;
	VkPipelineLayout layout; Check(vkCreatePipelineLayout(g_dev, &li, nullptr, &layout), "vkCreatePipelineLayout");
	VkComputePipelineCreateInfo ci {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
	ci.layout = layout;
	VkPipeline pipeline; Check(vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline), "vkCreateComputePipelines");

	VkCommandPoolCreateInfo pi {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	VkCommandPool pool; Check(vkCreateCommandPool(g_dev, &pi, nullptr, &pool), "vkCreateCommandPool");
	VkSemaphoreTypeCreateInfo st {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; st.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	VkSemaphoreCreateInfo sci {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; sci.pNext = &st;
	VkSemaphore timeline; Check(vkCreateSemaphore(g_dev, &sci, nullptr, &timeline), "vkCreateSemaphore");

	const uint32_t count = 1u << 20;
	// Each command buffer writes its own destination region, so the only hazard left is the lifetime of x.
	const VkDeviceSize region = VkDeviceSize {count} * 4;
	Mem src = MakeBuffer(region), dst = MakeBuffer(region * VkDeviceSize(queued + 1));
	uint64_t value = 0;
	for (int t = 0; t < trials; ++t) {
		Mem x = MakeBuffer(count * 4);  // only command buffer 0 reads it
		std::vector<VkCommandBuffer> cbs(queued + 1);
		VkCommandBufferAllocateInfo ai {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = uint32_t(cbs.size());
		Check(vkAllocateCommandBuffers(g_dev, &ai, cbs.data()), "vkAllocateCommandBuffers");
		for (size_t i = 0; i < cbs.size(); ++i) {
			VkCommandBufferBeginInfo bi {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			Check(vkBeginCommandBuffer(cbs[i], &bi), "vkBeginCommandBuffer");
			struct { uint64_t s, d; uint32_t it, n; } push {i == 0 ? x.address : src.address, dst.address + region * i, 256u, count};
			vkCmdBindPipeline(cbs[i], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdPushConstants(cbs[i], layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
			vkCmdDispatch(cbs[i], count / 64, 1, 1);
			Check(vkEndCommandBuffer(cbs[i]), "vkEndCommandBuffer");
		}
		const uint64_t first = value + 1;
		for (size_t i = 0; i < cbs.size(); ++i) {
			const uint64_t signal = ++value;
			VkTimelineSemaphoreSubmitInfo ts {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
			ts.signalSemaphoreValueCount = 1; ts.pSignalSemaphoreValues = &signal;
			VkSubmitInfo sub {VK_STRUCTURE_TYPE_SUBMIT_INFO}; sub.pNext = &ts;
			sub.commandBufferCount = 1; sub.pCommandBuffers = &cbs[i];
			sub.signalSemaphoreCount = 1; sub.pSignalSemaphores = &timeline;
			Check(vkQueueSubmit(queue, 1, &sub, VK_NULL_HANDLE), "vkQueueSubmit");
		}
		VkSemaphoreWaitInfo wi {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO}; wi.semaphoreCount = 1; wi.pSemaphores = &timeline;
		const uint64_t wait_value = early ? first : value;  // early: only the command buffer that used x
		wi.pValues = &wait_value;
		const VkResult w = vkWaitSemaphores(g_dev, &wi, 10000000000ull);
		if (w != VK_SUCCESS) {
			// x may still be in use: destroying it would test nothing.
			std::printf("trial %d mode=%s wait=%d: inconclusive, x kept alive\n", t, early ? "early" : "late", w);
			return 2;
		}
		vkDestroyBuffer(g_dev, x.buffer, nullptr);
		vkFreeMemory(g_dev, x.memory, nullptr);
		const VkResult idle = vkQueueWaitIdle(queue);
		std::printf("trial %d mode=%s wait=%d idle=%d\n", t, early ? "early" : "late", w, idle);
		if (early && idle == VK_ERROR_DEVICE_LOST) {
			std::printf("DeviceAddressLifetimeGpuTests: driver hazard reproduced\n");
			return 0;
		}
		if (idle != VK_SUCCESS) {
			std::printf("DeviceAddressLifetimeGpuTests: %s (idle=%d)\n", early ? "unexpected error" : "FAILED", idle);
			return early ? 2 : 1;
		}
		vkResetCommandPool(g_dev, pool, 0);
	}
	if (early) {
		std::printf("DeviceAddressLifetimeGpuTests: driver hazard not reproduced\n");
		return 1;
	}
	std::printf("DeviceAddressLifetimeGpuTests: ok\n");
	return 0;
}
