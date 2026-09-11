#include "graphics/shader/recompiler/ShaderReplayCapsule.h"

#include "common/file.h"
#include "graphics/guest_gpu/gpu_format.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace Libs::Graphics::ShaderRecompiler {
namespace {

using Json = nlohmann::json;

constexpr size_t MaxCodeWords       = 1u << 24u;
constexpr size_t MaxUserDataWords   = 1u << 20u;
constexpr size_t MaxSpecializations = 4096u;

void SetError(std::string* error, std::string_view message) {
	if (error != nullptr) {
		error->assign(message.data(), message.size());
	}
}

template <typename T>
bool ReadValue(const Json& object, const char* key, T& value, std::string* error) {
	if (!object.is_object() || !object.contains(key)) {
		SetError(error, std::string("missing capsule field: ") + key);
		return false;
	}
	try {
		value = object.at(key).get<T>();
	} catch (const Json::exception& exception) {
		SetError(error, std::string("invalid capsule field ") + key + ": " + exception.what());
		return false;
	}
	return true;
}

template <typename T>
bool ReadVector(const Json& object, const char* key, std::vector<T>& value, size_t max_size,
                std::string* error) {
	if (!object.is_object() || !object.contains(key) || !object.at(key).is_array() ||
	    object.at(key).size() > max_size) {
		SetError(error, std::string("invalid capsule array: ") + key);
		return false;
	}
	try {
		value = object.at(key).get<std::vector<T>>();
	} catch (const Json::exception& exception) {
		SetError(error, std::string("invalid capsule array ") + key + ": " + exception.what());
		return false;
	}
	return true;
}

Json ComputeToJson(const ShaderComputeInputInfo& info) {
	return Json {
	    {"threads_num", {info.threads_num[0], info.threads_num[1], info.threads_num[2]}},
	    {"lds_size_dwords", info.lds_size_dwords},
	    {"scratch_size_dwords", info.scratch_size_dwords},
	    {"host_subgroup_size", info.host_subgroup_size},
	    {"wave_size", info.wave_size},
	    {"dispatch_threads_num",
	     {info.dispatch_threads_num[0], info.dispatch_threads_num[1],
	      info.dispatch_threads_num[2]}},
	    {"group_id", {info.group_id[0], info.group_id[1], info.group_id[2]}},
	    {"dispatch_thread_dimensions", info.dispatch_thread_dimensions},
	    {"thread_ids_num", info.thread_ids_num},
	    {"workgroup_register", info.workgroup_register},
	    {"tg_size_en", info.tg_size_en},
	};
}

bool ReadU32Array(const Json& object, const char* key, uint32_t (&values)[3], std::string* error) {
	if (!object.contains(key) || !object.at(key).is_array() || object.at(key).size() != 3) {
		SetError(error, std::string("invalid capsule triple: ") + key);
		return false;
	}
	try {
		for (size_t index = 0; index < 3; index++) {
			values[index] = object.at(key).at(index).get<uint32_t>();
		}
	} catch (const Json::exception& exception) {
		SetError(error, std::string("invalid capsule triple ") + key + ": " + exception.what());
		return false;
	}
	return true;
}

bool ReadBoolArray(const Json& object, const char* key, bool (&values)[3], std::string* error) {
	if (!object.contains(key) || !object.at(key).is_array() || object.at(key).size() != 3) {
		SetError(error, std::string("invalid capsule boolean triple: ") + key);
		return false;
	}
	try {
		for (size_t index = 0; index < 3; index++) {
			values[index] = object.at(key).at(index).get<bool>();
		}
	} catch (const Json::exception& exception) {
		SetError(error,
		         std::string("invalid capsule boolean triple ") + key + ": " + exception.what());
		return false;
	}
	return true;
}

bool ComputeFromJson(const Json& object, ShaderComputeInputInfo& info, std::string* error) {
	if (!object.is_object()) {
		SetError(error, "invalid capsule compute state");
		return false;
	}
	if (!ReadU32Array(object, "threads_num", info.threads_num, error) ||
	    !ReadValue(object, "lds_size_dwords", info.lds_size_dwords, error) ||
	    !ReadValue(object, "scratch_size_dwords", info.scratch_size_dwords, error) ||
	    !ReadValue(object, "host_subgroup_size", info.host_subgroup_size, error) ||
	    !ReadValue(object, "wave_size", info.wave_size, error) ||
	    !ReadU32Array(object, "dispatch_threads_num", info.dispatch_threads_num, error) ||
	    !ReadBoolArray(object, "group_id", info.group_id, error) ||
	    !ReadValue(object, "dispatch_thread_dimensions", info.dispatch_thread_dimensions, error) ||
	    !ReadValue(object, "thread_ids_num", info.thread_ids_num, error) ||
	    !ReadValue(object, "workgroup_register", info.workgroup_register, error) ||
	    !ReadValue(object, "tg_size_en", info.tg_size_en, error)) {
		return false;
	}
	return true;
}

Json SpecializationToJson(const IR::ResourceSpecialization& specialization) {
	Json result {
	    {"bounded_srt_reads", Json::array()},
	    {"buffers", Json::array()},
	    {"images", Json::array()},
	};
	for (const auto& layout: specialization.bounded_srt_reads) {
		result["bounded_srt_reads"].push_back(
		    {{"count", layout.count}, {"flat_offset", layout.flat_offset}});
	}
	for (const auto& buffer: specialization.buffers) {
		result["buffers"].push_back({
		    {"packed_stride", buffer.packed_stride},
		    {"descriptor_format", static_cast<uint32_t>(buffer.descriptor_format)},
		    {"descriptor_swizzle", buffer.descriptor_swizzle},
		    {"indirect_root", buffer.indirect_root},
		    {"indirect_mapping_offset", buffer.indirect_mapping_offset},
		    {"indirect_search_iterations", buffer.indirect_search_iterations},
		});
	}
	for (const auto& image: specialization.images) {
		result["images"].push_back({
		    {"numeric_class", static_cast<uint32_t>(image.numeric_class)},
		    {"dimension", static_cast<uint32_t>(image.dimension)},
		    {"mip_count", image.mip_count},
		    {"conversion_format", static_cast<uint32_t>(image.conversion_format)},
		    {"shader_swizzle", image.shader_swizzle},
		    {"indirect_root", image.indirect_root},
		    {"indirect_mapping_offset", image.indirect_mapping_offset},
		    {"indirect_search_iterations", image.indirect_search_iterations},
		    {"cube", image.cube},
		    {"fmask", image.fmask},
		});
	}
	return result;
}

bool SpecializationFromJson(const Json& object, IR::ResourceSpecialization& specialization,
                            std::string* error) {
	if (!object.is_object() || !object.contains("bounded_srt_reads") ||
	    !object.contains("buffers") || !object.contains("images") ||
	    !object.at("bounded_srt_reads").is_array() || !object.at("buffers").is_array() ||
	    !object.at("images").is_array() ||
	    object.at("bounded_srt_reads").size() > MaxSpecializations ||
	    object.at("buffers").size() > MaxSpecializations ||
	    object.at("images").size() > MaxSpecializations) {
		SetError(error, "invalid capsule specialization arrays");
		return false;
	}
	try {
		for (const auto& item: object.at("bounded_srt_reads")) {
			IR::BoundedSrtLayout layout;
			layout.count       = item.at("count").get<uint32_t>();
			layout.flat_offset = item.at("flat_offset").get<uint32_t>();
			specialization.bounded_srt_reads.push_back(layout);
		}
		for (const auto& item: object.at("buffers")) {
			IR::ResourceSpecialization::Buffer buffer;
			buffer.packed_stride = item.at("packed_stride").get<uint32_t>();
			buffer.descriptor_format =
			    static_cast<Prospero::BufferFormat>(item.at("descriptor_format").get<uint32_t>());
			buffer.descriptor_swizzle      = item.at("descriptor_swizzle").get<uint32_t>();
			buffer.indirect_root           = item.at("indirect_root").get<uint32_t>();
			buffer.indirect_mapping_offset = item.at("indirect_mapping_offset").get<uint32_t>();
			buffer.indirect_search_iterations =
			    item.at("indirect_search_iterations").get<uint32_t>();
			specialization.buffers.push_back(buffer);
		}
		for (const auto& item: object.at("images")) {
			IR::ResourceSpecialization::Image image;
			image.numeric_class = static_cast<Prospero::TextureNumericClass>(
			    item.at("numeric_class").get<uint32_t>());
			image.dimension =
			    static_cast<Decoder::ImageDimension>(item.at("dimension").get<uint32_t>());
			image.mip_count = item.at("mip_count").get<uint32_t>();
			image.conversion_format =
			    static_cast<Prospero::BufferFormat>(item.at("conversion_format").get<uint32_t>());
			image.shader_swizzle          = item.at("shader_swizzle").get<uint32_t>();
			image.indirect_root           = item.at("indirect_root").get<uint32_t>();
			image.indirect_mapping_offset = item.at("indirect_mapping_offset").get<uint32_t>();
			image.indirect_search_iterations =
			    item.at("indirect_search_iterations").get<uint32_t>();
			image.cube  = item.at("cube").get<bool>();
			image.fmask = item.at("fmask").get<bool>();
			specialization.images.push_back(image);
		}
	} catch (const Json::exception& exception) {
		SetError(error, std::string("invalid capsule specialization: ") + exception.what());
		return false;
	}
	return true;
}

} // namespace

