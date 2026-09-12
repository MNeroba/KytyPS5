#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <atomic>
#include <mutex>

namespace Libs::Graphics {

struct GraphicContext;

class MasterSemaphore {
public:
	explicit MasterSemaphore(GraphicContext& graphics);
	~MasterSemaphore();
	KYTY_CLASS_NO_COPY(MasterSemaphore);

	[[nodiscard]] uint64_t CurrentTick() const noexcept {
		return m_current_tick.load(std::memory_order_acquire);
	}
	[[nodiscard]] uint64_t KnownGpuTick() const noexcept {
		return m_gpu_tick.load(std::memory_order_acquire);
	}
	[[nodiscard]] bool     IsFree(uint64_t tick) const noexcept { return KnownGpuTick() >= tick; }
	[[nodiscard]] uint64_t NextTick() noexcept {
		return m_current_tick.fetch_add(1, std::memory_order_release);
	}
	[[nodiscard]] vk::Semaphore Handle() const noexcept { return m_semaphore; }

	void Refresh();
	void Wait(uint64_t tick);
	void RecordSubmitDebug(uint64_t tick, uint32_t debug_op, uint64_t debug_submit, uint32_t arg0,
	                       uint32_t arg1, uint32_t arg2, uint32_t arg3, uint64_t arg4,
	                       uint32_t last_non_eop_op, uint64_t last_non_eop_submit,
	                       uint32_t last_non_eop_arg0, uint32_t last_non_eop_arg1,
	                       uint32_t last_non_eop_arg2, uint32_t last_non_eop_arg3,
	                       uint64_t last_non_eop_arg4);

private:
	struct SubmitDebugInfo {
		uint64_t tick                = UINT64_MAX;
		uint32_t debug_op            = 0;
		uint64_t debug_submit        = 0;
		uint32_t arg0                = 0;
		uint32_t arg1                = 0;
		uint32_t arg2                = 0;
		uint32_t arg3                = 0;
		uint64_t arg4                = 0;
		uint32_t last_non_eop_op     = UINT32_MAX;
		uint64_t last_non_eop_submit = 0;
		uint32_t last_non_eop_arg0   = 0;
		uint32_t last_non_eop_arg1   = 0;
		uint32_t last_non_eop_arg2   = 0;
		uint32_t last_non_eop_arg3   = 0;
		uint64_t last_non_eop_arg4   = 0;
	};

	void LogSubmitDebug(uint64_t tick) const;

	static constexpr size_t SubmitHistorySize = 128;

	GraphicContext&                                m_graphics;
	vk::Semaphore                                  m_semaphore = nullptr;
	std::atomic<uint64_t>                          m_gpu_tick {0};
	std::atomic<uint64_t>                          m_current_tick {1};
	mutable std::mutex                             m_submit_history_mutex;
	std::array<SubmitDebugInfo, SubmitHistorySize> m_submit_history {};
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_
