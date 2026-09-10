---
name: kyty-blocker-debugging
description: Evidence-driven workflow for clearing KytyPS5 shader/resource/runtime blockers with minimal false positives and minimal expensive target-game reruns.
---

# KytyPS5 Blocker Debugging Skill

## Purpose

Use this skill when debugging KytyPS5 game-progress blockers, especially shader decode, SRT/resource planning, resource materialization, CFG/PHI handling, BDA lowering, SPIR-V generation/validation, Vulkan execution, and BVH/ray-tracing paths.

Primary optimization target:

> Minimize **time to the next observable game milestone**.

Secondary metric:

> Maximize the number of **proven blockers cleared per wall-clock time**.

Do not optimize for report length, number of experiments, or token usage. Prefer the shortest experiment that can distinguish competing explanations.

This skill is generic. Never hard-code a game hash, SRT slot, PC, or shader hash into production logic.

## GAME PROGRESS STRATEGY

Debug toward the next observable target-game milestone, rather than toward subsystem completeness. Define the exact milestone names for the current game in `CURRENT_STATE.md`. A reusable default sequence is:

```text
M0 executable starts
M1 startup shaders compile
M2 first graphics/compute submissions succeed
M3 first visible frame
M4 intro/loading sequence
M5 main menu
M6 gameplay
```

At every meaningful checkpoint, record:

```text
TARGET GAME:
CURRENT MILESTONE:
NEXT MILESTONE:
CURRENT P0 BLOCKER:
LAST VALIDATED PROGRESS SIGNAL:
KNOWN P1 LIKELY BLOCKERS:
```

Prioritize blockers as follows:

```text
P0 current milestone blocker
P1 likely next blocker on the same execution path
P2 correctness issue not currently blocking progress
P3 unrelated cleanup or known baseline issue
```

Always work the current P0 first. Keep the critical path narrow: do not spend time on unrelated warnings, old baseline failures, or future subsystems while the target game is blocked earlier in the pipeline.

Treat these as valid progress signals: the first blocker changes, shader count increases, the target shader begins emitting, SPIR-V validates, pipeline creation succeeds, the first GPU submission succeeds, the first visible frame appears, or the game reaches a later milestone. Record code correctness separately from target-game progress; a generic fix can be correct even when it produces no immediate visual progress.

When the blocker moves stages, stop debugging the old domain and reclassify the new P0:

```text
SRT/materialization cleared → SPIR-V validation
SPIR-V valid               → Vulkan pipeline/runtime
pipeline succeeds          → GPU execution/rendering
first frame works          → game logic/media/input/etc.
```

Do not fully implement a large incomplete subsystem merely because it is unfinished. Prefer the narrowest semantically correct implementation that can clear the current milestone, and reject hacks that violate the program's meaning or safety boundaries.

For each blocker, choose the action with the highest expected information gain or milestone progress per unit time. Prefer:

```text
existing logs → static proof → focused test → narrow patch → one game run
```

over broad architecture exploration. A game run is a validator of a concrete hypothesis, not a substitute for cheap evidence.

## Persistent project memory

Keep these project documents separate:

```text
SKILL.md           reusable debugging process and decision rules
REFERENCE.md       stable Kyty architecture/reference knowledge
PROJECT_MEMORY.md  durable project facts that are expensive to rediscover
CURRENT_STATE.md   volatile checkpoint, active blocker, and next action
```

For every substantial debugging session, use the project paths listed in `AGENTS.md` and:

1. read `CURRENT_STATE.md` first for the active milestone, P0, and next action;
2. read `PROJECT_MEMORY.md` for durable facts before repeating project discovery;
3. use `REFERENCE.md` when the current question needs stable architecture or external-reference context;
4. update `CURRENT_STATE.md` after meaningful state, blocker, milestone, test, or runtime changes;
5. update `PROJECT_MEMORY.md` only after a durable, proven fact is learned.