bool WriteReplayCapsule(const std::filesystem::path& path, std::span<const uint32_t> code,
                        const CompileOptions&             options,
                        const IR::ResourceSpecialization& specialization,
                        uint32_t push_data_start_dword, std::string* error) {
	if (code.empty() || code.size() > MaxCodeWords || options.stage != ShaderType::Compute ||
	    options.input_info.compute == nullptr || options.user_data.size() > MaxUserDataWords ||
	    options.back_code.size() > MaxCodeWords) {
		SetError(error, "capsule requires a bounded compute compile state");
		return false;
	}
	const auto& compute = *options.input_info.compute;
	Json        object {
	    {"version", ReplayCapsule::CurrentVersion},
	    {"stage", static_cast<uint32_t>(options.stage)},
	    {"shader_hash", options.shader_hash},
	    {"wave_size", options.wave_size},
	    {"user_data_base", options.user_data_base},
	    {"scratch_dwords", options.scratch_dwords},
	    {"push_data_start_dword", push_data_start_dword},
	    {"code", std::vector<uint32_t>(code.begin(), code.end())},
	    {"user_data", std::vector<uint32_t>(options.user_data.begin(), options.user_data.end())},
	    {"back_code", std::vector<uint32_t>(options.back_code.begin(), options.back_code.end())},
	    {"compute", ComputeToJson(compute)},
	    {"specialization", SpecializationToJson(specialization)},
	};

	if (!path.parent_path().empty() && !Common::File::CreateDirectories(path.parent_path())) {
		SetError(error,
		         std::string("cannot create capsule directory: ") + path.parent_path().string());
		return false;
	}
	auto temporary_path = path;
	temporary_path += ".tmp";
	std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
	if (!output) {
		SetError(error, std::string("cannot create capsule: ") + temporary_path.string());
		return false;
	}
	const auto text = object.dump(2);
	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	output.put('\n');
	if (!output) {
		SetError(error, std::string("cannot write capsule: ") + temporary_path.string());
		return false;
	}
	output.flush();
	output.close();
	if (!output || !Common::File::AtomicReplaceFile(temporary_path, path)) {
		SetError(error, std::string("cannot replace capsule: ") + path.string());
		return false;
	}
	return true;
}

