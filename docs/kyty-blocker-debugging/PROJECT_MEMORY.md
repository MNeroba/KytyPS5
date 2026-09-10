# KytyPS5 Project Memory

This is durable, non-chronological project knowledge for fast continuation by a fresh Codex session. The project copy is canonical:

`G:/KytyPS5/repo/docs/kyty-blocker-debugging/PROJECT_MEMORY.md`

Keep the active milestone, P0 blocker, and next action in `CURRENT_STATE.md`. Use confidence labels when a fact is not fully proven.

## Project mission

### Execution progress — PROVEN

FACT: The engineering objective is to move the real target game along its execution path as quickly as possible while keeping fixes generic and semantically defensible.

WHY IT MATTERS: Optimize for time to the next observable game milestone; use proven blockers cleared per wall-clock time as the secondary metric.

EVIDENCE: `SKILL.md` GAME PROGRESS STRATEGY and the milestone checkpoint in `CURRENT_STATE.md`.

## Environment and durable paths

### Primary development environment — PROVEN

FACT: The primary machine is Windows 11 Pro on an Intel i9-12900K with 32 GB RAM and an NVIDIA RTX 3090 24 GB. The build toolchain is CMake + Ninja with `clang-cl`; do not assume the `cl.exe`/MSVC frontend.

WHY IT MATTERS: Build and runtime provenance must use the intended compiler and install tree.

EVIDENCE: Host inspection on 2026-09-10 and project setup records.

RELATED CODE/COMMIT: `G:/KytyPS5/AGENTS.md`, `REFERENCE.md` build workflow.

STRONG EVIDENCE: A secondary validation machine is Apple M1 Max with 32 GB RAM.

### Repository and target paths — PROVEN

FACT: The working fork is `MNeroba/KytyPS5`; the main repository is `G:/KytyPS5/repo`. Canonical project debugging documents are under `G:/KytyPS5/repo/docs/kyty-blocker-debugging/`.

FACT: The target baseline is ASTRO BOT EU, title ID `PPSA21567`, rooted at `G:/PS5 Games/PPSA21567/`. The normally used installed emulator is `G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe`.

FACT: Durable investigation folders are `G:/KytyPS5/logs`, `G:/KytyPS5/captures`, `G:/KytyPS5/notes`, `G:/KytyPS5/patches`, and `G:/KytyPS5/temp`.

WHY IT MATTERS: These paths avoid repeated discovery and prevent mixing source, install, logs, and generated artifacts.

EVIDENCE: `AGENTS.md`, `REFERENCE.md`, and verified path checks on 2026-09-10.

### Game extraction — STRONG EVIDENCE

FACT: EU extraction is already solved. MkPFS is used for archive inspection/unpack. Do not mix Japan `PPSA21559` DLC or packages into the EU `PPSA21567` baseline.

WHY IT MATTERS: Reopening extraction or mixing packages changes the runtime input and invalidates blocker comparisons.

EVIDENCE: Project setup record and existing game root.

### Provenance anchor — PROVEN

FACT: `CURRENT_STATE.md` is authoritative for current HEAD, dirty state, executable, and runtime evidence. A runtime `Source build ...` label that does not match the intended source revision cannot validate that revision.

WHY IT MATTERS: Historical logs from `c94816c-dirty` must not be treated as a clean runtime result for local `09d5e94`.

EVIDENCE: `CURRENT_STATE.md` audit and `AGENTS.md` provenance rule.

## Known baseline failures

### Focused test baseline — PROVEN

FACT: The relevant focused suites are `resource_materialization_tests`, `resource_tracking_tests`, `shader_cfg_tests`, `shader_recompiler_compute_tests`, and `scalar_provenance_tests`.

FACT: `resource_tracking_tests / dynamic storage mips` is a known unrelated baseline failure. An older aggregate `kyty_tests` issue involved obsolete `Log` symbol linking.

WHY IT MATTERS: Do not spend P0 time on these failures unless current changes alter them or new evidence connects them to the target path.

EVIDENCE: `CURRENT_STATE.md` and project history.

## Important checkpoints

### Semantic commit chain — PROVEN

FACT: Important checkpoints in the current blocker family are:

