#include "graphics/shader/recompiler/ShaderSwapPcDiagnostic.h"

#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Libs::Graphics::ShaderRecompiler {
namespace {

using Decoder::Instruction;
using Decoder::Opcode;
using Decoder::Operand;
using Decoder::OperandKind;

constexpr uint32_t MaxScalarRegisters = 128u;

struct ScalarValue {
	bool             known            = false;
	uint32_t         value            = 0;
	uint32_t         writer_pc        = 0;
	uint32_t         writer_raw       = 0;
	SwapPcWriterKind writer_kind      = SwapPcWriterKind::Unknown;
	uint64_t         load_address     = 0;
	bool             load_known       = false;
	uint64_t         descriptor_base  = 0;
	bool             descriptor_known = false;
};

struct ScalarState {
	std::array<ScalarValue, MaxScalarRegisters> values {};
	std::array<ScalarValue, 2>                  vcc {};
	bool                                        scc_known = false;
	bool                                        scc       = false;
};

bool IsSwapPc(uint32_t word) {
	return (word & 0xc0000000u) == 0x80000000u && ((word >> 23u) & 0x7fu) == 0x7du &&
	       ((word >> 8u) & 0xffu) == 0x21u;
}

bool IsSetPc(uint32_t word) {
	return (word & 0xc0000000u) == 0x80000000u && ((word >> 23u) & 0x7fu) == 0x7du &&
	       ((word >> 8u) & 0xffu) == 0x20u;
}

bool AddSigned(uint64_t base, uint32_t offset, uint64_t* result) {
	if (result == nullptr) {
		return false;
	}
	const auto signed_offset = static_cast<int64_t>(static_cast<int32_t>(offset));
	if (signed_offset >= 0) {
		const auto amount = static_cast<uint64_t>(signed_offset);
		if (base > std::numeric_limits<uint64_t>::max() - amount) {
			return false;
		}
		*result = base + amount;
		return true;
	}
	const auto amount = static_cast<uint64_t>(-signed_offset);
	if (base < amount) {
		return false;
	}
	*result = base - amount;
	return true;
}

bool ReadScalar(const Operand& operand, const ScalarState& state, uint32_t* value) {
	if (value == nullptr) {
		return false;
	}
	if (operand.kind == OperandKind::Sgpr && operand.reg < MaxScalarRegisters) {
		const auto& source = state.values[operand.reg];
		if (!source.known) {
			return false;
		}
		*value = source.value;
		return true;
	}
	if (operand.kind == OperandKind::VccLo || operand.kind == OperandKind::VccHi) {
		const auto& source = state.vcc[operand.kind == OperandKind::VccLo ? 0 : 1];
		if (!source.known) {
			return false;
		}
		*value = source.value;
		return true;
	}
	if (operand.kind == OperandKind::IntegerInlineConstant ||
	    operand.kind == OperandKind::LiteralConstant) {
		*value = operand.value;
		return true;
	}
	return false;
}

bool ReadPair(const Operand& operand, const ScalarState& state, uint64_t* value) {
	if (value == nullptr) {
		return false;
	}
	if (operand.kind == OperandKind::VccLo) {
		if (!state.vcc[0].known || !state.vcc[1].known) {
			return false;
		}
		*value = static_cast<uint64_t>(state.vcc[0].value) |
		         (static_cast<uint64_t>(state.vcc[1].value) << 32u);
		return true;
	}
	if (operand.kind != OperandKind::Sgpr || operand.reg + 1u >= MaxScalarRegisters ||
	    !state.values[operand.reg].known || !state.values[operand.reg + 1u].known) {
		return false;
	}
	*value = static_cast<uint64_t>(state.values[operand.reg].value) |
	         (static_cast<uint64_t>(state.values[operand.reg + 1u].value) << 32u);
	return true;
}

void SetValue(ScalarState& state, uint32_t reg, uint32_t value, uint32_t pc, uint32_t raw,
              SwapPcWriterKind kind, uint64_t load_address = 0, bool load_known = false,
              uint64_t descriptor_base = 0, bool descriptor_known = false) {
	if (reg >= MaxScalarRegisters) {
		return;
	}
	state.values[reg] = {.known            = true,
	                     .value            = value,
	                     .writer_pc        = pc,
	                     .writer_raw       = raw,
	                     .writer_kind      = kind,
	                     .load_address     = load_address,
	                     .load_known       = load_known,
	                     .descriptor_base  = descriptor_base,
	                     .descriptor_known = descriptor_known};
}

void SetSpecialValue(ScalarState& state, const Operand& operand, uint32_t value, uint32_t pc,
                     uint32_t raw, SwapPcWriterKind kind, uint64_t load_address = 0,
                     bool load_known = false) {
	if (operand.kind == OperandKind::VccLo || operand.kind == OperandKind::VccHi) {
		state.vcc[operand.kind == OperandKind::VccLo ? 0 : 1] = {.known        = true,
		                                                         .value        = value,
		                                                         .writer_pc    = pc,
		                                                         .writer_raw   = raw,
		                                                         .writer_kind  = kind,
		                                                         .load_address = load_address,
		                                                         .load_known   = load_known};
		return;
	}
	if (operand.kind == OperandKind::Sgpr) {
		SetValue(state, operand.reg, value, pc, raw, kind, load_address, load_known);
	}
}

void InvalidateValue(ScalarState& state, uint32_t reg, uint32_t pc, uint32_t raw) {
	if (reg < MaxScalarRegisters) {
		state.values[reg] = {.writer_pc = pc, .writer_raw = raw};
	}
}

bool ReadMemory(void* userdata, const SwapPcDiagnosticOptions& options, uint64_t address,
                uint32_t* value) {
	return options.read_u32 != nullptr && options.read_u32(userdata, address, value);
}

void SetLoadedWords(ScalarState& state, const Instruction& inst, std::span<const uint32_t> words,
                    uint64_t address, SwapPcWriterKind kind, uint64_t descriptor_base = 0,
                    bool descriptor_known = false) {
	if (inst.dst.kind != OperandKind::Sgpr && inst.dst.kind != OperandKind::VccLo &&
	    inst.dst.kind != OperandKind::VccHi) {
		return;
	}
	if (inst.dst.kind == OperandKind::VccLo) {
		for (uint32_t i = 0; i < words.size() && i < 2u; i++) {
			SetSpecialValue(state, i == 0 ? inst.dst : Operand {.kind = OperandKind::VccHi},
			                words[i], inst.pc, inst.raw[0], kind, address, true);
		}
		return;
	}
	for (uint32_t i = 0; i < words.size() && inst.dst.reg + i < MaxScalarRegisters; i++) {
		SetValue(state, inst.dst.reg + i, words[i], inst.pc, inst.raw[0], kind, address, true,
		         descriptor_base, descriptor_known);
	}
}

bool LoadWords(ScalarState& state, const Instruction& inst, const SwapPcDiagnosticOptions& options,
               uint64_t address, uint32_t count, SwapPcWriterKind kind) {
	if (count == 0 || count > 16u ||
	    address > std::numeric_limits<uint64_t>::max() -
	                  static_cast<uint64_t>(count - 1u) * sizeof(uint32_t)) {
		if (inst.dst.kind == OperandKind::Sgpr) {
			InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
		}
		return false;
	}
	std::array<uint32_t, 16> values {};
	for (uint32_t i = 0; i < count; i++) {
		if (!ReadMemory(options.memory_userdata, options, address + i * sizeof(uint32_t),
		                &values[i])) {
			if (inst.dst.kind == OperandKind::Sgpr) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
			}
			return false;
		}
	}
	SetLoadedWords(state, inst, std::span(values.data(), count), address, kind);
	return true;
}