Do not turn `PROJECT_MEMORY.md` into a chronological log. Keep temporary speculation, transient pointers, giant IR dumps, one-off commands, and unproven hypotheses in neither durable memory nor the reusable skill. Use `PROVEN` or `STRONG EVIDENCE` labels where confidence matters, with concise `FACT`, `WHY IT MATTERS`, `EVIDENCE`, and `RELATED CODE/COMMIT` fields.

If `PROJECT_MEMORY.md` already answers an architectural question, do not re-investigate it unless the current source contradicts it, a regression contradicts it, or new runtime evidence makes it stale.

## Core invariants

### 1. Prove runtime provenance before believing runtime results

Before accepting any game run as evidence, establish:

- `git rev-parse HEAD`
- `git status --short`
- exact executable path launched
- executable timestamp when provenance is uncertain
- runtime `Source build ...` label

A run is invalid for evaluating a fix if the executable build label does not correspond to the intended source revision. Do not analyze a stale-binary result as if it came from current source.

### 2. Do not fix the symptom before proving why the value reached it

Errors such as `PHI is not invariant` are frequently the final host-evaluator failure, not the root cause.

Always ask:

1. Why is this value being host-evaluated?
2. Which semantic consumer keeps it host-side or live?
3. Is that consumer truly host-only/resource-identity dependent?
4. Is the rejecting rule too broad for the operand role?
5. Is an existing per-use shader-side lowering supposed to remove that dependency?

Do not extend PHI evaluation merely because the crash mentions a PHI.

### 3. Classify operand roles, not only opcodes

For every rejection report:

```text
opcode
operand index
semantic operand role
shortest path from root
resource identity dependency? yes/no
```

Examples:

```text
ImageWrite:
  resource identity  -> host/resource-tracked
  address            -> separate semantic class
  payload data       -> may be ordinary shader-runtime data

ImageSampleRaw:
  image resource     -> resource identity
  sampler resource   -> resource identity
  ImageAddress       -> ordinary runtime coordinates/address

ReadConstBuffer:
  BufferResource     -> resource identity
  byte offset        -> ordinary runtime scalar operand
```

Never broaden eligibility for an entire opcode when only one operand role is proven safe.

### 4. Enumerate unique rejection classes, not just the first sink

When an eligibility walk fails, collect all distinct rejection sinks keyed approximately by:

```text
(opcode, operand, block, pc, rejecting rule)
```

For each sink keep semantic role, shortest path, and optional reach count. Traversal must be cycle-safe and memoized. Do not exponentially unfold PHI cycles.

### 5. Static/offline evidence first; game runs are expensive validators

Preferred loop:

```text
saved logs / existing IR / current source
        ↓
static classification
        ↓
focused fixture or temporary diagnostic only if needed
        ↓
generic fix
        ↓
focused regression
        ↓
post-pass lifetime verification
        ↓
focused test suite
        ↓
ONE clean game run
```

Do not repeatedly launch the target game when no source change exists that could affect the observable result. If a hypothesis fails to produce discriminating evidence in roughly 15–20 minutes, change approach.

### 6. Search for existing architecture before inventing a new subsystem

Before proposing new descriptor, BDA, CFG, or materialization architecture, inspect the current tree for existing lowering, helper functions, related tests, pass ordering, fallback paths, per-use rewrites, and liveness/specialization machinery.

Prefer answering:

> Why did the existing path not fire or not survive later passes?

before designing a parallel mechanism.

### 7. Verify the full lifetime of a rewrite

A rewrite that succeeds locally may be undone later. For any important lowering, inspect state at:

```text
before lowering
after lowering
after later tracking/refresh passes
at ResourcePlan extraction
at MaterializeResources
at SPIR-V emission
```

Ask whether later passes rewrite the value, strip metadata/flags, restore host liveness, recompute global eligibility, preserve stale wrappers, or reintroduce a descriptor dependency.

