#include "graphics/shader/recompiler/ir/passes/ResourceTracking.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <algorithm>
#include <fmt/format.h>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {

constexpr uint32_t SamplerBorderClampMask    = (1u << 2u) | (1u << 5u) | (1u << 8u);
constexpr uint32_t SamplerDword3ReservedMask = 0x3ffff000u;

uint32_t PossibleU32Bits(Value value) {
	value = value.Resolve();
	if (value.IsImmediate()) {
		return value.GetType() == Type::U32 ? value.U32() : UINT32_MAX;
	}
	const auto* inst = value.TryInstruction();
	if (inst == nullptr) {
		return UINT32_MAX;
	}
	switch (inst->GetOpcode()) {
		case ValueOpcode::BitwiseAnd32:
			return PossibleU32Bits(inst->Arg(0)) & PossibleU32Bits(inst->Arg(1));
		case ValueOpcode::BitwiseOr32:
			return PossibleU32Bits(inst->Arg(0)) | PossibleU32Bits(inst->Arg(1));
		case ValueOpcode::ShiftLeftLogical32: {
			const auto shift = inst->Arg(1).Resolve();
			return shift.IsImmediate() && shift.GetType() == Type::U32
			           ? PossibleU32Bits(inst->Arg(0)) << (shift.U32() & 31u)
			           : UINT32_MAX;
		}
		default: return UINT32_MAX;
	}
}

Value CanonicalizeSampleAdjustDword3(Value value) {
	for (;;) {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || inst->GetOpcode() != ValueOpcode::BitwiseOr32) {
			return value;
		}
		const auto left           = inst->Arg(0).Resolve();
		const auto right          = inst->Arg(1).Resolve();
		const bool left_reserved  = (PossibleU32Bits(left) & ~SamplerDword3ReservedMask) == 0;
		const bool right_reserved = (PossibleU32Bits(right) & ~SamplerDword3ReservedMask) == 0;
		if (left_reserved && right_reserved) {
			return Value(0u);
		}
		if (left_reserved) {
			value = right;
		} else if (right_reserved) {
			value = left;
		} else {
			return value;
		}
	}
}

const char* StageName(ShaderType stage) {
	switch (stage) {
		case ShaderType::Vertex: return "vertex";
		case ShaderType::Pixel: return "pixel";
		case ShaderType::Fetch: return "fetch";
		case ShaderType::Compute: return "compute";
		default: return "unknown";
	}
}

uint32_t ByteExtent(const MemoryInfo& memory) {
	const auto bytes = std::max((memory.data_bits + 7u) / 8u, 1u);
	const auto count = std::max(memory.data_dwords, 1u);
	const auto end   = static_cast<uint64_t>(memory.offset) + static_cast<uint64_t>(bytes) * count;
	return end > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(end);
}

class Tracker {
public:
	explicit Tracker(Program& program, std::array<uint32_t, 3> local_size, uint32_t shared_bytes)
	    : m_program(program), m_info(program.info), m_local_size(local_size),
	      m_shared_bytes(shared_bytes) {
		m_info.buffers.clear();
		m_info.images.clear();
		m_info.samplers.clear();
		m_info.sampled_pairs.clear();
		m_info.uses_dma = false;
	}

	void Run() {
		const bool trace_problem_shader = m_program.shader_hash == 0x78af8e269b528b5cULL;
		if (m_program.resource_tracking_complete) {
			Fail(0, "resources already tracked");
		}
		if (!m_program.srt_plan_complete) {
			Fail(0, "SRT plan is not ready");
		}
		// BuildSrtPlan runs before this pass and records shader-side eligibility
		// against the pre-rewrite graph. Preserve the original slot boundary so
		// SRT entries introduced later by bounded planning are not promoted here.
		const auto shader_side_slot_count = static_cast<uint32_t>(m_program.srt_reads.size());
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ForwardPrivateSharedReads begin\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		ForwardPrivateSharedReads();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ForwardPrivateSharedReads end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		FindSharedIndexRanges();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx FindSharedIndexRanges end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		PlanBoundedReads();
		if (trace_problem_shader) {
			LOGF("ResourceTracking trace hash=0x%016llx bounded plans=%u columns=%u\n",
			     static_cast<unsigned long long>(m_program.shader_hash),
			     static_cast<unsigned>(m_bounded_reads.size()),
			     static_cast<unsigned>(m_bounded_srt_reads.size()));
			for (const auto& plan: m_bounded_reads) {
				const auto* index = plan.proof.index.Resolve().TryInstruction();
				LOGF("ResourceTracking trace bounded plan op=%s pc=0x%08x index=%s read_id=%u "
				     "scale=%u bias=%u\n",
				     ValueOpcodeName(plan.read->GetOpcode()), plan.read->Flags<MemoryFlags>().pc,
				     index == nullptr ? "immediate" : ValueOpcodeName(index->GetOpcode()),
				     plan.read_id, plan.proof.offset_scale, plan.proof.offset_bias);
			}
		}
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx PlanBoundedReads end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		PlanIndirectImages();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx PlanIndirectImages end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx Collect begin\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		for (auto* block: m_program.blocks) {
			for (auto& inst: *block) {
				Collect(inst);
			}
		}
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx Collect end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		LinkImageAliases();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx LinkImageAliases end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		for (const auto& [handle, source]: m_indirect_buffers) {
			const auto& descriptor = m_sources[source];
			handle->SetArg(0, descriptor.indirect_buffer->byte_offset);
			handle->SetArg(1, descriptor.dwords[0]);
			handle->SetArg(2, descriptor.dwords[1]);
			handle->SetArg(3, Value(0u));
		}
		for (const auto& patch: m_handle_patches) {
			patch.handle->SetFlags<uint32_t>(patch.resource);
		}
		for (const auto& patch: m_memory_patches) {
			auto& memory    = m_program.memory_info[patch.index];
			memory.resource = patch.resource;
			if (patch.has_sampler) {
				memory.sampler = patch.sampler;
			}
		}
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ApplyBoundedRootReads begin\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		ApplyBoundedRootReads();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ApplyBoundedRootReads end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ApplyBoundedReads begin\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		ApplyBoundedReads();
		if (trace_problem_shader)
			LOGF("ResourceTracking trace hash=0x%016llx ApplyBoundedReads end\n",
			     static_cast<unsigned long long>(m_program.shader_hash));
		for (const auto& plan: m_indirect_images) {
			plan.handle->SetArg(0, plan.key);
			for (uint32_t dword = 0; dword < 4u; dword++) {
				plan.handle->SetArg(dword + 1u, plan.roots[dword + 4u]);
			}
			for (uint32_t dword = 5u; dword < plan.roots.size(); dword++) {
				plan.handle->SetArg(dword, plan.key);
			}
			for (const auto index: plan.memory) {
				m_program.memory_info[index].planning_only = true;
			}
		}
		std::erase_if(m_program.dynamic_reads, [&](Value value) {
			const auto* inst = value.Resolve().TryInstruction();
			return std::any_of(m_indirect_images.begin(), m_indirect_images.end(),
			                   [&](const IndirectImagePlan& plan) {
				                   return std::ranges::find(plan.reads, inst) != plan.reads.end();
			                   });
		});
		RetainBoundedDescriptorSources();
		RefreshShaderSideSrtEligibility(m_program, shader_side_slot_count, m_bda_srt_clones);
		m_program.descriptor_sources         = std::move(m_sources);
		m_program.info                       = std::move(m_info);
		m_program.resource_tracking_complete = true;
	}

private:
	struct HandlePatch {
		Inst*    handle   = nullptr;
		uint32_t resource = 0;
	};

	struct MemoryPatch {
		uint32_t index       = 0;
		uint32_t resource    = 0;
		uint32_t sampler     = 0;
		bool     has_sampler = false;
	};

	struct BoundedReadPlan {
		Inst*               read = nullptr;
		BoundedSrtReadProof proof;
		uint32_t            read_id = 0;
	};

	struct BoundedBufferPlan {
		Inst* handle = nullptr;
		Value index;
	};

	struct IndirectImagePlan {
		Inst*                      handle = nullptr;
		uint32_t                   source = 0;
		Value                      key;
		std::array<Value, 8>       roots {};
		std::array<uint32_t, 8>    memory {};
		std::array<const Inst*, 8> reads {};
	};

	const BlockInfo* BlockMetadata(const Block* block) const {
		const auto found = std::ranges::find(m_program.blocks, block);
		if (found == m_program.blocks.end()) return nullptr;
		const auto index = static_cast<size_t>(found - m_program.blocks.begin());
		return index < m_program.block_info.size() ? &m_program.block_info[index] : nullptr;
	}

	// Lower the small conditional diamonds used to choose a runtime descriptor.
	// This keeps the selector explicit at the resource use and lets bounded-SRT
	// recognition handle both incoming SRT reads without changing arbitrary PHIs.
	Value LowerRuntimeDescriptorPhi(Value value, Inst& anchor) {
		value     = value.Resolve();
		auto* phi = value.TryInstruction();
		if (phi == nullptr || phi->GetOpcode() != ValueOpcode::Phi || phi->NumArgs() != 2u ||
		    phi->NumPhiBlocks() != 2u || phi->Parent() == nullptr || anchor.Parent() == nullptr ||
		    phi->Parent()->ImmPredecessors().size() != 2u)
			return value;
		auto* yes_block = phi->PhiBlock(0u);
		auto* no_block  = phi->PhiBlock(1u);
		if (yes_block == nullptr || no_block == nullptr || yes_block == no_block ||
		    std::ranges::find(phi->Parent()->ImmPredecessors(), yes_block) ==
		        phi->Parent()->ImmPredecessors().end() ||
		    std::ranges::find(phi->Parent()->ImmPredecessors(), no_block) ==
		        phi->Parent()->ImmPredecessors().end() ||
		    yes_block->ImmPredecessors().size() != 1u || no_block->ImmPredecessors().size() != 1u ||
		    yes_block->ImmPredecessors().front() != no_block->ImmPredecessors().front())
			return value;
		auto*       split      = yes_block->ImmPredecessors().front();
		const auto* split_info = BlockMetadata(split);
		const auto* yes_info   = BlockMetadata(yes_block);
		const auto* no_info    = BlockMetadata(no_block);
		const auto* merge_info = BlockMetadata(phi->Parent());
		if (split_info == nullptr || yes_info == nullptr || no_info == nullptr ||
		    merge_info == nullptr ||
		    split_info->terminator.kind != CFG::TerminatorKind::ConditionalBranch ||
		    yes_info->terminator.kind != CFG::TerminatorKind::Branch ||
		    no_info->terminator.kind != CFG::TerminatorKind::Branch ||
		    yes_info->terminator.true_block != merge_info->id ||
		    no_info->terminator.true_block != merge_info->id)
			return value;
		Value yes;
		Value no;
		if (split_info->terminator.true_block == yes_info->id &&
		    split_info->terminator.false_block == no_info->id) {
			yes = phi->Arg(0u).Resolve();
			no  = phi->Arg(1u).Resolve();
		} else if (split_info->terminator.true_block == no_info->id &&
		           split_info->terminator.false_block == yes_info->id) {
			yes = phi->Arg(1u).Resolve();
			no  = phi->Arg(0u).Resolve();
		} else {
			return value;
		}
		const auto condition = split_info->condition.Resolve();
		if (condition.GetType() != Type::U1 || yes.GetType() != Type::U32 ||
		    no.GetType() != Type::U32 || !ValidateRuntimeValue(m_program, condition) ||
		    !ValidateRuntimeValue(m_program, yes) || !ValidateRuntimeValue(m_program, no))
			return value;
		const auto clonable_arm = [&](Value arm) {
			arm = arm.Resolve();
			if (arm.IsImmediate()) return true;
			const auto* read = arm.TryInstruction();
			const auto  slot = read != nullptr && read->GetOpcode() == ValueOpcode::ReadConst &&
			                           read->NumArgs() == 2u
			                       ? read->Arg(1).Resolve()
			                       : Value {};
			return slot.IsImmediate() && slot.GetType() == Type::U32 &&
			       slot.U32() < m_program.srt_reads.size();
		};
		if (!clonable_arm(yes) || !clonable_arm(no)) return value;
		auto* block = anchor.Parent();
		auto  where = std::ranges::find_if(block->Instructions(),
		                                   [&](const Inst& inst) { return &inst == &anchor; });
		if (where == block->Instructions().end()) return value;
		const auto clone_arm = [&](Value arm) {
			arm = arm.Resolve();
			if (arm.IsImmediate()) return arm;
			const auto* read = arm.TryInstruction();
			if (read == nullptr) return Value {};
			const auto slot = read->Arg(1).Resolve();
			const auto resource =
			    Value(&*block->PrependNewInst(where, ValueOpcode::GetSrtResource, {}));
			return Value(&*block->PrependNewInst(where, ValueOpcode::ReadConst, {resource, slot}));
		};
		yes = clone_arm(yes);
		no  = clone_arm(no);
		if (yes.IsEmpty() || no.IsEmpty()) return value;
		return Value(&*block->PrependNewInst(where, ValueOpcode::SelectU32, {condition, yes, no}));
	}

	[[noreturn]] void Fail(uint32_t pc, const std::string& reason) const {
		const auto message =
		    fmt::format("shader resource tracking: hash=0x{:016x} stage={} pc=0x{:08x} {}",
		                m_program.shader_hash, StageName(m_program.stage), pc, reason);
		EXIT("%s", message.c_str());
		std::abort();
	}

	void MakeSource(const Inst& handle, uint32_t width, bool sampler, bool sample_adjust,
	                DescriptorSource& descriptor, uint32_t pc) const {
		if (handle.NumArgs() != width) {
			Fail(pc, fmt::format("{} has {} descriptor dwords, expected {}",
			                     ValueOpcodeName(handle.GetOpcode()), handle.NumArgs(), width));
		}
		descriptor.dword_count = width;
		for (uint32_t i = 0; i < width; i++) {
			descriptor.dwords[i] = ResolveResourcePhi(m_program, handle.Arg(i), pc);
		}
		if (sample_adjust) {
			descriptor.dwords[3] = CanonicalizeSampleAdjustDword3(descriptor.dwords[3]);
		}
		const auto dword0 = descriptor.dwords[0].Resolve();
		if (sampler && dword0.IsImmediate() && dword0.GetType() == Type::U32 &&
		    (dword0.U32() & SamplerBorderClampMask) == 0) {
			// Border color and its table index are unused unless a clamp axis selects border mode.
			descriptor.dwords[3] = Value(0u);
		}
	}

	bool ValidateSource(const DescriptorSource& descriptor, uint32_t& bad_dword) const {
		for (uint32_t i = 0; i < descriptor.dword_count; i++) {
			bad_dword = i;
			if (descriptor.dwords[i].Resolve().GetType() != Type::U32) {
				return false;
			}
			if (!ValidateRuntimeValue(m_program, descriptor.dwords[i])) {
				return false;
			}
		}
		return true;
	}

