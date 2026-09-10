#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"

#include "common/assert.h"
#include "graphics/guest_gpu/gpu_format.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/shaderBindings.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {

constexpr uint64_t AddressMask            = 0x0000ffffffffffffull;
constexpr uint64_t MaxIndirectImageProbes = 65536u;

bool AddSignedAddress(uint64_t base, int64_t offset, uint64_t& result) {
	if (base > AddressMask) return false;
	if (offset < 0) {
		const auto magnitude = uint64_t {0} - static_cast<uint64_t>(offset);
		if (magnitude > base) return false;
		result = base - magnitude;
		return true;
	}
	const auto magnitude = static_cast<uint64_t>(offset);
	if (magnitude > AddressMask - base) return false;
	result = base + magnitude;
	return true;
}

void SortUniqueOffsets(std::vector<uint32_t>& values) {
	std::ranges::sort(values);
	values.erase(std::unique(values.begin(), values.end()), values.end());
}

struct IndirectDescriptorTable {
	uint32_t                     resource = 0;
	std::vector<uint32_t>        keys;
	std::vector<uint32_t>        candidates;
	std::vector<DescriptorValue> descriptors;
};

struct MaterializedSnapshot {
	ResourceSnapshot                     resources;
	std::vector<IndirectDescriptorTable> indirect_images;
	std::vector<IndirectDescriptorTable> indirect_buffers;
	std::vector<BoundedSrtLayout>        bounded_srt_reads;
};

bool SpecializationFail(std::string_view message) {
	std::fprintf(stderr, "shader resource specialization failed: %.*s\n",
	             static_cast<int>(message.size()), message.data());
	return false;
}

Decoder::ImageDimension DescriptorDimension(const DescriptorValue&  descriptor,
                                            Decoder::ImageDimension requested) {
	const bool is_array = requested == Decoder::ImageDimension::Dim1DArray ||
	                      requested == Decoder::ImageDimension::Dim2DArray ||
	                      requested == Decoder::ImageDimension::Dim2DMsaaArray;
	switch (static_cast<Prospero::ImageType>((descriptor.dwords[3] >> 28u) & 0xfu)) {
		case Prospero::ImageType::kColor1D: return Decoder::ImageDimension::Dim1D;
		case Prospero::ImageType::kColor1DArray:
			if (is_array) {
				return Decoder::ImageDimension::Dim1DArray;
			}
			return Decoder::ImageDimension::Dim1D;
		case Prospero::ImageType::kColor3D: return Decoder::ImageDimension::Dim3D;
		case Prospero::ImageType::kCube: return Decoder::ImageDimension::Dim2DArray;
		case Prospero::ImageType::kColor2DArray:
			if (is_array) {
				return Decoder::ImageDimension::Dim2DArray;
			}
			return Decoder::ImageDimension::Dim2D;
		case Prospero::ImageType::kColor2DMsaaArray:
			if (is_array) {
				return Decoder::ImageDimension::Dim2DMsaaArray;
			}
			return Decoder::ImageDimension::Dim2DMsaa;
		case Prospero::ImageType::kColor2D: return Decoder::ImageDimension::Dim2D;
		case Prospero::ImageType::kColor2DMsaa: return Decoder::ImageDimension::Dim2DMsaa;
		default: return Decoder::ImageDimension::Unknown;
	}
}

bool NullImageDescriptor(const DescriptorValue& descriptor) {
	return descriptor.dwords[0] == 0 && (descriptor.dwords[1] & 0xffu) == 0;
}

bool ValidImageDescriptor(const DescriptorValue& descriptor, bool r128 = false) {
	const auto type   = static_cast<Prospero::ImageType>((descriptor.dwords[3] >> 28u) & 0xfu);
	const auto format = static_cast<Prospero::BufferFormat>((descriptor.dwords[1] >> 20u) & 0x1ffu);
	if (type < Prospero::ImageType::kColor1D || format == Prospero::BufferFormat::kInvalid ||
	    format > Prospero::BufferFormat::kBc7Srgb) {
		return false;
	}
	if (r128 && type != Prospero::ImageType::kColor1D && type != Prospero::ImageType::kColor2D &&
	    type != Prospero::ImageType::kColor2DMsaa) {
		return false;
	}
	// DST_SEL encodings 2 and 3 are reserved. Speculative table candidates may
	// point at non-image data and must not reach native image-view creation.
	for (uint32_t channel = 0; channel < 4; ++channel) {
		const auto select = (descriptor.dwords[3] >> (channel * 3u)) & 7u;
		if (select == 2u || select == 3u) return false;
	}
	if (!r128) {
		// RDNA2 ISA 8.2.6: word 4 has depth[12:0], pitch[13], and base_array[28:16].
		// Reserved bits must be zero. Finite table enumeration can encounter adjacent
		// non-image data; accepting its address bits here creates invalid host images.
		if ((descriptor.dwords[4] & 0xe000c000u) != 0u ||
		    (descriptor.dwords[6] & 0x00007800u) != 0u) {
			return false;
		}
		const bool array = type == Prospero::ImageType::kColor1DArray ||
		                   type == Prospero::ImageType::kColor2DArray ||
		                   type == Prospero::ImageType::kColor2DMsaaArray ||
		                   type == Prospero::ImageType::kCube;
		if (array && ((descriptor.dwords[4] >> 16u) & 0x1fffu) > (descriptor.dwords[4] & 0x1fffu)) {
			return false;
		}
	}
	if (type == Prospero::ImageType::kColor2DMsaa ||
	    type == Prospero::ImageType::kColor2DMsaaArray) {
		const auto base_level = (descriptor.dwords[3] >> 12u) & 0xfu;
		const auto fragments  = (descriptor.dwords[3] >> 16u) & 0xfu;
		const auto max_mip    = (descriptor.dwords[5] >> 4u) & 0xfu;
		return base_level == 0 && fragments >= 1 && fragments <= 3 &&
		       (r128 || max_mip == fragments);
	}
	const auto base_level = (descriptor.dwords[3] >> 12u) & 15u;
	const auto last_level = (descriptor.dwords[3] >> 16u) & 15u;
	const auto max_mip    = (descriptor.dwords[5] >> 4u) & 15u;
	return base_level <= last_level && (r128 || base_level <= max_mip);
}

uint32_t DescriptorImageSwizzle(const DescriptorValue& descriptor) {
	return descriptor.dwords[3] & 0xfffu;
}

Prospero::BufferFormat ImageConversionFormat(Prospero::BufferFormat format) {
	return Prospero::RemapTextureFormat(format) != format ? format
	                                                      : Prospero::BufferFormat::kInvalid;
}

bool RequiresPointSampler(const ImageResource& image) {
	return image.numeric_class == Prospero::TextureNumericClass::Sint ||
	       image.numeric_class == Prospero::TextureNumericClass::Uint ||
	       image.conversion_format != Prospero::BufferFormat::kInvalid;
}

bool RequiresPointSampler(const ResourceSpecialization::Image& image) {
	return image.numeric_class == Prospero::TextureNumericClass::Sint ||
	       image.numeric_class == Prospero::TextureNumericClass::Uint ||
	       image.conversion_format != Prospero::BufferFormat::kInvalid;
}

bool DescriptorIsCube(const DescriptorValue& descriptor) {
	return static_cast<Prospero::ImageType>((descriptor.dwords[3] >> 28u) & 0xfu) ==
	       Prospero::ImageType::kCube;
}

uint32_t StorageMipCount(const ImageResource& image, const DescriptorValue& descriptor) {
	if (image.mip_mode != ImageMipMode::DynamicStorage || NullImageDescriptor(descriptor)) {
		return 1;
	}
	const auto base = (descriptor.dwords[3] >> 12u) & 0xfu;
	const auto last = (descriptor.dwords[3] >> 16u) & 0xfu;
	return base <= last ? last - base + 1u : 0u;
}

bool DecodeBufferDescriptor(const DescriptorValue& descriptor, ShaderBufferResource& result) {
	if (descriptor.dword_count != std::size(result.fields)) {
		return false;
	}
	std::copy_n(descriptor.dwords.begin(), std::size(result.fields), result.fields);
	return true;
}

const DescriptorSource* Source(const ResourcePlan& program, uint32_t source) {
	if (source >= program.descriptor_sources.size()) {
		return nullptr;
	}
	return &program.descriptor_sources[source];
}

void MarkCleanFlatSlots(const ResourcePlan& program, const DescriptorSource* source,
                        std::span<uint8_t> slots) {
	if (source == nullptr) {
		return;
	}
	std::vector<Value>       pending(source->dwords.begin(),
	                                 source->dwords.begin() + source->dword_count);
	std::vector<const Inst*> visited;
	while (!pending.empty()) {
		auto value = pending.back().Resolve();
		pending.pop_back();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || std::ranges::find(visited, inst) != visited.end()) {
			continue;
		}
		visited.push_back(inst);
		if (inst->GetOpcode() == ValueOpcode::ReadConst) {
			const auto slot = inst->Arg(1).Resolve();
			if (slot.IsImmediate() && slot.GetType() == Type::U32 && slot.U32() < slots.size()) {
				slots[slot.U32()] = 1u;
				pending.push_back(program.srt_reads[slot.U32()].value);
			}
			continue;
		}
		for (size_t arg = 0; arg < inst->NumArgs(); arg++) {
			pending.push_back(inst->Arg(arg));
		}
	}
}

uint64_t ScalarBufferSize(const ShaderBufferResource& descriptor) {
	return descriptor.Stride() == 0u
	           ? descriptor.NumRecords()
	           : static_cast<uint64_t>(descriptor.Stride()) * descriptor.NumRecords();
}

bool ReadSpecializationWord(const SrtRuntime& runtime, uint64_t address, uint32_t& word) {
	return runtime.read_specialization_memory != nullptr &&
	       runtime.read_specialization_memory(runtime.userdata, address, &word);
}

bool MaterializeBoundedReads(const ResourcePlan& program, const SrtRuntime& runtime,
                             MaterializedSnapshot& snapshot) {
	if (program.bounded_srt_reads.empty()) return true;
	if (runtime.read_specialization_memory == nullptr) {
		return SpecializationFail("bounded SRT reads require the coherent specialization reader");
	}
	SrtRuntime clean  = runtime;
	clean.read_memory = runtime.read_specialization_memory;
	uint64_t probes   = 0;
	for (uint32_t id = 0; id < program.bounded_srt_reads.size(); ++id) {
		const auto& read           = program.bounded_srt_reads[id];
		const auto* address_source = Source(program, read.address_source);
		if (address_source == nullptr ||
		    (address_source->dword_count != 2u && address_source->dword_count != 4u)) {
			return SpecializationFail("bounded SRT address source has an invalid width");
		}
		uint32_t count = 0;
		if (read.workgroup_axis != UINT32_MAX) {
			if (read.workgroup_axis >= 3u) return SpecializationFail("bounded SRT axis is invalid");
			count = runtime.workgroup_count[read.workgroup_axis];
		} else {
			const auto* count_source = Source(program, read.count_source);
			if (count_source == nullptr || count_source->dword_count != 1u) {
				return SpecializationFail("bounded SRT count source has an invalid width");
			}
			DescriptorValue value;
			if (!EvaluateDescriptorSource(program, read.count_source, clean, value)) {
				return SpecializationFail(
				    fmt::format("bounded SRT read {} count is not host-readable", id));
			}
			count = value.dwords[0];
		}
		std::fprintf(
		    stderr, "bounded SRT read %u count=%u source_dwords=%u scale=%u bias=%u offset=%u\\n",
		    id, count, read.source_dwords, read.offset_scale, read.offset_bias, read.memory_offset);
		std::fflush(stderr);
		probes += count;
		if (probes > MaxIndirectImageProbes ||
		    snapshot.resources.flattened_srt.size() > UINT32_MAX - uint64_t {count}) {
			return SpecializationFail(
			    fmt::format("bounded SRT read {} exceeds materialization limits", id));
		}
		const auto flat_offset = static_cast<uint32_t>(snapshot.resources.flattened_srt.size());
		snapshot.bounded_srt_reads.push_back({count, flat_offset});
		if (count == 0u) continue;
		DescriptorValue address;
		if (!EvaluateDescriptorSource(program, read.address_source, clean, address)) {
			return SpecializationFail(
			    fmt::format("bounded SRT read {} address is not host-readable", id));
		}
		const uint64_t base =
		    ((uint64_t {address.dwords[1]} << 32u) | address.dwords[0]) & AddressMask;
		const int64_t immediate = static_cast<int64_t>(static_cast<int32_t>(read.memory_offset));
		for (uint32_t index = 0; index < count; ++index) {
			const uint32_t dynamic  = index * read.offset_scale + read.offset_bias;
			int64_t        relative = 0;
			if (read.source_dwords == 4u) {
				if (immediate < 0)
					return SpecializationFail("bounded scalar-buffer offset is negative");
				const uint64_t byte_offset = static_cast<uint64_t>(immediate) + dynamic;
				const uint64_t aligned     = byte_offset & ~uint64_t {3};
				const uint32_t stride      = (address.dwords[1] >> 16u) & 0x3fffu;
				const uint64_t bytes       = stride == 0u ? uint64_t {address.dwords[2]}
				                                          : uint64_t {stride} * address.dwords[2];
				if (aligned > bytes || bytes - aligned < sizeof(uint32_t)) {
					snapshot.resources.flattened_srt.push_back(0u);
					continue;
				}
				relative = static_cast<int64_t>(aligned);
			} else {
				relative =
				    (immediate & ~int64_t {3}) + static_cast<int64_t>(dynamic & ~uint32_t {3});
			}
			uint64_t guest = 0;
			uint32_t word  = 0;
			if (!AddSignedAddress(base & ~uint64_t {3}, relative, guest) ||
			    !ReadSpecializationWord(runtime, guest, word)) {
				return SpecializationFail(
				    fmt::format("bounded SRT read {} candidate {} is unreadable", id, index));
			}
			snapshot.resources.flattened_srt.push_back(word);
		}
	}
	return true;
}

bool MaterializeBoundedBuffers(const ResourcePlan& program, const SrtRuntime& runtime,
                               MaterializedSnapshot& snapshot) {
	if (program.bounded_srt_reads.empty()) return true;
	for (uint32_t logical = 0; logical < program.info.buffers.size(); ++logical) {
		const auto& buffer = program.info.buffers[logical];
		const auto* source = Source(program, buffer.source);
		if (source == nullptr || !source->bounded_buffer.has_value()) continue;
		const auto& bounded = *source->bounded_buffer;
		const auto  read_id =
		    bounded.expression
		        ? (bounded.dependencies.empty() ? UINT32_MAX : bounded.dependencies.front())
		        : bounded.reads[0];
		if (read_id >= snapshot.bounded_srt_reads.size()) {
			return SpecializationFail("bounded buffer has an invalid read column");
		}
		const auto layout = snapshot.bounded_srt_reads[read_id];
		std::fprintf(stderr,
		             "bounded buffer logical=%u source=%u expression=%u count=%u deps=%zu\\n",
		             logical, buffer.source, bounded.expression ? 1u : 0u, layout.count,
		             bounded.dependencies.size());
		std::fflush(stderr);
		IndirectDescriptorTable table;
		table.resource = logical;
		table.keys.reserve(layout.count);
		table.candidates.reserve(layout.count);
		for (uint32_t candidate = 0; candidate < layout.count; ++candidate) {
			DescriptorValue descriptor;
			if (bounded.expression) {
				if (!EvaluateBoundedDescriptorSource(
				        program, buffer.source, runtime, snapshot.bounded_srt_reads,
				        snapshot.resources.flattened_srt, candidate, descriptor)) {
					return SpecializationFail(fmt::format(
					    "bounded buffer {} expression candidate {} failed", logical, candidate));
				}
			} else {
				descriptor.dword_count = 4u;
				for (uint32_t word = 0; word < 4u; ++word) {
					const auto column = bounded.reads[word];
					if (column >= snapshot.bounded_srt_reads.size()) return false;
					const auto column_layout = snapshot.bounded_srt_reads[column];
					descriptor.dwords[word] =
					    snapshot.resources.flattened_srt[column_layout.flat_offset + candidate];
				}
			}
			ShaderBufferResource decoded;
			if (!DecodeBufferDescriptor(descriptor, decoded)) return false;
			if (decoded.Type() != 0u) descriptor.dwords.fill(0);
			const auto found    = std::ranges::find(table.descriptors, descriptor);
			const auto selected = found == table.descriptors.end()
			                          ? static_cast<uint32_t>(table.descriptors.size())
			                          : static_cast<uint32_t>(found - table.descriptors.begin());
			if (found == table.descriptors.end()) table.descriptors.push_back(descriptor);
			table.keys.push_back(candidate);
			table.candidates.push_back(selected);
		}
		if (table.descriptors.empty()) table.descriptors.push_back({});
		snapshot.resources.buffers[logical] = table.descriptors[0];
		if (table.descriptors.size() > 1u) snapshot.indirect_buffers.push_back(std::move(table));
	}
	return true;
}

// Enumerate a conservative, finite set of GPU-selected table offsets. Memory used to
// narrow the set must be CPU-known for this dispatch, just like the descriptors themselves.
bool ReadScalarBufferWord(const ShaderBufferResource& descriptor, uint32_t dynamic_offset,
                          uint32_t immediate_offset, const SrtRuntime& runtime, uint32_t& word);

class BufferOffsets {
public:
	BufferOffsets(const ResourcePlan& program, const SrtRuntime& runtime,
	              std::span<const DescriptorSource::IndexRange> ranges = {})
	    : m_program(program), m_runtime(runtime), m_ranges(ranges) {
		m_runtime.read_memory = runtime.read_specialization_memory;
	}

