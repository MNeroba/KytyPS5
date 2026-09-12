#include "graphics/host_gpu/renderer/gpuFaultDiagnostics.h"

#include "common/logging/log.h"
#include "common/threads.h"
#include "graphics/host_gpu/graphicContext.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

namespace Libs::Graphics {

namespace {

const char* CheckpointPhaseName(uint32_t phase) {
	switch (static_cast<GpuCheckpointPhase>(phase)) {
		case GpuCheckpointPhase::Before: return "BEFORE";
		case GpuCheckpointPhase::After: return "AFTER";
		default: return "UNKNOWN";
	}
}

const char* DebugOpName(uint32_t op) {
	switch (op) {
		case 0: return "DispatchDirect";
		case 1: return "DrawIndex";
		case 2: return "DrawIndexAuto";
		case 3: return "EopWrite";
		case 4: return "EopInterrupt";
		case 5: return "EopWriteBack";
		case 6: return "EopFlip";
		case 7: return "EopWriteBackFlip";
		case 8: return "EopOnlyFlip";
		default: return "Unknown";
	}
}

} // namespace

GpuFaultDiagnostics::Marker GpuFaultDiagnostics::MakeMarker(GpuCheckpointPhase phase,
                                                            uint32_t           debug_op,
                                                            uint64_t debug_submit, uint32_t arg0,
                                                            uint32_t arg1, uint32_t arg2,
                                                            uint32_t arg3, uint64_t arg4) {
	auto marker          = std::make_unique<GpuCheckpointMarker>();
	marker->phase        = static_cast<uint32_t>(phase);
	marker->sequence     = m_next_sequence.fetch_add(1, std::memory_order_relaxed);
	marker->debug_op     = debug_op;
	marker->debug_submit = debug_submit;
	marker->arg0         = arg0;
	marker->arg1         = arg1;
	marker->arg2         = arg2;
	marker->arg3         = arg3;
	marker->arg4         = arg4;
	return marker;
}

void GpuFaultDiagnostics::CommitSubmit(uint64_t tick, std::vector<Marker>&& markers) {
	if (markers.empty()) {
		return;
	}
	std::lock_guard lock(m_mutex);
	m_submitted.push_back({tick, std::move(markers)});
}

void GpuFaultDiagnostics::RetireCompleted(uint64_t known_tick) {
	std::lock_guard lock(m_mutex);
	while (!m_submitted.empty() && m_submitted.front().tick <= known_tick) {
		m_submitted.pop_front();
	}
}

const GpuCheckpointMarker* GpuFaultDiagnostics::FindMarkerLocked(const void* marker) const {
	for (const auto& batch: m_submitted) {
		for (const auto& candidate: batch.markers) {
			if (candidate.get() == marker) {
				return candidate.get();
			}
		}
	}
	return nullptr;
}

void GpuFaultDiagnostics::DumpDeviceFault(const char* source, uint64_t tick) {
	if (m_graphics == nullptr || !m_graphics->device_fault_enabled ||
	    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceFaultInfoEXT == nullptr) {
		LOGF("GPU_DEVICE_FAULT unavailable=true\n");
		return;
	}

	vk::DeviceFaultCountsEXT counts {};
	counts.sType            = vk::StructureType::eDeviceFaultCountsEXT;
	const auto count_result = m_graphics->device.getFaultInfoEXT(&counts, nullptr);
	if (count_result != vk::Result::eSuccess && count_result != vk::Result::eIncomplete) {
		LOGF("GPU_DEVICE_FAULT source=%s tick=%" PRIu64 " query_result=%s (%d)\n", source, tick,
		     vk::to_string(count_result).c_str(), static_cast<int>(count_result));
		return;
	}

	std::vector<vk::DeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
	std::vector<vk::DeviceFaultVendorInfoEXT>  vendors(counts.vendorInfoCount);
	const auto                                 max_binary = vk::DeviceSize {16u * 1024u * 1024u};
	const auto                                 advertised_binary_size = counts.vendorBinarySize;
	const auto binary_capacity = std::min(advertised_binary_size, max_binary);
	// vendorBinarySize is an input capacity on the second query.  Keep the
	// driver's advertised size separately, but pass the actual allocation size
	// so pVendorBinaryData satisfies the Vulkan pointer/length contract.
	counts.vendorBinarySize = binary_capacity;
	std::vector<uint8_t>   binary(static_cast<size_t>(binary_capacity));
	vk::DeviceFaultInfoEXT info {};
	info.sType             = vk::StructureType::eDeviceFaultInfoEXT;
	info.pAddressInfos     = addresses.empty() ? nullptr : addresses.data();
	info.pVendorInfos      = vendors.empty() ? nullptr : vendors.data();
	info.pVendorBinaryData = binary.empty() ? nullptr : binary.data();
	const auto info_result = m_graphics->device.getFaultInfoEXT(&counts, &info);
	LOGF("GPU_DEVICE_FAULT\nsource=%s\ntick=%" PRIu64 "\ndescription=\"%s\"\n"
	     "address_count=%u\nvendor_count=%u\nvendor_binary_size_advertised=%" PRIu64
	     "\nvendor_binary_capacity=%" PRIu64 "\nvendor_binary_available=%s\n"
	     "count_result=%s (%d)\n",
	     source, tick, info.description.data(), counts.addressInfoCount, counts.vendorInfoCount,
	     static_cast<uint64_t>(advertised_binary_size), static_cast<uint64_t>(binary_capacity),
	     binary.empty() ? "false" : "true", vk::to_string(count_result).c_str(),
	     static_cast<int>(count_result));
	if (info_result != vk::Result::eSuccess && info_result != vk::Result::eIncomplete) {
		LOGF("GPU_DEVICE_FAULT info_result=%s (%d)\n", vk::to_string(info_result).c_str(),
		     static_cast<int>(info_result));
		return;
	}
	LOGF("GPU_DEVICE_FAULT info_result=%s (%d) partial=%s\n", vk::to_string(info_result).c_str(),
	     static_cast<int>(info_result), info_result == vk::Result::eIncomplete ? "true" : "false");
	for (uint32_t i = 0; i < counts.addressInfoCount; ++i) {
		const auto& address = addresses[i];
		LOGF("address[%u]: type=%u address=0x%016" PRIx64 " precision=0x%016" PRIx64 "\n", i,
		     static_cast<uint32_t>(address.addressType),
		     static_cast<uint64_t>(address.reportedAddress),
		     static_cast<uint64_t>(address.addressPrecision));
	}
	for (uint32_t i = 0; i < counts.vendorInfoCount; ++i) {
		const auto& vendor = vendors[i];
		LOGF("vendor[%u]: code=0x%016" PRIx64 " data=0x%016" PRIx64 " description=\"%s\"\n", i,
		     vendor.vendorFaultCode, vendor.vendorFaultData, vendor.description.data());
	}
}

void GpuFaultDiagnostics::DumpCheckpoints() {
	if (m_graphics == nullptr || !m_graphics->diagnostic_checkpoints_enabled ||
	    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetQueueCheckpointDataNV == nullptr ||
	    m_graphics->queue == nullptr) {
		LOGF("GPU_CHECKPOINT unavailable=true\n");
		return;
	}

	Common::LockGuard queue_lock(m_graphics->queue_mutex);
	uint32_t          count = 0;
	m_graphics->queue.getCheckpointDataNV(&count, nullptr);
	std::vector<vk::CheckpointDataNV> checkpoints(count);
	if (count != 0) {
		m_graphics->queue.getCheckpointDataNV(&count, checkpoints.data());
		checkpoints.resize(count);
	}
	std::lock_guard lock(m_mutex);
	for (const auto& checkpoint: checkpoints) {
		const auto* marker = FindMarkerLocked(checkpoint.pCheckpointMarker);
		if (marker == nullptr) {
			LOGF("GPU_CHECKPOINT stage=%u marker=unknown pointer=%p\n",
			     static_cast<uint32_t>(checkpoint.stage), checkpoint.pCheckpointMarker);
			continue;
		}
		if (marker->magic != GpuCheckpointMarker::Magic ||
		    marker->version != GpuCheckpointMarker::Version) {
			LOGF("GPU_CHECKPOINT stage=%u marker=invalid\n",
			     static_cast<uint32_t>(checkpoint.stage));
			continue;
		}
		LOGF("GPU_CHECKPOINT\nstage=%u\nseq=%" PRIu64 "\nphase=%s\nop=%s (%u)\n"
		     "debug_submit=%" PRIu64 "\nargs=%u,%u,%u,%u,0x%016" PRIx64 "\n",
		     static_cast<uint32_t>(checkpoint.stage), marker->sequence,
		     CheckpointPhaseName(marker->phase), DebugOpName(marker->debug_op), marker->debug_op,
		     marker->debug_submit, marker->arg0, marker->arg1, marker->arg2, marker->arg3,
		     marker->arg4);
	}
}

void GpuFaultDiagnostics::ReportDeviceLost(const char* source, vk::Result result, uint64_t tick) {
	if (result != vk::Result::eErrorDeviceLost) {
		return;
	}
	{
		std::lock_guard lock(m_mutex);
		if (m_reported) {
			LOGF("GPU_DEVICE_FAULT already_reported source=%s tick=%" PRIu64 "\n", source, tick);
			return;
		}
		m_reported = true;
	}
	DumpDeviceFault(source, tick);
	DumpCheckpoints();
}

} // namespace Libs::Graphics