	void PlanBoundedReads() {
		if (m_program.stage != ShaderType::Compute) return;
		for (auto* block: m_program.blocks) {
			for (auto& inst: *block) {
				if (!inst.HasUses()) continue;
				const auto proof = ProveBoundedSrtRead(m_program, inst);
				if (!proof) continue;
				if (!proof->count.IsEmpty()) PlanBoundedRootReads(proof->count);
				PlanBoundedRootReads(proof->address_low);
				PlanBoundedRootReads(proof->address_high);
				if (proof->source_dwords == 4u) {
					PlanBoundedRootReads(proof->descriptor_word2);
					PlanBoundedRootReads(proof->descriptor_word3);
				}
				DescriptorSource address;
				address.dword_count = proof->source_dwords;
				address.dwords[0]   = proof->address_low;
				address.dwords[1]   = proof->address_high;
				if (proof->source_dwords == 4u) {
					address.dwords[2] = proof->descriptor_word2;
					address.dwords[3] = proof->descriptor_word3;
				}
				DescriptorSource count;
				count.dword_count = 1u;
				count.dwords[0]   = proof->count.IsEmpty() ? Value(0u) : proof->count;
				const BoundedSrtRead read {
				    InternSource(address),
				    proof->count.IsEmpty() ? UINT32_MAX : InternSource(count),
				    proof->offset_scale,
				    proof->offset_bias,
				    proof->memory_offset,
				    proof->source_dwords,
				    proof->workgroup_axis,
				    proof->count_signed};
				auto       found   = std::ranges::find(m_bounded_srt_reads, read);
				const auto read_id = static_cast<uint32_t>(found - m_bounded_srt_reads.begin());
				if (found == m_bounded_srt_reads.end()) m_bounded_srt_reads.push_back(read);
				m_bounded_reads.push_back({&inst, *proof, read_id});
			}
		}
	}

	const BoundedReadPlan* BoundedRead(const Inst* read) const {
		const auto found = std::ranges::find_if(
		    m_bounded_reads, [&](const BoundedReadPlan& plan) { return plan.read == read; });
		return found == m_bounded_reads.end() ? nullptr : &*found;
	}

	const BoundedReadPlan* BoundedReadValue(Value value) const {
		value                             = value.Resolve();
		const Inst*                  read = value.TryInstruction();
		std::unordered_set<uint32_t> visited_slots;
		while (read != nullptr && read->GetOpcode() == ValueOpcode::ReadConst &&
		       read->NumArgs() == 2u) {
			const auto slot = read->Arg(1).Resolve();
			if (!slot.IsImmediate() || slot.GetType() != Type::U32 ||
			    slot.U32() >= m_program.srt_reads.size() ||
			    !visited_slots.insert(slot.U32()).second) {
				return nullptr;
			}
			read = m_program.srt_reads[slot.U32()].value.Resolve().TryInstruction();
		}
		return BoundedRead(read);
	}

