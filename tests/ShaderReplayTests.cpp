#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "graphics/shader/recompiler/ShaderReplayCapsule.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"
#include "spirv-tools/libspirv.hpp"
#include "xxhash.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Libs::Graphics {
namespace {

struct ReplayOptions {
	std::filesystem::path input;
	std::filesystem::path capsule;
	std::filesystem::path capsule_output;
	std::filesystem::path output;
	uint64_t              hash             = 0;
	uint32_t              wave_size       = 64;
	uint32_t              user_data_base  = 0;
	uint32_t              user_data_count = 0;
	uint32_t              workgroup_register = 0;
	std::array<uint32_t, 3> threads_num {1, 1, 1};
};

[[noreturn]] void Fail(std::string_view message) {
	std::fprintf(stderr, "shader_replay_tests: %.*s\n", static_cast<int>(message.size()),
	             message.data());
	std::exit(EXIT_FAILURE);
}

uint32_t ParseU32(std::string_view text, const char* name) {
	uint32_t value = 0;
	const auto* first = text.data();
	const auto* last  = first + text.size();
	int base = 10;
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		base = 16;
		first += 2;
	}
	const auto result = std::from_chars(first, last, value, base);
	if (result.ec != std::errc {} || result.ptr != last) {
		Fail(std::string("invalid ") + name + " value: " + std::string(text));
	}
	return value;
}

uint64_t ParseU64(std::string_view text, const char* name) {
	uint64_t value = 0;
	const auto* first = text.data();
	const auto* last  = first + text.size();
	int base = 10;
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		base = 16;
		first += 2;
	}
	const auto result = std::from_chars(first, last, value, base);
	if (result.ec != std::errc {} || result.ptr != last) {
		Fail(std::string("invalid ") + name + " value: " + std::string(text));
	}
	return value;
}

ReplayOptions ParseOptions(int argc, char** argv) {
	ReplayOptions options;
	for (int index = 1; index < argc; index++) {
		const std::string_view argument = argv[index];
		const auto next = [&]() -> std::string_view {
			if (++index >= argc) {
				Fail(std::string("missing value for ") + std::string(argument));
			}
			return argv[index];
		};
		if (argument == "--input") {
			options.input = next();
		} else if (argument == "--capsule") {
			options.capsule = next();
		} else if (argument == "--capture-capsule") {
			options.capsule_output = next();
		} else if (argument == "--output") {
			options.output = next();
		} else if (argument == "--hash") {
			options.hash = ParseU64(next(), "--hash");
		} else if (argument == "--wave-size") {
			options.wave_size = ParseU32(next(), "--wave-size");
		} else if (argument == "--user-data-base") {
			options.user_data_base = ParseU32(next(), "--user-data-base");
		} else if (argument == "--user-data-count") {
			options.user_data_count = ParseU32(next(), "--user-data-count");
		} else if (argument == "--workgroup-register") {
			options.workgroup_register = ParseU32(next(), "--workgroup-register");
		} else if (argument == "--threads") {
			for (auto& thread_count: options.threads_num) {
				thread_count = ParseU32(next(), "--threads");
			}
		} else if (argument == "--help" || argument == "-h") {
			std::printf(
			    "usage: shader_replay_tests (--input shader.bin | --capsule shader.json) [options]\n"
			    "  --output shader.spv\n"
			    "  --capture-capsule shader.json (input mode)\n"
			    "  --hash value --wave-size value --user-data-base value\n"
			    "  --user-data-count value --workgroup-register value\n"
			    "  --threads x y z\n");
			std::exit(EXIT_SUCCESS);
		} else {
			Fail(std::string("unknown argument: ") + std::string(argument));
		}
	}
	if (options.input.empty() == options.capsule.empty()) {
		Fail("exactly one of --input or --capsule is required");
	}
	return options;
}

