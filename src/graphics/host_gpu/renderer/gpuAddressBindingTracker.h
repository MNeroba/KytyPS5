#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUADDRESSBINDINGTRACKER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUADDRESSBINDINGTRACKER_H_

#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Libs::Graphics {

// A bounded host-side record of VK_EXT_device_address_binding_report events.
// The callback path only mutates these fixed-size arrays; no Vulkan calls are
// made and no unbounded container is used there.
struct GpuAddressBindingRecord {
	uint64_t sequence      = 0;
	uint64_t base_address  = 0;
	uint64_t size          = 0;
	uint64_t object_handle = 0;
	uint32_t object_type   = 0;
	uint32_t flags         = 0;
	bool     bind          = false;
	bool     live          = false;
};

class GpuAddressBindingTracker {
public:
	static constexpr size_t   MaxLiveBindings    = 512;
	static constexpr size_t   MaxHistoryRecords  = 2048;
	static constexpr uint32_t MaxObjectsPerEvent = 8;

	GpuAddressBindingTracker()                                           = default;
	GpuAddressBindingTracker(const GpuAddressBindingTracker&)            = delete;
	GpuAddressBindingTracker& operator=(const GpuAddressBindingTracker&) = delete;

	void SetEnabled(bool enabled) noexcept { m_enabled.store(enabled, std::memory_order_release); }
	[[nodiscard]] bool Enabled() const noexcept {
		return m_enabled.load(std::memory_order_acquire);
	}

	// Called from the Vulkan debug-utils callback.  callback_data->pObjects is
	// copied immediately; the pointed-to Vulkan structures are never retained.
	void RecordCallback(const vk::DeviceAddressBindingCallbackDataEXT& binding,
	                    const vk::DebugUtilsMessengerCallbackDataEXT*  callback_data) noexcept;

	// Deterministic test/diagnostic injection path.  It uses the same bounded
	// state machine as RecordCallback and performs no Vulkan operation.
	void RecordBinding(uint64_t base_address, uint64_t size, uint32_t flags, bool bind,
	                   uint32_t object_type, uint64_t object_handle) noexcept;

	[[nodiscard]] std::vector<GpuAddressBindingRecord> LiveSnapshot() const;
	[[nodiscard]] std::vector<GpuAddressBindingRecord> HistorySnapshot() const;
	[[nodiscard]] bool     HasLiveOverlap(uint64_t base_address, uint64_t size) const noexcept;
	[[nodiscard]] uint64_t DroppedHistory() const noexcept;
	[[nodiscard]] uint64_t DroppedLive() const noexcept;

private:
	void RecordBindingLocked(uint64_t base_address, uint64_t size, uint32_t flags, bool bind,
	                         uint32_t object_type, uint64_t object_handle) noexcept;
	void AppendHistoryLocked(const GpuAddressBindingRecord& record) noexcept;
	void AddDroppedHistoryLocked(uint64_t count) noexcept;

	std::atomic<bool>                                      m_enabled {false};
	mutable std::mutex                                     m_mutex;
	std::array<GpuAddressBindingRecord, MaxLiveBindings>   m_live {};
	std::array<GpuAddressBindingRecord, MaxHistoryRecords> m_history {};
	size_t                                                 m_live_count      = 0;
	size_t                                                 m_history_count   = 0;
	size_t                                                 m_history_next    = 0;
	uint64_t                                               m_dropped_history = 0;
	uint64_t                                               m_dropped_live    = 0;
	std::atomic<uint64_t>                                  m_next_sequence {1};
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUADDRESSBINDINGTRACKER_H_
