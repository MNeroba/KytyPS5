#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERSWAPPCDIAGNOSTIC_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERSWAPPCDIAGNOSTIC_H_

#include <cstdint>
#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler {

using SwapPcReadU32   = bool (*)(void* userdata, uint64_t address, uint32_t* value);
using SwapPcFindRange = bool (*)(void* userdata, uint64_t address, uint64_t* range_base,
                                 uint64_t* range_size);

struct SwapPcDiagnosticDecodeRecord {
	uint64_t invocation_id   = 0;
	uint32_t pc              = 0;
	uint32_t raw             = 0;
	uint64_t remaining_words = 0;
	uint64_t span_words      = 0;
};

using SwapPcDiagnosticDecodeCallback = void (*)(void*                               userdata,
                                                const SwapPcDiagnosticDecodeRecord& record);

enum class SwapPcWriterKind : uint8_t {
	Unknown,
	UserData,
	MoveImmediate,
	MovePair,
	ScalarLoad,
	ScalarLoadPair,
	ScalarLoadQuad,
	BufferLoad,
	BufferLoadPair,
};

struct SwapPcDiagnosticOptions {
	uint64_t                       shader_hash      = 0;
	uint64_t                       shader_base      = 0;
	uint64_t                       invocation_id    = 0;
	bool                           fused_front      = false;
	uint32_t                       user_data_base   = 0;
	std::span<const uint32_t>      user_data        = {};
	void*                          memory_userdata  = nullptr;
	SwapPcReadU32                  read_u32         = nullptr;
	SwapPcFindRange                find_range       = nullptr;
	SwapPcDiagnosticDecodeCallback decode_callback  = nullptr;
	void*                          decode_userdata  = nullptr;
	uint32_t                       max_call_sites   = 16;
	uint32_t                       max_callee_words = 64;
};

struct SwapPcDiagnosticRecord {
	uint32_t              call_pc                = 0;
	uint32_t              call_raw               = 0;
	uint32_t              source_sgpr            = 0;
	uint32_t              destination_sgpr       = 0;
	uint32_t              writer_pc              = 0;
	uint32_t              writer_raw             = 0;
	SwapPcWriterKind      writer_kind            = SwapPcWriterKind::Unknown;
	bool                  writer_pair_same       = false;
	bool                  source_known           = false;
	uint64_t              descriptor_address     = 0;
	bool                  descriptor_known       = false;
	uint64_t              effective_load_address = 0;
	bool                  effective_load_known   = false;
	uint32_t              target_lo              = 0;
	uint32_t              target_hi              = 0;
	bool                  target_known           = false;
	uint64_t              target_guest_va        = 0;
	bool                  target_mapped          = false;
	uint64_t              allocation_base        = 0;
	uint64_t              allocation_size        = 0;
	std::vector<uint32_t> callee_words;
	bool                  return_found        = false;
	uint32_t              return_pc           = 0;
	uint32_t              return_raw          = 0;
	uint32_t              return_source_sgpr  = 0;
	bool                  return_pair_matches = false;
};

// Best-effort, bounded, read-only reconstruction of dynamic S_SWAPPC_B64 call sites. This
// function never changes decoded code, compiler inputs, or control flow. Unknown/unreadable
// values remain unresolved in the returned record.
std::vector<SwapPcDiagnosticRecord>
ResolveSwapPcDiagnostics(std::span<const uint32_t> code, const SwapPcDiagnosticOptions& options);

} // namespace Libs::Graphics::ShaderRecompiler

#endif // EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERSWAPPCDIAGNOSTIC_H_
