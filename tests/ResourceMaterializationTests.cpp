#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_set>

namespace {

void Check(bool value, const char *text) {
  if (!value) {
    std::fprintf(stderr, "ResourceMaterializationTests: failed: %s\n", text);
    std::abort();
  }
}

bool RejectSpecializationRead(void *userdata, uint64_t, uint32_t *) {
  ++*static_cast<uint32_t *>(userdata);
  return false;
}

struct SelectMemoryProbe {
  uint64_t valid_address = 0;
  uint32_t valid_value = 0;
  uint32_t calls = 0;
  uint32_t rejected_calls = 0;
};

bool ReadSelectMemory(void *userdata, uint64_t address, uint32_t *value) {
  auto &probe = *static_cast<SelectMemoryProbe *>(userdata);
  ++probe.calls;
  if (address == probe.valid_address) {
    *value = probe.valid_value;
    return true;
  }
  ++probe.rejected_calls;
  return false;
}

Libs::Graphics::ShaderRecompiler::IR::Block &
AddValueBlock(Libs::Graphics::ShaderRecompiler::IR::Program &program) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto block = std::make_unique<Block>();
  auto *result = block.get();
  program.blocks.push_back(result);
  program.block_info.push_back({.id = 0});
  program.block_storage.push_back(std::move(block));
  return *result;
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan SrtPlan(uint64_t address) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  auto &value_block = AddValueBlock(program);

  MemoryInfo memory;
  memory.kind = ResourceKind::ScalarAddress;
  memory.planning_only = true;
  program.memory_info.push_back(memory);
  const auto low = Value(static_cast<uint32_t>(address));
  const auto high = Value(static_cast<uint32_t>(address >> 32u));
  auto &handle =
      value_block.AppendNewInst(ValueOpcode::GetAddressResource, {low, high});
  auto &raw = value_block.AppendNewInst(
      ValueOpcode::LoadAddressU32,
      {Value(&handle), Value(0u), Value(0u), Value(true)});
  raw.SetFlags(MemoryFlags{.index = 0, .pc = 0x40});
  program.srt_reads.push_back({Value(&raw), 0});

  auto &srt = value_block.AppendNewInst(ValueOpcode::GetSrtResource);
  auto &flat = value_block.AppendNewInst(ValueOpcode::ReadConst,
                                         {Value(&srt), Value(0u)});
  DescriptorSource source;
  source.dwords[0] = Value(&flat);
  source.dwords[1] = Value(0u);
  source.dword_count = 2;
  program.descriptor_sources.push_back(source);
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan UnbasedFlatPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  AddValueBlock(program);
  program.info.uses_dma = true;
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan UserDataBufferPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  auto &value_block = AddValueBlock(program);

  auto &user_data = value_block.AppendNewInst(
      ValueOpcode::GetUserData, {Value(static_cast<ScalarReg>(0))});
  DescriptorSource source;
  source.dwords[0] = Value(&user_data);
  source.dwords[1] = Value(0u);
  source.dwords[2] = Value(0u);
  source.dwords[3] = Value(0u);
  source.dword_count = 4;
  program.descriptor_sources.push_back(source);
  program.info.buffers.push_back({.source = 0});
  return ExtractResourcePlan(program);
}

Libs::Graphics::ShaderRecompiler::IR::ResourcePlan MixedSamplerPlan() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  AddValueBlock(program);

  const auto AddSource = [&program](uint32_t dword_count, uint32_t first) {
    DescriptorSource source;
    source.dword_count = dword_count;
    source.dwords[0] = Value(first);
    for (uint32_t i = 1; i < dword_count; i++) {
      source.dwords[i] = Value(0u);
    }
    program.descriptor_sources.push_back(source);
    return static_cast<uint32_t>(program.descriptor_sources.size() - 1u);
  };

  const auto image0 = AddSource(8, 0);
  const auto image1 = AddSource(8, 0);
  const auto sampler0 = AddSource(4, 0x11111111u);
  const auto sampler1 = AddSource(4, 0x22222222u);
  program.info.images.push_back(
      {.source = image0,
       .resource_class = ImageResourceClass::Sampled,
       .numeric_class = Libs::Graphics::Prospero::TextureNumericClass::Float,
       .dimension =
           Libs::Graphics::ShaderRecompiler::Decoder::ImageDimension::Dim2D});
  program.info.images.push_back(
      {.source = image1,
       .resource_class = ImageResourceClass::Sampled,
       .numeric_class = Libs::Graphics::Prospero::TextureNumericClass::Float,
       .dimension =
           Libs::Graphics::ShaderRecompiler::Decoder::ImageDimension::Dim2D,
       .conversion_format =
           Libs::Graphics::Prospero::BufferFormat::k8_8_8_8UNorm});
  program.info.samplers.push_back({.source = sampler0});
  program.info.samplers.push_back({.source = sampler1});
  program.info.sampled_pairs.push_back({.image = 0, .sampler = 0});
  program.info.sampled_pairs.push_back({.image = 0, .sampler = 1});
  program.info.sampled_pairs.push_back({.image = 1, .sampler = 1});
  return ExtractResourcePlan(program);
}

void TestMappedSrtUsesDirectReaderByDefault() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  const uint32_t dword = 0x12345678;
  auto plan = SrtPlan(reinterpret_cast<uint64_t>(&dword));
  uint32_t specialization_reads = 0;
  const SrtRuntime runtime{.userdata = &specialization_reads,
                           .read_specialization_memory =
                               RejectSpecializationRead};
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, runtime, snapshot, specialization),
        "mapped SRT stage materialization failed");
  Check(specialization_reads == 0,
        "ordinary SRT read used the specialization reader");
  Check(snapshot.flattened_srt.size() == 1 &&
            snapshot.flattened_srt[0] == dword,
        "cache rematerialization did not use the direct reader by default");
}

void TestIntegerRuntimeValueFollowsSrtReads() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = SrtPlan(0x10000);
  const auto root = plan.descriptor_sources.front().dwords[0];
  Check(ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "integer SRT read was rejected");

  Block values;
  auto &comparison = values.AppendNewInst(ValueOpcode::FPOrdLessThanEqual32,
                                          {Value::F32(1.f), Value::F32(0.f)});
  auto &selection = values.AppendNewInst(
      ValueOpcode::SelectU32, {Value(&comparison), Value(1u), Value(0u)});
  plan.srt_reads[0].value = Value(&selection);
  Check(ValidateRuntimeValue(plan, root),
        "ordinary SRT validation rejected a floating-point dependency");
  Check(!ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "integer SRT validation missed a hidden floating-point dependency");

  auto &first =
      values.AppendNewInst(ValueOpcode::ReadFirstLane, {root, Value(true)});
  Check(!ValidateRuntimeValue(plan, Value(&first), RuntimeValueType::Integer),
        "read-first-lane lost integer-only SRT validation");

  auto &active = values.AppendNewInst(ValueOpcode::ReadFirstLane,
                                      {Value(&selection), Value(&comparison)});
  Check(!ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "floating-point execution mask was accepted as integer-only");

  auto &lane = values.AppendNewInst(
      ValueOpcode::GetBuiltin,
      {Value(static_cast<uint32_t>(StageInputKind::LocalInvocationId)),
       Value(0u)});
  auto &mask =
      values.AppendNewInst(ValueOpcode::INotEqual32, {Value(&lane), Value(0u)});
  selection.SetArg(0, Value(&mask));
  active.SetArg(1, Value(&mask));
  Check(ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "nonuniform integer execution mask was rejected");
  auto &float_value =
      values.AppendNewInst(ValueOpcode::BitCastU32F32, {Value::F32(1.f)});
  selection.SetArg(2, Value(&float_value));
  Check(!ValidateRuntimeValue(plan, Value(&active), RuntimeValueType::Integer),
        "floating-point inactive arm was accepted as integer-only");

  plan.srt_reads[0].value = Value(&first);
  Check(!ValidateRuntimeValue(plan, root, RuntimeValueType::Integer),
        "cyclic SRT read-first-lane dependency was accepted");
}