- `3b52034` — runtime PHI resource selects with finite CFG context.
- `4320bf5` — retain producers for shader-side SRT reads.
- `c1f0dde` — retain pure scalar SRT reads in BDA.
- `f23d6f6` — recover SRT image reads for indirect planning.
- `28cce31` — refresh shader-side eligibility after tracking.
- `53081ec` — allow scalar SRT values in ImageWrite data.
- `c94816c` — allow scalar SRT values in runtime resource operands.
- `09d5e94` — preserve per-use BDA SRT clones across tracking.

WHY IT MATTERS: Start from the revision in `CURRENT_STATE.md`; use this list to understand which generic invariant a checkpoint established without inferring the current checkout from history.

EVIDENCE: `git log` and `CURRENT_STATE.md` blocker history.

RELATED CODE/COMMIT: `ResourceTracking.cpp`, `SrtWalker.cpp`, `ResourceMaterialization.cpp`, and `tests/ResourceTrackingTests.cpp`.

## Proven architecture boundaries

### Pipeline and host materialization — PROVEN

FACT: The execution pipeline is decode → CFG/IR → SRT planning → resource tracking/rewrites → descriptor source planning → eligibility refresh → ResourcePlan extraction → runtime materialization → SPIR-V → `spirv-val` → Vulkan pipeline/dispatch → GPU execution.

WHY IT MATTERS: A later failure can be caused by a decision several passes earlier. Trace backward, then hand off explicitly when the stage changes.

EVIDENCE: `REFERENCE.md` and source pass ordering.

### Resource identity versus runtime data — PROVEN

FACT: `GetBufferResource`, `GetImageResource`, and `GetSamplerResource` carry resource identity and remain a hard safety boundary. Payload data, coordinates, and byte offsets are separate operand roles and may be shader-side when a dedicated path supports them.

WHY IT MATTERS: Never broaden an entire opcode or `ShaderSideUseGraph` because a sibling operand is safe. The proven safe examples are ImageWrite operand 2, ImageSampleRaw operand 2, and ReadConstBuffer operand 1.

EVIDENCE: Slot241 analysis, focused regressions, and `REFERENCE.md`.

RELATED CODE/COMMIT: `53081ec`, `c94816c`, `ShaderSideUseGraph`, `RefreshShaderSideSrtEligibility`.

### Global versus per-use shader-side state — PROVEN

FACT: Global SRT slot eligibility and per-use shader-side/BDA lowering are distinct. One slot may keep an ordinary host consumer while a specific BDA use remains shader-side.

WHY IT MATTERS: Refresh passes must not clear a provenance-tagged BDA clone merely because the original slot remains host-live.

EVIDENCE: BDA-only and mixed-use regressions in `09d5e94`; slot245/246 investigation.

RELATED CODE/COMMIT: `ShaderSideSrtReadFlag`, `m_bda_srt_clones`, `LowerScalarBufferReadToBda`, `09d5e94`.

### Scalar-buffer BDA boundary — PROVEN

FACT: `LowerScalarBufferReadToBda` is intentionally narrow: raw `ResourceKind::ScalarBuffer`, 32-bit access, supported grouping, untyped/unformatted data, no GLC/SLC, IDXEN/OFFEN, secondary offset, and aligned supported offsets.

WHY IT MATTERS: Reuse this path before inventing descriptor architecture; do not broaden semantics because one game case is inconvenient.

EVIDENCE: `REFERENCE.md`, BDA guards, and the source22 investigation.

RELATED CODE/COMMIT: `LowerScalarBufferReadToBda`, `IsRawScalarBufferMemory`, `CloneBdaExpression`.

### PHI and loop-carried state — PROVEN

FACT: A finite structural CFG/diamond PHI may be lowered to a runtime select. A loop-carried PHI containing ReadLane/LaneId/BVH or other guest execution state is not a host invariant.

WHY IT MATTERS: Lane-zero substitution, first-incoming selection, arbitrary fixed points, and arbitrary iteration caps change shader semantics.

EVIDENCE: Runtime PHI handling and slot241/245 investigations.

RELATED CODE/COMMIT: `RuntimePhiSelect`, `ResolveInvariantPhi`, `3b52034`.

### Bounded plans and rewrite lifetime — PROVEN