void ApplyArithmetic(ScalarState& state, const Instruction& inst) {
	if (inst.dst.kind != OperandKind::Sgpr || inst.dst.reg >= MaxScalarRegisters) {
		return;
	}
	uint32_t lhs = 0;
	uint32_t rhs = 0;
	if (!ReadScalar(inst.src0, state, &lhs) || !ReadScalar(inst.src1, state, &rhs)) {
		InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
		state.scc_known = false;
		return;
	}
	uint32_t result = 0;
	bool     flag   = false;
	switch (inst.opcode) {
		case Opcode::S_ADD_U32: {
			const uint64_t sum = static_cast<uint64_t>(lhs) + rhs;
			result             = static_cast<uint32_t>(sum);
			flag               = (sum >> 32u) != 0;
			break;
		}
		case Opcode::S_ADDC_U32: {
			if (!state.scc_known) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
				state.scc_known = false;
				return;
			}
			const uint64_t sum = static_cast<uint64_t>(lhs) + rhs + (state.scc ? 1u : 0u);
			result             = static_cast<uint32_t>(sum);
			flag               = (sum >> 32u) != 0;
			break;
		}
		case Opcode::S_SUB_U32: {
			result = lhs - rhs;
			flag   = lhs < rhs;
			break;
		}
		case Opcode::S_SUBB_U32: {
			if (!state.scc_known) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
				state.scc_known = false;
				return;
			}
			const uint64_t sub = static_cast<uint64_t>(rhs) + (state.scc ? 1u : 0u);
			result             = lhs - static_cast<uint32_t>(sub);
			flag               = static_cast<uint64_t>(lhs) < sub;
			break;
		}
		case Opcode::S_AND_B32: result = lhs & rhs; break;
		case Opcode::S_OR_B32: result = lhs | rhs; break;
		case Opcode::S_XOR_B32: result = lhs ^ rhs; break;
		default: return;
	}
	SetValue(state, inst.dst.reg, result, inst.pc, inst.raw[0], SwapPcWriterKind::Unknown);
	state.scc_known = true;
	state.scc       = flag;
}

