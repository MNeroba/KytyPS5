#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/debug.h"

#include <chrono>

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

void MasterSemaphore::RecordSubmitDebug(uint64_t tick, uint32_t debug_op, uint64_t debug_submit,
                                        uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                                        uint64_t arg4) {
	std::lock_guard lock(m_submit_history_mutex);
	auto&           record = m_submit_history[tick % SubmitHistorySize];
	record.tick            = tick;
	record.debug_op        = debug_op;
	record.debug_submit    = debug_submit;
	record.arg0            = arg0;
	record.arg1            = arg1;
	record.arg2            = arg2;
	record.arg3            = arg3;
	record.arg4            = arg4;
}

void MasterSemaphore::LogSubmitDebug(uint64_t tick) const {
	std::lock_guard lock(m_submit_history_mutex);
	const auto&     record = m_submit_history[tick % SubmitHistorySize];
	if (record.tick == tick) {
		LOGF("wait failure submit: tick=%" PRIu64 " debug_op=%u debug_submit=%" PRIu64
		     " args=%u,%u,%u,%u,0x%016" PRIx64 "\n",
		     record.tick, record.debug_op, record.debug_submit, record.arg0, record.arg1,
		     record.arg2, record.arg3, record.arg4);
	} else {
		LOGF("wait failure submit: tick=%" PRIu64 " metadata=missing\n", tick);
	}

	const auto first_tick = tick > 16 ? tick - 16 : 1;
	for (auto history_tick = first_tick; history_tick <= tick; ++history_tick) {
		const auto& history = m_submit_history[history_tick % SubmitHistorySize];
		if (history.tick == history_tick) {
			LOGF("wait failure recent submit: tick=%" PRIu64 " debug_op=%u debug_submit=%" PRIu64
			     " args=%u,%u,%u,%u,0x%016" PRIx64 "\n",
			     history.tick, history.debug_op, history.debug_submit, history.arg0, history.arg1,
			     history.arg2, history.arg3, history.arg4);
		}
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
	const auto wait_begin = std::chrono::steady_clock::now();
	if (graphics_debug_dump_enabled()) {
		LOGF("HostWait begin tick=%" PRIu64 " known=%" PRIu64 " current=%" PRIu64 "\n", tick,
		     KnownGpuTick(), CurrentTick());
	}

	vk::SemaphoreWaitInfo wait_info {};
	wait_info.semaphoreCount = 1;
	wait_info.pSemaphores    = &m_semaphore;
	wait_info.pValues        = &tick;

	const auto result = m_graphics.device.waitSemaphores(&wait_info, UINT64_MAX);
	if (graphics_debug_dump_enabled()) {
		LOGF("HostWait done tick=%" PRIu64 " result=%s elapsed_ms=%" PRIu64 "\n", tick,
		     vk::to_string(result).c_str(),
		     static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		                               std::chrono::steady_clock::now() - wait_begin)
		                               .count()));
	}
	if (result != vk::Result::eSuccess) {
		LOGF("vkDevice.waitSemaphores failed: %s (%d), tick=%" PRIu64 " known=%" PRIu64
		     " current=%" PRIu64 "\n",
		     vk::to_string(result).c_str(), static_cast<int>(result), tick, KnownGpuTick(),
		     CurrentTick());
		LogSubmitDebug(tick);
	}
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);
	Refresh();
}

} // namespace Libs::Graphics
