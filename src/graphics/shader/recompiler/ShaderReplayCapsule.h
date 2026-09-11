#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERREPLAYCAPSULE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERREPLAYCAPSULE_H_

#include "graphics/shader/recompiler/ShaderRecompiler.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler {

// A replay capsule contains the compiler inputs which are not recoverable from a raw shader
// binary.  It intentionally stores only immutable compile state; guest pointers and runtime
// resource addresses are not part of CompileProgram's input and are therefore not serialized.
struct ReplayCapsule {
	static constexpr uint32_t CurrentVersion = 1;

	uint32_t                   version               = CurrentVersion;
	ShaderType                 stage                 = ShaderType::Unknown;
	uint64_t                   shader_hash           = 0;
	uint32_t                   wave_size             = 64;
	uint32_t                   user_data_base        = 0;
	uint32_t                   scratch_dwords        = 0;
	uint32_t                   push_data_start_dword = 0;
	std::vector<uint32_t>      code;
	std::vector<uint32_t>      user_data;
	std::vector<uint32_t>      back_code;
	ShaderComputeInputInfo     compute;
	bool                       has_compute_input = false;
	IR::ResourceSpecialization specialization;
};

// Captures all stage-independent CompileProgram inputs and the compute state needed by the
// current offline replay. Vertex/pixel stage input capture can be added without changing the
// file format; returning false keeps production capture best-effort for those stages today.
bool WriteReplayCapsule(const std::filesystem::path& path, std::span<const uint32_t> code,
                        const CompileOptions&             options,
                        const IR::ResourceSpecialization& specialization,
                        uint32_t push_data_start_dword, std::string* error = nullptr);

bool ReadReplayCapsule(const std::filesystem::path& path, ReplayCapsule& capsule,
                       std::string* error = nullptr);

} // namespace Libs::Graphics::ShaderRecompiler

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERREPLAYCAPSULE_H_ */
