#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINECACHEFINGERPRINT_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINECACHEFINGERPRINT_H_

#include "graphics/shader/shader.h"

#include <cstdint>
#include <xxhash.h>

namespace Libs::Graphics {

// Diagnostic-only semantic identity for comparing Vulkan compute-pipeline creation inputs.
// This is deliberately not used as a production cache key.
inline uint64_t MakePipelineSemanticFingerprint(ShaderType stage, uint32_t wave_size,
                                                uint64_t spirv_hash, uint64_t spirv_words,
                                                uint64_t specialization_hash, uint64_t layout_hash,
                                                uint64_t pipeline_flags,
                                                uint32_t push_constant_size) {
	uint64_t   hash = 0;
	const auto mix  = [&hash](uint64_t value) {
		hash = XXH3_64bits_withSeed(&value, sizeof(value), hash);
	};
	mix(static_cast<uint32_t>(stage));
	mix(wave_size);
	mix(spirv_hash);
	mix(spirv_words);
	mix(specialization_hash);
	mix(layout_hash);
	mix(pipeline_flags);
	mix(push_constant_size);
	return hash;
}

} // namespace Libs::Graphics

#endif /* EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINECACHEFINGERPRINT_H_ */
