#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_

#include "common/common.h"
#include "common/stringUtils.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"

#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler {

struct CompileOptions {
	ShaderType stage          = ShaderType::Compute;
	uint32_t   wave_size      = 64;
	uint32_t   user_data_base = 0;
	uint32_t   scratch_dwords = 0;
	uint64_t   shader_hash    = 0;
	// Diagnostic-only correlation for paired production ShaderReplayInput records.  It is not
	// consumed by translation, compilation, cache keys, or replay capsule serialization.
	uint64_t replay_invocation_id = 0;
	bool     dump_ir              = true;
	bool     early_dump           = false;
	// Bounded opt-in timing metadata. It is diagnostic-only and never affects compilation.
	bool                      compile_profile = false;
	const char*               dump_label      = nullptr;
	std::span<const uint32_t> user_data;
	std::span<const uint32_t> back_code;
	ShaderStageInputInfo      input_info;
};

struct CompileProfile {
	bool     enabled                     = false;
	uint64_t code_words                  = 0;
	uint64_t decoded_instruction_count   = 0;
	uint64_t cfg_block_count             = 0;
	uint64_t cfg_loop_count              = 0;
	uint64_t cfg_back_edge_count         = 0;
	uint64_t ir_instruction_count_before = 0;
	uint64_t ir_instruction_count_after  = 0;
	uint64_t decode_ms                   = 0;
	uint64_t cfg_ms                      = 0;
	uint64_t structurize_ms              = 0;
	uint64_t translate_ms                = 0;
	uint64_t resource_ms                 = 0;
	uint64_t optimize_ms                 = 0;
	uint64_t spirv_ms                    = 0;
	uint64_t validation_ms               = 0;
	uint64_t shader_module_ms            = 0;
	uint64_t total_ms                    = 0;
	uint64_t spirv_words                 = 0;
};

struct TranslateResult {
	IR::Program    program;
	std::string    decoded_dump;
	std::string    cfg_dump;
	CompileProfile profile;
};

struct CompileResult {
	std::vector<uint32_t> spirv;
	std::string           decoded_dump;
	std::string           ir_dump;
	IR::Program           program;
	CompileProfile        profile;
};

[[nodiscard]] TranslateResult TranslateProgram(std::span<const uint32_t> code,
                                               const CompileOptions&     options);
// Returns the number of words in the executable front of a fused shader.  The front must end
// with the merged-stage ABI handoff S_SETPC_B64 s6; this is the boundary used before back-code
// splicing and by opt-in diagnostics that inspect the front independently.
[[nodiscard]] uint32_t      FusedFrontWordCount(std::span<const uint32_t> front);
[[nodiscard]] CompileResult CompileProgram(TranslateResult                   translated,
                                           const CompileOptions&             options,
                                           const IR::ResourceSpecialization& specialization,
                                           uint32_t push_data_start_dword = 0);

} // namespace Libs::Graphics::ShaderRecompiler

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERRECOMPILER_H_ */