FACT: Bounded/indirect resource planning requires a finite provable candidate domain. Plans may cross tracking, rewrite, extraction, and materialization stages, so stored `Value`/`Inst*` references must be checked for replacement or invalidation before use.

WHY IT MATTERS: A stale plan reference can surface as an evaluator `Void` failure even when the original bounded proof is valid. Keep the active bounded-SRT diagnosis in `CURRENT_STATE.md`; this memory stores only the reusable lifetime lesson.

EVIDENCE: Fail-before audit at `09d5e94` plus the post-DCE regression and successful candidate materialization after `dfc7203`.

RELATED CODE/COMMIT: `PlanBoundedReads`, `ApplyBoundedReads`, `RetainBoundedDescriptorSources`, `ExtractResourcePlan`, `MaterializeBoundedReads`, `dfc7203`.

### BVH and FaultBuffer boundaries — PROVEN

FACT: `--stub-bvh` is a debug progression aid that always misses; it is not semantic BVH/ray-tracing support. `FaultBuffer` is a page-fault bitmap and must not become arbitrary debug telemetry.

WHY IT MATTERS: Passing a decode blocker or observing a stub miss does not prove traversal, node layout, address calculation, or rendering correctness.

EVIDENCE: Existing CLI path and `REFERENCE.md`.

## Confirmed blocker history

### BVH decode — PROVEN

FACT: MIMG opcode `0xe6` (`IMAGE_BVH_INTERSECT_RAY`) was an initial decode blocker; `0xe7` is the related BVH64 opcode.

WHY IT MATTERS: Work on real BVH semantics only when it becomes the current P0.

EVIDENCE: Runtime decode history and `--stub-bvh` progression path.

### slot114 family — STRONG EVIDENCE

FACT: The slot114 family involved SRT producer/liveness and shader-side planning; planning-only or shader-side-rooted producers sometimes had to survive for later lowering.

WHY IT MATTERS: Do not reopen it unless a regression puts it back on the critical path.

EVIDENCE: Runtime progression and the `4320bf5`–`28cce31` sequence.

### slot241 — PROVEN

FACT: The apparent `PHI is not invariant` symptom came from safe runtime operand roles: ImageWrite payload, ImageSampleRaw coordinates, and ReadConstBuffer byte offset. Resource identity did not need to be relaxed.

WHY IT MATTERS: Role-specific eligibility cleared slot241 while preserving the negative resource-identity boundary.

EVIDENCE: Focused fail-before/pass-after tests and runtime progression to slot245.

RELATED CODE/COMMIT: `53081ec`, `c94816c`.

### slot245/246 R1 path — STRONG EVIDENCE

FACT: BDA applicability passed, but a later eligibility refresh treated per-use BDA clones as ordinary wrappers, rejected their `GetBufferResource` path, cleared their shader-side flags, and made host evaluation reach loop-carried PHI state.

WHY IT MATTERS: The fix is explicit per-use clone provenance, not broader resource identity or host PHI evaluation. The previous slot245 blocker disappeared on the inherited labeled runtime path; revalidate any claim against the HEAD in `CURRENT_STATE.md`.

EVIDENCE: Lifetime tracing and BDA-only/mixed-use regressions.

RELATED CODE/COMMIT: `m_bda_srt_clones`, `RefreshShaderSideSrtEligibility`, `09d5e94`.

## Proven generic bug roots

### B6 clone-cache provenance — PROVEN

FACT: `CloneBdaExpression` originally cached only `Value`. A shared-subtree cache hit lost the `changed`/shader-side provenance, leaving part of a descriptor connected to original host-side IR.

WHY IT MATTERS: Any transformed-IR cache must preserve semantic/provenance metadata as well as the value.

EVIDENCE: Shared descriptor subtree fail-before/pass-after regression.

RELATED CODE/COMMIT: `CloneBdaExpression`, `BdaCloneResult { value, changed }`, `09d5e94`.

### R1 refresh lifetime — PROVEN

FACT: Per-use BDA-created `ReadConst` clones need explicit tracking across global eligibility refresh. Ordinary global wrappers and special BDA clones cannot be identified solely by a shared flag.

WHY IT MATTERS: This preserves mixed-use semantics: global slot state may stay host-side while the BDA clone stays shader-side.

EVIDENCE: BDA-only and mixed-use regressions.

