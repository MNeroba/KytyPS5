#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_

#include "graphics/host_gpu/vulkanCommon.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

namespace Libs::Graphics {

struct GraphicContext;

struct GpuCheckpointMarker {
	static constexpr uint64_t Magic   = 0x4b59545947504350ull; // "KYTYGPCP"
	static constexpr uint32_t Version = 1;

	uint64_t magic        = Magic;
	uint32_t version      = Version;
	uint32_t phase        = 0;
	uint64_t sequence     = 0;
	uint32_t debug_op     = 0;
	uint64_t debug_submit = 0;
	uint32_t arg0         = 0;
	uint32_t arg1         = 0;
	uint32_t arg2         = 0;
	uint32_t arg3         = 0;
	uint64_t arg4         = 0;
};

static_assert(std::is_standard_layout_v<GpuCheckpointMarker> &&
              std::is_trivially_copyable_v<GpuCheckpointMarker>);

enum class GpuCheckpointPhase : uint32_t {
	Before = 0,
	After  = 1,
};

class GpuFaultDiagnostics {
public:
	using Marker = std::unique_ptr<GpuCheckpointMarker>;

	explicit GpuFaultDiagnostics(GraphicContext& graphics): m_graphics(&graphics) {}
	~GpuFaultDiagnostics() = default;
	KYTY_CLASS_NO_COPY(GpuFaultDiagnostics);

	[[nodiscard]] Marker MakeMarker(GpuCheckpointPhase phase, uint32_t debug_op,
	                                uint64_t debug_submit, uint32_t arg0, uint32_t arg1,
	                                uint32_t arg2, uint32_t arg3, uint64_t arg4);

	void CommitSubmit(uint64_t tick, std::vector<Marker>&& markers);
	void RetireCompleted(uint64_t known_tick);
	void ReportDeviceLost(const char* source, vk::Result result, uint64_t tick);

private:
	struct SubmittedBatch {
		uint64_t            tick = 0;
		std::vector<Marker> markers;
	};

	[[nodiscard]] const GpuCheckpointMarker* FindMarkerLocked(const void* marker) const;
	void                                     DumpDeviceFault(const char* source, uint64_t tick);
	void                                     DumpCheckpoints();

	GraphicContext*            m_graphics = nullptr;
	std::mutex                 m_mutex;
	std::deque<SubmittedBatch> m_submitted;
	std::atomic<uint64_t>      m_next_sequence {1};
	bool                       m_reported = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_
