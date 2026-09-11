# KytyPS5 shader/resource architecture reference

Stable mechanisms and semantic boundaries. Process lives in `SKILL.md`, current target state in `CURRENT_STATE.md`, durable findings/history in `PROJECT_MEMORY.md`, and external evidence routing in `DEBUG_REFERENCE_SOURCES.md`.

## Pipeline and pass lifetime

```text
guest shader → decode → CFG/IR → SRT planning
→ resource tracking and rewrites → descriptor-source planning
→ SRT eligibility refresh → ResourcePlan extraction
→ runtime materialization → SPIR-V emission/validation
→ Vulkan pipeline/dispatch → GPU execution
```

A later sink can originate in an earlier decision. IR values stored in side plans are not ordinary use edges; any plan that crosses rewriting or DCE must explicitly preserve, refresh, or own its inputs until extraction.

## SRT planning and liveness

Key mechanisms: `BuildSrtPlan`, `PlanBuilder`, `srt_reads`, `ReadConst`, `GetSrtResource`, `planning_only`, `ShaderSideSrtReadFlag`, and `live_flat_slots`.

A raw read may remain as a `planning_only` producer for later analysis or per-use lowering. Retention does not imply host materialization. Final eligibility, real semantic users, and flattened-slot liveness decide host evaluation.

When a materialization error names a slot or PHI, inspect:

```text
program.srt_reads[slot].shader_side
live_flat_slots[slot]
ordinary wrappers with semantic users
special per-use shader-side wrappers
dead or reference-only wrappers
```

## Resource identity and operand roles

`ShaderSideUseGraph`, `CanRetainShaderSide`, and `RefreshShaderSideSrtEligibility` enforce the host/resource identity boundary.

```text
GetBufferResource / GetImageResource / GetSamplerResource
  → resource identity

ImageWrite operand 2
  → payload data

ImageSampleRaw operand 2
  → runtime image coordinates/address

ReadConstBuffer operand 1
  → runtime byte offset
```

A runtime value in payload, coordinates, or offset does not make the resource identity dynamic. Dynamic identity requires host materialization, a proven bounded/indirect plan, dedicated per-use lowering, or another explicit mechanism.

## Global and per-use state

`program.srt_reads[slot].shader_side` is global slot state. A BDA-created `ReadConst` clone marked with `ShaderSideSrtReadFlag` is per-use state.

The same slot can legally have:

```text
ordinary host-side consumer
+ explicitly tracked shader-side BDA clone
```

`m_bda_srt_clones` distinguishes exact BDA-created clones from ordinary wrappers that may share the same flag. Eligibility refresh must compute global state from ordinary uses without stripping the per-use clone.

## Scalar-buffer BDA lowering

Key mechanisms: `LowerScalarBufferReadToBda`, `IsShaderBdaBufferHandle`, `IsRawScalarBufferMemory`, `HasLaneDependentRawRead`, `ContainsLaneDependentAddress`, `CloneBdaExpression`, and `m_shader_bda_handles`.

The path turns supported raw scalar-buffer reads whose descriptor address depends on shader-side SRT values into explicit shader-side address/BDA loads. It avoids making Vulkan descriptor identity dynamic.

The supported boundary is intentionally narrow. Verify current source before changing it; typical guards include:

```text
ResourceKind::ScalarBuffer
32-bit, one dword per component
supported component grouping
untyped and unformatted
no glc/slc
no idxen/offen
no secondary offset
zero data/number format
aligned supported offset
```

Current descriptor interpretation is approximately:

```text
base    = dword0 + low 16 bits of dword1
stride  = dword1 bits [29:16]
records = dword2
```

Semantic changes to these fields require guest descriptor/ISA proof.

### CloneBdaExpression

The clone contains only the arithmetic envelope needed by one BDA descriptor use and replaces eligible SRT `ReadConst` nodes with per-use shader-side clones. It does not blindly clone `Phi`, resource identity, `LoadAddressU32`, or `ReadConstBuffer`.

Clone caches must preserve both the cloned value and whether it differs semantically from the original. A shared-subgraph cache hit must propagate that provenance.

## Bounded planning and lifetime

Key mechanisms: `BoundedReadProof`, `ParseBoundedOffset`, `PlanBoundedReads`, `ApplyBoundedReads`, `ReadBoundedSrtU32`, `DescriptorSource`, `ExtractResourcePlan`, and `MaterializeBoundedReads`.

A bounded plan is valid only with a proven finite candidate domain. Candidate caps, memory scans, and heuristic enumeration are not correctness mechanisms.

Address/count descriptor sources stored in the plan must survive later rewriting and DCE even after the original read loses its ordinary users. `RetainBoundedDescriptorSources` creates lifetime references for those roots before ResourcePlan extraction.

Indirect image planning uses `PlanIndirectImages`, `SupportsIndirectImageUse`, and `IndirectImagePlan`. Buffer and image shapes are not interchangeable; reuse only when semantics match.

## Runtime PHI classes

Key mechanisms: `ResolveInvariantPhi`, `ResolveResourcePhi`, and `RuntimePhiSelect`.