	bool EvaluateDispatch(Value value, std::vector<uint32_t>& out) {
		if (EvaluateCountedRows(value, out, true)) return true;
		// Most masked selectors already have a small conservative domain. Only
		// partition by workgroup when that domain cannot be resolved as a whole.

		const bool finite = Evaluate(value, out);

		if (finite) return true;
		if (EvaluateCountedRows(value, out)) return true;
		// Keep a tile's address and its loop bound correlated. Combining the bounds
		// of different workgroups would also read unused slots in each tile's list.
		std::array<bool, 3>             axes {};
		std::vector<Value>              pending {value};
		std::unordered_set<const Inst*> visited;
		for (const auto& range: m_ranges) {
			pending.push_back(range.begin);
			pending.push_back(range.end);
		}
		while (!pending.empty()) {
			const auto* inst = pending.back().Resolve().TryInstruction();
			pending.pop_back();
			if (inst == nullptr || !visited.insert(inst).second) continue;
			if (inst->GetOpcode() == ValueOpcode::GetBuiltin &&
			    inst->Arg(0) == Value(static_cast<uint32_t>(StageInputKind::WorkgroupId))) {
				const auto axis = inst->Arg(1).Resolve();
				if (!axis.IsImmediate() || axis.U32() >= axes.size()) return false;
				axes[axis.U32()] = true;
			}
			// GPU-only operations are evaluated through a finite mask or a buffer
			// domain fallback. Their own operands do not specialize that domain.
			switch (inst->GetOpcode()) {
				case ValueOpcode::Phi:
				case ValueOpcode::ReadFirstLane:
				case ValueOpcode::SelectU32:
				case ValueOpcode::ReadConstBuffer:
				case ValueOpcode::LoadAddressU32:
				case ValueOpcode::IAdd32:
				case ValueOpcode::ISub32:
				case ValueOpcode::IMul32:
				case ValueOpcode::BitwiseAnd32:
				case ValueOpcode::BitwiseOr32:
				case ValueOpcode::ShiftLeftLogical32:
				case ValueOpcode::ShiftRightLogical32:
				case ValueOpcode::BitFieldUExtract:
				case ValueOpcode::LogicalNot:
				case ValueOpcode::LogicalAnd:
				case ValueOpcode::LogicalOr:
				case ValueOpcode::IEqual32:
				case ValueOpcode::INotEqual32:
				case ValueOpcode::ULessThan32:
				case ValueOpcode::UGreaterThan32: break;
				default: continue;
			}
			for (size_t i = 0; i < inst->NumArgs(); ++i)
				pending.push_back(inst->Arg(i));
		}
		if (std::ranges::none_of(axes, [](bool used) { return used; })) return false;
		std::array<uint32_t, 3> extent {1, 1, 1};
		uint64_t                groups = 1;
		for (size_t axis = 0; axis < axes.size(); ++axis) {
			if (!axes[axis]) continue;
			extent[axis] = m_runtime.workgroup_count[axis];
			if (extent[axis] == 0 || extent[axis] > MaxValues || groups > MaxValues / extent[axis])
				return false;
			groups *= extent[axis];
		}
		std::unordered_set<uint32_t> keys;

		for (uint32_t z = 0; z < extent[2]; ++z) {
			for (uint32_t y = 0; y < extent[1]; ++y) {
				for (uint32_t x = 0; x < extent[0]; ++x) {
					BufferOffsets group(m_program, m_runtime, m_ranges);
					group.m_invariants = m_invariants;
					group.m_workgroup  = std::array {x, y, z};
					std::vector<uint32_t> values;
					if (!group.Evaluate(value, values)) return false;
					keys.insert(values.begin(), values.end());
					if (keys.size() > MaxValues) return false;
				}
			}
		}
		out.assign(keys.begin(), keys.end());
		std::ranges::sort(out);

		return true;
	}

	bool Evaluate(Value value, std::vector<uint32_t>& out) {
		value = value.Resolve();
		if (value.GetType() != Type::U32) {
			return false;
		}
		if (value.IsImmediate()) {
			out = {value.U32()};
			return true;
		}
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || m_visiting.size() >= 128 || m_visiting.contains(inst)) {
			return false;
		}
		if (const auto assigned = m_assigned.find(inst); assigned != m_assigned.end()) {
			out = assigned->second;
			return true;
		}
		if (inst == m_row_alias && m_active_mask == m_row_mask) {
			out = m_assigned.at(m_bound_row);
			return true;
		}
		if (const auto it = m_cache.find(inst); it != m_cache.end()) {
			if (!it->second) return false;
			out = *it->second;
			return true;
		}
		m_visiting.insert(inst);
		uint32_t   word  = 0;
		bool       valid = false;
		const auto range = std::ranges::find_if(
		    m_ranges, [&](const auto& candidate) { return candidate.value.Resolve() == value; });
		if (range != m_ranges.end()) {
			std::vector<uint32_t> begins, ends;
			valid = Evaluate(range->begin, begins) && Evaluate(range->end, ends);
			for (const auto& [bound, limit]: range->bound_limits) {
				std::vector<uint32_t> values;
				if (!valid || !Evaluate(bound, values) || values.empty() ||
				    *std::ranges::max_element(values) > limit) {
					valid = false;
					break;
				}
			}
			if (valid && !begins.empty() && !ends.empty()) {
				const auto begin = *std::ranges::min_element(begins);
				const auto end   = *std::ranges::max_element(ends);
				valid            = begin <= INT32_MAX && end <= INT32_MAX &&
				                   (end <= begin || end - begin <= MaxValues);
				if (valid) {
					out.resize(end > begin ? end - begin : 0u);
					std::iota(out.begin(), out.end(), begin);
				}
			} else if (valid) {
				out.clear();
			}
		} else if (IsUniform(value)) {
			const auto found = m_invariants->values.find(inst);
			if (found != m_invariants->values.end()) {
				valid = found->second.has_value();
				if (valid) word = *found->second;
			} else {
				valid = EvaluateUniformValues(m_program, std::span(&value, 1), m_runtime,
				                              std::span(&word, 1), &m_invariants->uniform_values);
				m_invariants->values.emplace(inst, valid ? std::optional {word} : std::nullopt);
			}
			if (valid) {
				out = {word};
			}
		} else {
			valid = EvaluateInst(*inst, out);
			if (!valid && !m_read_failed) {
				const auto mask = PossibleBits(value, 0);
				if (std::popcount(mask) <= 10) {
					out.clear();
					uint32_t bits = mask;
					do {
						out.push_back(bits);
						bits = (bits - 1u) & mask;
					} while (bits != mask);
					valid = true;
				}
			}
		}
		m_visiting.erase(inst);
		if (valid) {
			SortUniqueOffsets(out);
			valid = out.size() <= MaxValues;
		}
		m_cache[inst] = valid ? std::optional {out} : std::nullopt;
		return valid;
	}

private:
	static constexpr size_t MaxValues = 65536;
	bool EvaluateCountedRows(Value value, std::vector<uint32_t>& out, bool preferred = false) {
		if (m_counted_attempted) return false;
		// A zero count prevents the guarded resource access. Enumerate only rows
		// whose count is in bounds, keeping each row and its count correlated.
		if (m_ranges.size() != 1 || m_ranges[0].begin.Resolve() != Value(0u)) return false;
		const auto& range = m_ranges[0];
		const auto* count = range.end.Resolve().TryInstruction();
		if (count == nullptr || count->GetOpcode() != ValueOpcode::ReadConstBuffer) return false;
		const auto* offset = count->Arg(1).Resolve().TryInstruction();
		if (offset == nullptr || offset->GetOpcode() != ValueOpcode::ShiftLeftLogical32 ||
		    offset->Arg(1).Resolve() != Value(2u))
			return false;
		const auto row = offset->Arg(0).Resolve();
		if (row.TryInstruction() == nullptr || IsUniform(row)) return false;
		const auto row_op = row.TryInstruction()->GetOpcode();
		if (preferred && row_op != ValueOpcode::ReadFirstLane && row_op != ValueOpcode::ReadLane)
			return false;
		m_counted_attempted = true;
		Value row_alias, row_mask;
		if (row.TryInstruction()->GetOpcode() == ValueOpcode::ReadFirstLane) {
			row_alias = row.TryInstruction()->Arg(0).Resolve();
			row_mask  = row.TryInstruction()->Arg(1).Resolve();
			if (row_alias.TryInstruction() == nullptr) return false;
		}
		const auto memory_index = count->Flags<MemoryFlags>().index;
		if (memory_index >= m_program.memory_info.size()) return false;
		const auto& memory = m_program.memory_info[memory_index];
		if (memory.kind != ResourceKind::ScalarBuffer || memory.offset != 0) return false;
		const auto* handle = count->Arg(0).Resolve().TryInstruction();
		if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetBufferResource)
			return false;
		const std::array     roots {handle->Arg(0), handle->Arg(1), handle->Arg(2), handle->Arg(3)};
		ShaderBufferResource descriptor;
		if (!std::ranges::all_of(roots, [&](Value root) { return IsUniform(root); }) ||
		    !EvaluateUniformValues(m_program, roots, m_runtime, descriptor.fields,
		                           &m_invariants->uniform_values))
			return false;
		const auto rows = ScalarBufferSize(descriptor) / sizeof(uint32_t);
		if (rows > MaxValues) return false;
		// The output must require a loop index, so an out-of-bounds zero count
		// cannot contribute a resource through an unrelated select/phi branch.
		std::unordered_set<const Inst*> active;
		const auto requires_index = [&](auto&& self, Value input, Value mask) -> bool {
			input = input.Resolve();
			if (input == range.value.Resolve()) return true;
			const auto* inst = input.TryInstruction();
			if (inst == nullptr || !active.insert(inst).second) return false;
			bool required = false;
			switch (inst->GetOpcode()) {
				case ValueOpcode::ReadFirstLane:
					required = self(self, inst->Arg(0), inst->Arg(1).Resolve());
					break;
				case ValueOpcode::SelectU32:
					if (!mask.IsEmpty() && inst->Arg(0).Resolve() == mask)
						required = self(self, inst->Arg(1), mask);
					break;
				case ValueOpcode::ReadConstBuffer:
				case ValueOpcode::LoadAddressU32: required = self(self, inst->Arg(1), mask); break;
				case ValueOpcode::IAdd32:
				case ValueOpcode::ISub32:
				case ValueOpcode::IMul32:
				case ValueOpcode::BitwiseAnd32:
				case ValueOpcode::BitwiseOr32:
				case ValueOpcode::ShiftLeftLogical32:
				case ValueOpcode::ShiftRightLogical32:
					required = self(self, inst->Arg(0), mask) || self(self, inst->Arg(1), mask);
					break;
				default: break;
			}
			active.erase(inst);
			return required;
		};
		if (!requires_index(requires_index, value, {})) return false;
		// count[row] uses a wrapping 32-bit byte offset. High two row bits
		// may be discarded only if every other use discards them as well.
		std::vector<std::pair<Value, Value>> pending {{value, {}}, {range.end, {}}};
		for (const auto& [bound, limit]: range.bound_limits)
			pending.emplace_back(bound, Value {});
		std::unordered_map<const Inst*, std::vector<Value>> visited;
		while (!pending.empty()) {
			const auto [input_value, mask] = pending.back();
			const auto input               = input_value.Resolve();
			pending.pop_back();
			if (input == range.value.Resolve()) continue;
			const auto* inst = input.TryInstruction();
			if (inst == nullptr) continue;
			auto& contexts = visited[inst];
			if (std::ranges::find(contexts, mask) != contexts.end()) continue;
			contexts.push_back(mask);
			if (inst->GetOpcode() == ValueOpcode::ReadFirstLane) {
				pending.emplace_back(inst->Arg(0), inst->Arg(1).Resolve());
				continue;
			}
			if (inst->GetOpcode() == ValueOpcode::SelectU32 && !mask.IsEmpty() &&
			    inst->Arg(0).Resolve() == mask) {
				pending.emplace_back(inst->Arg(1), mask);
				continue;
			}
			for (size_t arg = 0; arg < inst->NumArgs(); ++arg) {
				const auto source = inst->Arg(arg).Resolve();
				if (source == row || (!row_alias.IsEmpty() && source == row_alias)) {
					if (source == row_alias && mask != row_mask) return false;
					if (arg != 0 || inst->GetOpcode() != ValueOpcode::ShiftLeftLogical32)
						return false;
					const auto shift = inst->Arg(1).Resolve();
					if (!shift.IsImmediate() || (shift.U32() & 31u) < 2u) return false;
				} else {
					pending.emplace_back(inst->Arg(arg), mask);
				}
			}
		}
		std::unordered_map<uint32_t, std::vector<uint32_t>> counted_rows;
		for (uint32_t index = 0; index < rows; ++index) {
			uint32_t length = 0;
			if (!ReadScalarBufferWord(descriptor, index * 4u, 0, m_runtime, length) ||
			    length > MaxValues)
				return false;
			if (length != 0) counted_rows[length].push_back(index);
		}
		std::unordered_set<uint32_t> keys;
		// Rows with the same count can be evaluated together without mixing the
		// bounds of different lists. Bound each batch's address cross product.
		for (const auto& [length, indices]: counted_rows) {
			const auto batch_size = std::min<size_t>(256, MaxValues / length);
			for (size_t begin = 0; begin < indices.size(); begin += batch_size) {
				const auto    end = std::min(begin + batch_size, indices.size());
				BufferOffsets tile(m_program, m_runtime, m_ranges);
				tile.m_invariants = m_invariants;
				tile.m_assigned.emplace(
				    row.TryInstruction(),
				    std::vector<uint32_t>(indices.begin() + begin, indices.begin() + end));
				tile.m_assigned.emplace(count, std::vector<uint32_t> {length});
				tile.m_bound_row = row.TryInstruction();
				tile.m_row_alias = row_alias.TryInstruction();
				tile.m_row_mask  = row_mask;
				std::vector<uint32_t> values;
				if (!tile.Evaluate(value, values)) return false;
				keys.insert(values.begin(), values.end());
				// Some compute passes populate only their dispatched rows. A broad
				// table domain can include many stale entries; prefer the ordinary
				// dispatch-aware analysis when that domain ceases to be small.
				if (keys.size() > 128u) return false;
			}
		}
		out.assign(keys.begin(), keys.end());
		std::ranges::sort(out);
		return true;
	}

	struct InvariantCache {
		UniformValueCache                                        uniform_values;
		std::unordered_map<const Inst*, bool>                    uniform;
		std::unordered_map<const Inst*, std::optional<uint32_t>> values;
		std::unordered_map<const Inst*, ShaderBufferResource>    buffers;
	};
	bool IsUniform(Value value) {
		const auto* inst = value.Resolve().TryInstruction();
		if (inst == nullptr) return ValidateRuntimeValue(m_program, value);
		const auto found = m_invariants->uniform.find(inst);
		if (found != m_invariants->uniform.end()) return found->second;
		const bool uniform = ValidateRuntimeValue(m_program, value);
		m_invariants->uniform.emplace(inst, uniform);
		return uniform;
	}

	bool Implies(Value predicate, bool truth, Value wanted, bool wanted_truth,
	             uint32_t depth = 0) const {
		predicate = predicate.Resolve();
		wanted    = wanted.Resolve();
		if (predicate == wanted) {
			return truth == wanted_truth;
		}
		const auto* inst = predicate.TryInstruction();
		if (inst == nullptr || depth >= 64) {
			return false;
		}
		if (inst->GetOpcode() == ValueOpcode::LogicalNot) {
			return Implies(inst->Arg(0), !truth, wanted, wanted_truth, depth + 1);
		}
		if ((truth && inst->GetOpcode() == ValueOpcode::LogicalAnd) ||
		    (!truth && inst->GetOpcode() == ValueOpcode::LogicalOr)) {
			return Implies(inst->Arg(0), truth, wanted, wanted_truth, depth + 1) ||
			       Implies(inst->Arg(1), truth, wanted, wanted_truth, depth + 1);
		}
		return false;
	}

	bool Nonzero(Value value, Value predicate, uint32_t depth = 0) const {
		const auto* inst = predicate.Resolve().TryInstruction();
		if (inst == nullptr || depth >= 64) {
			return false;
		}
		if (inst->GetOpcode() == ValueOpcode::LogicalAnd) {
			return Nonzero(value, inst->Arg(0), depth + 1) ||
			       Nonzero(value, inst->Arg(1), depth + 1);
		}
		const auto op = inst->GetOpcode();
		return ((op == ValueOpcode::ULessThan32 || op == ValueOpcode::INotEqual32) &&
		        inst->Arg(0).Resolve() == Value(0u) && inst->Arg(1).Resolve() == value.Resolve()) ||
		       ((op == ValueOpcode::UGreaterThan32 || op == ValueOpcode::INotEqual32) &&
		        inst->Arg(1).Resolve() == Value(0u) && inst->Arg(0).Resolve() == value.Resolve());
	}

	std::optional<bool> Predicate(Value value, uint32_t depth = 0) {
		value = value.Resolve();
		if (value.IsImmediate() && value.GetType() == Type::U1) {
			return value.U1();
		}
		if (depth >= 64) {
			return {};
		}
		if (!m_active_mask.IsEmpty()) {
			if (Implies(m_active_mask, true, value, true)) return true;
			if (Implies(m_active_mask, true, value, false)) return false;
		}
		const auto* inst = value.TryInstruction();
		if (inst == nullptr) return {};
		const auto op = inst->GetOpcode();
		if (op == ValueOpcode::LogicalNot) {
			const auto arg = Predicate(inst->Arg(0), depth + 1);
			return arg ? std::optional<bool> {!*arg} : std::nullopt;
		}
		if (op == ValueOpcode::LogicalAnd || op == ValueOpcode::LogicalOr) {
			const auto a = Predicate(inst->Arg(0), depth + 1);
			const auto b = Predicate(inst->Arg(1), depth + 1);
			if (op == ValueOpcode::LogicalAnd) {
				if ((a && !*a) || (b && !*b)) return false;
				if (a && b) return *a && *b;
			} else {
				if ((a && *a) || (b && *b)) return true;
				if (a && b) return *a || *b;
			}
			return {};
		}
		if (op != ValueOpcode::IEqual32 && op != ValueOpcode::INotEqual32 &&
		    op != ValueOpcode::ULessThan32 && op != ValueOpcode::UGreaterThan32)
			return {};
		if ((op == ValueOpcode::IEqual32 || op == ValueOpcode::INotEqual32) &&
		    ((inst->Arg(0).Resolve() == Value(0u) && Nonzero(inst->Arg(1), m_active_mask)) ||
		     (inst->Arg(1).Resolve() == Value(0u) && Nonzero(inst->Arg(0), m_active_mask)))) {
			return op == ValueOpcode::INotEqual32;
		}
		std::vector<uint32_t> lhs, rhs;
		if (!Evaluate(inst->Arg(0), lhs) || !Evaluate(inst->Arg(1), rhs) ||
		    lhs.size() * rhs.size() > MaxValues)
			return {};
		std::optional<bool> result;
		for (const auto a: lhs) {
			for (const auto b: rhs) {
				const bool test = op == ValueOpcode::IEqual32      ? a == b
				                  : op == ValueOpcode::INotEqual32 ? a != b
				                  : op == ValueOpcode::ULessThan32 ? a < b
				                                                   : a > b;
				if (result && *result != test) return {};
				result = test;
			}
		}
		return result;
	}

	uint32_t PossibleBits(Value value, uint32_t depth, Value active_mask = {}) const {
		value = value.Resolve();
		if (value.IsImmediate() && value.GetType() == Type::U32) {
			return value.U32();
		}
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || depth > 32) {
			return UINT32_MAX;
		}
		const auto op = inst->GetOpcode();
		if (op == ValueOpcode::BitFieldUExtract) {
			const auto offset = inst->Arg(1).Resolve();
			const auto width  = inst->Arg(2).Resolve();
			if (offset.IsImmediate() && width.IsImmediate() && offset.U32() <= 32u &&
			    width.U32() <= 32u - offset.U32()) {
				if (width.U32() == 0u) return 0u;
				const auto mask = width.U32() == 32u ? UINT32_MAX : (1u << width.U32()) - 1u;
				return (PossibleBits(inst->Arg(0), depth + 1, active_mask) >> offset.U32()) & mask;
			}
		}
		if (op == ValueOpcode::ReadFirstLane) {
			return PossibleBits(inst->Arg(0), depth + 1, inst->Arg(1).Resolve());
		}
		if (op == ValueOpcode::SelectU32) {
			if (!active_mask.IsEmpty() && inst->Arg(0).Resolve() == active_mask) {
				return PossibleBits(inst->Arg(1), depth + 1, active_mask);
			}
			return PossibleBits(inst->Arg(1), depth + 1, active_mask) |
			       PossibleBits(inst->Arg(2), depth + 1, active_mask);
		}
		if (op == ValueOpcode::IAdd32 || op == ValueOpcode::IMul32) {
			const auto left  = std::countr_zero(PossibleBits(inst->Arg(0), depth + 1, active_mask));
			const auto right = std::countr_zero(PossibleBits(inst->Arg(1), depth + 1, active_mask));
			const auto zeros =
			    op == ValueOpcode::IAdd32 ? std::min(left, right) : std::min(left + right, 32);
			return zeros == 32 ? 0u : UINT32_MAX << zeros;
		}
		if (op == ValueOpcode::BitwiseAnd32) {
			return PossibleBits(inst->Arg(0), depth + 1, active_mask) &
			       PossibleBits(inst->Arg(1), depth + 1, active_mask);
		}
		if (op == ValueOpcode::BitwiseOr32) {
			return PossibleBits(inst->Arg(0), depth + 1, active_mask) |
			       PossibleBits(inst->Arg(1), depth + 1, active_mask);
		}
		if (op == ValueOpcode::ShiftLeftLogical32 || op == ValueOpcode::ShiftRightLogical32) {
			const auto shift = inst->Arg(1).Resolve();
			if (shift.IsImmediate() && shift.GetType() == Type::U32) {
				const auto bits = PossibleBits(inst->Arg(0), depth + 1, active_mask);
				return op == ValueOpcode::ShiftLeftLogical32 ? bits << (shift.U32() & 31u)
				                                             : bits >> (shift.U32() & 31u);
			}
		}
		return UINT32_MAX;
	}

	bool EvaluateInst(const Inst& inst, std::vector<uint32_t>& out) {
		const auto op = inst.GetOpcode();
		if (op == ValueOpcode::BitFieldUExtract) {
			std::vector<uint32_t> values, offsets, widths;
			if (!Evaluate(inst.Arg(0), values) || !Evaluate(inst.Arg(1), offsets) ||
			    !Evaluate(inst.Arg(2), widths) || offsets.size() * widths.size() > MaxValues ||
			    values.size() * offsets.size() * widths.size() > MaxValues * 16u)
				return false;
			out.clear();
			for (const auto offset: offsets)
				for (const auto width: widths) {
					if (offset > 32u || width > 32u - offset) return false;
					const auto mask = width == 32u  ? UINT32_MAX
					                  : width == 0u ? 0u
					                                : (1u << width) - 1u;
					for (const auto value: values)
						out.push_back(width == 0u ? 0u : (value >> offset) & mask);
				}
			return true;
		}
		if (op == ValueOpcode::GetBuiltin && m_workgroup &&
		    inst.Arg(0) == Value(static_cast<uint32_t>(StageInputKind::WorkgroupId))) {
			const auto axis = inst.Arg(1).Resolve();
			if (!axis.IsImmediate() || axis.U32() >= m_workgroup->size()) return false;
			out = {(*m_workgroup)[axis.U32()]};
			return true;
		}
		if (op == ValueOpcode::ReadFirstLane) {
			if (m_lane_depth >= 16) return false;
			BufferOffsets lane(m_program, m_runtime, m_ranges);
			lane.m_invariants  = m_invariants;
			lane.m_assigned    = m_assigned;
			lane.m_bound_row   = m_bound_row;
			lane.m_row_alias   = m_row_alias;
			lane.m_row_mask    = m_row_mask;
			lane.m_workgroup   = m_workgroup;
			lane.m_active_mask = inst.Arg(1).Resolve();
			lane.m_lane_depth  = m_lane_depth + 1;
			const bool result  = lane.Evaluate(inst.Arg(0), out);
			m_read_failed |= lane.m_read_failed;
			return result;
		}
		if (op == ValueOpcode::SelectU32) {
			const auto condition = Predicate(inst.Arg(0));
			if (condition) {
				return Evaluate(inst.Arg(*condition ? 1 : 2), out);
			}
			std::vector<uint32_t> other;
			if (!Evaluate(inst.Arg(1), out) || !Evaluate(inst.Arg(2), other)) return false;
			out.insert(out.end(), other.begin(), other.end());
			return true;
		}
		if (op == ValueOpcode::FindUMsb32) {
			const auto possible = PossibleBits(inst.Arg(0), 0);
			out.clear();
			for (uint32_t bit = 0; bit < 32; ++bit) {
				if ((possible & (1u << bit)) != 0) out.push_back(bit);
			}
			if (!Nonzero(inst.Arg(0), m_active_mask)) out.push_back(UINT32_MAX);
			return true;
		}
		if (op == ValueOpcode::ReadConstBuffer) {
			const auto  index  = inst.Flags<MemoryFlags>().index;
			const auto* handle = inst.Arg(0).ResolveInstruction();
			if (index >= m_program.memory_info.size() ||
			    m_program.memory_info[index].kind != ResourceKind::ScalarBuffer ||
			    handle == nullptr || handle->GetOpcode() != ValueOpcode::GetBufferResource) {
				return false;
			}
			const std::array roots {handle->Arg(0), handle->Arg(1), handle->Arg(2), handle->Arg(3)};
			ShaderBufferResource descriptor;
			if (const auto found = m_invariants->buffers.find(handle);
			    found != m_invariants->buffers.end()) {
				descriptor = found->second;
			} else {
				if (!std::ranges::all_of(roots, [&](Value root) { return IsUniform(root); }) ||
				    !EvaluateUniformValues(m_program, roots, m_runtime, descriptor.fields,
				                           &m_invariants->uniform_values))
					return false;
				m_invariants->buffers.emplace(handle, descriptor);
			}
			std::vector<uint32_t> offsets;
			if (!Evaluate(inst.Arg(1), offsets)) {
				const auto         size          = ScalarBufferSize(descriptor) & ~uint64_t {3};
				constexpr uint64_t MaxIndexBytes = 4u * 1024u * 1024u;
				if (m_read_failed || size > MaxIndexBytes) {
					return false;
				}
				// An unknown index can select any word in the bounded index buffer. Read
				// its contents in one transaction; GPU-produced indices require a readback.
				std::vector<uint32_t> words(size / sizeof(uint32_t));
				const auto            base = descriptor.Base48() & ~uint64_t {3};
				if (size != 0 && m_runtime.read_specialization_range != nullptr) {
					if (!m_runtime.read_specialization_range(m_runtime.userdata, base, words.data(),
					                                         size)) {
						m_read_failed = true;
						return false;
					}
				} else {
					for (uint32_t word = 0; word < words.size(); ++word) {
						if (!ReadSpecializationWord(m_runtime, base + word * 4u, words[word])) {
							m_read_failed = true;
							return false;
						}
					}
				}
				out.clear();
				const auto step = uint64_t {1}
				                  << std::max(2, std::countr_zero(PossibleBits(inst.Arg(1), 0)));
				for (uint64_t offset = 0; offset < size; offset += step) {
					const auto word =
					    (offset + m_program.memory_info[index].offset) / sizeof(uint32_t);
					if (word < words.size()) {
						out.push_back(words[word]);
					}
				}
				out.push_back(0u); // Out-of-bounds scalar loads return zero.
				return true;
			}
			out.clear();
			for (const auto offset: offsets) {
				uint32_t word = 0;
				if (!ReadScalarBufferWord(descriptor, offset, m_program.memory_info[index].offset,
				                          m_runtime, word)) {
					m_read_failed = true;
					return false;
				}
				out.push_back(word);
			}
			return true;
		}
		if (op == ValueOpcode::Phi) {
			out.clear();
			for (size_t arg = 0; arg < inst.NumArgs(); ++arg) {
				std::vector<uint32_t> branch;
				if (!Evaluate(inst.Arg(arg), branch) || out.size() + branch.size() > MaxValues) {
					return false;
				}
				out.insert(out.end(), branch.begin(), branch.end());
			}
			return true;
		}
		if (op == ValueOpcode::LoadAddressU32) {
			const auto index = inst.Flags<MemoryFlags>().index;
			if (index >= m_program.memory_info.size() ||
			    m_program.memory_info[index].kind != ResourceKind::ScalarAddress) {
				return false;
			}
			const auto* base = inst.Arg(0).ResolveInstruction();
			if (base == nullptr || base->GetOpcode() != ValueOpcode::GetAddressResource) {
				return false;
			}
			const std::array        roots {base->Arg(0), base->Arg(1)};
			std::array<uint32_t, 2> address_words {};
			std::vector<uint32_t>   offsets;
			if (!ValidateRuntimeValue(m_program, roots[0]) ||
			    !ValidateRuntimeValue(m_program, roots[1]) ||
			    !EvaluateUniformValues(m_program, roots, m_runtime, address_words,
			                           &m_invariants->uniform_values) ||
			    !Evaluate(inst.Arg(1), offsets)) {
				return false;
			}
			const auto address = ((uint64_t {address_words[1]} << 32u) | address_words[0]) &
			                     (AddressMask & ~uint64_t {3});
			const auto immediate =
			    int64_t {static_cast<int32_t>(m_program.memory_info[index].offset)} & ~int64_t {3};
			out.clear();
			for (const auto offset: offsets) {
				const auto location = static_cast<int64_t>(address) + immediate + (offset & ~3u);
				uint32_t   word     = 0;
				if (location < 0 || static_cast<uint64_t>(location) > AddressMask - 3u ||
				    !ReadSpecializationWord(m_runtime, location, word)) {
					m_read_failed = true;
					return false;
				}
				out.push_back(word);
			}
			return true;
		}
		switch (op) {
			case ValueOpcode::IAdd32:
			case ValueOpcode::ISub32:
			case ValueOpcode::IMul32:
			case ValueOpcode::BitwiseAnd32:
			case ValueOpcode::BitwiseOr32:
			case ValueOpcode::ShiftLeftLogical32:
			case ValueOpcode::ShiftRightLogical32: break;
			default: return false;
		}
		std::vector<uint32_t> lhs, rhs;
		if (!Evaluate(inst.Arg(0), lhs) || !Evaluate(inst.Arg(1), rhs) ||
		    lhs.size() * rhs.size() > MaxValues * 16u) {
			return false;
		}
		out.clear();
		for (const auto a: lhs) {
			for (const auto b: rhs) {
				switch (op) {
					case ValueOpcode::IAdd32: out.push_back(a + b); break;
					case ValueOpcode::ISub32: out.push_back(a - b); break;
					case ValueOpcode::IMul32: out.push_back(a * b); break;
					case ValueOpcode::BitwiseAnd32: out.push_back(a & b); break;
					case ValueOpcode::BitwiseOr32: out.push_back(a | b); break;
					case ValueOpcode::ShiftLeftLogical32: out.push_back(a << (b & 31u)); break;
					case ValueOpcode::ShiftRightLogical32: out.push_back(a >> (b & 31u)); break;
					default: return false;
				}
			}
		}
		return true;
	}

	const ResourcePlan&                                                   m_program;
	SrtRuntime                                                            m_runtime;
	std::unordered_map<const Inst*, std::vector<uint32_t>>                m_assigned;
	const Inst*                                                           m_bound_row = nullptr;
	const Inst*                                                           m_row_alias = nullptr;
	Value                                                                 m_row_mask;
	std::unordered_map<const Inst*, std::optional<std::vector<uint32_t>>> m_cache;
	std::unordered_set<const Inst*>                                       m_visiting;
	bool                                                                  m_read_failed = false;
	bool                                          m_counted_attempted                   = false;
	std::span<const DescriptorSource::IndexRange> m_ranges;
	Value                                         m_active_mask;
	uint32_t                                      m_lane_depth = 0;
	std::optional<std::array<uint32_t, 3>>        m_workgroup;
	InvariantCache                                m_invariant_storage;
	InvariantCache*                               m_invariants = &m_invariant_storage;
};

bool ReadScalarBufferWord(const ShaderBufferResource& descriptor, uint32_t dynamic_offset,
                          uint32_t immediate_offset, const SrtRuntime& runtime, uint32_t& word) {
	const auto byte_offset = static_cast<uint64_t>(dynamic_offset) + immediate_offset;
	const auto aligned     = byte_offset & ~uint64_t {3};
	const auto size        = ScalarBufferSize(descriptor);
	if (aligned > size || size - aligned < sizeof(uint32_t)) {
		word = 0;
		return true;
	}
	const auto base = descriptor.Base48() & ~uint64_t {3};
	if (aligned > AddressMask - base) {
		return false;
	}
	const auto address = base + aligned;
	if (!ReadSpecializationWord(runtime, address, word)) {
		return false;
	}
	return true;
}

bool MaterializeIndirectImage(const DescriptorSource::IndirectImage& indirect,
                              const DescriptorValue&                 material_value,
                              const DescriptorValue& heap_value, bool r128,
                              const SrtRuntime& runtime, IndirectDescriptorTable& result) {
	ShaderBufferResource material;
	ShaderBufferResource heap;
	if (!DecodeBufferDescriptor(material_value, material) ||
	    !DecodeBufferDescriptor(heap_value, heap)) {
		return false;
	}
	if (material.Stride() != indirect.selector_stride) {
		return false;
	}

	// S_BUFFER_LOAD ignores vector-buffer swizzle/add-thread fields. The shader computes the
	// record stride explicitly; enumerate every wrapped 32-bit offset that can pass bounds.
	const auto period      = uint64_t {1} << 32u;
	const auto step        = std::gcd<uint64_t>(indirect.selector_stride, period);
	const auto residue     = static_cast<uint64_t>(indirect.selector_offset) % step;
	const auto size        = ScalarBufferSize(material);
	const auto limit       = std::min<uint64_t>(UINT32_MAX, size + 3u);
	const auto probe_count = residue <= limit ? (limit - residue) / step + 1u : 0u;
	if (probe_count > MaxIndirectImageProbes) {
		return false;
	}

	std::vector<uint32_t>        keys {0u};
	std::unordered_set<uint32_t> seen {0u};
	keys.reserve(static_cast<size_t>(probe_count) + 1u);
	seen.reserve(static_cast<size_t>(probe_count) + 1u);
	for (uint64_t offset = residue; offset <= limit && probe_count != 0u; offset += step) {
		uint32_t key = 0;
		if (!ReadScalarBufferWord(material, static_cast<uint32_t>(offset), 0u, runtime, key)) {
			return false;
		}
		if (seen.insert(key).second) {
			keys.push_back(key);
		}
		if (limit - offset < step) {
			break;
		}
	}

	IndirectDescriptorTable next;
	next.keys = std::move(keys);
	next.candidates.reserve(next.keys.size());
	next.descriptors.reserve(
	    std::min(next.keys.size(), static_cast<size_t>(ShaderInfo::MaxImages)));
	for (const auto key: next.keys) {
		DescriptorValue candidate;
		candidate.dword_count  = 8u;
		const auto heap_offset = key << 5u;
		for (uint32_t dword = 0; dword < candidate.dword_count; dword++) {
			if (!ReadScalarBufferWord(heap, heap_offset, dword * sizeof(uint32_t), runtime,
			                          candidate.dwords[dword])) {
				return false;
			}
		}
		if (NullImageDescriptor(candidate) || !ValidImageDescriptor(candidate, r128)) {
			candidate.dwords.fill(0);
		}
		const auto found = std::ranges::find(next.descriptors, candidate);
		if (found == next.descriptors.end()) {
			if (next.descriptors.size() >= ShaderInfo::MaxImages) {
				return false;
			}
			next.descriptors.push_back(candidate);
			next.candidates.push_back(static_cast<uint32_t>(next.descriptors.size() - 1u));
		} else {
			next.candidates.push_back(static_cast<uint32_t>(found - next.descriptors.begin()));
		}
	}
	result = std::move(next);
	return true;
}

} // namespace

static bool MaterializeSnapshot(const ResourcePlan& program, const SrtRuntime& runtime,
                                MaterializedSnapshot& snapshot) {
	if (!program.resource_tracking_complete) {
		return false;
	}

	if (program.requires_specialization_memory && runtime.read_specialization_memory == nullptr) {
		return false;
	}
	std::vector<DescriptorValue> values;
	std::vector<uint32_t>        flattened_srt;
	std::vector<uint8_t>         active_sources;
	if (!EvaluateRuntimeSources(program, program.materialization_sources, runtime, values,
	                            flattened_srt, program.clean_flat_slots, active_sources)) {
		std::fprintf(stderr, "resource sources map hash=0x%016llx\\n",
		             static_cast<unsigned long long>(program.shader_hash));
		for (uint32_t index = 0; index < program.info.buffers.size(); ++index)
			std::fprintf(stderr, "  buffer[%u] source=%u\\n", index,
			             program.info.buffers[index].source);
		for (uint32_t index = 0; index < program.info.images.size(); ++index)
			std::fprintf(stderr, "  image[%u] source=%u\\n", index,
			             program.info.images[index].source);
		for (uint32_t index = 0; index < program.info.samplers.size(); ++index)
			std::fprintf(stderr, "  sampler[%u] source=%u\\n", index,
			             program.info.samplers[index].source);
		std::fprintf(stderr,
		             "resource materialization: runtime sources failed hash=0x%016llx sources=%zu "
		             "buffers=%zu images=%zu srt=%zu\n",
		             static_cast<unsigned long long>(program.shader_hash),
		             program.materialization_sources.size(), program.info.buffers.size(),
		             program.info.images.size(), program.srt_reads.size());
		return false;
	}

	auto&                   next  = snapshot.resources;
	const auto&             fill  = program.uniform_fill;
	const auto              words = fill.fill.words;
	std::array<uint32_t, 4> stored {};
	if (words != 0 &&
	    EvaluateUniformValues(program, std::span(fill.values).first(words), runtime,
	                          std::span(stored).first(words)) &&
	    std::all_of(stored.begin(), stored.begin() + words,
	                [&](uint32_t value) { return value == stored[0]; })) {
		next.uniform_fill       = fill.fill;
		next.uniform_fill.value = stored[0];
	}
	auto cursor = values.begin();
	next.buffers.resize(program.info.buffers.size());
	for (uint32_t index = 0; index < program.info.buffers.size(); ++index) {
		const auto* source = Source(program, program.info.buffers[index].source);
		if (source != nullptr && source->bounded_buffer.has_value()) continue;
		if (cursor == values.end())
			return SpecializationFail("buffer materialization sources are incomplete");
		next.buffers[index] = *cursor++;
	}
	next.flattened_srt = std::move(flattened_srt);
	if (!MaterializeBoundedReads(program, runtime, snapshot) ||
	    !MaterializeBoundedBuffers(program, runtime, snapshot)) {
		return false;
	}
	for (uint32_t index = 0; index < program.info.buffers.size(); ++index) {
		const auto& buffer = program.info.buffers[index];
		const auto* source = Source(program, buffer.source);
		if (source != nullptr && source->bounded_buffer.has_value()) continue;
		if (source == nullptr || !source->indirect_buffer.has_value()) {
			continue;
		}
		const auto base_words = next.buffers[index];
		next.buffers[index].dwords.fill(0);
		if (!active_sources[buffer.source]) {
			continue;
		}
		IndirectDescriptorTable table;
		table.resource = index;
		if (!BufferOffsets(program, runtime, source->indirect_buffer->index_ranges)
		         .EvaluateDispatch(source->indirect_buffer->byte_offset, table.keys)) {
			return SpecializationFail(
			    fmt::format("shader 0x{:016x} buffer {} has no finite, CPU-known descriptor table",
			                program.shader_hash, index));
		}
		// Candidate zero is a null buffer, also used for an unmatched key.
		table.descriptors.push_back(next.buffers[index]);
		const auto base = ((uint64_t {base_words.dwords[1]} << 32u) | base_words.dwords[0]) &
		                  (AddressMask & ~uint64_t {3});
		const auto immediate =
		    int64_t {static_cast<int32_t>(source->indirect_buffer->immediate_offset)} &
		    ~int64_t {3};
		for (const auto key: table.keys) {
			DescriptorValue descriptor;
			descriptor.dword_count = 4;
			const auto address     = static_cast<int64_t>(base) + immediate + (key & ~3u);
			if (address < 0 || static_cast<uint64_t>(address) > AddressMask - 15u) {
				return SpecializationFail("indirect buffer descriptor address overflow");
			}
			for (uint32_t word = 0; word < 4; ++word) {
				if (!ReadSpecializationWord(runtime, address + word * 4u,
				                            descriptor.dwords[word])) {
					return SpecializationFail(fmt::format(
					    "indirect buffer descriptor at 0x{:x} is not CPU-known", address));
				}
			}
			ShaderBufferResource decoded;
			if (!DecodeBufferDescriptor(descriptor, decoded)) {
				return false;
			}
			if (decoded.Type() != 0) {
				descriptor.dwords.fill(0);
			}
			const auto found     = std::ranges::find(table.descriptors, descriptor);
			const auto candidate = static_cast<uint32_t>(found - table.descriptors.begin());
			if (found == table.descriptors.end()) {
				table.descriptors.push_back(descriptor);
			}
			table.candidates.push_back(candidate);
		}
		if (table.descriptors.size() > 1u) {
			snapshot.indirect_buffers.push_back(std::move(table));
		}
	}
	next.images.resize(program.info.images.size());
	for (uint32_t image_index = 0; image_index < program.info.images.size(); image_index++) {
		const auto& image  = program.info.images[image_index];
		const auto* source = Source(program, image.source);
		if (source != nullptr && source->indirect_image.has_value()) {
			if (!active_sources[image.source]) {
				next.images[image_index].dword_count = 8u;
				continue;
			}
			const std::array requests {source->indirect_image->material_source,
			                           source->indirect_image->heap_source};
			SrtRuntime       clean_runtime = runtime;
			clean_runtime.read_memory      = runtime.read_specialization_memory;
			std::vector<DescriptorValue> tables;
			if (!EvaluateDescriptorSources(program, requests, clean_runtime, tables)) {
				return false;
			}
			const auto&             material = tables[0];
			const auto&             heap     = tables[1];
			IndirectDescriptorTable table;
			if (!source->indirect_image->direct_offset.IsEmpty()) {
				ShaderBufferResource descriptor;
				if (!DecodeBufferDescriptor(material, descriptor) ||
				    !BufferOffsets(program, runtime, source->indirect_image->index_ranges)
				         .EvaluateDispatch(source->indirect_image->direct_offset, table.keys)) {

					return SpecializationFail(
					    fmt::format("shader 0x{:016x}: inline image descriptor offsets are not "
					                "bounded by readable tables",
					                program.shader_hash));
				}
				DescriptorValue null_image;
				null_image.dword_count = 8;
				table.descriptors.push_back(null_image);
				for (const auto key: table.keys) {
					DescriptorValue candidate;
					candidate.dword_count = 8;
					const auto immediate  = source->indirect_image->immediate_offset;
					const auto raw_address =
					    static_cast<int64_t>(descriptor.Base48() & ~uint64_t {3}) + (key & ~3u) +
					    (int64_t {static_cast<int32_t>(immediate)} & ~int64_t {3});
					if (source->indirect_image->direct_address &&
					    (raw_address < 0 || static_cast<uint64_t>(raw_address) > AddressMask - 31u))
						return SpecializationFail("inline image descriptor address overflow");
					const auto candidate_address = static_cast<uint64_t>(raw_address);
					// Finite-set analysis may include offsets that no invocation selects. An
					// unmapped speculative entry represents an unbound descriptor; do not read
					// it on the CPU. A single known key and any mapped read failure stay strict.
					if (source->indirect_image->direct_address && table.keys.size() > 1u &&
					    runtime.is_memory_mapped != nullptr &&
					    !runtime.is_memory_mapped(runtime.userdata, candidate_address, 32u)) {
						table.candidates.push_back(0u);
						continue;
					}
					for (uint32_t word = 0; word < 8; ++word) {
						const auto base = descriptor.Base48() & ~uint64_t {3};
						const bool read =
						    source->indirect_image->direct_address
						        ? ReadSpecializationWord(runtime, candidate_address + word * 4u,
						                                 candidate.dwords[word])
						        : ReadScalarBufferWord(descriptor, key, immediate + word * 4u,
						                               runtime, candidate.dwords[word]);
						if (!read) {
							return SpecializationFail(fmt::format(
							    "shader 0x{:016x} pc=0x{:x}: image descriptor is not readable, "
							    "base=0x{:x} key=0x{:x} word={} keys={} "
							    "table={:08x},{:08x},{:08x},{:08x}",
							    program.shader_hash, image.first_use_pc, base, key, word,
							    table.keys.size(), material.dwords[0], material.dwords[1],
							    material.dwords[2], material.dwords[3]));
						}
					}
					const auto image_address =
					    ((uint64_t {candidate.dwords[1]} << 32u | candidate.dwords[0]) &
					     0xffffffffffull)
					    << 8u;
					const bool unbound =
					    table.keys.size() > 1u && runtime.is_memory_mapped != nullptr &&
					    !runtime.is_memory_mapped(runtime.userdata, image_address, 1u);
					if (unbound || NullImageDescriptor(candidate) ||
					    !ValidImageDescriptor(candidate, image.r128)) {
						candidate.dwords.fill(0);
					}
					const auto found    = std::ranges::find(table.descriptors, candidate);
					const auto selected = static_cast<uint32_t>(found - table.descriptors.begin());
					if (found == table.descriptors.end()) {
						table.descriptors.push_back(candidate);
					}
					table.candidates.push_back(selected);
				}
			} else if (!MaterializeIndirectImage(*source->indirect_image, material, heap,
			                                     image.r128, runtime, table)) {
				return false;
			}
			// A guarded loop with zero iterations has no reachable image access.
			next.images[image_index] =
			    table.descriptors[table.candidates.empty() ? 0u : table.candidates[0]];
			if (table.descriptors.size() > 1u) {
				table.resource = image_index;
				snapshot.indirect_images.push_back(std::move(table));
			}
		} else {
			auto descriptor = *cursor++;
			if (!ValidImageDescriptor(descriptor, image.r128)) {
				descriptor.dwords.fill(0);
			}
			next.images[image_index] = descriptor;
		}
	}
	next.samplers.assign(cursor, cursor + program.info.samplers.size());
	next.user_data.assign(runtime.user_data.begin(), runtime.user_data.end());
	return true;
}

struct SamplerPlan {
	std::array<uint32_t, ShaderInfo::MaxSamplers> point_sampler {};
	uint32_t                                      sampler_count = 0;
};

struct ImageRemap {
	explicit ImageRemap(const ResourceSpecialization& specialization) {
		for (const auto& image: specialization.images) {
			indices.push_back(image.fmask ? UINT32_MAX : count++);
		}
	}

	template <typename T>
	void Apply(std::vector<T>& images) const {
		EXIT_IF(images.size() != indices.size());
		for (uint32_t index = 0; index < indices.size(); index++) {
			if (indices[index] != UINT32_MAX && indices[index] != index) {
				images[indices[index]] = std::move(images[index]);
			}
		}
		images.resize(count);
	}

	std::vector<uint32_t> indices;
	uint32_t              count = 0;
};

template <typename Images>
bool BuildSamplerPlan(const ShaderInfo& base, const Images& images, SamplerPlan& plan);