void ApplyInstruction(ScalarState& state, const Instruction& inst,
                      const SwapPcDiagnosticOptions& options) {
	if (inst.opcode == Opcode::S_MOV_B32 &&
	    (inst.dst.kind == OperandKind::Sgpr || inst.dst.kind == OperandKind::VccLo ||
	     inst.dst.kind == OperandKind::VccHi)) {
		uint32_t value = 0;
		if (ReadScalar(inst.src0, state, &value)) {
			const auto kind = inst.src0.kind == OperandKind::LiteralConstant
			                      ? SwapPcWriterKind::MoveImmediate
			                      : SwapPcWriterKind::MovePair;
			SetSpecialValue(state, inst.dst, value, inst.pc, inst.raw[0], kind);
		} else {
			InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
		}
		return;
	}
	if (inst.opcode == Opcode::S_MOV_B64 && inst.dst.kind == OperandKind::Sgpr) {
		uint64_t value = 0;
		if (ReadPair(inst.src0, state, &value)) {
			SetValue(state, inst.dst.reg, static_cast<uint32_t>(value), inst.pc, inst.raw[0],
			         SwapPcWriterKind::MovePair);
			SetValue(state, inst.dst.reg + 1u, static_cast<uint32_t>(value >> 32u), inst.pc,
			         inst.raw[0], SwapPcWriterKind::MovePair);
		} else {
			InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
			InvalidateValue(state, inst.dst.reg + 1u, inst.pc, inst.raw[0]);
		}
		return;
	}
	if (inst.opcode == Opcode::S_ADD_U32 || inst.opcode == Opcode::S_ADDC_U32 ||
	    inst.opcode == Opcode::S_SUB_U32 || inst.opcode == Opcode::S_SUBB_U32 ||
	    inst.opcode == Opcode::S_AND_B32 || inst.opcode == Opcode::S_OR_B32 ||
	    inst.opcode == Opcode::S_XOR_B32) {
		ApplyArithmetic(state, inst);
		return;
	}

	uint64_t base = 0;
	if (inst.opcode == Opcode::S_LOAD_DWORD || inst.opcode == Opcode::S_LOAD_DWORDX2 ||
	    inst.opcode == Opcode::S_LOAD_DWORDX4) {
		if (!ReadPair(inst.src0, state, &base) || !AddSigned(base, inst.offset, &base)) {
			if (inst.dst.kind == OperandKind::Sgpr) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
			}
			return;
		}
		const uint32_t count = inst.opcode == Opcode::S_LOAD_DWORD
		                           ? 1u
		                           : (inst.opcode == Opcode::S_LOAD_DWORDX2 ? 2u : 4u);
		const auto     kind  = count == 1u   ? SwapPcWriterKind::ScalarLoad
		                       : count == 2u ? SwapPcWriterKind::ScalarLoadPair
		                                     : SwapPcWriterKind::ScalarLoadQuad;
		LoadWords(state, inst, options, base, count, kind);
		return;
	}

	if (inst.opcode == Opcode::S_BUFFER_LOAD_DWORD ||
	    inst.opcode == Opcode::S_BUFFER_LOAD_DWORDX2) {
		uint64_t descriptor_base = 0;
		if (!ReadPair(inst.src0, state, &descriptor_base)) {
			if (inst.dst.kind == OperandKind::Sgpr) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
			}
			return;
		}
		uint64_t address = 0;
		if (!AddSigned(descriptor_base, inst.offset, &address)) {
			if (inst.dst.kind == OperandKind::Sgpr) {
				InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
			}
			return;
		}
		const uint32_t count = inst.opcode == Opcode::S_BUFFER_LOAD_DWORD ? 1u : 2u;
		const auto     kind =
            count == 1u ? SwapPcWriterKind::BufferLoad : SwapPcWriterKind::BufferLoadPair;
		std::array<uint32_t, 2> values {};
		if (count > values.size()) {
			return;
		}
		for (uint32_t i = 0; i < count; i++) {
			if (!ReadMemory(options.memory_userdata, options, address + i * sizeof(uint32_t),
			                &values[i])) {
				if (inst.dst.kind == OperandKind::Sgpr) {
					InvalidateValue(state, inst.dst.reg, inst.pc, inst.raw[0]);
				}
				return;
			}
		}
		SetLoadedWords(state, inst, std::span(values.data(), count), address, kind, descriptor_base,
		               true);
	}
}

