#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_

#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <atomic>
#include <condition_variable>
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

enum class GpuSnapshotBufferKind : uint32_t {
	Descriptor,
	Vertex,
	Index,
	Gds,
	BdaPageTable,
	FaultBuffer,
	FlattenedSrt,
	ShaderData,
};

enum class GpuSnapshotImageKind : uint32_t {
	Descriptor,
	ColorTarget,
	DepthTarget,
};

template <typename Handle>
[[nodiscard]] uint64_t GpuSnapshotHandleValue(Handle handle) {
	using Native = typename Handle::CType;
	if constexpr (std::is_pointer_v<Native>) {
		return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(static_cast<Native>(handle)));
	} else {
		return static_cast<uint64_t>(static_cast<Native>(handle));
	}
}

struct GpuBufferSnapshot {
	uint32_t kind              = 0;
	uint32_t shader_stage      = 0;
	uint32_t resource_index    = 0;
	uint32_t slot_index        = UINT32_MAX;
	uint32_t slot_generation   = 0;
	uint64_t vk_buffer         = 0;
	uint64_t guest_address     = 0;
	uint64_t host_bda          = 0;
	uint64_t descriptor_offset = 0;
	uint64_t descriptor_range  = 0;
	uint64_t allocation_guest  = 0;
	uint64_t allocation_size   = 0;
	uint32_t access            = 0;
	bool     allocation_live   = false;
	bool     deleted           = false;
};

struct GpuImageSnapshot {
	uint32_t                kind            = 0;
	uint32_t                shader_stage    = 0;
	uint32_t                resource_index  = 0;
	uint32_t                slot_index      = UINT32_MAX;
	uint32_t                slot_generation = 0;
	uint64_t                vk_image        = 0;
	uint64_t                vk_image_view   = 0;
	uint64_t                guest_address   = 0;
	uint64_t                guest_size      = 0;
	uint32_t                image_format    = 0;
	uint32_t                view_format     = 0;
	std::array<uint32_t, 3> extent {};
	uint32_t                layout          = 0;
	uint64_t                access_mask     = 0;
	uint64_t                pipeline_stage  = 0;
	uint32_t                access          = 0;
	bool                    allocation_live = false;
	bool                    registered      = false;
	bool                    retired         = false;
};

// A bounded, host-side description of a guest address considered by a BDA-capable
// dispatch.  This is diagnostic state only; it does not participate in descriptor
// binding or shader execution.
struct GpuBdaTranslationSnapshot {
	uint32_t push_pair_index       = 0;
	uint64_t guest_address         = 0;
	uint64_t page_index            = 0;
	uint64_t page_table_entry      = 0;
	uint64_t resolved_host_address = 0;
	uint32_t owner_slot_index      = UINT32_MAX;
	uint32_t owner_slot_generation = 0;
	uint64_t owner_guest_address   = 0;
	uint64_t owner_host_bda        = 0;
	uint64_t owner_allocation_size = 0;
	uint64_t owner_offset          = 0;
	uint64_t access_range          = 0;
	bool     mapping_published     = false;
	bool     allocation_live       = false;
};

struct GpuCommandSnapshot {
	static constexpr size_t MaxPushDataDwords = 32;

	uint32_t                                operation_order = 0;
	uint32_t                                debug_op        = 0;
	uint64_t                                guest_submit    = 0;
	uint64_t                                pipeline        = 0;
	std::array<uint64_t, 2>                 shader_hashes {};
	std::array<uint64_t, 8>                 arguments {};
	std::array<uint32_t, MaxPushDataDwords> push_data {};
	uint32_t                                push_data_count = 0;
	std::vector<GpuBufferSnapshot>          buffers;
	std::vector<GpuImageSnapshot>           images;
	std::vector<GpuBdaTranslationSnapshot>  bda_translations;
	uint32_t                                dropped_buffers = 0;
	uint32_t                                dropped_images  = 0;
	uint32_t                                dropped_bda     = 0;
};

struct GpuCommandSnapshotBatch {
	std::deque<GpuCommandSnapshot> commands;
	uint32_t                       total_commands   = 0;
	uint32_t                       dropped_commands = 0;
};

struct GpuFaultDiagnosticsTestAccess;

class GpuFaultDiagnostics {
public:
	enum class ReportTestHookStage {
		OwnerEntered,
		DuplicateObserved,
		DuplicateWaiting,
	};
	using ReportTestHook = void (*)(void* context, ReportTestHookStage stage);
	using Marker         = std::unique_ptr<GpuCheckpointMarker>;
	static constexpr size_t MaxSubmittedSnapshotBatches  = 64;
	static constexpr size_t MaxCommandsPerBuffer         = 128;
	static constexpr size_t MaxBuffersPerCommand         = 96;
	static constexpr size_t MaxImagesPerCommand          = 96;
	static constexpr size_t MaxBdaTranslationsPerCommand = 32;

	explicit GpuFaultDiagnostics(GraphicContext& graphics): m_graphics(&graphics) {}
	~GpuFaultDiagnostics() = default;
	KYTY_CLASS_NO_COPY(GpuFaultDiagnostics);

	[[nodiscard]] Marker MakeMarker(GpuCheckpointPhase phase, uint32_t debug_op,
	                                uint64_t debug_submit, uint32_t arg0, uint32_t arg1,
	                                uint32_t arg2, uint32_t arg3, uint64_t arg4);

	void CommitSubmit(uint64_t tick, std::vector<Marker>&& markers,
	                  GpuCommandSnapshotBatch&& snapshots);
	void RetireCompleted(uint64_t known_tick);
	void ReportDeviceLost(const char* source, vk::Result result, uint64_t tick);

private:
	enum class ReportState {
		NotStarted,
		Reporting,
		Complete,
	};

	struct SubmittedBatch {
		uint64_t            tick = 0;
		std::vector<Marker> markers;
	};
	struct SubmittedSnapshotBatch {
		uint64_t                tick = 0;
		GpuCommandSnapshotBatch snapshots;
	};

	[[nodiscard]] const GpuCheckpointMarker* FindMarkerLocked(const void* marker) const;
	void                                     DumpDeviceFault(const char* source, uint64_t tick);
	void                                     DumpCheckpoints();
	[[nodiscard]] size_t                     DumpCommandSnapshots(uint64_t failing_tick);

	GraphicContext*                    m_graphics = nullptr;
	std::mutex                         m_mutex;
	std::condition_variable            m_report_complete;
	std::deque<SubmittedBatch>         m_submitted;
	std::deque<SubmittedSnapshotBatch> m_snapshot_batches;
	std::atomic<uint64_t>              m_next_sequence {1};
	ReportState                        m_report_state             = ReportState::NotStarted;
	ReportTestHook                     m_report_test_hook         = nullptr;
	void*                              m_report_test_hook_context = nullptr;

	friend struct GpuFaultDiagnosticsTestAccess;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_GPUFAULTDIAGNOSTICS_H_