bool ReadReplayCapsule(const std::filesystem::path& path, ReplayCapsule& capsule,
                       std::string* error) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) {
		SetError(error, std::string("cannot open capsule: ") + path.string());
		return false;
	}
	const auto size = input.tellg();
	if (size <= 0 || size > static_cast<std::streamoff>(256u * 1024u * 1024u)) {
		SetError(error, "invalid capsule size");
		return false;
	}
	std::string text(static_cast<size_t>(size), '\0');
	input.seekg(0);
	if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) {
		SetError(error, "cannot read capsule");
		return false;
	}

	Json object;
	try {
		object = Json::parse(text);
	} catch (const Json::exception& exception) {
		SetError(error, std::string("invalid capsule JSON: ") + exception.what());
		return false;
	}
	ReplayCapsule result;
	uint32_t      stage = 0;
	if (!ReadValue(object, "version", result.version, error) ||
	    !ReadValue(object, "stage", stage, error) ||
	    !ReadValue(object, "shader_hash", result.shader_hash, error) ||
	    !ReadValue(object, "wave_size", result.wave_size, error) ||
	    !ReadValue(object, "user_data_base", result.user_data_base, error) ||
	    !ReadValue(object, "scratch_dwords", result.scratch_dwords, error) ||
	    !ReadValue(object, "push_data_start_dword", result.push_data_start_dword, error) ||
	    !ReadVector(object, "code", result.code, MaxCodeWords, error) ||
	    !ReadVector(object, "user_data", result.user_data, MaxUserDataWords, error) ||
	    !ReadVector(object, "back_code", result.back_code, MaxCodeWords, error) ||
	    !object.contains("compute") || !object.contains("specialization") ||
	    !ComputeFromJson(object.at("compute"), result.compute, error) ||
	    !SpecializationFromJson(object.at("specialization"), result.specialization, error)) {
		return false;
	}
	if (result.version != ReplayCapsule::CurrentVersion) {
		SetError(error, "unsupported shader replay capsule version");
		return false;
	}
	if (stage != static_cast<uint32_t>(ShaderType::Compute) || result.code.empty()) {
		SetError(error, "capsule stage is not compute or code is empty");
		return false;
	}
	result.stage             = static_cast<ShaderType>(stage);
	result.has_compute_input = true;
	capsule                  = std::move(result);
	return true;
}

} // namespace Libs::Graphics::ShaderRecompiler
