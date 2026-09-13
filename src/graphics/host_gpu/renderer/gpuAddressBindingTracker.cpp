#include "graphics/host_gpu/renderer/gpuAddressBindingTracker.h"

#include <algorithm>
#include <limits>

namespace Libs::Graphics {

namespace {

bool RangesOverlap(uint64_t first_base, uint64_t first_size, uint64_t second_base,
                   uint64_t second_size) noexcept {
	if (first_size == 0 || second_size == 0) {
		return false;
	}
	const auto first_end  = first_base > std::numeric_limits<uint64_t>::max() - first_size
	                            ? std::numeric_limits<uint64_t>::max()
	                            : first_base + first_size;
	const auto second_end = second_base > std::numeric_limits<uint64_t>::max() - second_size
	                            ? std::numeric_limits<uint64_t>::max()
	                            : second_base + second_size;
	return first_base < second_end && second_base < first_end;
}

bool SameBinding(const GpuAddressBindingRecord& record, uint64_t base_address, uint64_t size,
                 uint32_t object_type, uint64_t object_handle) {
	if (record.base_address != base_address) {
		return false;
	}
	if (object_handle != 0 && record.object_handle != object_handle) {
		return false;
	}
	if (object_type != 0 && record.object_type != object_type) {
		return false;
	}
	return size == 0 || record.size == size;
}

} // namespace

void GpuAddressBindingTracker::AddDroppedHistoryLocked(uint64_t count) noexcept {
	if (count > std::numeric_limits<uint64_t>::max() - m_dropped_history) {
		m_dropped_history = std::numeric_limits<uint64_t>::max();
	} else {
		m_dropped_history += count;
	}
}

void GpuAddressBindingTracker::AppendHistoryLocked(const GpuAddressBindingRecord& record) noexcept {
	if (m_history_count < MaxHistoryRecords) {
		m_history[m_history_count++] = record;
		return;
	}
	m_history[m_history_next] = record;
	m_history_next            = (m_history_next + 1) % MaxHistoryRecords;
	AddDroppedHistoryLocked(1);
}

void GpuAddressBindingTracker::RecordBindingLocked(uint64_t base_address, uint64_t size,
                                                   uint32_t flags, bool bind, uint32_t object_type,
                                                   uint64_t object_handle) noexcept {
	GpuAddressBindingRecord record {};
	record.sequence      = m_next_sequence.fetch_add(1, std::memory_order_relaxed);
	record.base_address  = base_address;
	record.size          = size;
	record.object_handle = object_handle;
	record.object_type   = object_type;
	record.flags         = flags;
	record.bind          = bind;
	record.live          = false;

	if (bind) {
		size_t existing = m_live_count;
		for (size_t i = 0; i < m_live_count; ++i) {
			if (SameBinding(m_live[i], base_address, size, object_type, object_handle)) {
				existing = i;
				break;
			}
		}
		if (existing == m_live_count) {
			if (m_live_count == MaxLiveBindings) {
				++m_dropped_live;
				AppendHistoryLocked(record);
				return;
			}
			++m_live_count;
		}
		record.live      = true;
		m_live[existing] = record;
	} else {
		for (size_t i = 0; i < m_live_count;) {
			if (!SameBinding(m_live[i], base_address, size, object_type, object_handle)) {
				++i;
				continue;
			}
			m_live[i] = m_live[m_live_count - 1];
			--m_live_count;
		}
	}
	AppendHistoryLocked(record);
}

void GpuAddressBindingTracker::RecordBinding(uint64_t base_address, uint64_t size, uint32_t flags,
                                             bool bind, uint32_t object_type,
                                             uint64_t object_handle) noexcept {
	if (!Enabled()) {
		return;
	}
	std::lock_guard lock(m_mutex);
	RecordBindingLocked(base_address, size, flags, bind, object_type, object_handle);
}

void GpuAddressBindingTracker::RecordCallback(
    const vk::DeviceAddressBindingCallbackDataEXT& binding,
    const vk::DebugUtilsMessengerCallbackDataEXT*  callback_data) noexcept {
	if (!Enabled()) {
		return;
	}
	const bool bind  = binding.bindingType == vk::DeviceAddressBindingTypeEXT::eBind;
	const auto flags = static_cast<uint32_t>(binding.flags);
	if (callback_data == nullptr || callback_data->objectCount == 0 ||
	    callback_data->pObjects == nullptr) {
		RecordBinding(binding.baseAddress, binding.size, flags, bind, 0, 0);
		return;
	}
	const auto count = std::min(callback_data->objectCount, MaxObjectsPerEvent);
	for (uint32_t i = 0; i < count; ++i) {
		const auto& object = callback_data->pObjects[i];
		RecordBinding(binding.baseAddress, binding.size, flags, bind,
		              static_cast<uint32_t>(object.objectType), object.objectHandle);
	}
	if (callback_data->objectCount > count) {
		std::lock_guard lock(m_mutex);
		AddDroppedHistoryLocked(callback_data->objectCount - count);
	}
}

std::vector<GpuAddressBindingRecord> GpuAddressBindingTracker::LiveSnapshot() const {
	std::lock_guard lock(m_mutex);
	return std::vector<GpuAddressBindingRecord>(m_live.begin(), m_live.begin() + m_live_count);
}

std::vector<GpuAddressBindingRecord> GpuAddressBindingTracker::HistorySnapshot() const {
	std::lock_guard                      lock(m_mutex);
	std::vector<GpuAddressBindingRecord> result;
	result.reserve(m_history_count);
	if (m_history_count < MaxHistoryRecords) {
		result.insert(result.end(), m_history.begin(), m_history.begin() + m_history_count);
	} else {
		result.insert(result.end(), m_history.begin() + m_history_next, m_history.end());
		result.insert(result.end(), m_history.begin(), m_history.begin() + m_history_next);
	}
	return result;
}

bool GpuAddressBindingTracker::HasLiveOverlap(uint64_t base_address, uint64_t size) const noexcept {
	std::lock_guard lock(m_mutex);
	for (size_t i = 0; i < m_live_count; ++i) {
		if (RangesOverlap(base_address, size, m_live[i].base_address, m_live[i].size)) {
			return true;
		}
	}
	return false;
}

uint64_t GpuAddressBindingTracker::DroppedHistory() const noexcept {
	std::lock_guard lock(m_mutex);
	return m_dropped_history;
}

uint64_t GpuAddressBindingTracker::DroppedLive() const noexcept {
	std::lock_guard lock(m_mutex);
	return m_dropped_live;
}

} // namespace Libs::Graphics