void TestSelectEvaluationIsLazy() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = SrtPlan(0x10000);

  const auto AddScalarRead = [&](uint64_t address, uint32_t pc) {
    auto &handle =
        plan.value_storage.emplace_back(ValueOpcode::GetAddressResource);
    handle.SetArg(0, Value(static_cast<uint32_t>(address)));
    handle.SetArg(1, Value(static_cast<uint32_t>(address >> 32u)));
    auto &read = plan.value_storage.emplace_back(ValueOpcode::LoadAddressU32);
    read.SetFlags(MemoryFlags{.index = 0, .pc = pc});
    read.SetArg(0, Value(&handle));
    read.SetArg(1, Value(0u));
    read.SetArg(2, Value(0u));
    read.SetArg(3, Value(true));
    return Value(&read);
  };

  // The inactive arm deliberately points at a location that the test reader
  // rejects.  An eager Select implementation would visit it and fail the whole
  // materialization.
  const auto rejected = AddScalarRead(0xdead0000u, 0x100u);
  const auto selected = AddScalarRead(0x20000u, 0x104u);
  auto &selection = plan.value_storage.emplace_back(ValueOpcode::SelectU32);
  selection.SetArg(0, Value(false));
  selection.SetArg(1, rejected);
  selection.SetArg(2, selected);

  const std::array values{Value(&selection)};
  std::array<uint32_t, 1> results{};
  SelectMemoryProbe probe{.valid_address = 0x20000u,
                          .valid_value = 0x12345678u};
  const SrtRuntime runtime{.read_memory = ReadSelectMemory,
                           .userdata = &probe,
                           .read_specialization_memory = ReadSelectMemory};
  Check(EvaluateUniformValues(plan, values, runtime, results),
        "lazy Select evaluation visited the inactive scalar read");
  Check(results[0] == probe.valid_value && probe.calls == 1u &&
            probe.rejected_calls == 0u,
        "lazy Select evaluation read the wrong descriptor arm");
}

void TestRuntimePhiSelectNormalDiamond() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  using namespace Libs::Graphics::ShaderRecompiler;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;
  program.user_data_base = 0;
  program.user_data_count = 1;

  for (uint32_t id = 0; id < 5; ++id) {
    program.block_storage.push_back(std::make_unique<Block>());
    program.blocks.push_back(program.block_storage.back().get());
    program.block_info.push_back({.id = id});
  }
  auto *entry = program.blocks[0];
  auto *split = program.blocks[1];
  auto *left = program.blocks[2];
  auto *right = program.blocks[3];
  auto *merge = program.blocks[4];

  program.block_info[0].terminator.kind = CFG::TerminatorKind::Branch;
  program.block_info[0].terminator.true_block = 1;
  program.block_info[1].terminator.kind =
      CFG::TerminatorKind::ConditionalBranch;
  program.block_info[1].terminator.true_block = 2;
  program.block_info[1].terminator.false_block = 3;
  program.block_info[2].terminator.kind = CFG::TerminatorKind::Branch;
  program.block_info[2].terminator.true_block = 4;
  program.block_info[3].terminator.kind = CFG::TerminatorKind::Branch;
  program.block_info[3].terminator.true_block = 4;
  program.block_info[4].terminator.kind = CFG::TerminatorKind::Return;

  entry->AddBranch(split);
  split->AddBranch(left);
  split->AddBranch(right);
  left->AddBranch(merge);
  right->AddBranch(merge);

  auto &user_data = split->AppendNewInst(ValueOpcode::GetUserData,
                                         {Value(static_cast<ScalarReg>(0))});
  auto &condition = split->AppendNewInst(ValueOpcode::INotEqual32,
                                         {Value(&user_data), Value(0u)});
  program.block_info[1].condition = Value(&condition);
  auto &phi = merge->AppendNewInst(ValueOpcode::Phi, {},
                                   static_cast<uint64_t>(Type::U32));
  // Deliberately store the false predecessor first. RuntimePhiSelect must use
  // the CFG successor identity rather than assuming PHI operand order.
  phi.AddPhiOperand(right, Value(0x22222222u));
  phi.AddPhiOperand(left, Value(0x11111111u));

  MemoryInfo memory;
  memory.kind = ResourceKind::ScalarAddress;
  memory.planning_only = true;
  program.memory_info.push_back(memory);
  auto &handle = merge->AppendNewInst(ValueOpcode::GetAddressResource,
                                      {Value(&phi), Value(0u)});
  auto &read =
      merge->AppendNewInst(ValueOpcode::LoadAddressU32,
                           {Value(&handle), Value(0u), Value(0u), Value(true)});
  read.SetFlags(MemoryFlags{.index = 0, .pc = 0x100});
  program.srt_reads.push_back({Value(&read), 0});

  auto plan = ExtractResourcePlan(program);
  const auto *cloned_read = plan.srt_reads[0].value.Resolve().TryInstruction();
  Check(cloned_read != nullptr && cloned_read->NumArgs() != 0,
        "normal diamond SRT read was not cloned");
  const auto *cloned_handle = cloned_read->Arg(0).ResolveInstruction();
  Check(cloned_handle != nullptr &&
            cloned_handle->GetOpcode() == ValueOpcode::GetAddressResource,
        "normal diamond address handle was not cloned");
  const auto *selected = cloned_handle->Arg(0).ResolveInstruction();
  Check(selected != nullptr &&
            selected->GetOpcode() == ValueOpcode::SelectU32 &&
            selected->NumArgs() == 3u,
        "normal diamond PHI was not lowered to a runtime Select");
  Check(selected->Arg(1).Resolve().IsImmediate() &&
            selected->Arg(1).Resolve().U32() == 0x11111111u &&
            selected->Arg(2).Resolve().IsImmediate() &&
            selected->Arg(2).Resolve().U32() == 0x22222222u,
        "normal diamond true/false PHI operands were swapped");
}