static bool BuildResourceSpecialization(const ResourcePlan& program, MaterializedSnapshot snapshot,
                                        ResourceSnapshot&       specialized_snapshot,
                                        ResourceSpecialization& specialization) {
	auto                   next_snapshot = std::move(snapshot.resources);
	ResourceSpecialization next_specialization;
	next_specialization.bounded_srt_reads = snapshot.bounded_srt_reads;
	next_specialization.buffers.resize(program.info.buffers.size());
	for (const auto& table: snapshot.indirect_buffers) {
		if (next_specialization.buffers.size() + table.descriptors.size() - 1u >
		    ShaderInfo::MaxBuffers) {
			return SpecializationFail(
			    "indirect buffer candidates exceed the dense buffer resource limit");
		}
		for (uint32_t candidate = 1; candidate < table.descriptors.size(); ++candidate) {
			ResourceSpecialization::Buffer buffer;
			buffer.indirect_root = table.resource;
			next_specialization.buffers.push_back(buffer);
			next_snapshot.buffers.push_back(table.descriptors[candidate]);
		}
		auto& root                      = next_specialization.buffers[table.resource];
		root.indirect_root              = table.resource;
		root.indirect_mapping_offset    = static_cast<uint32_t>(next_snapshot.flattened_srt.size());
		root.indirect_search_iterations = std::bit_width(table.keys.size());
		next_snapshot.flattened_srt.push_back(static_cast<uint32_t>(table.keys.size()));
		for (uint32_t entry = 0; entry < table.keys.size(); ++entry) {
			next_snapshot.flattened_srt.push_back(table.keys[entry]);
			next_snapshot.flattened_srt.push_back(table.candidates[entry]);
		}
	}
	size_t image_count   = program.info.images.size();
	size_t mapping_words = 0;
	for (const auto& table: snapshot.indirect_images) {
		if (table.resource >= program.info.images.size() || table.descriptors.size() < 2u ||
		    image_count + table.descriptors.size() - 1u > ShaderInfo::MaxImages) {
			return SpecializationFail(
			    "indirect image candidates exceed the dense image resource limit");
		}
		image_count += table.descriptors.size() - 1u;
		mapping_words += 1u + table.keys.size() * 2u;
	}
	next_snapshot.images.reserve(image_count);
	next_snapshot.flattened_srt.reserve(next_snapshot.flattened_srt.size() + mapping_words);
	next_specialization.images.reserve(image_count);
	for (const auto& image: program.info.images) {
		next_specialization.images.push_back({
		    .numeric_class              = image.numeric_class,
		    .dimension                  = image.dimension,
		    .mip_count                  = image.mip_count,
		    .conversion_format          = image.conversion_format,
		    .shader_swizzle             = image.shader_swizzle,
		    .indirect_root              = image.indirect_root,
		    .indirect_mapping_offset    = image.indirect_mapping_offset,
		    .indirect_search_iterations = image.indirect_search_iterations,
		    .cube                       = image.cube,
		});
	}
	for (const auto& table: snapshot.indirect_images) {
		const auto root_image = next_specialization.images[table.resource];
		for (uint32_t candidate = 1; candidate < table.descriptors.size(); candidate++) {
			auto image          = root_image;
			image.indirect_root = table.resource;
			next_specialization.images.push_back(image);
			next_snapshot.images.push_back(table.descriptors[candidate]);
		}
		auto& root                      = next_specialization.images[table.resource];
		root.indirect_root              = table.resource;
		root.indirect_mapping_offset    = static_cast<uint32_t>(next_snapshot.flattened_srt.size());
		root.indirect_search_iterations = std::bit_width(table.keys.size());
		next_snapshot.flattened_srt.resize(next_snapshot.flattened_srt.size() + 1u +
		                                   table.keys.size() * 2u);
		std::vector<uint32_t> order(table.keys.size());
		std::iota(order.begin(), order.end(), 0u);
		std::ranges::sort(order, {}, [&](uint32_t index) { return table.keys[index]; });
		next_snapshot.flattened_srt[root.indirect_mapping_offset] =
		    static_cast<uint32_t>(table.keys.size());
		for (uint32_t entry = 0; entry < order.size(); entry++) {
			const auto source                   = order[entry];
			const auto offset                   = root.indirect_mapping_offset + 1u + entry * 2u;
			next_snapshot.flattened_srt[offset] = table.keys[source];
			next_snapshot.flattened_srt[offset + 1] = table.candidates[source];
		}
		next_snapshot.images[table.resource] = table.descriptors[0];
	}
	for (uint32_t i = 0; i < next_specialization.buffers.size(); i++) {
		auto&      descriptor_value = next_snapshot.buffers[i];
		auto&      buffer           = next_specialization.buffers[i];
		const auto base_index       = i < program.info.buffers.size() ? i : buffer.indirect_root;
		ShaderBufferResource descriptor;
		if (!DecodeBufferDescriptor(descriptor_value, descriptor)) {
			return SpecializationFail(fmt::format("buffer descriptor {} has invalid width", i));
		}
		if (descriptor.Type() != 0) {
			descriptor_value.dwords.fill(0);
			descriptor = {};
		}
		auto       packed_stride = descriptor.PackedStride();
		const auto stride        = packed_stride & 0x3fffu;
		const bool swizzle       = stride != 0u && ((packed_stride >> 14u) & 1u) != 0u;
		if (stride == 0u) {
			packed_stride &= ~((1u << 14u) | (3u << 16u));
		} else if (!swizzle) {
			packed_stride &= ~(3u << 16u);
		}
		buffer.packed_stride      = packed_stride;
		buffer.descriptor_format  = program.info.buffers[base_index].formatted
		                                ? descriptor.Format()
		                                : Prospero::BufferFormat::kInvalid;
		buffer.descriptor_swizzle = program.info.buffers[base_index].formatted
		                                ? descriptor.DstSelXYZW()
		                                : DstSel(4, 5, 6, 7);
	}
	for (uint32_t i = 0; i < next_specialization.images.size(); i++) {
		const auto& descriptor = next_snapshot.images[i];
		auto&       image      = next_specialization.images[i];
		const auto  base_index = i < program.info.images.size() ? i : image.indirect_root;
		if (base_index >= program.info.images.size()) {
			return SpecializationFail(fmt::format("image resource {} has an invalid root", i));
		}
		const auto& base = program.info.images[base_index];
		if (base.resource_class == ImageResourceClass::None ||
		    (base.atomic && base.resource_class != ImageResourceClass::Storage)) {
			return SpecializationFail(fmt::format("image resource {} has an invalid class", i));
		}
		image.mip_count = StorageMipCount(base, descriptor);
		if (image.mip_count == 0u) {
			return SpecializationFail(
			    fmt::format("storage image descriptor {} has an invalid mip range", i));
		}
		if (NullImageDescriptor(descriptor)) {
			image.numeric_class = base.atomic ? Prospero::TextureNumericClass::Uint
			                                  : Prospero::TextureNumericClass::Float;
			image.dimension     = Decoder::ImageDimension::Dim2D;
			image.cube          = false;
			continue;
		}
		const auto descriptor_dimension = DescriptorDimension(descriptor, base.dimension);
		if (descriptor_dimension == Decoder::ImageDimension::Unknown) {
			return SpecializationFail(fmt::format(
			    "image descriptor {} has unsupported type {}: {:08x},{:08x},{:08x},{:08x},"
			    "{:08x},{:08x},{:08x},{:08x}",
			    i, (descriptor.dwords[3] >> 28u) & 0xfu, descriptor.dwords[0], descriptor.dwords[1],
			    descriptor.dwords[2], descriptor.dwords[3], descriptor.dwords[4],
			    descriptor.dwords[5], descriptor.dwords[6], descriptor.dwords[7]));
		}
		image.dimension = descriptor_dimension;
		image.cube      = DescriptorIsCube(descriptor);
		const auto format =
		    static_cast<Prospero::BufferFormat>((descriptor.dwords[1] >> 20u) & 0x1ffu);
		if (base.atomic && format != Prospero::BufferFormat::k32UInt) {
			return SpecializationFail(
			    fmt::format("atomic image descriptor {} uses unsupported format {}", i,
			                static_cast<uint32_t>(format)));
		}
		const bool storage = base.resource_class == ImageResourceClass::Storage;
		image.fmask        = Prospero::IsFmaskTextureFormat(format);
		if (image.fmask) {
			if (storage || base.depth_compare ||
			    image.indirect_root != ImageResource::NoIndirectImage ||
			    std::ranges::any_of(program.info.sampled_pairs,
			                        [&](const auto& pair) { return pair.image == i; })) {
				return SpecializationFail("FMASK requires a direct image load");
			}
		}
		image.conversion_format = ImageConversionFormat(format);
		if (storage || image.conversion_format != Prospero::BufferFormat::kInvalid) {
			image.shader_swizzle = DescriptorImageSwizzle(descriptor);
		}
		const bool raw_sint_storage = storage && format == Prospero::BufferFormat::k32SInt &&
		                              base.written && !base.read && !base.atomic;
		image.numeric_class         = Prospero::SampledTextureNumericClass(format);
		if (storage) {
			if ((!raw_sint_storage && image.numeric_class == Prospero::TextureNumericClass::Sint) ||
			    image.numeric_class == Prospero::TextureNumericClass::Unsupported) {
				return SpecializationFail(
				    fmt::format("storage image descriptor {} uses unsupported format {}", i,
				                static_cast<uint32_t>(format)));
			}
			if (raw_sint_storage) {
				image.numeric_class = Prospero::TextureNumericClass::Uint;
			}
		} else if (image.numeric_class == Prospero::TextureNumericClass::Unsupported ||
		           (base.depth_compare &&
		            image.numeric_class != Prospero::TextureNumericClass::Float)) {
			return SpecializationFail(
			    fmt::format("sampled image descriptor {} uses unsupported format {}", i,
			                static_cast<uint32_t>(format)));
		}
	}
	for (uint32_t root_index = 0; root_index < next_specialization.images.size(); root_index++) {
		auto& root = next_specialization.images[root_index];
		if (root.indirect_root != root_index) {
			continue;
		}
		const auto key_count = root.indirect_mapping_offset < next_snapshot.flattened_srt.size()
		                           ? next_snapshot.flattened_srt[root.indirect_mapping_offset]
		                           : 0u;
		if (root.indirect_search_iterations == 0u || key_count == 0u ||
		    static_cast<size_t>(root.indirect_mapping_offset) + 1u +
		            static_cast<size_t>(key_count) * 2u >
		        next_snapshot.flattened_srt.size()) {
			return SpecializationFail("indirect image specialization has an invalid key mapping");
		}
		uint32_t exemplar       = ImageResource::NoIndirectImage;
		uint32_t resource_count = 0;
		for (uint32_t resource = 0; resource < next_specialization.images.size(); resource++) {
			if (next_specialization.images[resource].indirect_root != root_index) {
				continue;
			}
			resource_count++;
			if (exemplar == ImageResource::NoIndirectImage &&
			    !NullImageDescriptor(next_snapshot.images[resource])) {
				exemplar = resource;
			}
		}
		if (resource_count < 2u || exemplar == ImageResource::NoIndirectImage) {
			return SpecializationFail("indirect image specialization has no typed candidate");
		}
		const auto& image_class = next_specialization.images[exemplar];
		for (uint32_t candidate = 0; candidate < next_specialization.images.size(); candidate++) {
			auto& image = next_specialization.images[candidate];
			if (image.indirect_root != root_index) {
				continue;
			}
			if (NullImageDescriptor(next_snapshot.images[candidate])) {
				image.numeric_class     = image_class.numeric_class;
				image.dimension         = image_class.dimension;
				image.mip_count         = image_class.mip_count;
				image.conversion_format = image_class.conversion_format;
				image.shader_swizzle    = image_class.shader_swizzle;
				image.cube              = image_class.cube;
			}
		}
	}
	SamplerPlan sampler_plan;
	if (!BuildSamplerPlan(program.info, next_specialization.images, sampler_plan)) {
		return SpecializationFail("specialized sampler layout exceeds its resource limit");
	}
	for (uint32_t index = 0; index < program.info.samplers.size(); index++) {
		const auto target = sampler_plan.point_sampler[index];
		if (target != UINT32_MAX && target >= program.info.samplers.size()) {
			next_snapshot.samplers.push_back(next_snapshot.samplers[index]);
		}
	}
	ImageRemap(next_specialization).Apply(next_snapshot.images);
	specialization       = std::move(next_specialization);
	specialized_snapshot = std::move(next_snapshot);
	return true;
}

template <typename Images>
bool BuildSamplerPlan(const ShaderInfo& base, const Images& images, SamplerPlan& plan) {
	if (base.samplers.size() > plan.point_sampler.size()) {
		return false;
	}
	std::array<uint8_t, ShaderInfo::MaxSamplers> usage {};
	plan.point_sampler.fill(UINT32_MAX);
	plan.sampler_count = static_cast<uint32_t>(base.samplers.size());
	for (const auto& pair: base.sampled_pairs) {
		if (pair.image >= images.size() || pair.sampler >= base.samplers.size()) {
			return false;
		}
		usage[pair.sampler] |= RequiresPointSampler(images[pair.image]) ? 2u : 1u;
		if (images[pair.image].indirect_root == pair.image) {
			for (const auto& candidate: images) {
				if (candidate.indirect_root == pair.image) {
					usage[pair.sampler] |= RequiresPointSampler(candidate) ? 2u : 1u;
				}
			}
		}
	}
	for (uint32_t index = 0; index < base.samplers.size(); index++) {
		if ((usage[index] & 2u) == 0u) {
			continue;
		}
		if ((usage[index] & 1u) == 0u) {
			plan.point_sampler[index] = index;
		} else {
			if (plan.sampler_count >= ShaderInfo::MaxSamplers) {
				return false;
			}
			plan.point_sampler[index] = plan.sampler_count++;
		}
	}
	return true;
}

static std::vector<ResourceBlock> ResourceControlFlow(const Program& program) {
	if (program.blocks.size() != program.block_info.size()) {
		return {};
	}
	std::unordered_map<uint32_t, uint32_t> indices;
	for (uint32_t i = 0; i < program.block_info.size(); i++) {
		if (!indices.emplace(program.block_info[i].id, i).second) {
			return {};
		}
	}
	std::vector<ResourceBlock> blocks(program.blocks.size());
	std::vector<uint8_t>       predicate_may_be_written(blocks.size());
	for (uint32_t i = 0; i < blocks.size(); i++) {
		auto&                 block      = blocks[i];
		const auto&           info       = program.block_info[i];
		const auto&           terminator = info.terminator;
		std::vector<uint32_t> successors;
		switch (terminator.kind) {
			case CFG::TerminatorKind::Branch: successors.push_back(terminator.true_block); break;
			case CFG::TerminatorKind::ConditionalBranch:
				successors = {terminator.true_block, terminator.false_block};
				if (ValidateRuntimeValue(program, info.condition, RuntimeValueType::Integer)) {
					block.condition = info.condition;
				}
				break;
			case CFG::TerminatorKind::IndirectBranch:
				successors = terminator.indirect_targets;
				break;
			case CFG::TerminatorKind::Return: break;
			default: return {};
		}
		for (const auto successor: successors) {
			const auto found = indices.find(successor);
			if (found == indices.end()) {
				return {};
			}
			block.successors.push_back(found->second);
		}
		for (const auto& inst: *program.blocks[i]) {
			const auto op = inst.GetOpcode();
			if (op == ValueOpcode::ReadConst) {
				const auto slot = inst.Arg(1).Resolve();
				if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
				    slot.U32() < program.srt_reads.size()) {
					block.flat_slots.push_back(slot.U32());
				}
			}
			const auto buffer = BufferAccessOf(op);
			const auto image  = ImageOpcodeInfoOf(op);
			// Only predicates reachable after a shader write can observe its result.
			// Earlier uniform branches still determine which descriptors are used.
			if (buffer == BufferAccess::Write || buffer == BufferAccess::Atomic ||
			    image.access == ImageAccess::Write || image.access == ImageAccess::Atomic ||
			    AddressOpcodeInfoOf(op).access == AddressAccess::Write) {
				predicate_may_be_written[i] = 1u;
			}
			if (buffer == BufferAccess::None && image.access == ImageAccess::None) {
				continue;
			}
			const auto& memory = program.memory_info.at(inst.Flags<MemoryFlags>().index);
			if (memory.planning_only) {
				continue;
			}
			if (buffer != BufferAccess::None) {
				block.sources.push_back(program.info.buffers.at(memory.resource).source);
			} else {
				block.sources.push_back(program.info.images.at(memory.resource).source);
				if (image.needs_sampler) {
					block.sources.push_back(program.info.samplers.at(memory.sampler).source);
				}
			}
		}
		std::ranges::sort(block.sources);
		block.sources.erase(std::unique(block.sources.begin(), block.sources.end()),
		                    block.sources.end());
	}
	// Include back edges: a predicate before a write in the first iteration may
	// depend on that write on a subsequent visit. Unknown predicates retain both edges.
	std::vector<uint32_t> pending;
	for (uint32_t i = 0; i < blocks.size(); ++i) {
		if (predicate_may_be_written[i]) pending.push_back(i);
	}
	while (!pending.empty()) {
		const auto index = pending.back();
		pending.pop_back();
		blocks[index].condition = {};
		for (const auto successor: blocks[index].successors) {
			if (!predicate_may_be_written[successor]) {
				predicate_may_be_written[successor] = 1u;
				pending.push_back(successor);
			}
		}
	}
	if (std::ranges::none_of(
	        blocks, [](const ResourceBlock& block) { return !block.condition.IsEmpty(); })) {
		return {};
	}
	return blocks;
}

// Nonnegative affine coefficients for constant, local and workgroup coordinates. Reject modular
// arithmetic that could wrap; runtime coverage also bounds the largest invocation index.
static std::optional<std::array<uint64_t, 3>> FillIndex(Value value, uint32_t axis,
                                                        uint32_t depth = 0) {
	value = value.Resolve();
	if (depth > 32 || value.GetType() != Type::U32) {
		return {};
	}
	if (value.IsImmediate()) {
		return std::array<uint64_t, 3> {value.U32(), 0, 0};
	}
	const auto* inst = value.TryInstruction();
	if (inst == nullptr) {
		return {};
	}
	const auto op = inst->GetOpcode();
	if (op == ValueOpcode::GetBuiltin && inst->Arg(1) == Value(axis)) {
		if (inst->Arg(0) == Value(static_cast<uint32_t>(StageInputKind::LocalInvocationId))) {
			return std::array<uint64_t, 3> {0, 1, 0};
		}
		if (inst->Arg(0) == Value(static_cast<uint32_t>(StageInputKind::WorkgroupId))) {
			return std::array<uint64_t, 3> {0, 0, 1};
		}
	}
	if (op != ValueOpcode::IAdd32 && op != ValueOpcode::IMul32 &&
	    op != ValueOpcode::ShiftLeftLogical32) {
		return {};
	}
	auto left  = FillIndex(inst->Arg(0), axis, depth + 1);
	auto right = FillIndex(inst->Arg(1), axis, depth + 1);
	if (!left || !right) {
		return {};
	}
	if (op == ValueOpcode::IMul32 && ((*right)[1] != 0 || (*right)[2] != 0)) {
		std::swap(left, right);
	}
	if (op != ValueOpcode::IAdd32 && ((*right)[1] != 0 || (*right)[2] != 0)) {
		return {};
	}
	if (op == ValueOpcode::ShiftLeftLogical32) {
		if ((*right)[0] >= 32) return {};
		(*right)[0] = uint64_t {1} << (*right)[0];
	}
	for (uint32_t i = 0; i < left->size(); ++i) {
		(*left)[i] =
		    op == ValueOpcode::IAdd32 ? (*left)[i] + (*right)[i] : (*left)[i] * (*right)[0];
		if ((*left)[i] > UINT32_MAX) return {};
	}
	return left;
}

static UniformFillPlan AnalyzeUniformFill(const Program& program) {
	if (program.stage != ShaderType::Compute || program.blocks.empty() ||
	    program.blocks.size() != program.block_info.size() || program.info.uses_dma ||
	    !program.info.samplers.empty()) {
		return {};
	}
	std::unordered_set<uint32_t> visited;
	uint32_t                     index = 0;
	const Inst*                  store = nullptr;
	for (;;) {
		if (!visited.insert(index).second) return {};
		for (const auto& inst: *program.blocks[index]) {
			if (AddressOpcodeInfoOf(inst.GetOpcode()).access != AddressAccess::None) return {};
			if (!inst.MayHaveSideEffects()) continue;
			if (store != nullptr || (BufferAccessOf(inst.GetOpcode()) != BufferAccess::Write &&
			                         inst.GetOpcode() != ValueOpcode::ImageWrite))
				return {};
			store = &inst;
		}
		const auto& term = program.block_info[index].terminator;
		if (term.kind == CFG::TerminatorKind::Return) break;
		if (term.kind != CFG::TerminatorKind::Branch) return {};
		const auto next = std::ranges::find(program.block_info, term.true_block, &BlockInfo::id);
		if (next == program.block_info.end()) return {};
		index = static_cast<uint32_t>(next - program.block_info.begin());
	}
	if (store == nullptr || visited.size() != program.blocks.size()) return {};
	for (const auto& buffer: program.info.buffers) {
		if (buffer.read && (!buffer.scalar || buffer.written)) return {};
	}
	const auto&     memory = program.memory_info.at(store->Flags<MemoryFlags>().index);
	UniformFillPlan result;
	result.fill.resource = memory.resource;
	Value data;
	if (store->GetOpcode() == ValueOpcode::ImageWrite) {
		if (program.info.images.size() != 1 || memory.dmask != 1 || memory.data_bits != 32 ||
		    memory.image_has_mip || memory.image_sample_flags != 0 || memory.image_r128 ||
		    memory.image_dimension != Decoder::ImageDimension::Dim2DArray ||
		    store->Arg(3).Resolve() != Value(true))
			return {};
		const auto& image = program.info.images[memory.resource];
		if (image.read || image.atomic || image.mip_mode != ImageMipMode::None) return {};
		const auto* address = store->Arg(1).ResolveInstruction();
		if (address == nullptr || address->GetOpcode() != ValueOpcode::MakeImageAddress) return {};
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const auto index = FillIndex(address->Arg(axis), axis);
			if (!index || (*index)[0] != 0) return {};
			if (axis < 2) {
				if ((*index)[1] != 1 || (*index)[2] == 0) return {};
			} else if ((*index)[1] != 0 || (*index)[2] != 1) {
				return {};
			}
			result.fill.group_stride[axis] = static_cast<uint32_t>((*index)[2]);
		}
		const auto* values = store->Arg(2).ResolveInstruction();
		if (values == nullptr || values->GetOpcode() != ValueOpcode::CompositeConstructU32x4)
			return {};
		result.fill.kind  = UniformFillKind::Image;
		result.fill.words = 1;
		data              = values->Arg(0);
	} else {
		if (!program.info.images.empty()) return {};
		const auto           op = store->GetOpcode();
		constexpr std::array stores {ValueOpcode::StoreBufferU32, ValueOpcode::StoreBufferU32x2,
		                             ValueOpcode::StoreBufferU32x3, ValueOpcode::StoreBufferU32x4};
		const auto           store_op = std::ranges::find(stores, op);
		if (store_op == stores.end() || store->Arg(2).Resolve() != Value(0u) ||
		    store->Arg(3).Resolve() != Value(0u) || store->Arg(5).Resolve() != Value(true))
			return {};
		if (!memory.formatted || memory.typed || !memory.idxen || memory.offen ||
		    memory.offset != 0 || memory.data_bits != 32 ||
		    memory.data_dwords != static_cast<uint32_t>(store_op - stores.begin() + 1))
			return {};
		const auto address = FillIndex(store->Arg(1), 0);
		if (!address || (*address)[0] != 0 || (*address)[1] != 1 || (*address)[2] == 0) return {};
		result.fill.kind            = UniformFillKind::Buffer;
		result.fill.group_stride[0] = static_cast<uint32_t>((*address)[2]);
		result.fill.words           = memory.data_dwords;
		data                        = store->Arg(4);
	}
	data                        = data.Resolve();
	const auto*          vector = data.TryInstruction();
	constexpr std::array composites {ValueOpcode::CompositeConstructU32x2,
	                                 ValueOpcode::CompositeConstructU32x3,
	                                 ValueOpcode::CompositeConstructU32x4};
	if (result.fill.words > 1 &&
	    (vector == nullptr || vector->GetOpcode() != composites[result.fill.words - 2]))
		return {};
	for (uint32_t i = 0; i < result.fill.words; ++i) {
		const auto word = result.fill.words == 1 ? data : vector->Arg(i);
		if (word.GetType() != Type::U32 ||
		    !ValidateRuntimeValue(program, word, RuntimeValueType::Integer))
			return {};
		result.values[i] = word;
	}
	return result;
}

