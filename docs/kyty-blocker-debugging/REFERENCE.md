# KytyPS5 Shader/Resource Debugging Reference

This file is the stable architecture/reference companion to `SKILL.md`.

## Pipeline mental model

```text
guest shader
→ decode
→ CFG / IR
→ SRT planning
→ resource tracking / rewrites
→ descriptor source planning
→ SRT eligibility refresh
→ ResourcePlan extraction
→ runtime materialization
→ SPIR-V emission
→ spirv-val
→ Vulkan pipeline / dispatch
→ GPU execution
```

A failure at a later stage can be caused by a bad decision made several passes earlier. Trace backward to the semantic decision that made the failing value live.

## SRT planning

Relevant concepts:

```text
BuildSrtPlan
PlanBuilder
srt_reads
ReadConst
GetSrtResource
planning_only
ShaderSideSrtReadFlag
```

Some original raw reads are retained as `planning_only` producers for later per-use lowering or analysis.

Important:

```text
raw read retained
!=
must be host-materialized
```

Host evaluation depends on final eligibility/liveness.

## Global shader-side eligibility

Relevant concepts:

```text
IsShaderSideScalarRoot
ShaderSideUseGraph
CanRetainShaderSide
RefreshShaderSideSrtEligibility
```

`ShaderSideUseGraph` is a safety boundary and must distinguish semantic operand roles.

Current proven-safe operand roles include:

```text
ImageWrite operand 2
ImageSampleRaw operand 2
ReadConstBuffer operand 1
```

Real descriptor/resource identity such as `GetBufferResource`, `GetImageResource`, and `GetSamplerResource` must remain rejected unless a dedicated per-use lowering removes that dependency.

## Per-use BDA lowering

Relevant concepts:

```text
LowerScalarBufferReadToBda
IsShaderBdaBufferHandle
IsRawScalarBufferMemory
HasLaneDependentRawRead
ContainsLaneDependentAddress
CloneBdaExpression
m_shader_bda_handles
```

Purpose: convert certain raw scalar-buffer reads whose descriptor address depends on shader-side SRT values into explicit shader-side address/BDA loads.

This allows:

```text
dynamic descriptor-derived address
→ shader-side address calculation
→ BDA load
```

without requiring Vulkan descriptor identity itself to become dynamic.

This is a per-use transformation. Do not confuse it with global SRT slot eligibility.

## Raw scalar-buffer BDA constraints

The existing path is intentionally narrow. Before extending it, inspect `IsRawScalarBufferMemory`.

Typical restrictions include:

```text
ResourceKind::ScalarBuffer
32-bit data
one dword per component read
supported component grouping
untyped
unformatted
no glc/slc
no idxen/offen
no secondary offset
data/number format zero
aligned offset
```

Do not broaden these constraints merely because a game uses an unsupported case. First prove actual guest memory semantics.

## BDA descriptor interpretation

Current scalar-buffer BDA lowering uses descriptor fields approximately as:

```text
base:
  dword0 + low 16 bits of dword1

stride:
  dword1 bits [29:16]

records:
  dword2
```

Any semantic change to these calculations requires independent validation against the guest ISA/descriptor specification.

## CloneBdaExpression

Purpose: clone only the arithmetic envelope needed for a per-use BDA descriptor and replace eligible SRT `ReadConst` nodes with per-use shader-side clones.

It intentionally does not generically clone:

```text
Phi
GetBufferResource
GetImageResource
LoadAddressU32
ReadConstBuffer
```

because those require structured semantics rather than blind argument cloning.

Learned invariant:

> Clone caches must preserve both the cloned value and whether the cached result differs semantically from the original expression.

For shared subgraphs, a cache hit must propagate clone provenance.

## Lane-dependent provenance

Relevant concepts:

```text
HasLaneDependentRawRead
ContainsLaneDependentAddress
ReadLane
LocalInvocationId
LocalInvocationIndex
```

A loop-carried/raw address dependency that includes lane-dependent state cannot generally be collapsed to one host value. This strongly indicates shader execution state rather than host specialization state.

## Resource identity

Resource identity is a hard semantic boundary.

Examples:

```text
GetBufferResource
GetImageResource
GetSamplerResource
```

If an SRT-derived value reaches these directly, the system needs one of:

- host-materializable descriptor
- bounded/indirect descriptor plan
- dedicated per-use lowering such as BDA
- another explicitly proven dynamic-resource mechanism

Do not treat resource identity as ordinary payload data.

## Operand-role examples

### ImageWrite

```text
operand 0: resource identity
operand 1: image address
operand 2: payload data
operand 3: execution predicate / exec
```

A value reaching payload does not imply dynamic resource identity.

### ImageSampleRaw

```text
operand 0: image resource
operand 1: sampler resource
operand 2: ImageAddress
```