Libs::Graphics::ShaderRecompiler::IR::Program
BuildNestedRuntimePhiProgram(bool ambiguous_inner) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  using namespace Libs::Graphics::ShaderRecompiler;
  Program program;
  program.stage = Libs::Graphics::ShaderType::Compute;
  program.srt_plan_complete = true;
  program.resource_tracking_complete = true;

  constexpr uint32_t block_count = 13;
  for (uint32_t id = 0; id < block_count; ++id) {
    program.block_storage.push_back(std::make_unique<Block>());
    program.blocks.push_back(program.block_storage.back().get());
    program.block_info.push_back({.id = id,
                                  .start_pc = 0x100u + id * 0x10u,
                                  .end_pc = 0x110u + id * 0x10u});
  }
  auto *entry = program.blocks[0];
  auto *outer_split = program.blocks[1];
  auto *outer_true = program.blocks[2];
  auto *outer_false = program.blocks[3];
  auto *true_split = program.blocks[4];
  auto *false_split = program.blocks[5];
  auto *true_a = program.blocks[6];
  auto *true_b = program.blocks[7];
  auto *true_merge = program.blocks[8];
  auto *false_a = program.blocks[9];
  auto *false_b = program.blocks[10];
  auto *false_merge = program.blocks[11];
  auto *outer_merge = program.blocks[12];

  auto &entry_info = program.block_info[0];
  entry_info.terminator.kind = CFG::TerminatorKind::Branch;
  entry_info.terminator.true_block = 1;
  auto &outer_info = program.block_info[1];
  outer_info.terminator.kind = CFG::TerminatorKind::ConditionalBranch;
  outer_info.terminator.true_block = 2;
  outer_info.terminator.false_block = 3;
  auto &outer_true_info = program.block_info[2];
  outer_true_info.terminator.kind = CFG::TerminatorKind::Branch;
  outer_true_info.terminator.true_block = 12;
  auto &outer_false_info = program.block_info[3];
  outer_false_info.terminator.kind = CFG::TerminatorKind::Branch;
  outer_false_info.terminator.true_block = 12;
  auto &true_split_info = program.block_info[4];
  true_split_info.terminator.kind = CFG::TerminatorKind::ConditionalBranch;
  true_split_info.terminator.true_block = 6;
  true_split_info.terminator.false_block = 7;
  auto &false_split_info = program.block_info[5];
  false_split_info.terminator.kind = CFG::TerminatorKind::ConditionalBranch;
  false_split_info.terminator.true_block = 9;
  false_split_info.terminator.false_block = 10;
  for (const uint32_t id : {6u, 7u}) {
    program.block_info[id].terminator.kind = CFG::TerminatorKind::Branch;
    program.block_info[id].terminator.true_block = 8;
  }
  for (const uint32_t id : {9u, 10u}) {
    program.block_info[id].terminator.kind = CFG::TerminatorKind::Branch;
    program.block_info[id].terminator.true_block = 11;
  }
  program.block_info[8].terminator.kind = CFG::TerminatorKind::Return;
  program.block_info[11].terminator.kind = CFG::TerminatorKind::Return;
  program.block_info[12].terminator.kind = CFG::TerminatorKind::Return;

  entry->AddBranch(outer_split);
  outer_split->AddBranch(outer_true);
  outer_split->AddBranch(outer_false);
  true_split->AddBranch(true_a);
  true_split->AddBranch(true_b);
  true_a->AddBranch(true_merge);
  true_b->AddBranch(true_merge);
  false_split->AddBranch(false_a);
  false_split->AddBranch(false_b);
  false_a->AddBranch(false_merge);
  false_b->AddBranch(false_merge);
  // The nested diamonds are separate CFG components whose values are consumed
  // by the two trivial outer arms.  This keeps the outer PHI a normal diamond
  // while still exercising recursive PHI lowering in each arm expression.
  outer_true->AddBranch(outer_merge);
  outer_false->AddBranch(outer_merge);

  auto &outer_condition = outer_split->AppendNewInst(ValueOpcode::INotEqual32,
                                                     {Value(1u), Value(0u)});
  auto &true_condition = true_split->AppendNewInst(ValueOpcode::INotEqual32,
                                                   {Value(2u), Value(0u)});
  auto &false_condition = false_split->AppendNewInst(ValueOpcode::INotEqual32,
                                                     {Value(3u), Value(0u)});
  outer_info.condition = Value(&outer_condition);
  true_split_info.condition = Value(&true_condition);
  false_split_info.condition = Value(&false_condition);

  auto &true_phi = true_merge->AppendNewInst(ValueOpcode::Phi, {},
                                             static_cast<uint64_t>(Type::U32));
  true_phi.AddPhiOperand(true_a, Value(0x11111111u));
  true_phi.AddPhiOperand(true_b, Value(0x22222222u));
  auto &true_select = outer_true->AppendNewInst(
      ValueOpcode::SelectU32,
      {Value(&true_condition), Value(&true_phi), Value(0x33333333u)});

  auto &false_phi = false_merge->AppendNewInst(
      ValueOpcode::Phi, {}, static_cast<uint64_t>(Type::U32));
  false_phi.AddPhiOperand(false_a, Value(0x44444444u));
  false_phi.AddPhiOperand(false_b, Value(0x55555555u));
  auto &false_select = outer_false->AppendNewInst(
      ValueOpcode::SelectU32,
      {Value(&false_condition), Value(&false_phi), Value(0x66666666u)});

  auto &outer_phi = outer_merge->AppendNewInst(
      ValueOpcode::Phi, {}, static_cast<uint64_t>(Type::U32));
  outer_phi.AddPhiOperand(outer_true, Value(&true_select));
  outer_phi.AddPhiOperand(outer_false, Value(&false_select));

  if (ambiguous_inner) {
    // Add a second conditional split feeding the same inner arm blocks.  Both
    // candidate splits now have cross-edges, so the nested PHI must remain
    // rejected instead of being guessed into a Select.
    program.block_storage.push_back(std::make_unique<Block>());
    auto *ambiguous = program.block_storage.back().get();
    program.blocks.push_back(ambiguous);
    program.block_info.push_back(
        {.id = block_count,
         .start_pc = 0x1d0u,
         .end_pc = 0x1e0u,
         .terminator = {.kind = CFG::TerminatorKind::ConditionalBranch,
                        .true_block = 6,
                        .false_block = 7},
         .condition = Value(true)});
    ambiguous->AddBranch(true_a);
    ambiguous->AddBranch(true_b);
  }

  MemoryInfo memory;
  memory.kind = ResourceKind::ScalarAddress;
  memory.planning_only = true;
  program.memory_info.push_back(memory);
  auto &handle = outer_merge->AppendNewInst(ValueOpcode::GetAddressResource,
                                            {Value(&outer_phi), Value(0u)});
  auto &read = outer_merge->AppendNewInst(
      ValueOpcode::LoadAddressU32,
      {Value(&handle), Value(0u), Value(0u), Value(true)});
  read.SetFlags(MemoryFlags{.index = 0, .pc = 0x1c4u});
  program.srt_reads.push_back({Value(&read), 0});
  DescriptorSource descriptor;
  descriptor.dwords[0] = Value(&read);
  descriptor.dword_count = 1;
  program.descriptor_sources.push_back(descriptor);
  return program;
}