ResourcePlan ExtractResourcePlan(const Program& program) {
	ResourcePlan plan;
	plan.stage                      = program.stage;
	plan.shader_hash                = program.shader_hash;
	plan.user_data_base             = program.user_data_base;
	plan.user_data_count            = program.user_data_count;
	plan.info                       = program.info;
	plan.memory_info                = program.memory_info;
	plan.srt_plan_complete          = program.srt_plan_complete;
	plan.resource_tracking_complete = program.resource_tracking_complete;
	plan.bounded_srt_reads          = program.bounded_srt_reads;

	// Scalar addresses may be defined on different control-flow arms. Preserve the
	// original GPU SSA, but resolve a PHI in the CPU materialization plan using the
	// PC of the scalar load that consumes it. The PC belongs in the clone key because
	// separate loads can legitimately select different incoming values.
	std::map<std::pair<const Inst*, uint32_t>, Inst*>        cloned;
	std::map<std::pair<const Inst*, uint32_t>, bool>         contextual;
	std::map<std::pair<const Inst*, uint32_t>, Value>        resolved_phis;
	std::map<std::pair<const Inst*, uint32_t>, Value>        lowered_phis;
	std::set<std::pair<const Inst*, uint32_t>>               lowering_phis;
	std::set<std::pair<const Inst*, uint32_t>>               cloning;
	std::set<std::tuple<const Inst*, uint32_t, std::string>> runtime_phi_reports;
	std::map<const Inst*, const Inst*>                       clone_origins;
	const auto BlockMetadata = [&](const Block* block) -> const BlockInfo* {
		const auto found = std::ranges::find(program.blocks, block);
		if (found == program.blocks.end()) return nullptr;
		const auto index = static_cast<size_t>(found - program.blocks.begin());
		return index < program.block_info.size() ? &program.block_info[index] : nullptr;
	};
	const auto ResolvePhiAt = [&](const Inst* phi, uint32_t pc) {
		const auto key = std::pair {phi, pc};
		if (const auto found = resolved_phis.find(key); found != resolved_phis.end())
			return found->second;
		const auto value = ResolveResourcePhi(program, Value(const_cast<Inst*>(phi)), pc);
		resolved_phis.emplace(key, value);
		return value;
	};
	const auto NeedsContext = [&](const Inst* root, uint32_t pc) {
		if (pc == UINT32_MAX) return false;
		const auto key = std::pair {root, pc};
		if (const auto found = contextual.find(key); found != contextual.end())
			return found->second;
		std::vector<const Inst*>        pending {root};
		std::unordered_set<const Inst*> visited;
		bool                            needed = false;
		while (!pending.empty() && !needed) {
			const auto* inst = pending.back();
			pending.pop_back();
			if (!visited.insert(inst).second) continue;
			if (const auto found = contextual.find({inst, pc}); found != contextual.end()) {
				needed = found->second;
				continue;
			}
			const auto op = inst->GetOpcode();
			// Raw loads establish their own context. ReadConst is a reference to a
			// separate flattened SRT entry and must not inherit its user's PC.
			if (op == ValueOpcode::LoadAddressU32 || op == ValueOpcode::ReadConstBuffer ||
			    op == ValueOpcode::ReadConst)
				continue;
			// A PHI is inherently context-sensitive until it has been resolved or
			// lowered.  Keep every containing expression keyed by the active PC;
			// treating an ambiguous PHI as context-free would let the first arm's
			// cached clone leak into another arm before RuntimePhiSelect can prove it.
			if (op == ValueOpcode::Phi) {
				needed = true;
				break;
			}
			for (size_t i = 0; i < inst->NumArgs(); ++i) {
				if (const auto* arg = inst->Arg(i).Resolve().TryInstruction())
					pending.push_back(arg);
			}
		}
		if (!needed) {
			for (const auto* inst: visited)
				contextual.emplace(std::pair {inst, pc}, false);
		}
		contextual.emplace(key, needed);
		return needed;
	};
	const bool trace_runtime_phi = std::getenv("KYTY_TRACE_RUNTIME_PHI") != nullptr;
	const auto RuntimePhiSelect  = [&](const Inst* phi, uint32_t pc, Value& condition,
	                                   Value& true_value, Value& false_value,
	                                   const Block*& true_pred, const Block*& false_pred) {
		const bool trace       = trace_runtime_phi;
		const auto parent      = phi == nullptr ? nullptr : phi->Parent();
		const auto parent_info = BlockMetadata(parent);
		const auto reject      = [&](const char* reason) {
			if (trace) {
				if (runtime_phi_reports.emplace(phi, pc, reason).second) {
					std::fprintf(stderr,
					             "runtime PHI select rejected phi=%p parent=%u pc=0x%08x "
					             "reason=%s\n",
					             static_cast<const void*>(phi),
					             parent_info == nullptr ? UINT32_MAX : parent_info->id, pc, reason);
				}
			}
			return false;
		};
		const auto note = [&](const char* reason) {
			if (trace) {
				if (runtime_phi_reports.emplace(phi, pc, reason).second) {
					std::fprintf(stderr,
					             "runtime PHI select candidate phi=%p parent=%u pc=0x%08x "
					             "note=%s\n",
					             static_cast<const void*>(phi),
					             parent_info == nullptr ? UINT32_MAX : parent_info->id, pc, reason);
				}
			}
		};
		if (pc == UINT32_MAX) return reject("missing consuming PC");
		if (phi == nullptr) return reject("null value");
		if (phi->GetOpcode() != ValueOpcode::Phi) return reject("value is not PHI");
		if (phi->NumArgs() != 2u || phi->NumPhiBlocks() != 2u) return reject("PHI is not binary");
		if (parent == nullptr) return reject("PHI has no parent block");
		if (parent->ImmPredecessors().size() != 2u)
			return reject("merge does not have exactly two predecessors");
		const auto* first  = phi->PhiBlock(0u);
		const auto* second = phi->PhiBlock(1u);
		if (first == nullptr || second == nullptr) return reject("missing PHI predecessor");
		if (first == second) return reject("PHI predecessors are identical");
		if (std::ranges::find(parent->ImmPredecessors(), first) ==
		        parent->ImmPredecessors().end() ||
		    std::ranges::find(parent->ImmPredecessors(), second) == parent->ImmPredecessors().end())
			return reject("PHI predecessor is not a merge predecessor");
		const auto* first_info  = BlockMetadata(first);
		const auto* second_info = BlockMetadata(second);
		if (first_info == nullptr || second_info == nullptr || parent_info == nullptr)
			return reject("missing CFG metadata");

		struct Shape {
			const Block* split      = nullptr;
			uint32_t     true_arg   = 0u;
			uint32_t     false_arg  = 0u;
			const Block* true_pred  = nullptr;
			const Block* false_pred = nullptr;
			bool         direct     = false;
		};
		Shape shape;

		// Keep the old compact form for CFGs where the conditional block itself is
		// one PHI predecessor. This compact form occurs in shaders that merge an
		// if-then edge directly with the conditional block.
		const auto try_direct_shape = [&](const Block* candidate_split,
		                                  const Block* candidate_other, uint32_t split_arg,
		                                  uint32_t other_arg) {
			const auto* split_info = BlockMetadata(candidate_split);
			const auto* other_info = BlockMetadata(candidate_other);
			if (split_info == nullptr || other_info == nullptr) return false;
			if (split_info->terminator.kind != CFG::TerminatorKind::ConditionalBranch ||
			    other_info->terminator.kind != CFG::TerminatorKind::Branch)
				return false;
			if (split_info->terminator.loop_header || other_info->terminator.loop_header)
				return false;
			if (candidate_split->ImmSuccessors().size() != 2u ||
			    std::ranges::find(candidate_split->ImmSuccessors(), parent) ==
			        candidate_split->ImmSuccessors().end() ||
			    std::ranges::find(candidate_split->ImmSuccessors(), candidate_other) ==
			        candidate_split->ImmSuccessors().end())
				return false;
			if (candidate_other->ImmPredecessors().size() != 1u ||
			    candidate_other->ImmPredecessors().front() != candidate_split ||
			    candidate_other->ImmSuccessors().size() != 1u ||
			    candidate_other->ImmSuccessors().front() != parent ||
			    other_info->terminator.true_block != parent_info->id)
				return false;
			if (split_info->terminator.true_block == parent_info->id &&
			    split_info->terminator.false_block == other_info->id) {
				shape = {candidate_split, split_arg, other_arg, candidate_split, candidate_other};
				shape.direct = true;
				return true;
			}
			if (split_info->terminator.false_block == parent_info->id &&
			    split_info->terminator.true_block == other_info->id) {
				shape = {candidate_split, other_arg, split_arg, candidate_other, candidate_split};
				shape.direct = true;
				return true;
			}
			return false;
		};
		if (!try_direct_shape(first, second, 0u, 1u) && !try_direct_shape(second, first, 1u, 0u)) {
			note("direct if-then shape rejected");
		}

		struct PathResult {
			bool                      ok     = false;
			const char*               reason = "unknown path failure";
			std::vector<const Block*> blocks;
			const Block*              edge_from = nullptr;
			const Block*              edge_to   = nullptr;
		};
		const auto follow_trivial_path = [&](const Block* split, const Block* start,
		                                     const Block* target) {
			PathResult result;
			if (split == nullptr || start == nullptr || target == nullptr || start == parent ||
			    target == parent || start == split || target == split) {
				result.reason = "path starts or ends at split/merge";
				return result;
			}
			const Block*           previous = split;
			const Block*           current  = start;
			std::set<const Block*> visited;
			while (current != target) {
				if (current == split || current == parent) {
					result.reason = "loop reaches split or merge";
					return result;
				}
				if (!visited.insert(current).second) {
					result.reason = "loop in trivial path";
					return result;
				}
				const auto* info = BlockMetadata(current);
				if (info == nullptr) {
					result.reason = "missing path metadata";
					return result;
				}
				if (info->terminator.loop_header) {
					result.reason = "loop header in trivial path";
					return result;
				}
				if (current->ImmPredecessors().size() != 1u ||
				    current->ImmPredecessors().front() != previous) {
					result.reason    = "cross-edge in trivial path";
					result.edge_from = previous;
					result.edge_to   = current;
					return result;
				}
				if (info->terminator.kind != CFG::TerminatorKind::Branch ||
				    current->ImmSuccessors().size() != 1u) {
					result.reason = "secondary branch in trivial path";
					return result;
				}
				const auto* next      = current->ImmSuccessors().front();
				const auto* next_info = BlockMetadata(next);
				if (next_info == nullptr || info->terminator.true_block != next_info->id) {
					result.reason = "inconsistent trivial successor";
					return result;
				}
				result.blocks.push_back(current);
				previous = current;
				current  = next;
			}
			const auto* target_info = BlockMetadata(target);
			if (target_info == nullptr || target_info->terminator.loop_header ||
			    target_info->terminator.kind != CFG::TerminatorKind::Branch ||
			    target->ImmSuccessors().size() != 1u || target->ImmSuccessors().front() != parent ||
			    target_info->terminator.true_block != parent_info->id) {
				result.reason = "secondary branch at PHI predecessor";
				return result;
			}
			if (current->ImmPredecessors().size() != 1u ||
			    current->ImmPredecessors().front() != previous) {
				result.reason    = "cross-edge at PHI predecessor";
				result.edge_from = previous;
				result.edge_to   = current;
				return result;
			}
			result.blocks.push_back(target);
			result.ok = true;
			return result;
		};

		// A normal diamond has a common conditional split and only unconditional,
		// single-entry blocks on each arm. Enumerate all possible splits so a block
		// immediately preceding an arm is not mistaken for the split itself.
		std::vector<Shape> matches;
		const char*        last_path_failure = "no conditional split candidate";
		if (shape.split == nullptr) {
			for (const auto* candidate_split: program.blocks) {
				if (BlockMetadata(candidate_split) == nullptr || candidate_split == parent)
					continue;
				const auto* split_info = BlockMetadata(candidate_split);
				if (split_info->terminator.kind != CFG::TerminatorKind::ConditionalBranch ||
				    split_info->terminator.loop_header ||
				    candidate_split->ImmSuccessors().size() != 2u)
					continue;
				const auto successor_for_id = [&](uint32_t id) -> const Block* {
					for (const auto* successor: candidate_split->ImmSuccessors()) {
						const auto* info = BlockMetadata(successor);
						if (info != nullptr && info->id == id) return successor;
					}
					return nullptr;
				};
				const auto* true_start  = successor_for_id(split_info->terminator.true_block);
				const auto* false_start = successor_for_id(split_info->terminator.false_block);
				if (true_start == false_start || true_start == parent || false_start == parent)
					continue;
				if (true_start == nullptr || false_start == nullptr) {
					last_path_failure = "conditional successor metadata mismatch";
					continue;
				}
				const auto try_paths = [&](const Block* true_target, const Block* false_target,
				                           uint32_t true_arg, uint32_t false_arg) {
					const auto true_path =
					    follow_trivial_path(candidate_split, true_start, true_target);
					const auto false_path =
					    follow_trivial_path(candidate_split, false_start, false_target);
					if (!true_path.ok || !false_path.ok) {
						last_path_failure = !true_path.ok ? true_path.reason : false_path.reason;
						if (trace && runtime_phi_reports.emplace(phi, pc, "path-detail").second) {
							const auto block_id = [&](const Block* block) {
								const auto* info = BlockMetadata(block);
								return info == nullptr ? UINT32_MAX : info->id;
							};
							const auto dump_path = [&](const char* label, const Block* start,
							                           const Block*      target,
							                           const PathResult& result) {
								std::fprintf(stderr,
								             "runtime PHI path candidate split=%u %s-start=%u "
								             "target=%u result=%s path=%u->",
								             block_id(candidate_split), label, block_id(start),
								             block_id(target), result.reason, block_id(start));
								for (const auto* block: result.blocks)
									std::fprintf(stderr, "%u->", block_id(block));
								std::fprintf(stderr, "%u\n", block_id(target));
								if (result.edge_to != nullptr) {
									std::fprintf(stderr,
									             "runtime PHI path unexpected edge from=%u to=%u "
									             "expected predecessor=%u actual-preds=",
									             block_id(result.edge_from),
									             block_id(result.edge_to),
									             block_id(result.edge_from));
									for (const auto* predecessor: result.edge_to->ImmPredecessors())
										std::fprintf(stderr, "%u,", block_id(predecessor));
									std::fprintf(stderr, "\n");
								}
							};
							dump_path("true", true_start, true_target, true_path);
							dump_path("false", false_start, false_target, false_path);
						}
						return;
					}
					for (const auto* block: true_path.blocks) {
						if (std::ranges::find(false_path.blocks, block) !=
						    false_path.blocks.end()) {
							last_path_failure = "arm paths intersect";
							return;
						}
					}
					matches.push_back(
					    {candidate_split, true_arg, false_arg, true_target, false_target, false});
				};
				try_paths(first, second, 0u, 1u);
				try_paths(second, first, 1u, 0u);
			}
		}
		if (shape.split == nullptr) {
			if (matches.empty()) {
				note(last_path_failure);
				return reject("no common conditional split");
			}
			if (matches.size() != 1u) return reject("ambiguous conditional split");
			shape = matches.front();
		}
		const auto* split_info = BlockMetadata(shape.split);
		if (split_info == nullptr) return reject("selected split has no metadata");
		if (trace) {
			std::fprintf(stderr,
			             "runtime PHI select CFG accepted phi=%p parent=%u split=%u true_pred=%u "
			             "false_pred=%u shape=%s pc=0x%08x; structural proof only\n",
			             static_cast<const void*>(phi), parent_info->id, split_info->id,
			             BlockMetadata(shape.true_pred)->id, BlockMetadata(shape.false_pred)->id,
			             shape.direct ? "direct" : "diamond", pc);
		}
		condition = split_info->condition.Resolve();
		if (condition.GetType() != Type::U1) return reject("condition has wrong type");
		true_value  = phi->Arg(shape.true_arg).Resolve();
		false_value = phi->Arg(shape.false_arg).Resolve();
		if (true_value.GetType() != Type::U32) return reject("true arm has wrong type");
		if (false_value.GetType() != Type::U32) return reject("false arm has wrong type");
		// Do not validate the condition or either incoming expression here.  The
		// incoming values may contain context-dependent PHIs (including a raw
		// SelectU32 root) whose validity is established only after CloneAt walks
		// them with the corresponding predecessor PC.  RuntimePhiSelect proves
		// only the CFG shape and exact operand mapping; the resulting graph is
		// validated after all recursive lowering has completed.
		true_pred  = shape.true_pred;
		false_pred = shape.false_pred;
		if (trace) {
			std::fprintf(stderr,
			             "runtime PHI select accepted phi=%p parent=%u split=%u pc=0x%08x\n",
			             static_cast<const void*>(phi), parent_info->id, split_info->id, pc);
		}
		return true;
	};
	std::function<Value(Value, uint32_t)> CloneAt = [&](Value root, uint32_t root_pc) -> Value {
		enum class FrameMode { Resolve, Redirect, Lower, Clone };
		struct Frame {
			Value                            value;
			uint32_t                         pc     = UINT32_MAX;
			const Inst*                      source = nullptr;
			std::pair<const Inst*, uint32_t> active_key {};
			Inst*                            target = nullptr;
			std::vector<Value>               children;
			std::vector<uint32_t>            child_pcs;
			std::vector<Value>               results;
			size_t                           next_child = 0;
			FrameMode                        mode       = FrameMode::Resolve;
			bool                             active     = false;
			bool                             lowering   = false;
		};
		const auto BlockContextPc = [&](const Block* block, uint32_t fallback) {
			const auto* info = BlockMetadata(block);
			return info != nullptr && info->end_pc > info->start_pc ? info->start_pc : fallback;
		};

		std::vector<Frame> stack;
		stack.push_back({.value = root, .pc = root_pc});
		Value      last_result;
		bool       have_result = false;
		const auto finish      = [&](Frame& frame, Value result) {
			if (frame.lowering) {
				lowering_phis.erase(frame.active_key);
			}
			if (frame.active) {
				cloning.erase(frame.active_key);
			}
			last_result = result;
			have_result = true;
			stack.pop_back();
		};
		const auto push_child = [&](Frame& frame, Value value, uint32_t pc) {
			frame.next_child++;
			have_result = false;
			stack.push_back({.value = value, .pc = pc});
		};

		while (!stack.empty()) {
			Frame& frame = stack.back();
			if (frame.mode == FrameMode::Resolve) {
				frame.value  = frame.value.Resolve();
				frame.source = frame.value.TryInstruction();
				if (frame.source == nullptr) {
					finish(frame, frame.value);
					continue;
				}
				const auto op = frame.source->GetOpcode();
				if (op == ValueOpcode::LoadAddressU32 || op == ValueOpcode::ReadConstBuffer) {
					frame.pc = frame.source->Flags<MemoryFlags>().pc;
				} else if (op == ValueOpcode::ReadConst || op == ValueOpcode::GetUserData ||
				           op == ValueOpcode::GetShaderBase || op == ValueOpcode::GetSrtResource) {
					frame.pc = UINT32_MAX;
				}
				frame.active_key = {frame.source, frame.pc};
				if (!cloning.insert(frame.active_key).second) {
					// A resource expression that re-enters the same contextual definition
					// is cyclic. Reject that dependency instead of imposing a depth limit.
					finish(frame, {});
					continue;
				}
				frame.active = true;
				if (op == ValueOpcode::Phi) {
					if (frame.pc != UINT32_MAX) {
						const auto guarded = ResolvePhiAt(frame.source, frame.pc);
						if (guarded != frame.value) {
							frame.mode      = FrameMode::Redirect;
							frame.children  = {guarded};
							frame.child_pcs = {frame.pc};
							frame.results.resize(1u);
							push_child(frame, guarded, frame.pc);
							continue;
						}
					}
					const auto invariant = ResolveInvariantPhi(program, frame.value);
					if (!invariant.IsEmpty() && invariant != frame.value) {
						frame.mode      = FrameMode::Redirect;
						frame.children  = {invariant};
						frame.child_pcs = {frame.pc};
						frame.results.resize(1u);
						push_child(frame, invariant, frame.pc);
						continue;
					}
					if (const auto found = lowered_phis.find(frame.active_key);
					    found != lowered_phis.end()) {
						finish(frame, found->second);
						continue;
					}
					Value        condition;
					Value        true_value;
					Value        false_value;
					const Block* true_pred  = nullptr;
					const Block* false_pred = nullptr;
					if (!lowering_phis.contains(frame.active_key) &&
					    RuntimePhiSelect(frame.source, frame.pc, condition, true_value, false_value,
					                     true_pred, false_pred)) {
						lowering_phis.insert(frame.active_key);
						auto& target = plan.value_storage.emplace_back(
						    ValueOpcode::SelectU32, frame.source->Flags<uint64_t>());
						frame.target   = &target;
						frame.mode     = FrameMode::Lower;
						frame.lowering = true;
						lowered_phis.emplace(frame.active_key, Value(&target));
						frame.children = {condition, true_value, false_value};
						// Keep the active use-site PC for the selector itself.  Incoming
						// values are evaluated in the corresponding predecessor block so
						// nested PHIs can select their own edge without losing context.
						frame.child_pcs = {frame.pc, BlockContextPc(true_pred, frame.pc),
						                   BlockContextPc(false_pred, frame.pc)};
						frame.results.resize(frame.children.size());
						push_child(frame, frame.children[0], frame.child_pcs[0]);
						continue;
					}
				}
				const auto clone_key = std::pair {
				    frame.source, NeedsContext(frame.source, frame.pc) ? frame.pc : UINT32_MAX};
				if (const auto found = cloned.find(clone_key); found != cloned.end()) {
					finish(frame, Value(found->second));
					continue;
				}
				auto& target = plan.value_storage.emplace_back(frame.source->GetOpcode(),
				                                               frame.source->Flags<uint64_t>());
				cloned.emplace(clone_key, &target);
				clone_origins.emplace(&target, frame.source);
				frame.target = &target;
				frame.mode   = FrameMode::Clone;
				frame.children.reserve(frame.source->NumArgs());
				frame.child_pcs.reserve(frame.source->NumArgs());
				frame.results.resize(frame.source->NumArgs());
				for (size_t index = 0; index < frame.source->NumArgs(); ++index) {
					frame.children.push_back(frame.source->Arg(index));
					frame.child_pcs.push_back(frame.pc);
				}
				if (frame.children.empty()) {
					finish(frame, Value(frame.target));
					continue;
				}
				push_child(frame, frame.children[0], frame.pc);
				continue;
			}
			if (!have_result) continue;
			frame.results[frame.next_child - 1u] = last_result;
			if (frame.next_child < frame.children.size()) {
				push_child(frame, frame.children[frame.next_child],
				           frame.child_pcs[frame.next_child]);
				continue;
			}
			if (frame.mode == FrameMode::Redirect) {
				finish(frame, frame.results[0]);
				continue;
			}
			for (size_t index = 0; index < frame.results.size(); ++index) {
				if (frame.mode == FrameMode::Lower ||
				    frame.source->GetOpcode() != ValueOpcode::Phi) {
					frame.target->SetArg(index, frame.results[index]);
				} else {
					frame.target->AddPhiOperand(nullptr, frame.results[index]);
				}
			}
			finish(frame, Value(frame.target));
		}
		return last_result;
	};
	const auto Clone = [&](Value value) { return CloneAt(value, UINT32_MAX); };

	plan.descriptor_sources.reserve(program.descriptor_sources.size());
	for (const auto& source: program.descriptor_sources) {
		auto& target          = plan.descriptor_sources.emplace_back();
		target.dword_count    = source.dword_count;
		target.indirect_image = source.indirect_image;
		target.bounded_buffer = source.bounded_buffer;
		if (source.indirect_image.has_value() && !source.indirect_image->direct_offset.IsEmpty()) {
			target.indirect_image->direct_offset = Clone(source.indirect_image->direct_offset);
			for (auto& range: target.indirect_image->index_ranges) {
				range.value = Clone(range.value);
				range.begin = Clone(range.begin);
				range.end   = Clone(range.end);
				for (auto& [bound, limit]: range.bound_limits)
					bound = Clone(bound);
			}
		}
		if (source.indirect_buffer.has_value()) {
			target.indirect_buffer              = source.indirect_buffer;
			target.indirect_buffer->byte_offset = Clone(source.indirect_buffer->byte_offset);
			for (auto& range: target.indirect_buffer->index_ranges) {
				range.value = Clone(range.value);
				range.begin = Clone(range.begin);
				range.end   = Clone(range.end);
				for (auto& [bound, limit]: range.bound_limits)
					bound = Clone(bound);
			}
			plan.requires_specialization_memory = true;
		}
		for (uint32_t dword = 0; dword < source.dword_count; dword++) {
			target.dwords[dword] = Clone(source.dwords[dword]);
		}
	}
	plan.srt_reads.reserve(program.srt_reads.size());
	for (const auto& read: program.srt_reads) {
		plan.srt_reads.push_back({Clone(read.value), read.flat_offset, read.shader_side});
	}
	bool       residual_phi_reported = false;
	const auto DescribeValue         = [](Value value) {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (value.IsEmpty()) return std::string {"empty"};
		if (value.IsImmediate()) {
			return fmt::format("immediate:{}:{}", TypeName(value.GetType()), value.U32());
		}
		if (inst == nullptr) return std::string {"unknown"};
		return fmt::format("{}:{}", ValueOpcodeName(inst->GetOpcode()), TypeName(value.GetType()));
	};
	const auto TerminatorName = [](CFG::TerminatorKind kind) {
		switch (kind) {
			case CFG::TerminatorKind::Branch: return "branch";
			case CFG::TerminatorKind::ConditionalBranch: return "conditional";
			case CFG::TerminatorKind::IndirectBranch: return "indirect";
			case CFG::TerminatorKind::Return: return "return";
			default: return "unsupported";
		}
	};
	const auto BlockId = [&](const Block* block) {
		const auto* info = BlockMetadata(block);
		return info == nullptr ? UINT32_MAX : info->id;
	};
	const auto PrintBlock = [&](const char* label, const Block* block) {
		const auto* info = BlockMetadata(block);
		if (block == nullptr || info == nullptr) {
			std::fprintf(stderr, "runtime PHI %s block=<unknown>\n", label);
			return;
		}
		const auto& term = info->terminator;
		std::fprintf(stderr,
		             "runtime PHI %s block=%u pc=[0x%08x,0x%08x) term=%s loop=%u "
		             "true=%u false=%u preds=",
		             label, info->id, info->start_pc, info->end_pc, TerminatorName(term.kind),
		             term.loop_header ? 1u : 0u, term.true_block, term.false_block);
		for (const auto* predecessor: block->ImmPredecessors())
			std::fprintf(stderr, "%u,", BlockId(predecessor));
		std::fprintf(stderr, " succs=");
		for (const auto* successor: block->ImmSuccessors())
			std::fprintf(stderr, "%u,", BlockId(successor));
		if (term.kind == CFG::TerminatorKind::ConditionalBranch) {
			std::fprintf(stderr, " condition=%s", DescribeValue(info->condition).c_str());
		}
		std::fprintf(stderr, "\n");
	};
	const auto HasExecDependency = [&](Value root) {
		std::unordered_set<const Inst*> visited;
		std::function<bool(Value)>      walk = [&](Value value) {
			value            = value.Resolve();
			const auto* inst = value.TryInstruction();
			if (inst == nullptr || !visited.insert(inst).second) return false;
			switch (inst->GetOpcode()) {
				case ValueOpcode::GetExec:
				case ValueOpcode::GetExecLo:
				case ValueOpcode::GetExecHi:
				case ValueOpcode::GetVcc:
				case ValueOpcode::GetVccLo:
				case ValueOpcode::GetVccHi:
				case ValueOpcode::SetExec:
				case ValueOpcode::SetExecLo:
				case ValueOpcode::SetExecHi:
				case ValueOpcode::SetVcc:
				case ValueOpcode::SetVccLo:
				case ValueOpcode::SetVccHi: return true;
				default: break;
			}
			for (size_t index = 0; index < inst->NumArgs(); ++index)
				if (walk(inst->Arg(index))) return true;
			return false;
		};
		return walk(root);
	};
	const auto EmitResidualPhiDiagnostic = [&](Value root, const char* kind, uint32_t index,
	                                           uint32_t dword) {
		if (!trace_runtime_phi || residual_phi_reported) return;
		std::vector<std::string>        path {"root"};
		std::unordered_set<const Inst*> visited;
		const Inst*                     residual         = nullptr;
		const Inst*                     first_read_const = nullptr;
		const auto                      FindPhi          = [&](auto&& self, Value value) -> bool {
			value            = value.Resolve();
			const auto* inst = value.TryInstruction();
			if (inst == nullptr) return false;
			if (inst->GetOpcode() == ValueOpcode::Phi) {
				residual = inst;
				return true;
			}
			if (inst->GetOpcode() == ValueOpcode::ReadConst && first_read_const == nullptr)
				first_read_const = inst;
			if (!visited.insert(inst).second) return false;
			if (inst->GetOpcode() == ValueOpcode::ReadConst && inst->NumArgs() == 2u) {
				const auto slot = inst->Arg(1).Resolve();
				if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
				    slot.U32() < plan.srt_reads.size()) {
					path.push_back(fmt::format("ReadConst[srt={}]", slot.U32()));
					if (self(self, plan.srt_reads[slot.U32()].value)) return true;
					path.pop_back();
				}
			}
			for (size_t arg = 0; arg < inst->NumArgs(); ++arg) {
				path.push_back(fmt::format("{}[{}]", ValueOpcodeName(inst->GetOpcode()), arg));
				if (self(self, inst->Arg(arg))) return true;
				path.pop_back();
			}
			return false;
		};
		if (!FindPhi(FindPhi, root)) return;
		residual_phi_reported        = true;
		const auto            origin = clone_origins.find(residual);
		const auto*           phi    = origin == clone_origins.end() ? residual : origin->second;
		std::vector<uint32_t> slots;
		const auto            Contains = [&](Value value, const Inst* target) {
			std::unordered_set<const Inst*> graph_visited;
			std::function<bool(Value)>      walk = [&](Value current) {
				current          = current.Resolve();
				const auto* inst = current.TryInstruction();
				if (inst == nullptr) return false;
				if (inst == target) return true;
				if (!graph_visited.insert(inst).second) return false;
				if (inst->GetOpcode() == ValueOpcode::ReadConst && inst->NumArgs() == 2u) {
					const auto slot = inst->Arg(1).Resolve();
					if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
					    slot.U32() < plan.srt_reads.size() &&
					    walk(plan.srt_reads[slot.U32()].value))
						return true;
				}
				for (size_t arg = 0; arg < inst->NumArgs(); ++arg)
					if (walk(inst->Arg(arg))) return true;
				return false;
			};
			return walk(value);
		};
		for (uint32_t slot = 0; slot < plan.srt_reads.size(); ++slot) {
			if (Contains(plan.srt_reads[slot].value, residual) ||
			    (origin != clone_origins.end() && Contains(program.srt_reads[slot].value, phi)))
				slots.push_back(slot);
		}
		std::fprintf(stderr,
		             "runtime PHI residual shader=0x%016llx kind=%s source=%u dword=%s "
		             "slots=",
		             static_cast<unsigned long long>(program.shader_hash), kind, index,
		             dword == UINT32_MAX ? "<none>" : std::to_string(dword).c_str());
		if (slots.empty()) {
			std::fprintf(stderr, "<none>");
		} else {
			for (const auto slot: slots)
				std::fprintf(stderr, "%u,", slot);
		}
		std::fprintf(stderr, " path=");
		for (size_t part = 0; part < path.size(); ++part)
			std::fprintf(stderr, "%s%s", part == 0 ? "" : " -> ", path[part].c_str());
		std::fprintf(stderr, " residual=%p origin=%p op=%s type=%s\n",
		             static_cast<const void*>(residual), static_cast<const void*>(phi),
		             ValueOpcodeName(phi->GetOpcode()).data(),
		             TypeName(Value(phi).GetType()).c_str());
		if (first_read_const != nullptr) {
			std::fprintf(stderr, "runtime PHI ReadConst context read=%p args=%u\n",
			             static_cast<const void*>(first_read_const),
			             static_cast<unsigned>(first_read_const->NumArgs()));
			for (const auto slot: slots) {
				uint32_t context_pc = UINT32_MAX;
				if (slot < program.srt_reads.size()) {
					const auto* srt_root = program.srt_reads[slot].value.ResolveInstruction();
					if (srt_root != nullptr &&
					    (srt_root->GetOpcode() == ValueOpcode::LoadAddressU32 ||
					     srt_root->GetOpcode() == ValueOpcode::ReadConstBuffer))
						context_pc = srt_root->Flags<MemoryFlags>().pc;
				}
				if (context_pc != UINT32_MAX) {
					std::fprintf(
					    stderr,
					    "runtime PHI ReadConst context slot=%u pc=0x%08x "
					    "read_needs=%u srt_needs=%u\n",
					    slot, context_pc, NeedsContext(first_read_const, context_pc) ? 1u : 0u,
					    NeedsContext(program.srt_reads[slot].value.TryInstruction(), context_pc)
					        ? 1u
					        : 0u);
				}
			}
		}
		if (phi->GetOpcode() != ValueOpcode::Phi) return;
		PrintBlock("phi-parent", phi->Parent());
		if (phi->Parent() != nullptr) {
			for (const auto* predecessor: phi->Parent()->ImmPredecessors())
				PrintBlock("parent-predecessor", predecessor);
			for (const auto* successor: phi->Parent()->ImmSuccessors())
				PrintBlock("parent-successor", successor);
		}
		for (size_t incoming = 0; incoming < phi->NumArgs(); ++incoming) {
			const auto  value         = phi->Arg(incoming).Resolve();
			const auto* incoming_inst = value.TryInstruction();
			std::fprintf(stderr, "runtime PHI incoming=%u block=%u value=%s\n",
			             static_cast<unsigned>(incoming), BlockId(phi->PhiBlock(incoming)),
			             DescribeValue(value).c_str());
			if (incoming_inst != nullptr && incoming_inst->GetOpcode() == ValueOpcode::Phi) {
				std::fprintf(stderr, "runtime PHI incoming=%u nested-parent=%u nested-args=%u\n",
				             static_cast<unsigned>(incoming), BlockId(incoming_inst->Parent()),
				             static_cast<unsigned>(incoming_inst->NumArgs()));
				PrintBlock("nested-phi-parent", incoming_inst->Parent());
				for (const auto* nested_predecessor:
				     incoming_inst->Parent() == nullptr
				         ? std::span<Block* const> {}
				         : incoming_inst->Parent()->ImmPredecessors())
					PrintBlock("nested-phi-predecessor", nested_predecessor);
			}
			PrintBlock("incoming-predecessor", phi->PhiBlock(incoming));
			if (phi->PhiBlock(incoming) != nullptr) {
				for (const auto* predecessor: phi->PhiBlock(incoming)->ImmPredecessors())
					PrintBlock("incoming-predecessor-parent", predecessor);
			}
		}
		const auto ContainsOpcode = [](Value root, ValueOpcode opcode) {
			std::unordered_set<const Inst*> opcode_visited;
			std::function<bool(Value)>      walk = [&](Value value) {
				value            = value.Resolve();
				const auto* inst = value.TryInstruction();
				if (inst == nullptr) return false;
				if (inst->GetOpcode() == opcode) return true;
				if (!opcode_visited.insert(inst).second) return false;
				for (size_t arg = 0; arg < inst->NumArgs(); ++arg)
					if (walk(inst->Arg(arg))) return true;
				return false;
			};
			return walk(root);
		};
		std::unordered_set<const Inst*> lane_visited;
		std::unordered_set<const Inst*> lane_reported;
		const auto                      WalkReadLane = [&](auto&& self, Value value) -> void {
			value            = value.Resolve();
			const auto* inst = value.TryInstruction();
			if (inst == nullptr || !lane_visited.insert(inst).second) return;
			if (inst->GetOpcode() == ValueOpcode::ReadLane && lane_reported.insert(inst).second) {
				const auto lane = inst->NumArgs() > 1u ? inst->Arg(1).Resolve() : Value {};
				const auto data = inst->NumArgs() > 0u ? inst->Arg(0).Resolve() : Value {};
				std::fprintf(stderr,
				             "runtime PHI ReadLane inst=%p value=%s lane=%s lane_immediate=%u "
				             "lane_runtime_valid=%u exec_dependency=%u lane_id=%u lane_phi=%u "
				             "lane_readlane=%u data_phi=%u\n",
				             static_cast<const void*>(inst), DescribeValue(data).c_str(),
				             DescribeValue(lane).c_str(), lane.IsImmediate() ? 1u : 0u,
				             ValidateRuntimeValue(program, lane, RuntimeValueType::Integer) ? 1u
				                                                                            : 0u,
				             HasExecDependency(data) || HasExecDependency(lane) ? 1u : 0u,
				             ContainsOpcode(lane, ValueOpcode::LaneId) ? 1u : 0u,
				             ContainsOpcode(lane, ValueOpcode::Phi) ? 1u : 0u,
				             ContainsOpcode(lane, ValueOpcode::ReadLane) ? 1u : 0u,
				             ContainsOpcode(data, ValueOpcode::Phi) ? 1u : 0u);
			}
			if (inst->GetOpcode() == ValueOpcode::ReadConst && inst->NumArgs() == 2u) {
				const auto slot = inst->Arg(1).Resolve();
				if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
				    slot.U32() < program.srt_reads.size())
					self(self, program.srt_reads[slot.U32()].value);
			}
			for (size_t arg = 0; arg < inst->NumArgs(); ++arg)
				self(self, inst->Arg(arg));
		};
		for (size_t incoming = 0; incoming < phi->NumArgs(); ++incoming)
			WalkReadLane(WalkReadLane, phi->Arg(incoming));
	};
	bool       runtime_graph_valid      = true;
	const auto ValidateMaterializedRoot = [&](Value value, const char* kind, uint32_t index,
	                                          uint32_t dword = UINT32_MAX) {
		value            = value.Resolve();
		const auto* root = value.TryInstruction();
		// Descriptor arrays may carry an unused Void slot beyond their populated
		// dword count.  There is no runtime graph to validate in that slot.
		if (value.IsEmpty() || root == nullptr || root->GetOpcode() == ValueOpcode::Void ||
		    ValidateRuntimeValue(plan, value, RuntimeValueType::Integer))
			return;
		runtime_graph_valid = false;
		if (trace_runtime_phi) {
			const auto* inst = value.TryInstruction();
			std::fprintf(stderr, "runtime graph validation rejected kind=%s index=%u opcode=%s\n",
			             kind, index,
			             inst == nullptr ? "immediate-or-empty"
			                             : ValueOpcodeName(inst->GetOpcode()).data());
			EmitResidualPhiDiagnostic(value, kind, index, dword);
		}
	};
	for (uint32_t source_index = 0; source_index < plan.descriptor_sources.size(); ++source_index) {
		const auto& source = plan.descriptor_sources[source_index];
		for (uint32_t dword = 0; dword < source.dword_count; ++dword)
			ValidateMaterializedRoot(source.dwords[dword], "descriptor-dword", source_index, dword);
		if (source.indirect_image.has_value()) {
			ValidateMaterializedRoot(source.indirect_image->direct_offset, "image-offset",
			                         source_index);
			for (const auto& range: source.indirect_image->index_ranges) {
				ValidateMaterializedRoot(range.value, "image-index", source_index);
				ValidateMaterializedRoot(range.begin, "image-begin", source_index);
				ValidateMaterializedRoot(range.end, "image-end", source_index);
				for (const auto& [bound, limit]: range.bound_limits)
					ValidateMaterializedRoot(bound, "image-bound", source_index);
			}
		}
		if (source.indirect_buffer.has_value()) {
			ValidateMaterializedRoot(source.indirect_buffer->byte_offset, "buffer-offset",
			                         source_index);
			for (const auto& range: source.indirect_buffer->index_ranges) {
				ValidateMaterializedRoot(range.value, "buffer-index", source_index);
				ValidateMaterializedRoot(range.begin, "buffer-begin", source_index);
				ValidateMaterializedRoot(range.end, "buffer-end", source_index);
				for (const auto& [bound, limit]: range.bound_limits)
					ValidateMaterializedRoot(bound, "buffer-bound", source_index);
			}
		}
	}
	plan.control_flow = ResourceControlFlow(program);
	for (auto& block: plan.control_flow) {
		block.condition = Clone(block.condition);
	}
	plan.uniform_fill = AnalyzeUniformFill(program);
	for (uint32_t i = 0; i < plan.uniform_fill.fill.words; ++i) {
		plan.uniform_fill.values[i] = Clone(plan.uniform_fill.values[i]);
		ValidateMaterializedRoot(plan.uniform_fill.values[i], "uniform-fill", i);
	}
	for (uint32_t index = 0; index < plan.control_flow.size(); ++index)
		ValidateMaterializedRoot(plan.control_flow[index].condition, "control-flow", index);
	if (!runtime_graph_valid) {
		// A graph with an unresolved non-invariant PHI is not a usable resource
		// plan.  Keep the graph intact for diagnostics, but make materialization
		// reject it before the evaluator can treat the PHI as a runtime value.
		plan.resource_tracking_complete = false;
	}
	plan.materialization_sources.reserve(plan.info.buffers.size() + plan.info.images.size() +
	                                     plan.info.samplers.size());
	for (const auto& buffer: plan.info.buffers) {
		const auto* source = Source(plan, buffer.source);
		if (source != nullptr && source->bounded_buffer.has_value()) {
			plan.requires_specialization_memory = true;
		} else {
			plan.materialization_sources.push_back(buffer.source);
		}
	}
	for (const auto& image: plan.info.images) {
		const auto* source = Source(plan, image.source);
		if (source != nullptr && source->indirect_image.has_value()) {
			plan.requires_specialization_memory = true;
		} else {
			plan.materialization_sources.push_back(image.source);
		}
	}
	for (const auto& sampler: plan.info.samplers) {
		plan.materialization_sources.push_back(sampler.source);
	}
	plan.clean_flat_slots.resize(plan.srt_reads.size());
	for (const auto& buffer: plan.info.buffers) {
		const auto* source = Source(plan, buffer.source);
		if (source != nullptr && source->indirect_buffer.has_value()) {
			MarkCleanFlatSlots(plan, source, plan.clean_flat_slots);
		}
	}
	for (const auto& image: plan.info.images) {
		const auto* source = Source(plan, image.source);
		if (source == nullptr || !source->indirect_image.has_value()) {
			continue;
		}
		MarkCleanFlatSlots(plan, Source(plan, source->indirect_image->material_source),
		                   plan.clean_flat_slots);
		MarkCleanFlatSlots(plan, Source(plan, source->indirect_image->heap_source),
		                   plan.clean_flat_slots);
	}
	return plan;
}