	bool CollectBoundedDependencies(Value value, std::vector<const BoundedReadPlan*>& dependencies,
	                                std::unordered_set<const Inst*>& visiting,
	                                std::unordered_set<const Inst*>& completed) const {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr) return true;
		if (const auto* bounded = BoundedRead(inst); bounded != nullptr) {
			if (std::ranges::find(dependencies, bounded) == dependencies.end())
				dependencies.push_back(bounded);
			return true;
		}
		if (completed.contains(inst)) return true;
		if (!visiting.insert(inst).second) return false;
		const auto finish = [&](bool result) {
			visiting.erase(inst);
			if (result) completed.insert(inst);
			return result;
		};
		if (inst->GetOpcode() == ValueOpcode::ReadConst && inst->NumArgs() == 2u) {
			const auto slot = inst->Arg(1).Resolve();
			if (!slot.IsImmediate() || slot.GetType() != Type::U32 ||
			    slot.U32() >= m_program.srt_reads.size())
				return finish(false);
			return finish(CollectBoundedDependencies(m_program.srt_reads[slot.U32()].value,
			                                         dependencies, visiting, completed));
		}
		for (uint32_t arg = 0; arg < inst->NumArgs(); ++arg) {
			if (!CollectBoundedDependencies(inst->Arg(arg), dependencies, visiting, completed))
				return finish(false);
		}
		return finish(true);
	}

	uint32_t InternBoundedSelector(Value selector) {
		selector = selector.Resolve();
		for (uint32_t group = 0; group < m_bounded_selectors.size(); ++group) {
			if (EquivalentValue(m_program, selector, m_bounded_selectors[group])) return group;
		}
		m_bounded_selectors.push_back(selector);
		return static_cast<uint32_t>(m_bounded_selectors.size() - 1u);
	}

	bool MakeBoundedBufferExpression(Inst& handle, DescriptorSource descriptor, uint32_t& source) {
		std::vector<const BoundedReadPlan*> dependencies;
		std::unordered_set<const Inst*>     visiting;
		std::unordered_set<const Inst*>     completed;
		for (uint32_t word = 0; word < descriptor.dword_count; ++word) {
			if (!CollectBoundedDependencies(descriptor.dwords[word], dependencies, visiting,
			                                completed))
				return false;
		}
		if (m_program.shader_hash == 0x78af8e269b528b5cULL)
			LOGF("ResourceTracking trace hash=0x%016llx bounded dependencies=%u completed=%u\n",
			     static_cast<unsigned long long>(m_program.shader_hash),
			     static_cast<unsigned>(dependencies.size()),
			     static_cast<unsigned>(completed.size()));
		if (dependencies.empty()) return false;
		const auto& first = m_bounded_srt_reads[dependencies.front()->read_id];
		if (first.workgroup_axis != UINT32_MAX) return false;
		for (const auto* dependency: dependencies) {
			const auto& read = m_bounded_srt_reads[dependency->read_id];
			if (dependency->proof.index != dependencies.front()->proof.index ||
			    read.count_source != first.count_source ||
			    read.workgroup_axis != first.workgroup_axis ||
			    read.count_signed != first.count_signed)
				return false;
		}
		descriptor.bounded_buffer.emplace();
		descriptor.bounded_buffer->expression = true;
		descriptor.bounded_buffer->selector_group =
		    InternBoundedSelector(dependencies.front()->proof.index);
		for (const auto* dependency: dependencies)
			descriptor.bounded_buffer->dependencies.push_back(dependency->read_id);
		source = InternSource(descriptor);
		if (std::ranges::none_of(m_bounded_buffers, [&](const BoundedBufferPlan& plan) {
			    return plan.handle == &handle;
		    })) {
			m_bounded_buffers.push_back({&handle, dependencies.front()->proof.index});
		}
		return true;
	}

	bool MakeBoundedBufferSource(Inst& handle, uint32_t& source) {
		if (handle.NumArgs() != 4u) return false;
		std::array<const BoundedReadPlan*, 4> words;
		for (uint32_t word = 0; word < words.size(); ++word) {
			words[word] = BoundedReadValue(handle.Arg(word));
			if (words[word] == nullptr) return false;
		}
		const auto& first = m_bounded_srt_reads[words[0]->read_id];
		if (first.workgroup_axis != UINT32_MAX) return false;
		for (uint32_t word = 1; word < words.size(); ++word) {
			const auto& next = m_bounded_srt_reads[words[word]->read_id];
			if (words[word]->proof.index != words[0]->proof.index ||
			    next.address_source != first.address_source ||
			    next.count_source != first.count_source ||
			    next.offset_scale != first.offset_scale || next.offset_bias != first.offset_bias ||
			    next.memory_offset != first.memory_offset + word * sizeof(uint32_t))
				return false;
		}
		DescriptorSource descriptor;
		descriptor.dword_count = 4u;
		descriptor.dwords[0]   = words[0]->proof.address_low;
		descriptor.dwords[1]   = words[0]->proof.address_high;
		descriptor.dwords[2]   = words[0]->proof.descriptor_word2;
		descriptor.dwords[3]   = words[0]->proof.descriptor_word3;
		descriptor.bounded_buffer.emplace();
		descriptor.bounded_buffer->selector_group = InternBoundedSelector(words[0]->proof.index);
		for (uint32_t word = 0; word < words.size(); ++word)
			descriptor.bounded_buffer->reads[word] = words[word]->read_id;
		source = InternSource(descriptor);
		if (std::ranges::none_of(m_bounded_buffers, [&](const BoundedBufferPlan& plan) {
			    return plan.handle == &handle;
		    }))
			m_bounded_buffers.push_back({&handle, words[0]->proof.index});
		return true;
	}

	// A descriptor PHI selected by a wave-uniform branch is a finite table, not an
	// arbitrary GPU-produced descriptor. Keep the selector live and snapshot only
	// the finite candidate descriptors during specialization.
	static bool WaveUniformValue(Value value, std::unordered_set<const Inst*>& visiting,
	                             std::unordered_set<const Inst*>& complete) {
		value = value.Resolve();
		if (value.IsImmediate()) return true;
		const auto* inst = value.TryInstruction();
		if (inst == nullptr) return false;
		if (complete.contains(inst)) return true;
		switch (inst->GetOpcode()) {
			case ValueOpcode::GetUserData:
			case ValueOpcode::ReadConst:
			case ValueOpcode::ReadFirstLane:
			case ValueOpcode::ReadLane:
			case ValueOpcode::Ballot: complete.insert(inst); return true;
			case ValueOpcode::Identity:
			case ValueOpcode::CompositeExtractU32x4:
			case ValueOpcode::BitwiseAnd32:
			case ValueOpcode::BitwiseOr32:
			case ValueOpcode::BitwiseXor32:
			case ValueOpcode::BitwiseNot32:
			case ValueOpcode::IAdd32:
			case ValueOpcode::ISub32:
			case ValueOpcode::IMul32:
			case ValueOpcode::ShiftLeftLogical32:
			case ValueOpcode::ShiftRightLogical32:
			case ValueOpcode::ShiftRightArithmetic32:
			case ValueOpcode::IEqual32:
			case ValueOpcode::INotEqual32:
			case ValueOpcode::SLessThan32:
			case ValueOpcode::ULessThan32:
			case ValueOpcode::SLessThanEqual32:
			case ValueOpcode::ULessThanEqual32:
			case ValueOpcode::SGreaterThan32:
			case ValueOpcode::UGreaterThan32:
			case ValueOpcode::SGreaterThanEqual32:
			case ValueOpcode::UGreaterThanEqual32:
			case ValueOpcode::LogicalAnd:
			case ValueOpcode::LogicalOr:
			case ValueOpcode::LogicalXor:
			case ValueOpcode::LogicalNot:
			case ValueOpcode::SelectU1:
			case ValueOpcode::SelectU32: break;
			default: return false;
		}
		if (!visiting.insert(inst).second) return false;
		for (uint32_t argument = 0; argument < inst->NumArgs(); ++argument) {
			if (!WaveUniformValue(inst->Arg(argument), visiting, complete)) {
				visiting.erase(inst);
				return false;
			}
		}
		visiting.erase(inst);
		complete.insert(inst);
		return true;
	}

	static bool WaveUniformValue(Value value) {
		std::unordered_set<const Inst*> visiting;
		std::unordered_set<const Inst*> complete;
		return WaveUniformValue(value, visiting, complete);
	}

	bool
	DecodeWaveBufferCandidate(Value                                            value,
	                          DescriptorSource::BoundedBuffer::CandidateDword& candidate) const {
		value = value.Resolve();
		if (value.IsImmediate()) {
			if (value.GetType() != Type::U32) return false;
			candidate.value     = value.U32();
			candidate.immediate = true;
			return true;
		}
		const auto* read = value.TryInstruction();
		const auto  slot =
		    read != nullptr && read->GetOpcode() == ValueOpcode::ReadConst && read->NumArgs() == 2u
		        ? read->Arg(1).Resolve()
		        : Value {};
		if (!slot.IsImmediate() || slot.GetType() != Type::U32 ||
		    slot.U32() >= m_program.srt_reads.size() || !ValidateRuntimeValue(m_program, value))
			return false;
		candidate.value     = slot.U32();
		candidate.immediate = false;
		return true;
	}

	bool MakeWaveUniformBufferSource(Inst& handle, uint32_t& source) {
		if (handle.GetOpcode() != ValueOpcode::GetBufferResource || handle.NumArgs() != 4u ||
		    handle.Parent() == nullptr)
			return false;
		const bool                 trace = m_program.shader_hash == 0x78af8e269b528b5cULL;
		std::array<const Inst*, 4> phis {};
		for (uint32_t word = 0; word < phis.size(); ++word) {
			phis[word] = handle.Arg(word).Resolve().TryInstruction();
			if (trace) {
				const auto opcode = phis[word] == nullptr
				                        ? "immediate"
				                        : ValueOpcodeName(phis[word]->GetOpcode()).data();
				LOGF("ResourceTracking wave probe hash=0x%016llx dword=%u opcode=%s\n",
				     static_cast<unsigned long long>(m_program.shader_hash), word, opcode);
			}
			if (phis[word] == nullptr || phis[word]->GetOpcode() != ValueOpcode::Phi ||
			    phis[word]->NumArgs() != 2u || phis[word]->NumPhiBlocks() != 2u ||
			    phis[word]->Parent() != phis[0]->Parent())
				return false;
		}
		for (uint32_t word = 1; word < phis.size(); ++word) {
			for (uint32_t argument = 0; argument < 2u; ++argument) {
				if (phis[word]->PhiBlock(argument) != phis[0]->PhiBlock(argument)) return false;
			}
		}
		auto* first_arm  = phis[0]->PhiBlock(0u);
		auto* second_arm = phis[0]->PhiBlock(1u);
		auto* merge      = phis[0]->Parent();
		if (first_arm == nullptr || second_arm == nullptr || first_arm == second_arm ||
		    merge == nullptr || merge->ImmPredecessors().size() != 2u ||
		    std::ranges::find(merge->ImmPredecessors(), first_arm) ==
		        merge->ImmPredecessors().end() ||
		    std::ranges::find(merge->ImmPredecessors(), second_arm) ==
		        merge->ImmPredecessors().end() ||
		    first_arm->ImmPredecessors().size() != 1u ||
		    second_arm->ImmPredecessors().size() != 1u ||
		    first_arm->ImmPredecessors().front() != second_arm->ImmPredecessors().front())
			return false;
		auto*       split       = first_arm->ImmPredecessors().front();
		const auto* split_info  = BlockMetadata(split);
		const auto* first_info  = BlockMetadata(first_arm);
		const auto* second_info = BlockMetadata(second_arm);
		const auto* merge_info  = BlockMetadata(merge);
		if (trace) {
			LOGF("ResourceTracking wave shape hash=0x%016llx merge=%u first_arm=%u second_arm=%u "
			     "split=%u condition=%s\n",
			     static_cast<unsigned long long>(m_program.shader_hash),
			     merge_info == nullptr ? UINT32_MAX : merge_info->id,
			     first_info == nullptr ? UINT32_MAX : first_info->id,
			     second_info == nullptr ? UINT32_MAX : second_info->id,
			     split_info == nullptr ? UINT32_MAX : split_info->id,
			     split_info == nullptr
			         ? "unknown"
			         : ValueOpcodeName(
			               split_info->condition.Resolve().TryInstruction() == nullptr
			                   ? ValueOpcode::Identity
			                   : split_info->condition.Resolve().TryInstruction()->GetOpcode())
			               .data());
		}
		if (split_info == nullptr || first_info == nullptr || second_info == nullptr ||
		    merge_info == nullptr ||
		    split_info->terminator.kind != CFG::TerminatorKind::ConditionalBranch ||
		    first_info->terminator.kind != CFG::TerminatorKind::Branch ||
		    second_info->terminator.kind != CFG::TerminatorKind::Branch ||
		    first_info->terminator.true_block != merge_info->id ||
		    second_info->terminator.true_block != merge_info->id)
			return false;
		uint32_t true_candidate  = 0u;
		uint32_t false_candidate = 0u;
		if (split_info->terminator.true_block == first_info->id &&
		    split_info->terminator.false_block == second_info->id) {
			true_candidate  = 0u;
			false_candidate = 1u;
		} else if (split_info->terminator.true_block == second_info->id &&
		           split_info->terminator.false_block == first_info->id) {
			true_candidate  = 1u;
			false_candidate = 0u;
		} else {
			return false;
		}
		const auto condition = split_info->condition.Resolve();
		if (condition.GetType() != Type::U1 || !WaveUniformValue(condition)) return false;

		std::array<std::array<DescriptorSource::BoundedBuffer::CandidateDword, 4>, 2> candidates {};
		for (uint32_t candidate = 0; candidate < candidates.size(); ++candidate) {
			for (uint32_t word = 0; word < phis.size(); ++word) {
				if (!DecodeWaveBufferCandidate(phis[word]->Arg(candidate),
				                               candidates[candidate][word]))
					return false;
			}
		}
		auto* block = handle.Parent();
		auto  where = std::ranges::find_if(block->Instructions(),
		                                   [&](const Inst& inst) { return &inst == &handle; });
		if (where == block->Instructions().end()) return false;
		const auto key = Value(
		    &*block->PrependNewInst(where, ValueOpcode::SelectU32,
		                            {condition, Value(true_candidate), Value(false_candidate)}));
		DescriptorSource descriptor;
		descriptor.dword_count = 4u;
		descriptor.dwords.fill(Value(0u));
		descriptor.dwords[0] = key;
		descriptor.bounded_buffer.emplace();
		descriptor.bounded_buffer->wave_uniform = true;
		descriptor.bounded_buffer->wave_candidates.assign(candidates.begin(), candidates.end());
		descriptor.bounded_buffer->key_arg = 0u;
		source                             = InternSource(descriptor);
		if (trace)
			LOGF("ResourceTracking wave candidate accepted hash=0x%016llx source=%u\n",
			     static_cast<unsigned long long>(m_program.shader_hash), source);
		if (std::ranges::none_of(m_bounded_buffers, [&](const BoundedBufferPlan& plan) {
			    return plan.handle == &handle;
		    }))
			m_bounded_buffers.push_back({&handle, key});
		return true;
	}

	void PlanBoundedRootReads(Value value) {
		value      = value.Resolve();
		auto* inst = value.TryInstruction();
		if (inst == nullptr ||
		    std::ranges::find(m_bounded_root_visited, inst) != m_bounded_root_visited.end())
			return;
		m_bounded_root_visited.push_back(inst);
		if (inst->GetOpcode() == ValueOpcode::ReadConst) {
			const auto slot = inst->Arg(1).Resolve();
			if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
			    slot.U32() < m_program.srt_reads.size())
				PlanBoundedRootReads(m_program.srt_reads[slot.U32()].value);
		}
		for (size_t arg = 0; arg < inst->NumArgs(); ++arg)
			PlanBoundedRootReads(inst->Arg(arg));
		const auto op = inst->GetOpcode();
		if (op != ValueOpcode::LoadAddressU32 && op != ValueOpcode::ReadConstBuffer) return;
		const auto flags = inst->Flags<MemoryFlags>();
		if (flags.index >= m_program.memory_info.size())
			Fail(flags.pc, "bounded root memory metadata out of range");
		const auto& memory = m_program.memory_info[flags.index];
		if (memory.planning_only) return;
		if ((op == ValueOpcode::LoadAddressU32 && memory.kind != ResourceKind::ScalarAddress) ||
		    (op == ValueOpcode::ReadConstBuffer && memory.kind != ResourceKind::ScalarBuffer) ||
		    inst->Parent() == nullptr || !ValidateRuntimeValue(m_program, value))
			Fail(flags.pc, "bounded root is not a valid runtime scalar read");
		m_bounded_root_reads.push_back(inst);
	}

	void ApplyBoundedRootReads() {
		for (auto* read: m_bounded_root_reads) {
			auto*      block = read->Parent();
			const auto where =
			    std::ranges::find_if(*block, [&](const Inst& inst) { return &inst == read; });
			const auto slot = static_cast<uint32_t>(m_program.srt_reads.size());
			const auto srt  = Value(&*block->PrependNewInst(where, ValueOpcode::GetSrtResource));
			const auto flat =
			    Value(&*block->PrependNewInst(where, ValueOpcode::ReadConst, {srt, Value(slot)}));
			const auto original = Value(read);
			for (const auto& use: read->Uses())
				use.user->SetArg(use.operand, flat);
			for (auto& info: m_program.block_info) {
				if (info.condition.Resolve() == original) info.condition = flat;
				if (info.indirect_target.Resolve() == original) info.indirect_target = flat;
			}
			for (auto& source: m_sources)
				for (uint32_t word = 0; word < source.dword_count; ++word)
					if (source.dwords[word].Resolve() == original) source.dwords[word] = flat;
			read->SetFlags<MemoryFlags>(
			    {read->Flags<MemoryFlags>().index, read->Flags<MemoryFlags>().pc});
			m_program.srt_reads.push_back({original, slot});
			block->AppendNewInst(ValueOpcode::ReferenceU32, {original});
		}
		std::erase_if(m_program.dynamic_reads, [&](Value value) {
			return std::ranges::find(m_bounded_root_reads, value.Resolve().TryInstruction()) !=
			       m_bounded_root_reads.end();
		});
	}

	void ApplyBoundedReads() {
		m_program.bounded_srt_reads = m_bounded_srt_reads;
		for (const auto& plan: m_bounded_reads) {
			auto*      block = plan.read->Parent();
			const auto where =
			    std::ranges::find_if(*block, [&](const Inst& inst) { return &inst == plan.read; });
			const auto replacement = Value(&*block->PrependNewInst(
			    where, ValueOpcode::ReadBoundedSrtU32, {plan.proof.index}, plan.read_id));
			plan.read->ReplaceUsesWith(replacement);
		}
		for (const auto& plan: m_bounded_buffers) {
			plan.handle->SetArg(0, plan.index);
			for (uint32_t word = 1; word < plan.handle->NumArgs(); ++word)
				plan.handle->SetArg(word, Value(0u));
		}
		std::erase_if(m_program.dynamic_reads, [](Value value) {
			const auto* inst = value.Resolve().TryInstruction();
			return inst != nullptr && inst->GetOpcode() == ValueOpcode::ReadBoundedSrtU32;
		});
	}

	void RetainBoundedDescriptorSources() {
		// Bounded reads are lowered to a runtime selector before the resource plan
		// is extracted.  Their descriptor expressions live only in m_sources, so
		// ordinary IR use tracking cannot keep those expressions alive through the
		// post-translation dead-code pass.  Retain the source roots explicitly;
		// ReferenceU32 is the existing side-effecting lifetime marker used for
		// planning-only values and is ignored as a semantic resource use.
		std::unordered_set<uint32_t> source_indices;
		for (const auto& read: m_bounded_srt_reads) {
			source_indices.insert(read.address_source);
			if (read.count_source != UINT32_MAX) source_indices.insert(read.count_source);
		}
		std::unordered_set<Inst*> retained;
		for (const auto source_index: source_indices) {
			if (source_index >= m_sources.size()) continue;
			const auto& source = m_sources[source_index];
			for (uint32_t dword = 0; dword < source.dword_count; ++dword) {
				const auto value = source.dwords[dword].Resolve();
				if (value.GetType() != Type::U32 || value.IsImmediate()) continue;
				auto* inst = value.TryInstruction();
				if (inst == nullptr || inst->Parent() == nullptr || !retained.insert(inst).second)
					continue;
				inst->Parent()->AppendNewInst(ValueOpcode::ReferenceU32, {value});
			}
		}
	}

	uint32_t InternSource(const DescriptorSource& descriptor) {
		for (uint32_t candidate = 0; candidate < m_sources.size(); candidate++) {
			const auto& current = m_sources[candidate];
			if (current.dword_count != descriptor.dword_count ||
			    current.indirect_image != descriptor.indirect_image ||
			    current.indirect_buffer != descriptor.indirect_buffer ||
			    current.bounded_buffer != descriptor.bounded_buffer) {
				continue;
			}
			bool same = true;
			for (uint32_t i = 0; i < descriptor.dword_count; i++) {
				same = same && EquivalentValue(m_program, current.dwords[i], descriptor.dwords[i]);
			}
			if (same) {
				return candidate;
			}
		}
		m_sources.push_back(descriptor);
		return static_cast<uint32_t>(m_sources.size() - 1);
	}

	static bool ImmediateU32(Value value, uint32_t& result) {
		value = value.Resolve();
		if (!value.IsImmediate() || value.GetType() != Type::U32) {
			return false;
		}
		result = value.U32();
		return true;
	}

	static bool UsesOnly(const Inst& value, std::span<const Inst* const> users) {
		return !value.Uses().empty() && std::ranges::all_of(value.Uses(), [&](const Use& use) {
			return std::ranges::find(users, use.user) != users.end();
		});
	}

	const MemoryInfo* ScalarReadMemory(const Inst& read, uint32_t& index) const {
		if (read.GetOpcode() != ValueOpcode::ReadConstBuffer || read.NumArgs() != 2u) {
			return nullptr;
		}
		index = read.Flags<MemoryFlags>().index;
		if (index >= m_program.memory_info.size()) {
			return nullptr;
		}
		const auto& memory = m_program.memory_info[index];
		return memory.kind == ResourceKind::ScalarBuffer && memory.data_bits == 32u &&
		               memory.data_dwords == 1u
		           ? &memory
		           : nullptr;
	}

	bool MemoryIndexBelongsTo(uint32_t index, const Inst& owner) const {
		for (const auto* block: m_program.blocks) {
			for (const auto& inst: *block) {
				const auto op = inst.GetOpcode();
				if ((BufferAccessOf(op) == BufferAccess::None &&
				     AddressOpcodeInfoOf(op).access == AddressAccess::None &&
				     ImageOpcodeInfoOf(op).access == ImageAccess::None) ||
				    &inst == &owner) {
					continue;
				}
				if (inst.Flags<MemoryFlags>().index == index) {
					return false;
				}
			}
		}
		return true;
	}

	bool MakeRuntimeBufferSource(const Inst& handle, uint32_t pc, uint32_t& source,
	                             DescriptorSource& descriptor) {
		if (handle.GetOpcode() != ValueOpcode::GetBufferResource) {
			return false;
		}
		MakeSource(handle, 4u, false, false, descriptor, pc);
		uint32_t bad_dword = 0;
		if (!ValidateSource(descriptor, bad_dword)) {
			return false;
		}
		source = InternSource(descriptor);
		return true;
	}

	bool MatchMaterialOffset(Value value, Value& selector, uint32_t& stride,
	                         uint32_t& offset) const {
		value           = value.Resolve();
		offset          = 0;
		auto* candidate = value.TryInstruction();
		if (candidate != nullptr && candidate->GetOpcode() == ValueOpcode::IAdd32 &&
		    candidate->NumArgs() == 2u) {
			uint32_t immediate = 0;
			if (ImmediateU32(candidate->Arg(0), immediate)) {
				value = candidate->Arg(1).Resolve();
			} else if (ImmediateU32(candidate->Arg(1), immediate)) {
				value = candidate->Arg(0).Resolve();
			} else {
				return false;
			}
			offset = immediate;
		}
		const auto* multiply = value.TryInstruction();
		if (multiply == nullptr || multiply->GetOpcode() != ValueOpcode::IMul32 ||
		    multiply->NumArgs() != 2u) {
			return false;
		}
		if (ImmediateU32(multiply->Arg(0), stride)) {
			selector = multiply->Arg(1).Resolve();
		} else if (ImmediateU32(multiply->Arg(1), stride)) {
			selector = multiply->Arg(0).Resolve();
		} else {
			return false;
		}
		const auto* selector_inst = selector.TryInstruction();
		return stride != 0u && selector_inst != nullptr &&
		       selector_inst->GetOpcode() == ValueOpcode::ReadFirstLane;
	}

	// SRT planning replaces scalar descriptor reads with ReadConst references before
	// resource tracking runs.  Recover the original raw read for descriptor-shape
	// recognition without changing the runtime value or its provenance.
	const Inst* ResolveDescriptorRead(Value value) const {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || inst->GetOpcode() != ValueOpcode::ReadConst ||
		    inst->NumArgs() != 2u) {
			return inst;
		}
		const auto slot = inst->Arg(1).Resolve();
		if (!slot.IsImmediate() || slot.GetType() != Type::U32 ||
		    slot.U32() >= m_program.srt_reads.size()) {
			return nullptr;
		}
		return m_program.srt_reads[slot.U32()].value.Resolve().TryInstruction();
	}

	bool DescriptorReadUsesOnlyImages(Value value, const Inst& read) const {
		value               = value.Resolve();
		const auto* wrapper = value.TryInstruction();
		if (wrapper == &read) {
			return !read.Uses().empty() && std::ranges::all_of(read.Uses(), [](const Use& use) {
				return use.user->GetOpcode() == ValueOpcode::GetImageResource;
			});
		}
		if (wrapper == nullptr || wrapper->GetOpcode() != ValueOpcode::ReadConst ||
		    wrapper->NumArgs() != 2u) {
			return false;
		}
		const auto slot = wrapper->Arg(1).Resolve();
		if (!slot.IsImmediate() || slot.GetType() != Type::U32 ||
		    slot.U32() >= m_program.srt_reads.size() ||
		    m_program.srt_reads[slot.U32()].value.Resolve().TryInstruction() != &read) {
			return false;
		}
		bool has_wrapper = false;
		for (const auto* block: m_program.blocks) {
			for (const auto& candidate: *block) {
				if (candidate.GetOpcode() != ValueOpcode::ReadConst || candidate.NumArgs() != 2u ||
				    candidate.Arg(1).Resolve() != slot) {
					continue;
				}
				has_wrapper |= &candidate == wrapper;
				if (candidate.Uses().empty() ||
				    !std::ranges::all_of(candidate.Uses(), [](const Use& wrapper_use) {
					    return wrapper_use.user->GetOpcode() == ValueOpcode::GetImageResource;
				    })) {
					return false;
				}
			}
		}
		return has_wrapper;
	}

	bool TryMakeIndirectImage(Inst& handle, uint32_t pc, IndirectImagePlan& plan) {
		if (handle.GetOpcode() != ValueOpcode::GetImageResource || handle.NumArgs() != 8u) {
			return false;
		}

		std::array<Inst*, 8> heap_reads {};
		Inst*                heap_handle = nullptr;
		Value                heap_offset;
		for (uint32_t dword = 0; dword < heap_reads.size(); dword++) {
			heap_reads[dword] = handle.Arg(dword).Resolve().TryInstruction();
			if (heap_reads[dword] == nullptr) {
				return false;
			}
			uint32_t    memory_index = 0;
			const auto* memory       = ScalarReadMemory(*heap_reads[dword], memory_index);
			if (memory == nullptr || memory->offset != dword * sizeof(uint32_t) ||
			    !MemoryIndexBelongsTo(memory_index, *heap_reads[dword])) {
				return false;
			}
			auto* current_handle = heap_reads[dword]->Arg(0).Resolve().TryInstruction();
			if (current_handle == nullptr ||
			    (heap_handle != nullptr && current_handle != heap_handle)) {
				return false;
			}
			heap_handle = current_handle;
			if (dword == 0u) {
				heap_offset = heap_reads[dword]->Arg(1).Resolve();
			} else if (!EquivalentValue(m_program, heap_offset, heap_reads[dword]->Arg(1))) {
				return false;
			}
			plan.memory[dword] = memory_index;
			plan.reads[dword]  = heap_reads[dword];
		}

		const auto* shift        = heap_offset.TryInstruction();
		uint32_t    shift_amount = 0;
		if (shift == nullptr || shift->GetOpcode() != ValueOpcode::ShiftLeftLogical32 ||
		    shift->NumArgs() != 2u || !ImmediateU32(shift->Arg(1), shift_amount) ||
		    shift_amount != 5u) {
			return false;
		}
		auto* material_read = shift->Arg(0).Resolve().TryInstruction();
		if (material_read == nullptr) {
			return false;
		}
		uint32_t    material_memory_index = 0;
		const auto* material_memory       = ScalarReadMemory(*material_read, material_memory_index);
		if (material_memory == nullptr || material_memory->offset != 0u ||
		    !MemoryIndexBelongsTo(material_memory_index, *material_read)) {
			return false;
		}
		auto* material_handle = material_read->Arg(0).Resolve().TryInstruction();
		if (material_handle == nullptr) {
			return false;
		}

		Value    selector;
		uint32_t selector_stride = 0;
		uint32_t selector_offset = 0;
		if (!MatchMaterialOffset(material_read->Arg(1), selector, selector_stride,
		                         selector_offset)) {
			return false;
		}

		const std::array<const Inst*, 1> material_users {shift};
		std::array<const Inst*, 8>       heap_users {};
		std::copy(heap_reads.begin(), heap_reads.end(), heap_users.begin());
		const std::array<const Inst*, 1> image_users {&handle};
		if (!UsesOnly(*material_read, material_users) || !UsesOnly(*shift, heap_users)) {
			return false;
		}
		for (const auto* read: heap_reads) {
			if (!UsesOnly(*read, image_users)) {
				return false;
			}
		}

		DescriptorSource material_source;
		DescriptorSource heap_source;
		uint32_t         material_source_index = 0;
		uint32_t         heap_source_index     = 0;
		if (!MakeRuntimeBufferSource(*material_handle, pc, material_source_index,
		                             material_source) ||
		    !MakeRuntimeBufferSource(*heap_handle, pc, heap_source_index, heap_source)) {
			return false;
		}

		DescriptorSource image_source;
		image_source.dword_count = 8u;
		std::copy(material_source.dwords.begin(), material_source.dwords.begin() + 4u,
		          image_source.dwords.begin());
		std::copy(heap_source.dwords.begin(), heap_source.dwords.begin() + 4u,
		          image_source.dwords.begin() + 4u);
		image_source.indirect_image = DescriptorSource::IndirectImage {
		    material_source_index, heap_source_index, selector_stride, selector_offset, 0u};

		plan.handle = &handle;
		plan.source = InternSource(image_source);
		plan.key    = Value(material_read);
		plan.roots  = image_source.dwords;
		return true;
	}

	const IndirectImagePlan* FindIndirectImage(const Inst& handle) const {
		const auto found =
		    std::find_if(m_indirect_images.begin(), m_indirect_images.end(),
		                 [&](const IndirectImagePlan& plan) { return plan.handle == &handle; });
		return found == m_indirect_images.end() ? nullptr : &*found;
	}

	static bool SupportsIndirectImageUse(const Inst& handle) {
		return !handle.Uses().empty() && std::ranges::all_of(handle.Uses(), [](const Use& use) {
			return use.user->GetOpcode() == ValueOpcode::ImageSampleRaw ||
			       use.user->GetOpcode() == ValueOpcode::ImageRead;
		});
	}

	bool IsIndirectPlanningMemory(uint32_t index) const {
		return std::any_of(m_indirect_images.begin(), m_indirect_images.end(),
		                   [&](const IndirectImagePlan& plan) {
			                   return std::ranges::find(plan.memory, index) != plan.memory.end();
		                   });
	}

	void PlanIndirectImages() {
		for (auto* block: m_program.blocks) {
			for (auto& inst: *block) {
				if (ImageOpcodeInfoOf(inst.GetOpcode()).access == ImageAccess::None ||
				    inst.NumArgs() == 0u) {
					continue;
				}
				auto* handle = inst.Arg(0).Resolve().TryInstruction();
				if (handle == nullptr || FindIndirectImage(*handle) != nullptr) {
					continue;
				}
				if (!SupportsIndirectImageUse(*handle)) continue;
				IndirectImagePlan plan;
				if (TryMakeIndirectImage(*handle, inst.Flags<MemoryFlags>().pc, plan) ||
				    TryMakeDirectImage(*handle, inst.Flags<MemoryFlags>().pc, plan)) {
					m_indirect_images.push_back(std::move(plan));
				}
			}
		}
	}

	bool PredicateContains(Value predicate, bool truth, const Inst* comparison,
	                       uint32_t depth = 0) const {
		const auto* inst = predicate.Resolve().TryInstruction();
		if (inst == comparison) {
			return truth;
		}
		if (inst == nullptr || depth >= 64) {
			return false;
		}
		if (inst->GetOpcode() == ValueOpcode::LogicalNot) {
			return PredicateContains(inst->Arg(0), !truth, comparison, depth + 1);
		}
		if ((truth && inst->GetOpcode() == ValueOpcode::LogicalAnd) ||
		    (!truth && inst->GetOpcode() == ValueOpcode::LogicalOr)) {
			return PredicateContains(inst->Arg(0), truth, comparison, depth + 1) ||
			       PredicateContains(inst->Arg(1), truth, comparison, depth + 1);
		}
		return false;
	}

	bool EdgeGuards(uint32_t from, uint32_t to, uint32_t pc) const {
		if (m_program.block_info.empty()) {
			return false;
		}
		std::vector<uint32_t> pending {m_program.block_info[0].id};
		std::vector<uint32_t> visited;
		while (!pending.empty()) {
			const auto id = pending.back();
			pending.pop_back();
			if (std::ranges::find(visited, id) != visited.end()) {
				continue;
			}
			visited.push_back(id);
			const auto block = std::ranges::find(m_program.block_info, id, &BlockInfo::id);
			if (block == m_program.block_info.end()) {
				return false;
			}
			if (pc >= block->start_pc && pc < block->end_pc) {
				return false;
			}
			const auto Add = [&](uint32_t next) {
				if (next != UINT32_MAX && !(id == from && next == to)) {
					pending.push_back(next);
				}
			};
			const auto& term = block->terminator;
			if (term.kind == CFG::TerminatorKind::Branch ||
			    term.kind == CFG::TerminatorKind::ConditionalBranch) {
				Add(term.true_block);
				if (term.kind == CFG::TerminatorKind::ConditionalBranch) {
					Add(term.false_block);
				}
			} else if (term.kind != CFG::TerminatorKind::Return) {
				return false;
			}
		}
		return true;
	}

	void ForwardPrivateSharedReads() {
		const uint64_t threads = uint64_t {m_local_size[0]} * m_local_size[1] * m_local_size[2];
		if (m_program.stage != ShaderType::Compute || threads == 0 || threads > 1024 ||
		    m_shared_bytes == 0 || m_program.dispatcher_fallback)
			return;
		using Terms  = std::array<uint64_t, 4>;
		auto maximum = [&](const Terms& terms) {
			uint64_t value = terms[0];
			for (size_t axis = 0; axis < 3; ++axis)
				value += terms[axis + 1] * (m_local_size[axis] - 1u);
			return value;
		};
		std::function<std::optional<Terms>(Value, uint32_t)> affine =
		    [&](Value value, uint32_t depth) -> std::optional<Terms> {
			value = value.Resolve();
			if (depth > 24 || value.GetType() != Type::U32) return {};
			if (value.IsImmediate()) return Terms {value.U32(), 0, 0, 0};
			const auto* inst = value.TryInstruction();
			if (!inst) return {};
			const auto op = inst->GetOpcode();
			if (op == ValueOpcode::GetBuiltin &&
			    inst->Arg(0) == Value(static_cast<uint32_t>(StageInputKind::LocalInvocationId)) &&
			    inst->Arg(1).IsImmediate() && inst->Arg(1).U32() < 3) {
				Terms terms {};
				terms[inst->Arg(1).U32() + 1] = 1;
				return terms;
			}
			if (op == ValueOpcode::BitFieldUExtract && inst->Arg(1).Resolve() == Value(0u)) {
				const auto width = inst->Arg(2).Resolve();
				auto       terms = affine(inst->Arg(0), depth + 1);
				if (terms && width.IsImmediate() && width.U32() <= 32u &&
				    maximum(*terms) < (uint64_t {1} << width.U32()))
					return terms;
				return {};
			}
			if (op != ValueOpcode::IAdd32 && op != ValueOpcode::IMul32 &&
			    op != ValueOpcode::ShiftLeftLogical32)
				return {};
			auto lhs = affine(inst->Arg(0), depth + 1), rhs = affine(inst->Arg(1), depth + 1);
			if (!lhs || !rhs) return {};
			const auto constant = [](const Terms& terms) {
				return terms[1] == 0 && terms[2] == 0 && terms[3] == 0;
			};
			if (op == ValueOpcode::IMul32 && !constant(*rhs) && constant(*lhs)) std::swap(lhs, rhs);
			if (op != ValueOpcode::IAdd32 && !constant(*rhs)) return {};
			if (op == ValueOpcode::ShiftLeftLogical32) {
				if ((*rhs)[0] >= 32) return {};
				(*rhs)[0] = uint64_t {1} << (*rhs)[0];
			}
			for (size_t i = 0; i < lhs->size(); ++i) {
				(*lhs)[i] =
				    op == ValueOpcode::IAdd32 ? (*lhs)[i] + (*rhs)[i] : (*lhs)[i] * (*rhs)[0];
				if ((*lhs)[i] > UINT32_MAX) return {};
			}
			return maximum(*lhs) <= UINT32_MAX ? lhs : std::nullopt;
		};
		auto region = [&](const Inst& inst) -> std::optional<std::pair<uint32_t, uint32_t>> {
			const auto index = inst.Flags<MemoryFlags>().index;
			if (index >= m_program.memory_info.size()) return {};
			const auto& memory = m_program.memory_info[index];
			const auto  terms  = affine(inst.Arg(0), 0);
			if (!terms || memory.data_bits != 32 || memory.data_dwords != 1 ||
			    memory.secondary_offset != 0)
				return {};
			uint32_t stride = 4;
			for (size_t axis = 0; axis < 3; ++axis) {
				if (m_local_size[axis] > 1 && (*terms)[axis + 1] != stride) return {};
				stride *= m_local_size[axis];
			}
			const auto begin = (*terms)[0] + memory.offset;
			const auto end   = begin + threads * 4u;
			if (begin % 4 || end > m_shared_bytes) return {};
			return std::pair {static_cast<uint32_t>(begin), static_cast<uint32_t>(end)};
		};
		struct Store {
			Inst*                         inst;
			Block*                        block;
			std::pair<uint32_t, uint32_t> region;
		};
		std::vector<Store>                        stores;
		std::unordered_map<const Inst*, uint32_t> positions;
		for (auto* block: m_program.blocks) {
			uint32_t position = 0;
			for (auto& inst: *block) {
				positions.emplace(&inst, position++);
				const auto access = SharedAccessOf(inst.GetOpcode());
				if (access == SharedAccess::None || access == SharedAccess::Read) continue;
				if (inst.GetOpcode() != ValueOpcode::WriteSharedU32) return;
				const auto span = region(inst);
				if (!span) return;
				stores.push_back({&inst, block, *span});
			}
		}
		for (auto* block: m_program.blocks)
			for (auto it = block->begin(); it != block->end(); ++it) {
				auto& inst = *it;
				if (inst.GetOpcode() != ValueOpcode::LoadSharedU32) continue;
				const auto span = region(inst);
				if (!span) continue;
				const Store* writer = nullptr;
				bool         unique = true;
				for (const auto& store: stores) {
					if (store.region.first >= span->second || store.region.second <= span->first)
						continue;
					if (writer || store.region != *span) {
						unique = false;
						break;
					}
					writer = &store;
				}
				if (!unique || !writer ||
				    (writer->inst->Arg(2).Resolve() != inst.Arg(1).Resolve() &&
				     writer->inst->Arg(2).Resolve() != Value(true)))
					continue;
				bool dominates = false;
				if (writer->block == block) {
					dominates = positions.at(writer->inst) < positions.at(&inst);
				} else {
					std::vector<Block*> pending {m_program.blocks.front()}, visited;
					dominates = true;
					while (!pending.empty()) {
						auto* current = pending.back();
						pending.pop_back();
						if (current == writer->block ||
						    std::ranges::find(visited, current) != visited.end())
							continue;
						if (current == block) {
							dominates = false;
							break;
						}
						visited.push_back(current);
						for (auto* successor: current->ImmSuccessors())
							pending.push_back(successor);
					}
				}
				if (!dominates) continue;
				// Every invocation owns one distinct word in this region, and the sole
				// writer dominates the load. Preserve the masked load's zero result.
				const auto replacement = block->PrependNewInst(
				    it, ValueOpcode::SelectU32, {inst.Arg(1), writer->inst->Arg(1), Value(0u)});
				inst.ReplaceUsesWith(Value(&*replacement));
			}
	}

	void FindSharedIndexRanges() {
		// Recognize append lists built from a strided traversal of input indices.
		// Prove every writer and the consumer's counter guard before narrowing LDS
		// values; runtime capacity checks keep the arrays and counters disjoint.
		const uint64_t threads = uint64_t {m_local_size[0]} * m_local_size[1] * m_local_size[2];
		if (m_program.stage != ShaderType::Compute || threads == 0 || threads > 1024 ||
		    m_shared_bytes == 0 || m_program.dispatcher_fallback)
			return;
		std::unordered_map<const Inst*, Block*> owners;
		for (auto* block: m_program.blocks)
			for (const auto& inst: *block)
				owners.emplace(&inst, block);
		auto selected = [](Value value, Value predicate) {
			for (uint32_t depth = 0; depth < 32; ++depth) {
				value            = value.Resolve();
				const auto* inst = value.TryInstruction();
				if (inst == nullptr || inst->GetOpcode() != ValueOpcode::SelectU32 ||
				    inst->Arg(0).Resolve() != predicate.Resolve())
					break;
				value = inst->Arg(1);
			}
			return value.Resolve();
		};
		using Coefficients = std::array<uint64_t, 4>;
		std::function<std::optional<Coefficients>(Value, uint32_t)> linear =
		    [&](Value value, uint32_t depth) -> std::optional<Coefficients> {
			value = value.Resolve();
			if (depth > 16 || value.GetType() != Type::U32) return {};
			if (value.IsImmediate()) return Coefficients {value.U32(), 0, 0, 0};
			const auto* inst = value.TryInstruction();
			if (!inst) return {};
			if (inst->GetOpcode() == ValueOpcode::GetBuiltin &&
			    inst->Arg(0) == Value(static_cast<uint32_t>(StageInputKind::LocalInvocationId)) &&
			    inst->Arg(1).IsImmediate() && inst->Arg(1).U32() < 3) {
				Coefficients result {};
				result[inst->Arg(1).U32() + 1] = 1;
				return result;
			}
			const auto op = inst->GetOpcode();
			if (op != ValueOpcode::IAdd32 && op != ValueOpcode::IMul32 &&
			    op != ValueOpcode::ShiftLeftLogical32)
				return {};
			auto lhs = linear(inst->Arg(0), depth + 1), rhs = linear(inst->Arg(1), depth + 1);
			if (!lhs || !rhs) return {};
			if (op != ValueOpcode::IAdd32) {
				if ((*rhs)[1] || (*rhs)[2] || (*rhs)[3]) return {};
				if (op == ValueOpcode::ShiftLeftLogical32) {
					if ((*rhs)[0] >= 32) return {};
					(*rhs)[0] = uint64_t {1} << (*rhs)[0];
				}
			}
			for (size_t i = 0; i < lhs->size(); ++i) {
				(*lhs)[i] =
				    op == ValueOpcode::IAdd32 ? (*lhs)[i] + (*rhs)[i] : (*lhs)[i] * (*rhs)[0];
				if ((*lhs)[i] > UINT32_MAX) return {};
			}
			return lhs;
		};
		auto local_index = [&](Value value) {
			const auto terms = linear(value, 0);
			if (!terms || (*terms)[0]) return false;
			uint32_t stride = 1;
			for (size_t axis = 0; axis < 3; ++axis) {
				if (m_local_size[axis] > 1 && (*terms)[axis + 1] != stride) return false;
				stride *= m_local_size[axis];
			}
			return true;
		};
		std::function<Value(Value, Value, bool, uint32_t)> guarded_bound =
		    [&](Value index, Value predicate, bool truth, uint32_t depth) -> Value {
			const auto* inst = predicate.Resolve().TryInstruction();
			if (!inst || depth > 32) return {};
			const auto op = inst->GetOpcode();
			if (op == ValueOpcode::LogicalNot)
				return guarded_bound(index, inst->Arg(0), !truth, depth + 1);
			if ((truth && op == ValueOpcode::LogicalAnd) ||
			    (!truth && op == ValueOpcode::LogicalOr)) {
				auto bound = guarded_bound(index, inst->Arg(0), truth, depth + 1);
				return bound.IsEmpty() ? guarded_bound(index, inst->Arg(1), truth, depth + 1)
				                       : bound;
			}
			Value bound;
			if (truth && op == ValueOpcode::ULessThan32 &&
			    inst->Arg(0).Resolve() == index.Resolve())
				bound = inst->Arg(1);
			if (!truth && op == ValueOpcode::ULessThanEqual32 &&
			    inst->Arg(1).Resolve() == index.Resolve())
				bound = inst->Arg(0);
			return !bound.IsEmpty() && ValidateRuntimeValue(m_program, bound) ? bound.Resolve()
			                                                                  : Value {};
		};
		auto reachable = [](Block* start, Block* target, Block* forbidden, Block* edge_from,
		                    Block* edge_to, bool nonempty) {
			std::vector<Block*> pending, visited;
			auto                add = [&](Block* from) {
				for (auto* to: from->ImmSuccessors())
					if (!(from == edge_from && to == edge_to)) pending.push_back(to);
			};
			if (nonempty)
				add(start);
			else
				pending.push_back(start);
			while (!pending.empty()) {
				auto* block = pending.back();
				pending.pop_back();
				if (block == forbidden || std::ranges::find(visited, block) != visited.end())
					continue;
				if (block == target) return true;
				visited.push_back(block);
				add(block);
			}
			return false;
		};
		struct Initialization {
			uint32_t begin, end, pc;
			Block*   guard;
		};
		struct List {
			const Inst* store;
			const Inst* atomic;
			Value       count;
			uint32_t    offset, counter;
		};
		std::vector<Initialization> initializations;
		std::vector<List>           lists;
		std::vector<const Inst*>    atomics;
		for (auto* block: m_program.blocks)
			for (const auto& inst: *block) {
				const auto access = SharedAccessOf(inst.GetOpcode());
				if (access == SharedAccess::None || access == SharedAccess::Read) continue;
				const auto flags = inst.Flags<MemoryFlags>();
				if (flags.index >= m_program.memory_info.size()) return;
				const auto& memory = m_program.memory_info[flags.index];
				if (memory.data_bits != 32 || memory.offset % 4 || memory.secondary_offset != 0)
					return;
				if (inst.GetOpcode() == ValueOpcode::SharedAtomicIAdd32) {
					atomics.push_back(&inst);
					continue;
				}
				if (access != SharedAccess::Write) return;
				const auto components = SharedComponentCount(inst.GetOpcode());
				const auto predicate  = inst.Arg(components + 1).Resolve();
				const auto address    = selected(inst.Arg(0), predicate);
				bool       zero       = true;
				for (uint32_t i = 0; i < components; ++i)
					zero &= selected(inst.Arg(i + 1), predicate) == Value(0u);
				if (zero && address.IsImmediate()) {
					const auto* comparison = predicate.TryInstruction();
					if (!comparison || comparison->GetOpcode() != ValueOpcode::IEqual32 ||
					    !((comparison->Arg(0) == Value(0u) && local_index(comparison->Arg(1))) ||
					      (comparison->Arg(1) == Value(0u) && local_index(comparison->Arg(0)))))
						return;
					if (block->ImmPredecessors().size() != 1 || block->ImmSuccessors().size() != 1)
						return;
					auto*      guard = block->ImmPredecessors()[0];
					const auto gi =
					    std::ranges::find(m_program.blocks, guard) - m_program.blocks.begin();
					const auto& info = m_program.block_info[gi];
					const auto  bi =
					    std::ranges::find(m_program.blocks, block) - m_program.blocks.begin();
					const bool true_edge =
					    info.terminator.true_block == m_program.block_info[bi].id;
					if (info.terminator.kind != CFG::TerminatorKind::ConditionalBranch ||
					    !PredicateContains(info.condition, true_edge, comparison))
						return;
					const auto begin = uint64_t {address.U32()} + memory.offset;
					const auto end   = begin + components * 4u;
					if (end > m_shared_bytes) return;
					initializations.push_back({static_cast<uint32_t>(begin),
					                           static_cast<uint32_t>(end), flags.pc, guard});
					continue;
				}
				if (inst.GetOpcode() != ValueOpcode::WriteSharedU32) return;
				const auto* shift = address.TryInstruction();
				if (!shift || shift->GetOpcode() != ValueOpcode::ShiftLeftLogical32 ||
				    shift->Arg(1) != Value(2u))
					return;
				const auto* atomic = selected(shift->Arg(0), predicate).TryInstruction();
				if (!atomic || atomic->GetOpcode() != ValueOpcode::SharedAtomicIAdd32 ||
				    owners.at(atomic) != block || atomic->Arg(2).Resolve() != predicate ||
				    selected(atomic->Arg(0), predicate) != Value(0u) ||
				    selected(atomic->Arg(1), predicate) != Value(1u))
					return;
				const auto  count = guarded_bound(inst.Arg(1), predicate, true, 0);
				const auto* sum   = inst.Arg(1).Resolve().TryInstruction();
				if (count.IsEmpty() || !sum || sum->GetOpcode() != ValueOpcode::IAdd32) return;
				const Inst* phi = nullptr;
				for (size_t arg = 0; arg < 2; ++arg) {
					const auto* candidate = sum->Arg(arg).Resolve().TryInstruction();
					if (candidate && candidate->GetOpcode() == ValueOpcode::Phi &&
					    local_index(sum->Arg(1 - arg)))
						phi = candidate;
				}
				if (!phi || phi->NumArgs() != 2) return;
				const size_t back      = phi->Arg(0).Resolve() == Value(0u) ? 1u : 0u;
				const auto*  increment = phi->Arg(back).Resolve().TryInstruction();
				if (phi->Arg(1 - back).Resolve() != Value(0u) || !increment ||
				    increment->GetOpcode() != ValueOpcode::IAdd32 ||
				    increment->Arg(0).Resolve() != Value(const_cast<Inst*>(phi)) ||
				    increment->Arg(1) != Value(static_cast<uint32_t>(threads)))
					return;
				auto* header = owners.at(phi);
				auto* tail   = phi->PhiBlock(back);
				if (!tail || owners.at(increment) != tail ||
				    reachable(header, header, nullptr, tail, header, true) ||
				    reachable(block, block, nullptr, tail, header, true))
					return;
				bool bounded_loop = false;
				for (const auto& info: m_program.block_info) {
					if (info.terminator.kind != CFG::TerminatorKind::ConditionalBranch) continue;
					for (const bool truth: {false, true}) {
						const auto bound =
						    guarded_bound(Value(const_cast<Inst*>(phi)), info.condition, truth, 0);
						bounded_loop |=
						    bound == count && EdgeGuards(info.id,
						                                 truth ? info.terminator.true_block
						                                       : info.terminator.false_block,
						                                 flags.pc);
					}
				}
				if (!bounded_loop) return;
				const auto atomic_flags = atomic->Flags<MemoryFlags>();
				if (atomic_flags.index >= m_program.memory_info.size() ||
				    atomic_flags.pc >= flags.pc)
					return;
				lists.push_back({&inst, atomic, count, memory.offset,
				                 m_program.memory_info[atomic_flags.index].offset});
			}
		if (lists.empty() || atomics.size() != lists.size()) return;
		std::ranges::sort(lists, {}, &List::offset);
		std::vector<std::pair<Value, uint32_t>> limits;
		for (size_t i = 0; i < lists.size(); ++i) {
			const auto& list = lists[i];
			if (std::ranges::count(lists, list.atomic, &List::atomic) != 1 ||
			    std::ranges::count(lists, list.counter, &List::counter) != 1 ||
			    list.counter + 4u > lists.front().offset)
				return;
			const auto end = i + 1 < lists.size() ? lists[i + 1].offset : m_shared_bytes;
			if (list.offset >= end) return;
			for (const auto& init: initializations)
				if (init.end > lists.front().offset) return;
			const auto initialized = std::ranges::any_of(initializations, [&](const auto& init) {
				return init.begin <= list.counter && init.end >= list.counter + 4u &&
				       init.pc < list.atomic->Flags<MemoryFlags>().pc &&
				       !reachable(m_program.blocks.front(), owners.at(list.atomic), init.guard,
				                  nullptr, nullptr, false);
			});
			if (!initialized) return;
			limits.emplace_back(list.count, std::min((end - list.offset) / 4u, 65536u));
		}
		std::vector<DescriptorSource::IndexRange> ranges;
		for (auto* block: m_program.blocks)
			for (auto& inst: *block) {
				if (inst.GetOpcode() != ValueOpcode::LoadSharedU32) continue;
				const auto  flags  = inst.Flags<MemoryFlags>();
				const auto& memory = m_program.memory_info[flags.index];
				const auto  list   = std::ranges::find(lists, memory.offset, &List::offset);
				if (list == lists.end() || flags.pc <= list->store->Flags<MemoryFlags>().pc)
					continue;
				const auto* shift = inst.Arg(0).Resolve().TryInstruction();
				if (!shift || shift->GetOpcode() != ValueOpcode::ShiftLeftLogical32 ||
				    shift->Arg(1) != Value(2u))
					continue;
				for (const auto& range: FindIndexRanges(inst.Arg(0), flags.pc)) {
					if (range.value.Resolve() != shift->Arg(0).Resolve() ||
					    range.begin != Value(0u))
						continue;
					const auto* count = range.end.Resolve().TryInstruction();
					if (!count || count->GetOpcode() != ValueOpcode::LoadSharedU32 ||
					    count->Arg(0).Resolve() != Value(0u))
						continue;
					const auto& counter_memory =
					    m_program.memory_info[count->Flags<MemoryFlags>().index];
					if (counter_memory.offset == list->counter &&
					    counter_memory.secondary_offset == 0 && counter_memory.data_bits == 32 &&
					    memory.secondary_offset == 0 && memory.data_bits == 32) {
						ranges.push_back({Value(&inst), Value(0u), list->count, limits});
					}
				}
			}
		m_shared_ranges = std::move(ranges);
	}

	std::vector<DescriptorSource::IndexRange> FindIndexRanges(Value offset, uint32_t pc) const {
		std::vector<DescriptorSource::IndexRange> result;
		std::vector<Value>                        pending {offset};
		std::vector<const Inst*>                  visited;
		while (!pending.empty()) {
			const auto value = pending.back().Resolve();
			pending.pop_back();
			const auto* phi = value.TryInstruction();
			if (phi == nullptr || std::ranges::find(visited, phi) != visited.end()) {
				continue;
			}
			visited.push_back(phi);
			if (const auto shared = std::ranges::find_if(
			        m_shared_ranges,
			        [&](const auto& range) { return range.value.Resolve() == value; });
			    shared != m_shared_ranges.end()) {
				result.push_back(*shared);
				continue;
			}
			for (size_t arg = 0; arg < phi->NumArgs(); ++arg) {
				pending.push_back(phi->Arg(arg));
			}
			if (phi->GetOpcode() != ValueOpcode::Phi || value.GetType() != Type::U32) {
				continue;
			}
			bool unsigned_bound = false;
			for (const auto& use: phi->Uses()) {
				const auto* comparison = use.user;
				if (comparison->GetOpcode() != ValueOpcode::ULessThan32 ||
				    comparison->Arg(0).Resolve() != value)
					continue;
				for (const auto& block: m_program.block_info) {
					if (block.terminator.kind != CFG::TerminatorKind::ConditionalBranch) continue;
					for (const bool truth: {false, true}) {
						const auto target =
						    truth ? block.terminator.true_block : block.terminator.false_block;
						unsigned_bound |= PredicateContains(block.condition, truth, comparison) &&
						                  EdgeGuards(block.id, target, pc);
					}
				}
				if (unsigned_bound) {
					// A dominating unsigned comparison proves 0 <= index < bound
					// independently of the loop's initial value or induction step.
					result.push_back({value, Value(0u), comparison->Arg(1)});
					break;
				}
			}
			if (unsigned_bound || phi->NumArgs() != 2) continue;
			uint32_t    start     = 0;
			const Inst* increment = nullptr;
			if (ImmediateU32(phi->Arg(0), start)) {
				increment = phi->Arg(1).Resolve().TryInstruction();
			} else if (ImmediateU32(phi->Arg(1), start)) {
				increment = phi->Arg(0).Resolve().TryInstruction();
			}
			if (increment == nullptr || start >= INT32_MAX || increment->NumArgs() != 2 ||
			    increment->Arg(0).Resolve() != value) {
				continue;
			}
			const auto step = increment->Arg(1).Resolve();
			const bool descending =
			    (increment->GetOpcode() == ValueOpcode::ISub32 && step == Value(1u)) ||
			    (increment->GetOpcode() == ValueOpcode::IAdd32 && step == Value(UINT32_MAX));
			if (!descending &&
			    !(increment->GetOpcode() == ValueOpcode::IAdd32 && step == Value(1u))) {
				continue;
			}
			for (const auto& use: phi->Uses()) {
				const auto* comparison = use.user;
				const auto  expected =
				    descending ? ValueOpcode::SGreaterThanEqual32 : ValueOpcode::SLessThan32;
				const bool unsigned_ascending =
				    !descending && comparison->GetOpcode() == ValueOpcode::ULessThan32;
				if ((comparison->GetOpcode() != expected && !unsigned_ascending) ||
				    comparison->Arg(0).Resolve() != value) {
					continue;
				}
				if (descending && comparison->Arg(1).Resolve() != Value(0u)) {
					continue;
				}
				bool guarded = false;
				for (const auto& block: m_program.block_info) {
					if (block.terminator.kind != CFG::TerminatorKind::ConditionalBranch) {
						continue;
					}
					for (const bool truth: {false, true}) {
						const auto target =
						    truth ? block.terminator.true_block : block.terminator.false_block;
						guarded |= PredicateContains(block.condition, truth, comparison) &&
						           EdgeGuards(block.id, target, pc);
					}
				}
				if (guarded) {
					result.push_back({value, descending ? Value(0u) : Value(start),
					                  descending ? Value(start + 1u) : comparison->Arg(1)});
					break;
				}
			}
		}
		return result;
	}

	bool TryMakeDirectImage(Inst& handle, uint32_t pc, IndirectImagePlan& plan) {
		if (handle.GetOpcode() != ValueOpcode::GetImageResource || handle.NumArgs() != 8u) {
			return false;
		}
		const Inst* table = nullptr;
		Value       offset;
		bool        raw_address = false;
		uint32_t    immediate   = 0;
		for (uint32_t dword = 0; dword < 8u; ++dword) {
			const auto* read   = ResolveDescriptorRead(handle.Arg(dword));
			uint32_t    index  = 0;
			const auto* memory = read == nullptr ? nullptr : ScalarReadMemory(*read, index);
			if (read != nullptr && read->GetOpcode() == ValueOpcode::LoadAddressU32 &&
			    read->Arg(3).Resolve() == Value(true)) {
				index = read->Flags<MemoryFlags>().index;
				if (index < m_program.memory_info.size()) {
					const auto& candidate = m_program.memory_info[index];
					if (candidate.kind == ResourceKind::ScalarAddress &&
					    candidate.data_bits == 32u && candidate.data_dwords == 1u) {
						memory = &candidate;
					}
				}
			}
			const bool belongs = read != nullptr && MemoryIndexBelongsTo(index, *read);
			const bool image_only =
			    read != nullptr && DescriptorReadUsesOnlyImages(handle.Arg(dword), *read);
			if (memory == nullptr || !belongs || !image_only) {
				return false;
			}
			if (dword == 0) immediate = memory->offset;
			if (int64_t {static_cast<int32_t>(memory->offset)} !=
			    int64_t {static_cast<int32_t>(immediate)} + dword * 4u) {
				return false;
			}
			const auto* base = read->Arg(0).Resolve().TryInstruction();
			if (base == nullptr || (table != nullptr && table != base) ||
			    !DescriptorReadUsesOnlyImages(handle.Arg(dword), *read)) {
				return false;
			}
			table = base;
			if (dword == 0) {
				offset      = read->Arg(1).Resolve();
				raw_address = memory->kind == ResourceKind::ScalarAddress;
			} else if (raw_address != (memory->kind == ResourceKind::ScalarAddress) ||
			           !EquivalentValue(m_program, offset, read->Arg(1))) {
				return false;
			}
			plan.memory[dword] = index;
			plan.reads[dword]  = read;
		}
		DescriptorSource table_source;
		uint32_t         source = 0;
		if (raw_address) {
			if (table->GetOpcode() != ValueOpcode::GetAddressResource) {
				return false;
			}
			table_source.dword_count = 4;
			table_source.dwords[0]   = table->Arg(0);
			table_source.dwords[1]   = table->Arg(1);
			table_source.dwords[2] = table_source.dwords[3] = Value(0u);
			uint32_t bad                                    = 0;
			if (!ValidateSource(table_source, bad)) {
				return false;
			}
			source = InternSource(table_source);
		} else if (!MakeRuntimeBufferSource(*table, pc, source, table_source)) {
			return false;
		}
		DescriptorSource image_source;
		image_source.dword_count = 8;
		std::copy_n(table_source.dwords.begin(), 4, image_source.dwords.begin());
		std::copy_n(table_source.dwords.begin(), 4, image_source.dwords.begin() + 4);
		image_source.indirect_image =
		    DescriptorSource::IndirectImage {source, source, 0, 0, 0, offset, raw_address};
		image_source.indirect_image->immediate_offset = immediate;
		image_source.indirect_image->index_ranges     = FindIndexRanges(offset, pc);
		plan.handle                                   = &handle;
		plan.source                                   = InternSource(image_source);
		plan.key                                      = offset;
		plan.roots                                    = image_source.dwords;
		return true;
	}

	bool MakeIndirectBuffer(const Inst& handle, DescriptorSource& descriptor, uint32_t pc) const {
		DescriptorSource candidate;
		candidate.dword_count = 4;
		candidate.dwords[2] = candidate.dwords[3] = Value(0u);
		const Inst* address                       = nullptr;
		Value       offset;
		uint32_t    immediate = 0;
		for (uint32_t dword = 0; dword < 4; ++dword) {
			const auto* read = handle.Arg(dword).Resolve().TryInstruction();
			if (read == nullptr || read->GetOpcode() != ValueOpcode::LoadAddressU32 ||
			    read->Arg(3).Resolve() != Value(true)) {
				return false;
			}
			const auto index = read->Flags<MemoryFlags>().index;
			if (index >= m_program.memory_info.size()) {
				return false;
			}
			const auto& memory = m_program.memory_info[index];
			const auto* base   = read->Arg(0).Resolve().TryInstruction();
			if (memory.kind != ResourceKind::ScalarAddress || memory.data_bits != 32u ||
			    memory.data_dwords != 1u || base == nullptr ||
			    base->GetOpcode() != ValueOpcode::GetAddressResource) {
				return false;
			}
			if (dword == 0) {
				address   = base;
				offset    = read->Arg(1).Resolve();
				immediate = memory.offset;
			} else if (int64_t {static_cast<int32_t>(memory.offset)} !=
			               int64_t {static_cast<int32_t>(immediate)} + dword * 4u ||
			           !EquivalentValue(m_program, offset, read->Arg(1)) ||
			           !EquivalentValue(m_program, address->Arg(0), base->Arg(0)) ||
			           !EquivalentValue(m_program, address->Arg(1), base->Arg(1))) {
				return false;
			}
		}
		candidate.dwords[0] = address->Arg(0);
		candidate.dwords[1] = address->Arg(1);
		uint32_t bad_dword  = 0;
		if (!ValidateSource(candidate, bad_dword)) {
			return false;
		}
		candidate.indirect_buffer = DescriptorSource::IndirectBuffer {offset, immediate};
		candidate.indirect_buffer->index_ranges = FindIndexRanges(offset, pc);
		descriptor                              = std::move(candidate);
		return true;
	}

	void GetHandle(Value value, ValueOpcode expected, uint32_t width, uint32_t pc, Inst*& handle,
	               uint32_t& source, bool sampler = false, bool sample_adjust = false) {
		if (m_program.shader_hash == 0x78af8e269b528b5cULL &&
		    expected == ValueOpcode::GetBufferResource) {
			const auto ordinal = ++m_trace_buffer_handles;
			if (ordinal <= 10u || (ordinal % 100u) == 0u)
				LOGF(
				    "ResourceTracking trace hash=0x%016llx GetBufferResource #%u pc=0x%08x begin\n",
				    static_cast<unsigned long long>(m_program.shader_hash), ordinal, pc);
		}
		handle = value.Resolve().TryInstruction();
		if (handle == nullptr || handle->GetOpcode() != expected) {
			Fail(pc, fmt::format("memory operation requires {}", ValueOpcodeName(expected)));
		}
		if (m_program.shader_hash == 0x78af8e269b528b5cULL &&
		    expected == ValueOpcode::GetBufferResource && handle->NumArgs() == 4u &&
		    handle->Arg(0).Resolve().TryInstruction() != nullptr &&
		    handle->Arg(0).Resolve().TryInstruction()->GetOpcode() ==
		        ValueOpcode::CompositeExtractU64) {
			LOGF("ResourceTracking dynamic descriptor hash=0x%016llx pc=0x%08x parent=%p\n",
			     static_cast<unsigned long long>(m_program.shader_hash), pc,
			     static_cast<void*>(handle->Parent()));
			for (uint32_t word = 0; word < handle->NumArgs(); ++word) {
				const auto  item = handle->Arg(word).Resolve();
				const auto* inst = item.TryInstruction();
				LOGF("ResourceTracking dynamic descriptor dword=%u opcode=%s args=%u\n", word,
				     inst == nullptr ? "immediate" : ValueOpcodeName(inst->GetOpcode()).data(),
				     inst == nullptr ? 0u : inst->NumArgs());
				if (word < 2u && inst != nullptr) {
					std::function<void(Value, uint32_t, std::unordered_set<const Inst*>&)> dump =
					    [&](Value value, uint32_t depth, std::unordered_set<const Inst*>& visited) {
						    value            = value.Resolve();
						    const auto* node = value.TryInstruction();
						    if (node == nullptr || depth > 8u || !visited.insert(node).second)
							    return;
						    const auto slot =
						        node->GetOpcode() == ValueOpcode::ReadConst && node->NumArgs() > 1u
						            ? node->Arg(1).Resolve()
						            : Value {};
						    LOGF("ResourceTracking dynamic graph depth=%u op=%s args=%u slot=%s "
						         "parent=%p\n",
						         depth, ValueOpcodeName(node->GetOpcode()).data(), node->NumArgs(),
						         slot.IsImmediate() && slot.GetType() == Type::U32
						             ? fmt::format("{}", slot.U32()).c_str()
						             : "-",
						         static_cast<void*>(node->Parent()));
						    for (uint32_t arg = 0; arg < node->NumArgs(); ++arg)
							    dump(node->Arg(arg), depth + 1u, visited);
					    };
					std::unordered_set<const Inst*> visited;
					dump(item, 0u, visited);
				}
			}
			for (uint32_t slot = 240u; slot < std::min<uint32_t>(250u, m_program.srt_reads.size());
			     ++slot) {
				const auto  value = m_program.srt_reads[slot].value.Resolve();
				const auto* inst  = value.TryInstruction();
				LOGF("ResourceTracking dynamic SRT slot=%u root=%s flat=%u\n", slot,
				     inst == nullptr ? (value.IsImmediate() ? "immediate" : "unknown")
				                     : ValueOpcodeName(inst->GetOpcode()).data(),
				     m_program.srt_reads[slot].flat_offset);
				if (slot >= 245u && slot <= 248u && inst != nullptr) {
					for (uint32_t arg = 0; arg < inst->NumArgs(); ++arg) {
						const auto  child      = inst->Arg(arg).Resolve();
						const auto* child_inst = child.TryInstruction();
						LOGF("ResourceTracking dynamic SRT slot=%u arg=%u op=%s\n", slot, arg,
						     child_inst == nullptr
						         ? (child.IsImmediate() ? "immediate" : "unknown")
						         : ValueOpcodeName(child_inst->GetOpcode()).data());
					}
					const auto* address = inst->Arg(0).Resolve().TryInstruction();
					if (address != nullptr &&
					    address->GetOpcode() == ValueOpcode::GetAddressResource) {
						for (uint32_t arg = 0; arg < address->NumArgs(); ++arg) {
							const auto  child      = address->Arg(arg).Resolve();
							const auto* child_inst = child.TryInstruction();
							LOGF("ResourceTracking dynamic address slot=%u arg=%u op=%s type=%u\n",
							     slot, arg,
							     child_inst == nullptr
							         ? (child.IsImmediate() ? "immediate" : "unknown")
							         : ValueOpcodeName(child_inst->GetOpcode()).data(),
							     static_cast<unsigned>(child.GetType()));
						}
					}
				}
			}
		}
		if (expected == ValueOpcode::GetBufferResource &&
		    MakeWaveUniformBufferSource(*handle, source))
			return;
		if (expected == ValueOpcode::GetBufferResource) {
			for (uint32_t dword = 0; dword < handle->NumArgs(); ++dword)
				handle->SetArg(dword, LowerRuntimeDescriptorPhi(handle->Arg(dword), *handle));
		}
		if (expected == ValueOpcode::GetBufferResource &&
		    MakeBoundedBufferSource(*handle, source)) {
			return;
		}
		DescriptorSource descriptor;
		MakeSource(*handle, width, sampler, sample_adjust, descriptor, pc);
		if (m_program.shader_hash == 0x78af8e269b528b5cULL &&
		    expected == ValueOpcode::GetBufferResource)
			LOGF("ResourceTracking trace hash=0x%016llx GetBufferResource #%u "
			     "MakeBoundedBufferExpression begin\n",
			     static_cast<unsigned long long>(m_program.shader_hash), m_trace_buffer_handles);
		if (expected == ValueOpcode::GetBufferResource &&
		    MakeBoundedBufferExpression(*handle, descriptor, source)) {
			if (m_program.shader_hash == 0x78af8e269b528b5cULL)
				LOGF(
				    "ResourceTracking trace hash=0x%016llx bounded expression accepted pc=0x%08x\n",
				    static_cast<unsigned long long>(m_program.shader_hash), pc);
			return;
		}
		if (m_program.shader_hash == 0x78af8e269b528b5cULL &&
		    expected == ValueOpcode::GetBufferResource)
			LOGF("ResourceTracking trace hash=0x%016llx GetBufferResource #%u after bounded "
			     "expression\n",
			     static_cast<unsigned long long>(m_program.shader_hash), m_trace_buffer_handles);
		uint32_t bad_dword = 0;
		if (expected == ValueOpcode::GetImageResource) {
			for (; bad_dword < descriptor.dword_count; bad_dword++) {
				const auto* value = descriptor.dwords[bad_dword].Resolve().TryInstruction();
				if (value != nullptr && value->GetOpcode() == ValueOpcode::ReadConstBuffer) {
					Fail(pc, fmt::format("{} dword {} is not a valid runtime value",
					                     ValueOpcodeName(expected), bad_dword));
				}
			}
			bad_dword = 0;
		}
		if (!ValidateSource(descriptor, bad_dword)) {
			if (expected != ValueOpcode::GetBufferResource ||
			    !MakeIndirectBuffer(*handle, descriptor, pc)) {
				Fail(pc, fmt::format("{} dword {} is not a valid runtime value",
				                     ValueOpcodeName(expected), bad_dword));
			}
		}
		source = InternSource(descriptor);
		if (m_program.shader_hash == 0x78af8e269b528b5cULL &&
		    expected == ValueOpcode::GetBufferResource) {
			const auto root_name = [](Value item) {
				const auto* inst = item.Resolve().TryInstruction();
				return inst == nullptr ? "immediate" : ValueOpcodeName(inst->GetOpcode());
			};
			LOGF("ResourceTracking trace hash=0x%016llx buffer source=%u roots=%s,%s,%s,%s\n",
			     static_cast<unsigned long long>(m_program.shader_hash), source,
			     root_name(descriptor.dwords[0]), root_name(descriptor.dwords[1]),
			     root_name(descriptor.dwords[2]), root_name(descriptor.dwords[3]));
		}
		if (descriptor.indirect_buffer.has_value()) {
			m_indirect_buffers.emplace_back(handle, source);
		}
	}

	void ValidateAddressHandle(Value value, uint32_t pc) const {
		const auto* handle = value.Resolve().TryInstruction();
		if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetAddressResource) {
			Fail(pc, "address operation requires GetAddressResource");
		}
		if (handle->NumArgs() != 2) {
			Fail(pc, "GetAddressResource must have two address dwords");
		}
	}

	uint32_t AddBuffer(uint32_t source, const MemoryInfo& memory, ValueOpcode op, uint32_t pc) {
		for (uint32_t i = 0; i < m_info.buffers.size(); i++) {
			if (m_info.buffers[i].source == source) {
				Merge(m_info.buffers[i], memory, op, pc);
				return i;
			}
		}
		if (m_info.buffers.size() >= ShaderInfo::MaxBuffers) {
			return UINT32_MAX;
		}
		BufferResource resource;
		resource.source       = source;
		resource.first_use_pc = pc;
		Merge(resource, memory, op, pc);
		m_info.buffers.push_back(resource);
		return static_cast<uint32_t>(m_info.buffers.size() - 1);
	}

	static void Merge(BufferResource& resource, const MemoryInfo& memory, ValueOpcode op,
	                  uint32_t pc) {
		const auto access        = BufferAccessOf(op);
		const bool atomic        = access == BufferAccess::Atomic;
		const bool write         = access == BufferAccess::Write || atomic;
		resource.first_use_pc    = std::min(resource.first_use_pc, pc);
		resource.max_byte_extent = std::max(resource.max_byte_extent, ByteExtent(memory));
		resource.read            = resource.read || !write || atomic;
		resource.written         = resource.written || write;
		resource.atomic          = resource.atomic || atomic;
		resource.formatted       = resource.formatted || memory.formatted;
		resource.scalar          = resource.scalar || op == ValueOpcode::ReadConstBuffer ||
		                           memory.kind == ResourceKind::ScalarBuffer;
	}

	uint32_t AddImage(uint32_t source, const MemoryInfo& memory, ValueOpcode op, uint32_t pc) {
		const auto resource_class = ImageOpcodeInfoOf(op).resource_class;
		const auto mip   = resource_class == ImageResourceClass::Storage && memory.image_has_mip
		                       ? ImageMipMode::DynamicStorage
		                       : ImageMipMode::None;
		const bool depth = (memory.image_sample_flags & Decoder::ImageSampleFlagCompare) != 0;
		for (uint32_t i = 0; i < m_info.images.size(); i++) {
			auto& image = m_info.images[i];
			if (image.source == source && image.resource_class == resource_class &&
			    image.dimension == memory.image_dimension && image.mip_mode == mip &&
			    image.depth_compare == depth && image.r128 == memory.image_r128) {
				Merge(image, op, pc);
				return i;
			}
		}
		if (m_info.images.size() >= ShaderInfo::MaxImages) {
			return UINT32_MAX;
		}
		ImageResource image;
		image.source         = source;
		image.first_use_pc   = pc;
		image.resource_class = resource_class;
		image.dimension      = memory.image_dimension;
		image.mip_mode       = mip;
		image.depth_compare  = depth;
		image.r128           = memory.image_r128;
		Merge(image, op, pc);
		m_info.images.push_back(image);
		return static_cast<uint32_t>(m_info.images.size() - 1);
	}

	static void Merge(ImageResource& image, ValueOpcode op, uint32_t pc) {
		const auto access  = ImageOpcodeInfoOf(op).access;
		const bool atomic  = access == ImageAccess::Atomic;
		const bool write   = access == ImageAccess::Write || atomic;
		image.first_use_pc = std::min(image.first_use_pc, pc);
		image.read         = image.read || !write || atomic;
		image.written      = image.written || write;
		image.atomic       = image.atomic || atomic;
	}

	uint32_t AddSampler(uint32_t source, uint32_t pc) {
		for (uint32_t i = 0; i < m_info.samplers.size(); i++) {
			if (m_info.samplers[i].source == source) {
				m_info.samplers[i].first_use_pc = std::min(m_info.samplers[i].first_use_pc, pc);
				return i;
			}
		}
		if (m_info.samplers.size() >= ShaderInfo::MaxSamplers) {
			return UINT32_MAX;
		}
		m_info.samplers.push_back({source, pc});
		return static_cast<uint32_t>(m_info.samplers.size() - 1);
	}

	void AddSampledPair(uint32_t image, uint32_t sampler, uint32_t pc) {
		for (auto& pair: m_info.sampled_pairs) {
			if (pair.image == image && pair.sampler == sampler) {
				pair.first_use_pc = std::min(pair.first_use_pc, pc);
				return;
			}
		}
		if (m_info.sampled_pairs.size() >= ShaderInfo::MaxSampledPairs) {
			Fail(pc, "sampled image/sampler pair limit exceeded");
		}
		m_info.sampled_pairs.push_back({image, sampler, pc});
	}

	void AddHandlePatch(Inst* handle, uint32_t resource, uint32_t pc) {
		for (const auto& patch: m_handle_patches) {
			if (patch.handle == handle) {
				if (patch.resource != resource) {
					Fail(pc, fmt::format("{} is reused with incompatible resource classes",
					                     ValueOpcodeName(handle->GetOpcode())));
				}
				return;
			}
		}
		m_handle_patches.push_back({handle, resource});
	}

	void AddMemoryPatch(uint32_t index, uint32_t resource, uint32_t sampler, bool has_sampler,
	                    uint32_t pc) {
		for (auto& patch: m_memory_patches) {
			if (patch.index != index) {
				continue;
			}
			if (patch.resource != resource ||
			    (has_sampler && patch.has_sampler && patch.sampler != sampler)) {
				Fail(pc, "memory metadata is reused with incompatible resources");
			}
			if (has_sampler) {
				patch.sampler     = sampler;
				patch.has_sampler = true;
			}
			return;
		}
		m_memory_patches.push_back({index, resource, sampler, has_sampler});
	}

	bool IsShaderSideRawRead(Value value) const {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || inst->GetOpcode() != ValueOpcode::LoadAddressU32 ||
		    inst->NumArgs() != 4u) {
			return false;
		}
		const auto flags = inst->Flags<MemoryFlags>();
		if (flags.index >= m_program.memory_info.size()) return false;
		const auto& memory = m_program.memory_info[flags.index];
		const auto* handle = inst->Arg(0).ResolveInstruction();
		return memory.kind == ResourceKind::ScalarAddress && memory.data_bits == 32u &&
		       memory.data_dwords == 1u && handle != nullptr &&
		       handle->GetOpcode() == ValueOpcode::GetAddressResource && handle->NumArgs() == 2u;
	}

	static bool IsRawScalarBufferMemory(const MemoryInfo& memory) {
		const bool valid_group_width =
		    memory.component_count == 1u || memory.component_count == 2u ||
		    memory.component_count == 4u || memory.component_count == 8u ||
		    memory.component_count == 16u;
		return memory.kind == ResourceKind::ScalarBuffer && memory.data_bits == 32u &&
		       memory.data_dwords == 1u && valid_group_width &&
		       memory.component_index < memory.component_count && !memory.typed &&
		       !memory.formatted && !memory.glc && !memory.slc && !memory.idxen && !memory.offen &&
		       memory.secondary_offset == 0u && memory.data_format == 0u &&
		       memory.number_format == 0u && (memory.offset & 3u) == 0u;
	}

	bool IsShaderBdaBufferHandle(const Inst& handle) const {
		if (handle.GetOpcode() != ValueOpcode::GetBufferResource || handle.NumArgs() != 4u) {
			return false;
		}
		for (const auto& use: handle.Uses()) {
			const auto* user = use.user;
			if (user == nullptr || user->GetOpcode() != ValueOpcode::ReadConstBuffer) {
				return false;
			}
			const auto flags = user->Flags<MemoryFlags>();
			if (flags.index >= m_program.memory_info.size() ||
			    !IsRawScalarBufferMemory(m_program.memory_info[flags.index])) {
				return false;
			}
		}
		return !handle.Uses().empty();
	}

	struct BdaCloneResult {
		Value value;
		bool  changed;
	};

	// Clone only the arithmetic envelope of a descriptor dword.  ReadConst nodes
	// that name raw address reads are replaced by a shader-side ReadConst clone;
	// all other values remain shared.  This keeps a slot that is also consumed by
	// an image or host descriptor on the ordinary flattened path while giving the
	// BDA handle a per-use expression.  PHIs and memory operations are not cloned
	// here; a descriptor containing either needs a separate structured lowering.
	std::optional<Value> CloneBdaExpression(Value value, Block& block, Block::iterator where,
	                                        std::unordered_map<const Inst*, BdaCloneResult>& cache,
	                                        std::unordered_set<const Inst*>& visiting,
	                                        bool&                            changed) {
		value = value.Resolve();
		if (value.IsImmediate()) return value;
		const auto* inst = value.TryInstruction();
		if (inst == nullptr) return std::nullopt;
		if (inst->GetOpcode() == ValueOpcode::ReadConst) {
			if (inst->NumArgs() != 2u) return value;
			const auto slot = inst->Arg(1).Resolve();
			if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
			    slot.U32() < m_program.srt_reads.size() &&
			    IsShaderSideRawRead(m_program.srt_reads[slot.U32()].value)) {
				changed = true;
				const auto clone =
				    block.PrependNewInst(where, ValueOpcode::ReadConst,
				                         {inst->Arg(0), inst->Arg(1)}, ShaderSideSrtReadFlag);
				const auto result = Value(&*clone);
				m_bda_srt_clones.insert(&*clone);
				cache.emplace(inst, BdaCloneResult {result, true});
				return result;
			}
			return value;
		}
		if (const auto it = cache.find(inst); it != cache.end()) {
			changed = changed || it->second.changed;
			return it->second.value;
		}
		if (!visiting.insert(inst).second) return std::nullopt;
		std::vector<Value> args;
		args.reserve(inst->NumArgs());
		bool local_changed = false;
		for (uint32_t index = 0; index < inst->NumArgs(); ++index) {
			bool       arg_changed = false;
			const auto arg =
			    CloneBdaExpression(inst->Arg(index), block, where, cache, visiting, arg_changed);
			if (!arg.has_value()) {
				visiting.erase(inst);
				return std::nullopt;
			}
			args.push_back(*arg);
			local_changed = local_changed || arg_changed;
		}
		visiting.erase(inst);
		if (!local_changed) {
			cache.emplace(inst, BdaCloneResult {value, false});
			return value;
		}
		// A PHI carries predecessor metadata that cannot be reconstructed from a
		// plain argument list.  The same applies to resource/memory instructions.
		if (inst->GetOpcode() == ValueOpcode::Phi ||
		    inst->GetOpcode() == ValueOpcode::GetBufferResource ||
		    inst->GetOpcode() == ValueOpcode::GetImageResource ||
		    inst->GetOpcode() == ValueOpcode::LoadAddressU32 ||
		    inst->GetOpcode() == ValueOpcode::ReadConstBuffer || args.size() > 8u) {
			return std::nullopt;
		}
		Inst* clone = nullptr;
		switch (args.size()) {
			case 0:
				clone =
				    &*block.PrependNewInst(where, inst->GetOpcode(), {}, inst->Flags<uint64_t>());
				break;
			case 1:
				clone = &*block.PrependNewInst(where, inst->GetOpcode(), {args[0]},
				                               inst->Flags<uint64_t>());
				break;
			case 2:
				clone = &*block.PrependNewInst(where, inst->GetOpcode(), {args[0], args[1]},
				                               inst->Flags<uint64_t>());
				break;
			case 3:
				clone = &*block.PrependNewInst(
				    where, inst->GetOpcode(), {args[0], args[1], args[2]}, inst->Flags<uint64_t>());
				break;
			case 4:
				clone = &*block.PrependNewInst(where, inst->GetOpcode(),
				                               {args[0], args[1], args[2], args[3]},
				                               inst->Flags<uint64_t>());
				break;
			case 5:
				clone = &*block.PrependNewInst(where, inst->GetOpcode(),
				                               {args[0], args[1], args[2], args[3], args[4]},
				                               inst->Flags<uint64_t>());
				break;
			case 6:
				clone =
				    &*block.PrependNewInst(where, inst->GetOpcode(),
				                           {args[0], args[1], args[2], args[3], args[4], args[5]},
				                           inst->Flags<uint64_t>());
				break;
			case 7:
				clone = &*block.PrependNewInst(
				    where, inst->GetOpcode(),
				    {args[0], args[1], args[2], args[3], args[4], args[5], args[6]},
				    inst->Flags<uint64_t>());
				break;
			case 8:
				clone = &*block.PrependNewInst(
				    where, inst->GetOpcode(),
				    {args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]},
				    inst->Flags<uint64_t>());
				break;
			default: return std::nullopt;
		}
		const auto result = Value(clone);
		cache.emplace(inst, BdaCloneResult {result, true});
		changed = true;
		return result;
	}

	bool ContainsLaneDependentAddress(Value                            value,
	                                  std::unordered_set<const Inst*>& visiting) const {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || !visiting.insert(inst).second) return false;
		const auto finish = [&](bool result) {
			visiting.erase(inst);
			return result;
		};
		if (inst->GetOpcode() == ValueOpcode::ReadLane) return finish(true);
		if (inst->GetOpcode() == ValueOpcode::GetBuiltin && inst->NumArgs() >= 2u) {
			const auto kind = inst->Arg(0).Resolve();
			if (kind.IsImmediate() && kind.GetType() == Type::U32 &&
			    (kind.U32() == static_cast<uint32_t>(StageInputKind::LocalInvocationId) ||
			     kind.U32() == static_cast<uint32_t>(StageInputKind::LocalInvocationIndex))) {
				return finish(true);
			}
		}
		for (uint32_t index = 0; index < inst->NumArgs(); ++index) {
			if (ContainsLaneDependentAddress(inst->Arg(index), visiting)) return finish(true);
		}
		return finish(false);
	}

	bool HasLaneDependentRawRead(Value value, std::unordered_set<const Inst*>& visiting) const {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || !visiting.insert(inst).second) return false;
		const auto finish = [&](bool result) {
			visiting.erase(inst);
			return result;
		};
		if (inst->GetOpcode() == ValueOpcode::ReadConst && inst->NumArgs() == 2u) {
			const auto slot = inst->Arg(1).Resolve();
			if (slot.IsImmediate() && slot.GetType() == Type::U32 &&
			    slot.U32() < m_program.srt_reads.size() &&
			    IsShaderSideRawRead(m_program.srt_reads[slot.U32()].value)) {
				std::unordered_set<const Inst*> address_visiting;
				return finish(ContainsLaneDependentAddress(m_program.srt_reads[slot.U32()].value,
				                                           address_visiting));
			}
		}
		for (uint32_t index = 0; index < inst->NumArgs(); ++index) {
			if (HasLaneDependentRawRead(inst->Arg(index), visiting)) return finish(true);
		}
		return finish(false);
	}

	bool LowerScalarBufferReadToBda(Inst& read, const MemoryInfo& memory, uint32_t pc) {
		if (read.GetOpcode() != ValueOpcode::ReadConstBuffer || !IsRawScalarBufferMemory(memory)) {
			return false;
		}
		if (read.NumArgs() != 2u || read.Parent() == nullptr) return false;
		auto* handle = read.Arg(0).ResolveInstruction();
		if (handle == nullptr || !IsShaderBdaBufferHandle(*handle)) {
			return false;
		}
		if (handle->Arg(0).GetType() != Type::U32 || handle->Arg(1).GetType() != Type::U32 ||
		    handle->Arg(2).GetType() != Type::U32) {
			return false;
		}
		std::unordered_set<const Inst*> raw_visiting;
		bool                            has_lane_dependent_raw = false;
		for (uint32_t word = 0; word < 3u; ++word) {
			has_lane_dependent_raw =
			    has_lane_dependent_raw || HasLaneDependentRawRead(handle->Arg(word), raw_visiting);
		}
		if (!has_lane_dependent_raw) return false;
		const bool already_lowered = m_shader_bda_handles.contains(handle);
		if (!already_lowered) {
			if (handle->Parent() == nullptr) return false;
			auto where = std::ranges::find_if(handle->Parent()->Instructions(),
			                                  [&](const Inst& inst) { return &inst == handle; });
			if (where == handle->Parent()->Instructions().end()) return false;
			std::unordered_map<const Inst*, BdaCloneResult> cache;
			std::unordered_set<const Inst*>                 visiting;
			bool                                            any_raw_word = false;
			for (uint32_t word = 0; word < 3u; ++word) {
				bool       changed = false;
				const auto clone   = CloneBdaExpression(handle->Arg(word), *handle->Parent(), where,
				                                        cache, visiting, changed);
				if (!clone.has_value()) return false;
				if (changed) handle->SetArg(word, *clone);
				any_raw_word = any_raw_word || changed;
			}
			// Keep ordinary scalar descriptors on the existing host-materialized path.
			if (!any_raw_word) return false;
		}

		auto& block = *read.Parent();
		auto  where = std::ranges::find_if(block.Instructions(),
		                                   [&](const Inst& inst) { return &inst == &read; });
		if (where == block.Instructions().end()) return false;
		auto insert = [&](ValueOpcode opcode, std::initializer_list<Value> args) {
			const auto it = block.PrependNewInst(where, opcode, args);
			where         = std::next(it);
			return Value(&*it);
		};

		const auto descriptor_high = handle->Arg(1);
		const auto stride_shifted =
		    insert(ValueOpcode::ShiftRightLogical32, {descriptor_high, Value(16u)});
		const auto stride = insert(ValueOpcode::BitwiseAnd32, {stride_shifted, Value(0x3fffu)});
		const auto stride_zero = insert(ValueOpcode::IEqual32, {stride, Value(0u)});
		const auto effective_stride =
		    insert(ValueOpcode::SelectU32, {stride_zero, Value(1u), stride});
		const auto stride_u64 =
		    insert(ValueOpcode::CompositeConstructU64, {effective_stride, Value(0u)});
		const auto records_u64 =
		    insert(ValueOpcode::CompositeConstructU64, {handle->Arg(2), Value(0u)});
		const auto size_u64 = insert(ValueOpcode::IMul64, {stride_u64, records_u64});
		const auto offset_aligned =
		    insert(ValueOpcode::BitwiseAnd32, {read.Arg(1), Value(~uint32_t {3u})});
		const auto effective_offset =
		    memory.offset == 0u
		        ? offset_aligned
		        : insert(ValueOpcode::IAdd32, {offset_aligned, Value(memory.offset)});
		const auto offset_u64 =
		    insert(ValueOpcode::CompositeConstructU64, {effective_offset, Value(0u)});
		const auto size_at_least_word =
		    insert(ValueOpcode::UGreaterThan64, {size_u64, Value(uint64_t {3u})});
		const auto size_minus_word = insert(ValueOpcode::ISub64, {size_u64, Value(uint64_t {4u})});
		const auto end_exclusive =
		    insert(ValueOpcode::IAdd64, {size_minus_word, Value(uint64_t {1u})});
		const auto offset_in_bounds = insert(ValueOpcode::ULessThan64, {offset_u64, end_exclusive});
		const auto in_bounds =
		    insert(ValueOpcode::LogicalAnd, {size_at_least_word, offset_in_bounds});

		const auto base_high = insert(ValueOpcode::BitwiseAnd32, {descriptor_high, Value(0xffffu)});
		const auto address   = insert(ValueOpcode::GetAddressResource, {handle->Arg(0), base_high});
		MemoryInfo address_memory      = memory;
		address_memory.kind            = ResourceKind::ScalarAddress;
		address_memory.resource        = 0u;
		address_memory.sampler         = 0u;
		address_memory.address_is_full = false;
		address_memory.planning_only   = false;
		const auto address_index       = static_cast<uint32_t>(m_program.memory_info.size());
		m_program.memory_info.push_back(address_memory);
		MemoryFlags address_flags {address_index, pc};
		const auto  load_it = block.PrependNewInst(where, ValueOpcode::LoadAddressU32,
		                                           {address, read.Arg(1), Value(0u), in_bounds});
		where               = std::next(load_it);
		auto& load          = *load_it;
		load.SetFlags(address_flags);
		read.ReplaceUsesWith(Value(&load));
		m_info.uses_dma = true;
		if (m_shader_bda_handles.insert(handle).second) {
			LOGF("ResourceTracking shader-side BDA scalar buffer hash=0x%016llx source_handle=%p "
			     "pc=0x%08x base=(dword0,dword1[15:0]) stride=dword1[29:16] records=dword2\n",
			     static_cast<unsigned long long>(m_program.shader_hash), static_cast<void*>(handle),
			     pc);
		}
		return true;
	}

	void Collect(Inst& inst) {
		if (BoundedRead(&inst) != nullptr ||
		    std::ranges::find(m_bounded_root_reads, &inst) != m_bounded_root_reads.end()) {
			return;
		}
		const auto op           = inst.GetOpcode();
		const auto buffer       = BufferAccessOf(op);
		const auto address_info = AddressOpcodeInfoOf(op);
		const auto image_info   = ImageOpcodeInfoOf(op);
		if (buffer == BufferAccess::None && address_info.access == AddressAccess::None &&
		    image_info.access == ImageAccess::None) {
			return;
		}
		const auto flags = inst.Flags<MemoryFlags>();
		if (flags.index >= m_program.memory_info.size()) {
			Fail(flags.pc, fmt::format("memory metadata index {} is out of range", flags.index));
		}
		if (inst.NumArgs() == 0) {
			Fail(flags.pc, "memory operation has no resource handle");
		}
		const auto& memory = m_program.memory_info[flags.index];
		// A scalar-buffer read that was retained as a planning root can still be
		// lowered when its descriptor has shader-side SRT dwords.  Try that path
		// before the generic planning-only early return; ordinary planning reads
		// remain host-materialized below.
		if (buffer != BufferAccess::None && op == ValueOpcode::ReadConstBuffer &&
		    LowerScalarBufferReadToBda(inst, memory, flags.pc)) {
			return;
		}
		if (memory.planning_only || IsIndirectPlanningMemory(flags.index)) {
			return;
		}
		Inst*    handle   = nullptr;
		uint32_t source   = 0;
		uint32_t resource = 0;

		if (buffer != BufferAccess::None) {
			GetHandle(inst.Arg(0), ValueOpcode::GetBufferResource, 4, flags.pc, handle, source);
			resource = AddBuffer(source, memory, op, flags.pc);
			if (resource == UINT32_MAX) {
				Fail(flags.pc, "buffer resource limit exceeded");
			}
			AddHandlePatch(handle, resource, flags.pc);
			AddMemoryPatch(flags.index, resource, 0, false, flags.pc);
			return;
		}
		if (address_info.access != AddressAccess::None) {
			if (!IsAddressResourceKind(memory.kind)) {
				Fail(flags.pc, "address operation has invalid resource kind");
			}
			if (memory.kind == ResourceKind::Scratch) {
				handle = inst.Arg(0).Resolve().TryInstruction();
				if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetScratchResource ||
				    handle->NumArgs() != 0) {
					Fail(flags.pc, "scratch operation requires GetScratchResource");
				}
				if (m_program.scratch_dwords == 0) {
					Fail(flags.pc, "scratch operation requires a nonzero AGC per-thread size");
				}
				return;
			}
			ValidateAddressHandle(inst.Arg(0), flags.pc);
			m_info.uses_dma = true;
			return;
		}

		if (memory.kind != ResourceKind::Image ||
		    image_info.resource_class == ImageResourceClass::None) {
			Fail(flags.pc, "image operation has invalid resource kind");
		}
		handle               = inst.Arg(0).Resolve().TryInstruction();
		const auto* indirect = handle != nullptr ? FindIndirectImage(*handle) : nullptr;
		if (indirect != nullptr) {
			source = indirect->source;
		} else {
			GetHandle(inst.Arg(0), ValueOpcode::GetImageResource, 8, flags.pc, handle, source);
		}
		resource = AddImage(source, memory, op, flags.pc);
		if (resource == UINT32_MAX) {
			Fail(flags.pc, "image resource limit exceeded");
		}
		AddHandlePatch(handle, resource, flags.pc);
		uint32_t sampler = 0;
		if (image_info.needs_sampler) {
			if (inst.NumArgs() < 2) {
				Fail(flags.pc, "sampled image operation has no sampler handle");
			}
			Inst*      sampler_handle = nullptr;
			uint32_t   sampler_source = 0;
			const bool sample_adjust =
			    (memory.image_sample_flags & Decoder::ImageSampleFlagAdjust) != 0;
			GetHandle(inst.Arg(1), ValueOpcode::GetSamplerResource, 4, flags.pc, sampler_handle,
			          sampler_source, true, sample_adjust);
			sampler = AddSampler(sampler_source, flags.pc);
			if (sampler == UINT32_MAX) {
				Fail(flags.pc, "sampler resource limit exceeded");
			}
			AddHandlePatch(sampler_handle, sampler, flags.pc);
			AddSampledPair(resource, sampler, flags.pc);
		}
		AddMemoryPatch(flags.index, resource, sampler, image_info.needs_sampler, flags.pc);
	}

	const DescriptorSource* Source(uint32_t source) const {
		return source < m_sources.size() ? &m_sources[source] : nullptr;
	}

	void LinkImageAliases() {
		for (auto& buffer: m_info.buffers) {
			const auto* buffer_source = Source(buffer.source);
			if (buffer_source == nullptr || buffer_source->dword_count != 4 ||
			    buffer_source->indirect_buffer.has_value() ||
			    buffer_source->bounded_buffer.has_value()) {
				continue;
			}
			for (uint32_t image = 0; image < m_info.images.size(); image++) {
				const auto* image_source = Source(m_info.images[image].source);
				if (image_source == nullptr || image_source->dword_count != 8 ||
				    image_source->indirect_image.has_value()) {
					continue;
				}
				bool alias = true;
				for (uint32_t dword = 0; dword < 4; dword++) {
					alias = alias && EquivalentValue(m_program, buffer_source->dwords[dword],
					                                 image_source->dwords[dword]);
				}
				if (alias) {
					buffer.image_alias = image;
					break;
				}
			}
		}
	}

	Program&                                  m_program;
	ShaderInfo                                m_info;
	std::vector<DescriptorSource>             m_sources;
	std::vector<BoundedSrtRead>               m_bounded_srt_reads;
	std::vector<BoundedReadPlan>              m_bounded_reads;
	std::vector<const Inst*>                  m_bounded_root_visited;
	std::vector<Inst*>                        m_bounded_root_reads;
	std::vector<BoundedBufferPlan>            m_bounded_buffers;
	std::vector<Value>                        m_bounded_selectors;
	std::vector<HandlePatch>                  m_handle_patches;
	std::vector<MemoryPatch>                  m_memory_patches;
	std::vector<IndirectImagePlan>            m_indirect_images;
	std::vector<std::pair<Inst*, uint32_t>>   m_indirect_buffers;
	std::unordered_set<Inst*>                 m_shader_bda_handles;
	std::unordered_set<Inst*>                 m_bda_srt_clones;
	uint32_t                                  m_trace_buffer_handles = 0;
	std::array<uint32_t, 3>                   m_local_size;
	uint32_t                                  m_shared_bytes;
	std::vector<DescriptorSource::IndexRange> m_shared_ranges;
};

} // namespace

void TrackResources(Program& program, std::array<uint32_t, 3> local_size, uint32_t shared_bytes) {
	Tracker(program, local_size, shared_bytes).Run();
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