bool ContainsPhi(Libs::Graphics::ShaderRecompiler::IR::Value root) {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  std::vector<Value> pending{root};
  std::unordered_set<const Inst *> visited;
  while (!pending.empty()) {
    const auto value = pending.back().Resolve();
    pending.pop_back();
    const auto *inst = value.TryInstruction();
    if (inst == nullptr || !visited.insert(inst).second)
      continue;
    if (inst->GetOpcode() == ValueOpcode::Phi)
      return true;
    for (size_t index = 0; index < inst->NumArgs(); ++index)
      pending.push_back(inst->Arg(index));
  }
  return false;
}

void TestRuntimePhiSelectNestedDiamonds() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = ExtractResourcePlan(BuildNestedRuntimePhiProgram(false));
  const auto *read = plan.srt_reads[0].value.ResolveInstruction();
  Check(read != nullptr, "nested diamond SRT read was not cloned");
  const auto *handle = read->Arg(0).ResolveInstruction();
  Check(handle != nullptr &&
            handle->GetOpcode() == ValueOpcode::GetAddressResource,
        "nested diamond address handle was not cloned");
  const auto *outer = handle->Arg(0).ResolveInstruction();
  Check(outer != nullptr && outer->GetOpcode() == ValueOpcode::SelectU32,
        "outer nested diamond PHI was not lowered");
  const auto *true_select = outer->Arg(1).ResolveInstruction();
  const auto *false_select = outer->Arg(2).ResolveInstruction();
  Check(true_select != nullptr &&
            true_select->GetOpcode() == ValueOpcode::SelectU32 &&
            false_select != nullptr &&
            false_select->GetOpcode() == ValueOpcode::SelectU32,
        "nested SelectU32 arms were not recursively lowered");
  const auto *true_nested = true_select->Arg(1).ResolveInstruction();
  const auto *false_nested = false_select->Arg(1).ResolveInstruction();
  Check(true_nested != nullptr &&
            true_nested->GetOpcode() == ValueOpcode::SelectU32 &&
            false_nested != nullptr &&
            false_nested->GetOpcode() == ValueOpcode::SelectU32 &&
            true_nested->Arg(1).Resolve().IsImmediate() &&
            true_nested->Arg(1).Resolve().U32() == 0x11111111u &&
            true_nested->Arg(2).Resolve().IsImmediate() &&
            true_nested->Arg(2).Resolve().U32() == 0x22222222u &&
            false_nested->Arg(1).Resolve().IsImmediate() &&
            false_nested->Arg(1).Resolve().U32() == 0x44444444u &&
            false_nested->Arg(2).Resolve().IsImmediate() &&
            false_nested->Arg(2).Resolve().U32() == 0x55555555u,
        "nested true/false PHI operand mapping was changed");
  Check(!ContainsPhi(Value(const_cast<Inst *>(outer))),
        "lowerable nested PHI remained in the materialized expression");
  Check(plan.resource_tracking_complete,
        "fully lowered nested graph was marked invalid");
}