bool MaterializeResources(const ResourcePlan& program, const SrtRuntime& runtime,
                          ResourceSnapshot& snapshot, ResourceSpecialization& specialization) {
	MaterializedSnapshot materialized;
	if (!MaterializeSnapshot(program, runtime, materialized)) {
		return false;
	}
	return BuildResourceSpecialization(program, std::move(materialized), snapshot, specialization);
}

void ApplyResourceSpecialization(Program& program, const ResourceSpecialization& specialization) {
	EXIT_IF(!program.resource_tracking_complete || program.shader_info_complete ||
	        program.binding_layout_complete);
	EXIT_IF(program.info.buffers.size() > specialization.buffers.size() ||
	        program.info.images.size() > specialization.images.size());
	program.info.bounded_srt_reads = specialization.bounded_srt_reads;

	auto buffers = program.info.buffers;
	for (size_t index = 0; index < specialization.buffers.size(); index++) {
		const auto& source = specialization.buffers[index];
		if (index >= buffers.size()) {
			EXIT_IF(source.indirect_root >= program.info.buffers.size());
			buffers.push_back(program.info.buffers[source.indirect_root]);
		}
		buffers[index].packed_stride           = specialization.buffers[index].packed_stride;
		buffers[index].descriptor_format       = specialization.buffers[index].descriptor_format;
		buffers[index].descriptor_swizzle      = specialization.buffers[index].descriptor_swizzle;
		buffers[index].indirect_root           = source.indirect_root;
		buffers[index].indirect_mapping_offset = source.indirect_mapping_offset;
		buffers[index].indirect_search_iterations = source.indirect_search_iterations;
		buffers[index].indirect_resources.clear();
	}
	for (uint32_t index = 0; index < buffers.size(); ++index) {
		const auto root = buffers[index].indirect_root;
		if (root != BufferResource::NoIndirectBuffer) {
			EXIT_IF(root >= buffers.size());
			buffers[root].indirect_resources.push_back(index);
		}
	}
	auto images = program.info.images;
	images.reserve(specialization.images.size());
	for (uint32_t index = 0; index < specialization.images.size(); index++) {
		const auto& source = specialization.images[index];
		if (index >= images.size()) {
			EXIT_IF(source.indirect_root >= program.info.images.size());
			images.push_back(program.info.images[source.indirect_root]);
		}
		auto& image                      = images[index];
		image.numeric_class              = source.numeric_class;
		image.dimension                  = source.dimension;
		image.mip_count                  = source.mip_count;
		image.conversion_format          = source.conversion_format;
		image.shader_swizzle             = source.shader_swizzle;
		image.indirect_root              = source.indirect_root;
		image.indirect_mapping_offset    = source.indirect_mapping_offset;
		image.indirect_search_iterations = source.indirect_search_iterations;
		image.cube                       = source.cube;
		image.indirect_resources.clear();
	}
	for (uint32_t index = 0; index < images.size(); index++) {
		const auto root = images[index].indirect_root;
		if (root != ImageResource::NoIndirectImage) {
			EXIT_IF(root >= images.size());
			images[root].indirect_resources.push_back(index);
		}
	}

	SamplerPlan sampler_plan;
	EXIT_IF(!BuildSamplerPlan(program.info, images, sampler_plan));
	auto samplers      = program.info.samplers;
	auto sampled_pairs = program.info.sampled_pairs;
	samplers.reserve(sampler_plan.sampler_count);
	for (uint32_t index = 0; index < program.info.samplers.size(); index++) {
		const auto target = sampler_plan.point_sampler[index];
		if (target == UINT32_MAX) {
			continue;
		}
		if (target == index) {
			samplers[index].force_point_filtering = true;
		} else {
			EXIT_IF(target != samplers.size());
			auto sampler                  = samplers[index];
			sampler.force_point_filtering = true;
			samplers.push_back(std::move(sampler));
		}
	}
	for (auto& pair: sampled_pairs) {
		if (RequiresPointSampler(images[pair.image])) {
			EXIT_IF(sampler_plan.point_sampler[pair.sampler] == UINT32_MAX);
			pair.sampler = sampler_plan.point_sampler[pair.sampler];
		}
		samplers[pair.sampler].depth_compare |= images[pair.image].depth_compare;
	}

	auto             memory_info = program.memory_info;
	const ImageRemap image_remap(specialization);
	for (auto* block: program.blocks) {
		for (auto it = block->begin(); it != block->end(); ++it) {
			auto&      inst         = *it;
			const auto image_opcode = ImageOpcodeInfoOf(inst.GetOpcode());
			if (image_opcode.access == ImageAccess::None) {
				continue;
			}
			const auto index = inst.Flags<MemoryFlags>().index;
			EXIT_IF(index >= memory_info.size());
			auto& memory = memory_info[index];
			EXIT_IF(memory.resource >= images.size());
			const auto& image = images[memory.resource];
			if (specialization.images[memory.resource].fmask) {
				EXIT_IF(inst.GetOpcode() != ValueOpcode::ImageRead || memory.data_bits != 32u);
				// Vulkan MSAA stores each sample directly; FMASK's four-bit fragment indices
				// therefore map each coverage sample to the same host sample.
				constexpr uint32_t   indices[] = {0x76543210u, 0xfedcba98u};
				std::array<Value, 2> fragments;
				for (uint32_t component = 0; component < fragments.size(); component++) {
					const auto selected =
					    block->PrependNewInst(it, ValueOpcode::SelectU32,
					                          {inst.Arg(2), Value(indices[component]), Value(0u)});
					fragments[component] = Value(&*selected);
				}
				const auto result =
				    block->PrependNewInst(it, ValueOpcode::CompositeConstructU32x4,
				                          {fragments[0], fragments[1], Value(0u), Value(0u)});
				inst.ReplaceUsesWith(Value(&*result));
				continue;
			}
			if (image_opcode.needs_sampler && RequiresPointSampler(image) &&
			    memory.sampler < program.info.samplers.size()) {
				EXIT_IF(sampler_plan.point_sampler[memory.sampler] == UINT32_MAX);
				memory.sampler = sampler_plan.point_sampler[memory.sampler];
			}
			EXIT_IF(image.indirect_root == memory.resource &&
			        inst.GetOpcode() != ValueOpcode::ImageSampleRaw &&
			        inst.GetOpcode() != ValueOpcode::ImageRead);
		}
	}
	for (auto* block: program.blocks) {
		for (auto& inst: *block) {
			if (inst.GetOpcode() == ValueOpcode::GetImageResource) {
				inst.SetFlags(image_remap.indices.at(inst.Flags<uint32_t>()));
			}
		}
	}
	for (auto& memory: memory_info) {
		if (memory.kind == ResourceKind::Image && !memory.planning_only) {
			memory.resource = image_remap.indices.at(memory.resource);
		}
	}
	for (auto& buffer: buffers) {
		if (buffer.image_alias != BufferResource::NoImageAlias) {
			buffer.image_alias = image_remap.indices.at(buffer.image_alias);
		}
	}
	for (auto& pair: sampled_pairs) {
		pair.image = image_remap.indices.at(pair.image);
	}
	for (auto& image: images) {
		if (image.indirect_root != ImageResource::NoIndirectImage) {
			image.indirect_root = image_remap.indices.at(image.indirect_root);
		}
		for (auto& resource: image.indirect_resources) {
			resource = image_remap.indices.at(resource);
		}
	}
	image_remap.Apply(images);
	program.info.buffers       = std::move(buffers);
	program.info.images        = std::move(images);
	program.info.samplers      = std::move(samplers);
	program.info.sampled_pairs = std::move(sampled_pairs);
	program.memory_info        = std::move(memory_info);
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