Runtime coordinates can be shader-side while image/sampler identity stays tracked separately.

### ReadConstBuffer

```text
operand 0: BufferResource
operand 1: runtime byte offset
```

The backend can support a runtime offset without making the buffer resource itself dynamic.

## Bounded resource planning

Relevant concepts:

```text
BoundedReadProof
ParseBoundedOffset
ReadBoundedSrtU32
PlanBoundedReads
MakeIndirectBuffer
```

Use only when there is a **proven finite candidate domain**.

Do not use arbitrary candidate caps, memory scanning, or heuristic enumeration as correctness mechanisms.

## Indirect images

Relevant concepts:

```text
PlanIndirectImages
SupportsIndirectImageUse
IndirectImagePlan
```

Do not copy image-indirect logic into buffers unless semantics match.

## Runtime PHI handling

Relevant concepts:

```text
ResolveInvariantPhi
ResolveResourcePhi
RuntimePhiSelect / structural select lowering
```

Important PHI classes:

```text
Host-invariant PHI:
  all reachable values equivalent
  → safe host evaluation

Structural conditional descriptor PHI:
  small acyclic branch/diamond
  → may be lowered to runtime Select

Loop-carried PHI with ReadLane/BVH/runtime state:
  → not a host invariant
  → not safe to choose one incoming edge
  → not safe to iterate arbitrarily on host
```

Treat the last class as true shader execution state unless disproven.

## Flattened SRT liveness

Relevant concept:

```text
live_flat_slots
```

Only flattened SRT slots with real semantic host-visible consumers should force host evaluation.

When debugging host materialization, inspect:

```text
slot.shader_side
live_flat_slots[slot]
ordinary wrappers with semantic users
special per-use shader-side wrappers
dead/reference-only wrappers
```

A liveness problem can look exactly like an evaluator problem.

## Global vs per-use state

Critical invariant:

```text
program.srt_reads[slot].shader_side
```

is global slot state.

A special BDA clone with `ShaderSideSrtReadFlag` can represent per-use state.

The same slot may require both simultaneously:

```text
host-side ordinary consumer
+
shader-side BDA clone
```

Any refresh pass must not conflate them.

## MaterializeResources

A message such as:

```text
srt evaluation: flat failed ... PHI is not invariant
```

means only that host evaluation was attempted and failed. It does not prove host evaluation was semantically required. Trace liveness/eligibility first.

## SPIR-V generation

Once the target shader reaches emission, immediately validate it:

```text
spirv-val
numeric undefined-ID scan
```

Historical failure class:

```text
planning_only producer omitted
→ consumer Result()/Define() obtains unbound numeric ID
```

Do not proceed directly to Vulkan runtime debugging when generated SPIR-V is invalid.

## BDA memory emitter concepts

Relevant current architecture:

```text
DeviceAddressFromWords
GuestAddress
GetBdaPointer
LoadBdaDword
LoadBda
```

Guest address translation and mapped-memory behavior must remain distinct from descriptor bounds.

## FaultBuffer

`FaultBuffer` is a page-fault bitmap / fault-tracking mechanism. Do not repurpose it for arbitrary debug telemetry such as ballot values, slot numbers, or temporary counters.

## BVH / ray tracing boundary

`--stub-bvh` can expose downstream blockers, but a stub miss is only a control-flow/debugging aid. It does not validate BVH node semantics, traversal correctness, ray intersection, or address layout.

Real BVH work is a separate correctness milestone.

## External evidence priority

Preferred evidence chain:

```text
1. real game reproduction
2. primary ISA/specification
3. Kyty implementation
4. independent implementation corroboration
5. regression test
6. game progress
```

Useful corroborating sources may include AMD RDNA2 ISA, GPUOpen PAL/GPURT, Mesa/RADV, Vulkan/SPIR-V specifications and tools, Prosperity, and other PS5 reverse-engineering references.

Do not copy behavior blindly from another emulator. Use it as corroboration, not authority.

## Recommended local workflow

Typical build:

```powershell
cmake --build _Build/windows --target kyty_emulator
cmake --build _Build/windows --target resource_tracking_tests
cmake --build _Build/windows --target resource_materialization_tests
cmake --build _Build/windows --target shader_cfg_tests
cmake --build _Build/windows --target shader_recompiler_compute_tests
cmake --build _Build/windows --target scalar_provenance_tests
```

If runtime uses install tree, update/install it and verify the exact path before interpreting results.

Primary Windows compiler in this environment: `clang-cl`.

## Commit discipline

Prefer small semantic commits. Good commit properties:

```text
generic
regression-covered
no game-specific exception
no logs
no generated shader dumps
human-readable message
runtime-confirmed where appropriate
```

Before submitting upstream, review project AI contribution policy and disclose AI assistance where required.