void TestRuntimePhiSelectRejectsAmbiguousNestedPhi() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = ExtractResourcePlan(BuildNestedRuntimePhiProgram(true));
  const auto *read = plan.srt_reads[0].value.ResolveInstruction();
  Check(read != nullptr, "ambiguous nested SRT read was not cloned");
  const auto *handle = read->Arg(0).ResolveInstruction();
  const auto *outer =
      handle == nullptr ? nullptr : handle->Arg(0).ResolveInstruction();
  Check(outer != nullptr && outer->GetOpcode() == ValueOpcode::SelectU32,
        "ambiguous outer PHI was not lowered");
  Check(ContainsPhi(outer->Arg(1)),
        "ambiguous nested PHI was incorrectly accepted as a runtime Select");
  Check(!plan.resource_tracking_complete,
        "ambiguous nested PHI did not invalidate the materialization graph");
}

void TestUnbasedFlatCacheHitMaterializes() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = UnbasedFlatPlan();
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, {}, snapshot, specialization),
        "unbased FLAT stage materialization failed");
  Check(snapshot.buffers.empty() && snapshot.images.empty(),
        "unbased FLAT plan produced unexpected descriptors");
}

void TestFailedMaterializationPreservesPriorStage() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = UserDataBufferPlan();
  ResourceSnapshot snapshot;
  snapshot.user_data.push_back(0xfeedbeefu);
  ResourceSpecialization specialization;
  specialization.buffers.push_back({.packed_stride = 7});
  Check(!MaterializeResources(plan, {}, snapshot, specialization),
        "missing runtime user data did not reject the cached stage");
  Check(snapshot.user_data == std::vector<uint32_t>{0xfeedbeefu} &&
            specialization.buffers.size() == 1 &&
            specialization.buffers[0].packed_stride == 7,
        "failed cache materialization changed its destinations");
}

