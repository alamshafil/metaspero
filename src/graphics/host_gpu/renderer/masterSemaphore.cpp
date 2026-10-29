#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include "common/assert.h"
#include "graphics/host_gpu/graphicContext.h"

namespace Libs::Graphics {

MasterSemaphore::MasterSemaphore(GraphicContext& graphics): m_graphics(graphics) {
	vk::SemaphoreTypeCreateInfo type_info {};
	type_info.semaphoreType = vk::SemaphoreType::eTimeline;
	type_info.initialValue  = 0;

	vk::SemaphoreCreateInfo create_info {};
	create_info.pNext = &type_info;

	const auto result = m_graphics.device.createSemaphore(&create_info, nullptr, &m_semaphore);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess || m_semaphore == nullptr);
}

MasterSemaphore::~MasterSemaphore() {
	if (m_semaphore != nullptr) {
		m_graphics.device.destroySemaphore(m_semaphore, nullptr);
	}
}

void MasterSemaphore::Refresh() {
	uint64_t   counter = 0;
	const auto result  = m_graphics.device.getSemaphoreCounterValue(m_semaphore, &counter);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);

	auto known = m_gpu_tick.load(std::memory_order_acquire);
	while (known < counter &&
	       !m_gpu_tick.compare_exchange_weak(known, counter, std::memory_order_release,
	                                         std::memory_order_relaxed)) {
	}
}

void MasterSemaphore::Wait(uint64_t tick) {
	if (IsFree(tick)) {
		return;
	}
	Refresh();
	if (IsFree(tick)) {
		return;
	}

	constexpr uint64_t TIMEOUT_NS = 100000000ULL; // 100ms
	int wait_retries = 0;
	while (!IsFree(tick)) {
		vk::SemaphoreWaitInfo wait_info {};
		wait_info.semaphoreCount = 1;
		wait_info.pSemaphores    = &m_semaphore;
		wait_info.pValues        = &tick;

		const auto result = m_graphics.device.waitSemaphores(&wait_info, TIMEOUT_NS);
		Refresh();
		if (result == vk::Result::eSuccess || IsFree(tick)) {
			break;
		}
		if (result == vk::Result::eTimeout) {
			if (++wait_retries % 20 == 0) { // Every 2 seconds
				std::printf("[MasterSemaphore::Wait] Still waiting for GPU tick %" PRIu64 " (known GPU tick: %" PRIu64 ", elapsed: %d ms)...\n",
				     tick, KnownGpuTick(), wait_retries * 100);
				std::fflush(stdout);
			}
			continue;
		}
		EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);
	}
	Refresh();
}

} // namespace Libs::Graphics