void InitializeUserData(ScalarState& state, const SwapPcDiagnosticOptions& options) {
	for (uint32_t i = 0;
	     i < options.user_data.size() && options.user_data_base + i < MaxScalarRegisters; i++) {
		SetValue(state, options.user_data_base + i, options.user_data[i], 0, 0,
		         SwapPcWriterKind::UserData);
	}
}

void CaptureCallee(SwapPcDiagnosticRecord& record, const SwapPcDiagnosticOptions& options) {
	if (!record.target_known || options.read_u32 == nullptr) {
		return;
	}
	uint64_t range_base = 0;
	uint64_t range_size = 0;
	if (options.find_range != nullptr &&
	    options.find_range(options.memory_userdata, record.target_guest_va, &range_base,
	                       &range_size) &&
	    range_size != 0 && record.target_guest_va >= range_base &&
	    record.target_guest_va - range_base < range_size) {
		record.target_mapped   = true;
		record.allocation_base = range_base;
		record.allocation_size = range_size;
	}

	const uint32_t limit = options.max_callee_words == 0 ? 64u : options.max_callee_words;
	for (uint32_t i = 0; i < limit; i++) {
		if (record.target_guest_va >
		    std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(i) * sizeof(uint32_t)) {
			break;
		}
		if (record.target_mapped &&
		    static_cast<uint64_t>(i) * sizeof(uint32_t) >=
		        record.allocation_base + record.allocation_size - record.target_guest_va) {
			break;
		}
		uint32_t word = 0;
		if (!ReadMemory(options.memory_userdata, options,
		                record.target_guest_va + static_cast<uint64_t>(i) * sizeof(uint32_t),
		                &word)) {
			break;
		}
		record.callee_words.push_back(word);
		if (!record.return_found && IsSetPc(word)) {
			record.return_found        = true;
			record.return_pc           = record.target_guest_va + static_cast<uint64_t>(i) * 4u;
			record.return_raw          = word;
			record.return_source_sgpr  = word & 0xffu;
			record.return_pair_matches = record.return_source_sgpr == record.source_sgpr;
		}
	}
	if (!record.target_mapped && !record.callee_words.empty()) {
		record.target_mapped = true;
	}
}