### 8. Distinguish global slot eligibility from per-use lowering

These are different concepts:

```text
GLOBAL SRT slot eligibility
!=
PER-USE shader-side lowering
```

A single SRT slot can legally have an ordinary host-side consumer and a per-use shader-side BDA consumer.

Therefore:

- do not promote an entire slot because one use can be lowered shader-side;
- do not destroy a proven per-use shader-side clone merely because another use keeps the global slot host-side.

Use explicit provenance for special per-use clones where necessary.

### 9. Regressions must prove causality

Preferred regression quality:

```text
fails on parent/baseline
passes after fix
```

Prefer semantic assertions over internal implementation details. For risky eligibility/resource changes, add a positive case, a negative identity case, and a transitive-negative case when relevant.

## Blocker taxonomy

### P — Provenance
- P1 stale executable
- P2 wrong install/build tree
- P3 dirty build uncertainty
- P4 log from different revision

### D — Decode
- D1 unsupported decoded instruction
- D2 wrong opcode mapping
- D3 instruction semantics missing

### CFG — Control flow / SSA / PHI
- CFG1 structural branch/diamond lowering
- CFG2 loop-carried PHI
- CFG3 context-sensitive clone
- CFG4 invalid dominance/SSA
- CFG5 unresolved active-lane semantics

### SRT — SRT planning/materialization
- SRT1 host materialization failure
- SRT2 operand-role eligibility false positive
- SRT3 flattened-slot liveness error
- SRT4 global/per-use eligibility conflation
- SRT5 stale wrapper or planning-only dependency
- SRT6 cross-slot dependency

### RES — Resource identity/descriptor planning
- RES1 buffer resource identity
- RES2 image resource identity
- RES3 sampler resource identity
- RES4 bounded candidate domain missing
- RES5 indirect descriptor shape unsupported
- RES6 descriptor source invalid after rewrite

### BDA — Shader-side address lowering
- BDA1 unsupported scalar-buffer semantics
- BDA2 lane-dependent provenance not recognized
- BDA3 expression cloning failure
- BDA4 shared-expression/cache provenance loss
- BDA5 pass ordering/lifetime undo
- BDA6 stale host dependency after successful lowering
- BDA7 incorrect bounds/base/stride semantics

### SPV — SPIR-V generation/validation
- SPV1 undefined ID
- SPV2 dominance/SSA invalid
- SPV3 type mismatch
- SPV4 missing capability/extension
- SPV5 planning-only producer omitted
- SPV6 invalid physical-address usage

### GPU — Runtime Vulkan/GPU execution
- GPU1 descriptor/binding mismatch
- GPU2 BDA/page-table fault
- GPU3 image semantics
- GPU4 synchronization/barrier
- GPU5 robustness/null descriptor behavior
- GPU6 device-specific issue

### RT — Ray tracing/BVH
- RT1 BVH instruction decode/lowering
- RT2 node layout/type semantics
- RT3 traversal state
- RT4 BVH address calculation
- RT5 stub-vs-real implementation boundary

## Standard blocker-clearing state machine

### Step 1 — Verify provenance

Required before a runtime conclusion:

```text
HEAD:
WORKTREE:
EXECUTABLE:
BUILD LABEL:
```

If mismatch: stop runtime analysis.

### Step 2 — Reproduce once

Record only the first blocker and minimal context:

```text
shader hash
stage
PC
slot/source/resource
error
shader count
whether target SPIR-V exists
```

### Step 3 — Classify the domain

Assign one primary class from the taxonomy. Do not patch before classification unless the defect is mechanically obvious and already covered by a focused failing test.

### Step 4 — Trace the causal path

Trace:

```text
root
→ wrappers
→ semantic consumers
→ rejection/liveness reason
→ final failing evaluator/emitter
```

For host-evaluation failures, prove why the value is still host-live.

### Step 5 — Enumerate unique rejection sinks when applicable