std::vector<uint32_t> ReadCode(const std::filesystem::path& path) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		Fail(std::string("cannot open input: ") + path.string());
	}
	const auto size = input.tellg();
	if (size <= 0 || size % static_cast<std::streamoff>(sizeof(uint32_t)) != 0) {
		Fail("input size is not a non-empty uint32_t stream");
	}
	std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size))) {
		Fail(std::string("cannot read input: ") + path.string());
	}
	return code;
}

ShaderRecompiler::IR::ResourceSpecialization ShapeSpecialization(
    const ShaderRecompiler::IR::Program& program) {
	using namespace ShaderRecompiler::IR;
	ResourceSpecialization specialization;
	// The runtime materializer fills these layouts from guest memory. A raw shader
	// replay has no guest descriptor reader, but the emitter still needs the exact
	// number of proven columns to type every ReadBoundedSrtU32. Use one synthetic
	// row per column so replay exercises the same resource/IR topology without
	// claiming to reproduce runtime descriptor values.
	specialization.bounded_srt_reads.reserve(program.bounded_srt_reads.size());
	for (uint32_t index = 0; index < program.bounded_srt_reads.size(); index++) {
		specialization.bounded_srt_reads.push_back({1u, index});
	}
	specialization.buffers.reserve(program.info.buffers.size());
	for (const auto& source: program.info.buffers) {
		specialization.buffers.push_back({.packed_stride                   = source.packed_stride,
		                                 .descriptor_format               = source.descriptor_format,
		                                 .descriptor_swizzle              = source.descriptor_swizzle,
		                                 .indirect_root                   = source.indirect_root,
		                                 .indirect_mapping_offset         = source.indirect_mapping_offset,
		                                 .indirect_search_iterations      = source.indirect_search_iterations});
	}
	specialization.images.reserve(program.info.images.size());
	for (const auto& source: program.info.images) {
		// A raw code replay has no descriptor payload. Preserve the static shape and
		// use a valid class for each operation so binding planning and SPIR-V emission
		// can run without Vulkan or guest memory. This is intentionally a shape replay;
		// a production capsule supplies the exact runtime specialization.
		const auto numeric_class =
		    source.atomic
		        ? Prospero::TextureNumericClass::Uint
		        : (source.numeric_class == Prospero::TextureNumericClass::Unsupported
		               ? Prospero::TextureNumericClass::Float
		               : source.numeric_class);
		specialization.images.push_back({.numeric_class                 = numeric_class,
		                                .dimension                     = source.dimension,
		                                .mip_count                     = source.mip_count,
		                                .conversion_format             = source.conversion_format,
		                                .shader_swizzle                = source.shader_swizzle,
		                                .indirect_root                 = source.indirect_root,
		                                .indirect_mapping_offset       = source.indirect_mapping_offset,
		                                .indirect_search_iterations    = source.indirect_search_iterations,
		                                .cube                         = source.cube,
		                                .fmask                        = false});
	}
	return specialization;
}

void Validate(const std::vector<uint32_t>& binary) {
	spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
	std::string messages;
	tools.SetMessageConsumer([&messages](spv_message_level_t, const char*,
	                                    const spv_position_t& position, const char* message) {
		char buffer[1024] = {};
		std::snprintf(buffer, sizeof(buffer), "%zu:%zu: %s\n", position.line, position.column,
		              message);
		messages += buffer;
	});
	if (!tools.Validate(binary)) {
		std::fprintf(stderr, "SPIR-V validation failed:\n%s", messages.c_str());
		std::exit(EXIT_FAILURE);
	}
}

void WriteSpirv(const std::filesystem::path& path, const std::vector<uint32_t>& binary) {
	if (path.empty()) {
		return;
	}
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output || !output.write(reinterpret_cast<const char*>(binary.data()),
                             static_cast<std::streamsize>(binary.size() * sizeof(uint32_t)))) {
		Fail(std::string("cannot write output: ") + path.string());
	}
}