void TestMixedSamplerDuplicatesTheCorrectSnapshot() {
  using namespace Libs::Graphics::ShaderRecompiler::IR;
  auto plan = MixedSamplerPlan();
  ResourceSnapshot snapshot;
  ResourceSpecialization specialization;
  Check(MaterializeResources(plan, {}, snapshot, specialization),
        "mixed sampler materialization failed");
  Check(snapshot.samplers.size() == 3,
        "mixed sampler materialization appended unrelated samplers");
  Check(snapshot.samplers[2] == snapshot.samplers[1] &&
            snapshot.samplers[2] != snapshot.samplers[0],
        "point sampler variant duplicated the wrong runtime descriptor");
}

} // namespace

namespace Common {

int DbgExitHandler(const char *, int, std::string_view) { std::abort(); }

int DbgExitHandler(const char *, int, fmt::text_style, std::string_view) {
  std::abort();
}

int DbgExitIfHandler(const char *, const char *, int) { return 1; }

void DbgExit(int) { std::abort(); }

} // namespace Common

int main() {
  TestMappedSrtUsesDirectReaderByDefault();
  TestIntegerRuntimeValueFollowsSrtReads();
  TestSelectEvaluationIsLazy();
  TestRuntimePhiSelectNormalDiamond();
  TestRuntimePhiSelectNestedDiamonds();
  TestRuntimePhiSelectRejectsAmbiguousNestedPhi();
  TestUnbasedFlatCacheHitMaterializes();
  TestFailedMaterializationPreservesPriorStage();
  TestMixedSamplerDuplicatesTheCorrectSnapshot();
  std::puts("ResourceMaterializationTests: all cases passed");
  return 0;
}

// Keep this focused standalone target self-contained by amalgamating its small
// typed-IR implementation set.
#include "graphics/shader/recompiler/ir/Block.cpp"
#include "graphics/shader/recompiler/ir/Program.cpp"
#include "graphics/shader/recompiler/ir/Type.cpp"
#include "graphics/shader/recompiler/ir/Value.cpp"
#include "graphics/shader/recompiler/ir/opcodes/ValueOpcodes.cpp"