RELATED CODE/COMMIT: `m_bda_srt_clones`, `RefreshShaderSideSrtEligibility`, `09d5e94`.

### Bounded descriptor-source DCE lifetime — PROVEN

FACT: `PlanBoundedReads` stores address/count `DescriptorSource` values outside ordinary IR uses. After `ApplyBoundedReads` removes the original read users, post-tracking DCE can invalidate those producers before `ExtractResourcePlan` clones them.

WHY IT MATTERS: Retaining only the bounded read result is insufficient; materialization must still evaluate the descriptor source. `RetainBoundedDescriptorSources` emits side-effecting `ReferenceU32` lifetime markers for the final address/count roots before the sources leave `Tracker`.

EVIDENCE: `TestBoundedDescriptorSourceSurvivesDeadCodeElimination` fails before the fix and passes after `dfc7203`, including `MaterializeResources` reading candidate `0x12345678`. The fix is generic and leaves BDA/R1 and bounded-read semantics unchanged.

RELATED CODE/COMMIT: `ResourceTracking.cpp`, `ResourceMaterialization.cpp`, `tests/ResourceTrackingTests.cpp`, `dfc7203`.

## Disproven approaches and unsafe retries

Do not retry these without new source, regression, or runtime evidence:

- host fixed-point evaluation of genuine loop-carried shader state;
- lane 0, first PHI incoming, arbitrary PHI choice, or arbitrary loop iteration caps;
- arbitrary descriptor candidate caps, guest-memory scans, zero/dummy/null descriptors;
- broad allow-all resource eligibility or allowing `GetBufferResource` merely to clear a blocker;
- promoting a whole SRT slot when only one use is shader-side;
- game/shader hash exceptions, hard-coded SRT slots or PCs;
- treating the BVH miss stub as real BVH support;
- repurposing `FaultBuffer` for diagnostics;
- repeated game runs without a source change or a hypothesis that can change the observed result.

Previously disproven conclusions:

- “slot245 means BDA applicability failed” — false; guards passed.
- “B6 alone explains slot245” — incomplete; B6 was real, but refresh lifetime was a separate R1 defect.
- “GetBufferResource should simply be allowed shader-side” — false without new resource-identity semantics.
- “source22 loop-carried state can be solved by host PHI evaluation” — false.

## Useful existing mechanisms

Prefer these before designing parallel systems:

`LowerScalarBufferReadToBda`, `IsRawScalarBufferMemory`, `CloneBdaExpression`, `ShaderSideSrtReadFlag`, `m_bda_srt_clones`, `RefreshShaderSideSrtEligibility`, `PlanBoundedReads`, `ReadBoundedSrtU32`, `MakeIndirectBuffer`, `RuntimePhiSelect`, `EvaluateDescriptorSource`, `ExtractResourcePlan`, and `MaterializeResources`.

High-value source locations:

- `src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp`
- `src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp`
- `src/graphics/shader/recompiler/ir/passes/SrtWalker.cpp`
- `src/graphics/shader/recompiler/ir/Value.cpp`
- `src/graphics/shader/recompiler/ShaderRecompiler.cpp`
- `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`

## Validated external/reference sources

### Corroboration inventory — STRONG EVIDENCE

FACT: The project has identified AMD RDNA2 ISA, LLVM/Clang AMDGPU BVH builtins, AMD GPUOpen GPURT/PAL, Mesa/RADV, Vulkan and SPIR-V specifications/tools, and the Prosperity emulator as useful corroborating references.

WHY IT MATTERS: Use primary ISA/specification and Kyty source to establish semantics; use other emulators/drivers as corroboration, not authority. Do not re-research a source unless the current implementation needs a specific semantic detail.

EVIDENCE: `REFERENCE.md` external evidence priority and prior investigations.

## Maintenance boundary

This file is not a diary. Do not store current milestone/P0 details, temporary speculation, transient pointer values, giant IR dumps, every command, or every encountered slot here. Put volatile checkpoint and next-action data in `CURRENT_STATE.md`; put stable architecture details in `REFERENCE.md`; put reusable process rules in `SKILL.md`.

If this memory already answers an architectural question, reopen it only when current source, a regression, or new runtime evidence contradicts or makes it stale.