Output:

```text
# | opcode | operand | block | pc | rule | semantic role | shortest path | reaches
```

Then separate real resource-identity rejections from payload/coordinate/offset false positives.

### Step 6 — Inspect existing architecture

Search for current mechanisms such as:

```text
Lower*ToBda
MakeIndirect*
PlanBounded*
Refresh*Eligibility
Clone*
Materialize*
Evaluate*
live_flat_slots
planning_only
ShaderSideSrtReadFlag
```

### Step 7 — Form one falsifiable hypothesis

A good hypothesis predicts a specific observation. Example:

```text
If RefreshShaderSideSrtEligibility strips a valid BDA clone flag,
the clone must be flagged immediately after BDA lowering and unflagged immediately after Refresh.
```

Avoid vague tasks such as "investigate all SRT logic".

### Step 8 — Prove offline

Use, in order:

1. current source
2. saved logs/IR artifacts
3. focused unit fixture
4. temporary narrow diagnostic
5. one diagnostic game run only if absolutely necessary

Temporary diagnostics must be narrowly filtered, semantics-preserving, and removed afterward.

### Step 9 — Implement the narrowest generic fix

Requirements:

- no game-specific hash/slot/PC conditions
- no arbitrary fallback constants
- preserve existing safety boundaries
- distinguish operand role where needed
- preserve per-use semantics when global state differs
- avoid broad allow-all resource logic

### Step 10 — Regression

At minimum:

```text
bug reproduction
expected semantic outcome
negative safety case when relevant
```

Where practical verify fail-before/pass-after.

### Step 11 — Post-rewrite lifetime check

Before launching the game, prove the fix survives later passes. Typical checks:

```text
flag before/after refresh
semantic users before/after rewrite
live_flat slot state
descriptor arguments after rewrite
planning_only state
ResourcePlan extraction state
```

This step is mandatory for pass-ordering, metadata, clone, or liveness fixes.

### Step 12 — Focused tests

Default relevant set:

```text
resource_materialization_tests
resource_tracking_tests
shader_cfg_tests
shader_recompiler_compute_tests
scalar_provenance_tests
```

Known baseline failures must be documented and not misreported as new regressions. Run `git diff --check`.

### Step 13 — One clean game run

Requirements:

- exact intended executable
- correct build label
- no stale trace variables
- no temporary diagnostic substitutions
- same intentional runtime feature flags as baseline

Record:

```text
BUILD LABEL:
SHADER COUNT:
OLD BLOCKER: cleared/still failing
NEW FIRST BLOCKER:
TARGET SHADER GENERATED: yes/no
```

### Step 14 — Validate SPIR-V immediately when target emission begins

If the target shader is finally generated:

1. run `spirv-val`
2. perform numeric undefined-ID scan
3. do this before broad runtime/GPU debugging

### Step 15 — Record next blocker

Do not conflate a new blocker with the previous one. Even if the error string is identical, reclassify from first principles.

### Step 16 — Commit/checkpoint

Commit only generic, tested changes. Keep logs and generated shaders out of the commit.

## Hard prohibitions

Do not use these shortcuts without a proof that changes the rule:

```text
- force lane 0
- choose first PHI incoming value
- arbitrary loop iteration count
- arbitrary candidate cap
- arbitrary descriptor/memory scan
- dummy descriptor
- null descriptor fallback without semantic proof
- broad allow-all Image*/Buffer* eligibility
- treat all memory/image operands identically
- host fixed-point evaluation for true loop-carried ReadLane/BVH state
- game/shader hash exceptions
- SRT slot exceptions
- PC-specific production behavior
- repurpose FaultBuffer as a general diagnostics channel
- bypass resource identity safety because a sibling operand is runtime-safe
- assume a helper succeeded semantically because it returned true
```

## Decision rules for common situations

### `PHI is not invariant`