void EnsureInitialized() {
	static Common::Subsystems subsystems;
	Common::InitializeThreads();
	subsystems.Initialize<Config::Lifecycle>();
	Config::ConfigOptions config;
	config.printf_direction = Config::OutputDirection::Silent;
	Config::Load(config);
	subsystems.Initialize<Log::Lifecycle>();
	ShaderInit();
}

} // namespace
} // namespace Libs::Graphics

int main(int argc, char** argv) {
	using namespace Libs::Graphics;
	const auto options = ParseOptions(argc, argv);
	EnsureInitialized();
	ShaderRecompiler::ReplayCapsule capsule;
	std::string capsule_error;
	const bool exact = !options.capsule.empty();
	if (exact &&
	    !ShaderRecompiler::ReadReplayCapsule(options.capsule, capsule, &capsule_error)) {
		Fail(capsule_error);
	}
	const auto code = exact ? capsule.code : ReadCode(options.input);

	ShaderRecompiler::CompileOptions compile_options;
	compile_options.stage           = exact ? capsule.stage : ShaderType::Compute;
	compile_options.wave_size      = exact ? capsule.wave_size : options.wave_size;
	compile_options.user_data_base = exact ? capsule.user_data_base : options.user_data_base;
	compile_options.scratch_dwords = exact ? capsule.scratch_dwords : 0;
	compile_options.shader_hash = exact
	                                 ? capsule.shader_hash
	                                 : (options.hash != 0
	                                        ? options.hash
	                                        : XXH3_64bits(code.data(), code.size() * sizeof(uint32_t)));
	compile_options.dump_ir = false;
	ShaderComputeInputInfo compute = exact ? capsule.compute : ShaderComputeInputInfo {};
	if (!exact) {
		compute.wave_size          = options.wave_size;
		compute.host_subgroup_size = options.wave_size;
		compute.workgroup_register = options.workgroup_register;
		compute.threads_num[0]     = options.threads_num[0];
		compute.threads_num[1]     = options.threads_num[1];
		compute.threads_num[2]     = options.threads_num[2];
	}
	compile_options.input_info.compute = &compute;
	std::vector<uint32_t> user_data = exact ? capsule.user_data
	                                        : std::vector<uint32_t>(options.user_data_count);
	compile_options.user_data = user_data;
	if (exact) {
		compile_options.back_code = capsule.back_code;
	}

	const auto start = std::chrono::steady_clock::now();
	auto translated = ShaderRecompiler::TranslateProgram(code, compile_options);
	const auto specialization =
	    exact ? capsule.specialization : ShapeSpecialization(translated.program);
	if (!options.capsule_output.empty()) {
		if (exact) {
			Fail("--capture-capsule cannot be combined with --capsule");
		}
		std::string capture_error;
		if (!ShaderRecompiler::WriteReplayCapsule(options.capsule_output, code, compile_options,
		                                          specialization, 0, &capture_error)) {
			Fail(capture_error);
		}
	}
	std::printf(
	    "replay plan buffers=%zu images=%zu samplers=%zu srt_reads=%zu bounded=%zu uses_dma=%s\n",
	    translated.program.info.buffers.size(), translated.program.info.images.size(),
	    translated.program.info.samplers.size(), translated.program.srt_reads.size(),
	    translated.program.bounded_srt_reads.size(),
	    translated.program.info.uses_dma ? "true" : "false");
	auto compiled = ShaderRecompiler::CompileProgram(std::move(translated), compile_options,
	                                                 specialization,
	                                                 exact ? capsule.push_data_start_dword : 0u);
	Validate(compiled.spirv);
	WriteSpirv(options.output, compiled.spirv);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
	    std::chrono::steady_clock::now() - start);
	std::printf(
	    "replay hash=0x%016" PRIx64
	    " mode=%s code_words=%zu spirv_words=%zu elapsed_ms=%lld validation=PASS\n",
	    compile_options.shader_hash, exact ? "exact" : "shape", code.size(), compiled.spirv.size(),
	    static_cast<long long>(elapsed.count()));
	return EXIT_SUCCESS;
}