SwapPcDiagnosticRecord MakeRecord(const ScalarState& state, const Instruction& call,
                                  const SwapPcDiagnosticOptions& options) {
	SwapPcDiagnosticRecord record;
	record.call_pc          = call.pc;
	record.call_raw         = call.raw[0];
	record.source_sgpr      = call.raw[0] & 0xffu;
	record.destination_sgpr = (call.raw[0] >> 16u) & 0x7fu;
	if (record.source_sgpr >= MaxScalarRegisters) {
		return record;
	}
	const auto& low         = state.values[record.source_sgpr];
	const auto& high        = record.source_sgpr + 1u < MaxScalarRegisters
	                              ? state.values[record.source_sgpr + 1u]
	                              : ScalarValue {};
	record.source_known     = low.known && high.known;
	record.writer_pair_same = record.source_known && low.writer_pc == high.writer_pc &&
	                          low.writer_raw == high.writer_raw &&
	                          low.writer_kind != SwapPcWriterKind::Unknown;
	record.writer_pc    = low.writer_pc;
	record.writer_raw   = low.writer_raw;
	record.writer_kind  = low.writer_kind;
	record.target_lo    = low.value;
	record.target_hi    = high.value;
	record.target_known = record.source_known;
	if (record.target_known) {
		record.target_guest_va = static_cast<uint64_t>(record.target_lo) |
		                         (static_cast<uint64_t>(record.target_hi) << 32u);
	}
	if (low.load_known) {
		record.effective_load_address = low.load_address;
		record.effective_load_known   = true;
	}
	if (record.writer_kind == SwapPcWriterKind::BufferLoad ||
	    record.writer_kind == SwapPcWriterKind::BufferLoadPair) {
		record.descriptor_address = low.descriptor_base;
		record.descriptor_known   = low.descriptor_known;
	}
	CaptureCallee(record, options);
	return record;
}

} // namespace

std::vector<SwapPcDiagnosticRecord>
ResolveSwapPcDiagnostics(std::span<const uint32_t> code, const SwapPcDiagnosticOptions& options) {
	std::vector<SwapPcDiagnosticRecord> records;
	if (code.empty() || options.max_call_sites == 0) {
		return records;
	}
	ScalarState state;
	InitializeUserData(state, options);
	uint32_t       word_index = 0;
	uint32_t       steps      = 0;
	const uint32_t max_steps  = static_cast<uint32_t>(
        std::min<size_t>(code.size() * 2u, std::numeric_limits<uint32_t>::max()));
	while (word_index < code.size() && steps++ < max_steps) {
		const uint32_t raw = code[word_index];
		if (IsSwapPc(raw)) {
			Instruction call;
			call.pc         = word_index * sizeof(uint32_t);
			call.word_count = 1;
			call.raw[0]     = raw;
			records.push_back(MakeRecord(state, call, options));
			if (records.size() >= options.max_call_sites) {
				break;
			}
			word_index++;
			continue;
		}

		const auto family = Decoder::GetInstructionFamily(raw);
		if (family == Decoder::Family::Unknown) {
			word_index++;
			continue;
		}
		Instruction inst;
		Decoder::DecodeInstruction(code, word_index, inst);
		if (inst.word_count == 0 || inst.word_count > code.size() - word_index) {
			word_index++;
			continue;
		}
		ApplyInstruction(state, inst, options);
		word_index += inst.word_count;
		// The diagnostic walk is intentionally read-only and must follow the same
		// executable boundary as the production decoder.  Post-END padding and
		// embedded metadata are not instructions and can contain reserved source
		// fields that would otherwise make this diagnostic path fail-fast.
		if (inst.opcode == Opcode::S_ENDPGM) {
			break;
		}
	}
	return records;
}

} // namespace Libs::Graphics::ShaderRecompiler
