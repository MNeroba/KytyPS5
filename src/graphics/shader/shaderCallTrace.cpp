#include "graphics/shader/shaderCallTrace.h"

#include "graphics/shader/recompiler/ShaderSwapPcDiagnostic.h"
#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"
#include "graphics/shader/shader.h"
#include "kernel/memory.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Libs::Graphics {

namespace {

namespace Decoder = ShaderRecompiler::Decoder;

constexpr uint32_t Sop1Prefix = 0x17du;
constexpr uint32_t Sop1SetpcB64 = 0x20u;
constexpr uint32_t SCodeEnd = 0xbf9f0000u;
constexpr uint32_t SEndpgm = 0xbf810000u;

bool ContainsSwapPcEncoding(std::span<const uint32_t> code) {
	return std::any_of(code.begin(), code.end(), [](uint32_t word) {
		return (word & 0xc0000000u) == 0x80000000u && ((word >> 23u) & 0x7fu) == 0x7du &&
		       ((word >> 8u) & 0xffu) == 0x21u;
	});
}

bool ReadGuestWord(void*, uint64_t address, uint32_t* value) {
	return value != nullptr &&
	       (Libs::LibKernel::Memory::TryReadGpuCleanBacking(address, value, sizeof(*value)) ||
	        Libs::LibKernel::Memory::TryReadBacking(address, value, sizeof(*value)));
}

bool FindMappedShaderRange(void*, uint64_t address, uint64_t* range_base, uint64_t* range_size) {
	return ShaderFindMappedRange(address, range_base, range_size);
}

} // namespace

std::vector<IndirectCallSite> ResolveIndirectCalls(std::span<const uint32_t> code,
                                                   std::span<const uint32_t> user_data,
                                                   uint32_t                  user_data_base,
                                                   uint64_t                  shader_addr) {
	std::vector<IndirectCallSite> resolved;
	if (code.empty()) {
		return resolved;
	}
	if (!ContainsSwapPcEncoding(code)) {
		return resolved;
	}

	// Reuse the production branch-aware diagnostic walker for call-site discovery and scalar
	// provenance. In particular, do not decode arbitrary post-terminal padding merely because it
	// happens to contain an S_SWAPPC encoding.
	const ShaderRecompiler::SwapPcDiagnosticOptions options {
	    .shader_base      = shader_addr,
	    .user_data_base   = user_data_base,
	    .user_data        = user_data,
	    .read_u32         = ReadGuestWord,
	    .find_range       = FindMappedShaderRange,
	    .max_call_sites   = 64,
	    .max_callee_words = 0,
	};
	const auto records = ShaderRecompiler::ResolveSwapPcDiagnostics(code, options);
	resolved.reserve(records.size());
	for (const auto& record: records) {
		if (!record.target_known || !record.target_mapped || record.target_guest_va == 0) {
			continue;
		}
		resolved.push_back({.pc          = record.call_pc,
		                   .target_sgpr = record.source_sgpr,
		                   .return_sgpr = record.destination_sgpr,
		                   .handler     = record.target_guest_va});
	}
	return resolved;
}

std::span<const uint32_t> TrimToCode(std::span<const uint32_t> code, uint32_t return_sgpr) {
	for (size_t word = 0; word < code.size(); word++) {
		if (code[word] == SCodeEnd) {
			return code.first(word);
		}
	}
	for (size_t word = code.size(); word-- > 0;) {
		if (code[word] == SEndpgm) {
			return code.first(word + 1u);
		}
	}
	if (return_sgpr != UINT32_MAX) {
		for (size_t word = code.size(); word-- > 0;) {
			const auto candidate = code[word];
			if ((candidate >> 23u) == Sop1Prefix && ((candidate >> 8u) & 0xffu) == Sop1SetpcB64 &&
			    (candidate & 0xffu) == return_sgpr) {
				return code.first(word + 1u);
			}
		}
	}
	return code;
}

bool EndsWithReturn(std::span<const uint32_t> code, uint32_t return_sgpr) {
	if (code.empty()) {
		return false;
	}
	const auto last = code.back();
	return (last >> 23u) == Sop1Prefix && ((last >> 8u) & 0xffu) == Sop1SetpcB64 &&
	       (last & 0xffu) == return_sgpr;
}

namespace {

bool IsBranchOpcode(Decoder::Opcode opcode) {
	switch (opcode) {
		case Decoder::Opcode::S_BRANCH:
		case Decoder::Opcode::S_CBRANCH_SCC0:
		case Decoder::Opcode::S_CBRANCH_SCC1:
		case Decoder::Opcode::S_CBRANCH_VCCZ:
		case Decoder::Opcode::S_CBRANCH_VCCNZ:
		case Decoder::Opcode::S_CBRANCH_EXECZ:
		case Decoder::Opcode::S_CBRANCH_EXECNZ: return true;
		default: return false;
	}
}

} // namespace

std::vector<uint32_t> SpliceIndirectCalls(std::span<const uint32_t>                  code,
                                          std::span<const IndirectCallSite>          sites,
                                          std::span<const std::span<const uint32_t>> handlers) {
	if (sites.size() != handlers.size() || sites.empty()) {
		return {};
	}

	const auto base = TrimToCode(code);
	Decoder::Program program {};
	Decoder::DecodeProgram(base, program);
	if (program.instructions.empty()) {
		return {};
	}

	struct Insertion {
		uint32_t                  word = 0;
		std::span<const uint32_t> body;
	};
	std::vector<Insertion> insertions;
	insertions.reserve(sites.size());
	for (size_t index = 0; index < sites.size(); index++) {
		const auto& site    = sites[index];
		const auto  handler = handlers[index];
		if (site.handler == 0 || handler.empty() || !EndsWithReturn(handler, site.return_sgpr)) {
			return {};
		}
		const auto word = site.pc / 4u;
		if (word >= base.size()) {
			return {};
		}
		insertions.push_back({word, handler.first(handler.size() - 1u)});
	}
	std::sort(insertions.begin(), insertions.end(),
	          [](const Insertion& lhs, const Insertion& rhs) { return lhs.word < rhs.word; });

	std::vector<uint32_t> remap(base.size(), 0);
	std::vector<uint32_t> out;
	out.reserve(base.size() + 512u);
	size_t next = 0;
	for (const auto& insertion: insertions) {
		for (; next < insertion.word; next++) {
			remap[next] = static_cast<uint32_t>(out.size());
			out.push_back(base[next]);
		}
		remap[insertion.word] = static_cast<uint32_t>(out.size());
		out.insert(out.end(), insertion.body.begin(), insertion.body.end());
		next = insertion.word + 1u;
	}
	for (; next < base.size(); next++) {
		remap[next] = static_cast<uint32_t>(out.size());
		out.push_back(base[next]);
	}

	for (const auto& inst: program.instructions) {
		if (!IsBranchOpcode(inst.opcode)) {
			continue;
		}
		const auto from = inst.pc / 4u;
		const auto to   = inst.branch_target / 4u;
		if (from >= base.size() || to >= base.size()) {
			return {};
		}
		const auto new_from = remap[from];
		const auto new_to   = remap[to];
		const auto delta = static_cast<int64_t>(new_to) - static_cast<int64_t>(new_from) - 1;
		if (delta < INT16_MIN || delta > INT16_MAX || new_from >= out.size()) {
			return {};
		}
		out[new_from] = (out[new_from] & 0xffff0000u) |
		                static_cast<uint32_t>(static_cast<uint16_t>(delta));
	}
	return out;
}

} // namespace Libs::Graphics