```text
Host-invariant PHI:
  all reachable values equivalent → host evaluation is valid

Finite structural conditional PHI:
  acyclic branch/diamond → may lower to a runtime select

Loop-carried PHI with lane/BVH/runtime state:
  guest execution state → not a host invariant
```

ReadLane, LocalInvocationId/Index, BVH state, or another lane-dependent address inside a loop prevents collapse to one host value unless independent proof says otherwise.

## Materialization

`EvaluateDescriptorSource` and `MaterializeResources` consume the extracted plan. “PHI is not invariant,” “cannot evaluate Void,” or “address is not host-readable” identifies the evaluator sink only.

Interpretation examples:

- `Void` can mean a plan retained an invalidated IR value.
- A PHI failure can mean liveness or eligibility incorrectly selected host evaluation.
- An address-readability failure before a candidate loop is distinct from failure to read a concrete candidate.

Trace the calling branch and whether base address, byte count, candidate index, or memory callback was reached.

## SPIR-V emission and validation

Generated SPIR-V must satisfy structured control flow, SSA dominance, type, capability, extension, and physical-address rules. Run `spirv-val` for the applicable Vulkan environment and inspect `spirv-dis` output at the first error.

For `Selection must be structured`, map the reported `OpBranchConditional` labels and condition back to CFG/`BlockInfo`; determine whether selection/merge metadata is absent, wrong, or ignored by emission.

A numeric undefined-ID scan complements `spirv-val`. Planning-only producers and side-plan values still need valid definitions when emitted.

Dispatcher fallback executes an unstructured CFG through a structured SPIR-V loop and switch. Its spill table is for native SPIR-V values only. SRT/resource handles and typed image-address metadata are resolved through their consumers and have no standalone SPIR-V value; planning-only scalar reads are omitted unless a shader-side SRT wrapper retains that producer. A shader-side `ReadConst` wrapper aliases the retained `program.srt_reads[slot].value` producer, so cross-block spill analysis and `Def` must follow that producer rather than spill the metadata wrapper.

`GraphicsRunDebugDumpEnabled()` additionally requires a non-silent printf direction. To observe `QueuePoint`, pipeline, submit, and wait traces, use `--graphics-debug-dump true` together with `--printf-direction File` and `--printf-output-file`; `Silent` suppresses those traces even when the graphics flag is true.

## BDA memory emitter

Key mechanisms: `DeviceAddressFromWords`, `GuestAddress`, `GetBdaPointer`, `LoadBdaDword`, and `LoadBda`.

Guest-address translation and mapped-memory behavior are distinct from descriptor bounds. Preserve base, stride, record count, access width, alignment, and page-table/fault behavior independently.

## BVH and fault tracking

`--stub-bvh` provides an always-miss control-flow path for downstream diagnosis. It does not implement node layout, traversal, ray intersection, or address semantics.

`FaultBuffer` is a page-fault bitmap/fault-tracking mechanism. It is not a general-purpose telemetry buffer.

## Build and runtime boundary

The usual Windows build is CMake/Ninja with `clang-cl`. The executable used for target validation may come from the install tree, so build-tree and install-tree binaries can diverge. Stable architecture ends at that boundary; the exact current commit, commands, binary hashes, flags, and runtime label belong in `CURRENT_STATE.md`.

## Compute input provenance and replay

`ShaderComputeInputInfo` is assembled from live PM4 state, not from the shader binary. `COMPUTE_NUM_THREAD_X/Y/Z` populate `CsStageRegisters.num_thread_*`; `COMPUTE_PGM_RSRC2` supplies `USER_SGPR`, `TGID_*_EN`, `TG_SIZE_EN`, and `TIDIG_COMP_CNT`; `ShaderGetStaticInputInfoCS` copies them into the compute input. `GetShaderParams` then copies the first `USER_SGPR` values into `CompileOptions::user_data`.

`PipelineCache::GetComputeProgram` sets `host_subgroup_size` from `SupportsComputeWave64()`. `RenderExecutor::DispatchDirect` passes the dispatch mode and group counts, but writes `dispatch_threads_num` only after `GetComputeProgram` returns; therefore `CompileProgram`, which runs inside that call, sees the initial zero triple for that field. Compute `CompileOptions::user_data_base` remains its default zero because only vertex/mesh paths override it.

The raw code and SPIR-V `LocalSize` do not uniquely recover this state. Exact replay requires a capture of all fields and user-data count from the production call. Place any crash-safe diagnostic before resource-plan extraction and an exact marker immediately before `CompileProgram`; do not infer fields from IR builtin use or substitute values from another shader.

## Timeline waits and end-of-pipe markers

`CommandScheduler::Submit` associates each host submission with a master timeline tick;
`MasterSemaphore::Wait` is the host observation point for completion. `CommandBufferDebugOp`
values 3 and 5 identify end-of-pipe write and write-back marker submissions. The guest
`CommandProcessor::WriteAtEndOfPipe` and host `Sync::WriteAtEndOfPipe*` paths record marker
metadata and perform the emulated write, but an `ErrorDeviceLost` returned by the wait does not
by itself identify that marker as the offending GPU command. Recover the last non-EOP submission
and its resource state before changing GPU semantics.