Do not modify PHI evaluation first. Ask why this expression is host-evaluated and what live wrapper/resource keeps it host-side.

If the PHI is loop-carried and contains ReadLane/BVH-dependent state, treat it as true shader execution state until disproven.

### One false-positive eligibility sink found

Do not patch immediately. Enumerate all unique rejection classes for the same root/slot first.

### Runtime operand reaches a memory/image op

Classify the operand. Resource identity and payload/address/offset are different cases.

### Dynamic descriptor discovered

Before designing indirect descriptors, check existing BDA, bounded-resource, and indirect-resource paths, plus whether the descriptor is used only by raw scalar reads.

### Existing lowering fires but game does not progress

Inspect post-lowering users, later refresh passes, flags, global vs per-use state, liveness tables, ResourcePlan extraction, and MaterializeResources.

### Fix passes tests but no runtime progress

Do not discard it automatically. Determine whether it fixed a real bug but not the active runtime dependency, a later pass undid it, a sibling dependency remains, the regression was too synthetic, or the executable was stale.

## False-positive risk assessment

```text
LOW
  semantic operand role proven;
  backend already supports runtime value;
  resource identity remains rejected;
  positive + negative regressions exist.

MEDIUM
  semantics plausible but not end-to-end exercised;
  some backend assumptions remain unproven.

HIGH
  broad eligibility change;
  resource identity becomes dynamic without dedicated lowering;
  arbitrary host approximation;
  no negative regression;
  no fail-before/pass-after proof.
```

Game progress alone is not correctness proof.

## Standard Codex task contract

```text
CURRENT HEAD:
CURRENT FIRST BLOCKER:

DO NOT:
- <unsafe shortcuts>
- <unnecessary runtime runs>

GOAL:
<one falsifiable question>

EVIDENCE TO COLLECT:
<minimal fields>

CLASSIFY:
<finite categories>

IMPLEMENT:
only if <clear condition>

REGRESSION:
<required fail-before/pass-after semantics>

VALIDATION:
<focused tests>

GAME RUN:
at most one, after tests

FINAL REPORT:
HEAD:
BUILD LABEL:
CLASS:
ROOT CAUSE:
FIX:
REGRESSION:
TESTS:
GAME RESULT:
NEXT BLOCKER:
```

## Standard concise report format

```text
HEAD:
BUILD LABEL:
EXECUTABLE:

FIRST BLOCKER:

CLASS:

ROOT CAUSE:
<1–3 sentences>

EVIDENCE:
<minimum discriminating evidence>

EXISTING ARCHITECTURE:
<relevant current mechanism>

PROPOSED / IMPLEMENTED FIX:

FALSE-POSITIVE RISK:
low / medium / high
why

REGRESSION:
fail-before:
pass-after:

FOCUSED TESTS:

GAME RESULT:

TARGET SPIR-V:
generated yes/no

SPIRV-VAL:

NEXT BLOCKER:

COMMIT:
```

Avoid giant IR dumps unless explicitly requested or necessary to support a disputed claim.

## Exit criteria for a blocker

A blocker is considered cleared only when evidence is adequate for its class.

Preferred chain:

```text
real game reproduces
+ implementation/spec semantics understood
+ root cause isolated
+ generic fix
+ regression
+ focused tests
+ game advances
```

For SPIR-V blockers add `spirv-val`. For GPU execution blockers add runtime validation/capture. For BVH/RT blockers distinguish stub progress from real semantic correctness.

## Updating this skill

Update `SKILL.md` only when a **new reusable debugging principle or blocker class** is discovered.

Do not update the skill for every new slot or shader.

Keep project-specific durable facts in `PROJECT_MEMORY.md`, not in this reusable skill. Update `CURRENT_STATE.md` after meaningful checkpoints. Update `PROJECT_MEMORY.md` only for durable proven knowledge. Update `REFERENCE.md` when architecture changes or a new stable subsystem becomes relevant.
