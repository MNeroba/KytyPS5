# Current KytyPS5 / ASTRO Debugging State

Last updated: 2026-09-10 (bounded-source lifetime fix validated offline)

This file is the canonical volatile state for the `kyty-blocker-debugging` skill. Slot numbers, shader hashes, and commits below are evidence for this investigation, not production rules.

Project artifact locations:

```text
Skill: G:/KytyPS5/repo/docs/kyty-blocker-debugging/SKILL.md
Stable reference: G:/KytyPS5/repo/docs/kyty-blocker-debugging/REFERENCE.md
Durable project memory: G:/KytyPS5/repo/docs/kyty-blocker-debugging/PROJECT_MEMORY.md
Canonical current state: G:/KytyPS5/repo/docs/kyty-blocker-debugging/CURRENT_STATE.md
System skill registration: C:/Users/mneroba/.codex/skills/kyty-blocker-debugging/
```

## Repository

```text
Fork: https://github.com/MNeroba/KytyPS5
Repository: G:/KytyPS5/repo
Branch: astro/materialize-resources
Local HEAD: dfc7203c8f9df87cc9d518d885606ab52c5051d8
HEAD subject: shader: retain bounded descriptor sources through DCE
Tracked working-tree changes: none (source/test fix committed)
Untracked: _Shaders/ (pre-existing)
```

Primary runtime context:

```text
Game: ASTRO BOT EU
Title id: PPSA21567
Game root: G:/PS5 Games/PPSA21567/
Preferred executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
```

## Game progress checkpoint — 2026-09-10

```text
TARGET GAME: ASTRO BOT EU (PPSA21567)
CURRENT MILESTONE: M1 startup shaders compile — bounded-source fix is ready for target runtime validation
NEXT MILESTONE: M2 first graphics/compute submissions succeed
CURRENT P0 BLOCKER: Validate the BS1 bounded descriptor-source lifetime fix in one current-head ASTRO run; no target runtime result exists yet
LAST VALIDATED PROGRESS SIGNAL: The generic BS1 regression survives post-tracking DCE, extracts a valid ResourcePlan, and materializes candidate `0x12345678`; target shader SPIR-V has not yet been re-run
KNOWN P1 LIKELY BLOCKERS: Target-shader SPIR-V emission/validation failure after the bounded materialization gate (not yet observed)
```

The milestone and blocker fields above are the current target-game checkpoint. The prior runtime signal is inherited from the labeled `c94816c-dirty` run; the new generic fix is validated by focused offline tests at `dfc7203`, but no new ASTRO game run has been performed for this checkpoint.

## Scope of the 2026-09-10 audit

Continue from local `09d5e94`. The bounded descriptor-source lifetime fix is committed as `dfc7203`; BDA/R1 logic and bounded-SRT semantics were not changed. No ASTRO run was performed after the fix. Temporary offline audit instrumentation was removed afterward.

The earlier Desktop `CURRENT_STATE.md` was missing when this checkpoint was written and was recreated from the supplied state and observed repository data. Historical claims are retained below only where they remain relevant and are marked as inherited or verified.

## Inherited progress

The supplied prior state reports that slot245/246 were cleared by the R1 per-use BDA work and that the game then reached bounded SRT materialization. This is consistent with the supplied `logs/ASTRO_R1/combined.log`, whose binary label is `Source build c94816c-dirty`; that runtime label does not prove provenance for local HEAD `09d5e94`.

The focused validation after `dfc7203` is:

```text
resource_materialization_tests
resource_tracking_tests
shader_cfg_tests
shader_recompiler_compute_tests
scalar_provenance_tests
```

`resource_tracking_tests` reaches the known unrelated `dynamic storage mips` baseline failure after the new bounded-source regression passes. The other four focused suites pass.

## Verified bounded read 0 audit

The saved runtime log reports:

```text
Shader: 0x78af8e269b528b5c
bounded SRT read 0 count=1 source_dwords=4 scale=4 bias=0 offset=0
srt evaluation: descriptor failed source=0 dword=0 reason=cannot evaluate Void
shader resource specialization failed: bounded SRT read 0 address is not host-readable
```

An offline translation of the exact saved target binary (`_Build/windows/_Shaders_before_4320bf5/original/0019_new_shader_cs_78af8e269b528b5c.bin`) through the current source produced 199 blocks, 303 SRT slots, and one bounded read. The final `BoundedSrtRead[0]` is:

```text
address_source = 0
count_source   = UINT32_MAX (workgroup-derived)
index          = GetBuiltin(WorkgroupId, component 0)
scale          = 4
bias           = 0
memory_offset  = 0
source_dwords  = 4
workgroup_axis = 0
count_signed   = false
```

The original read is `ReadConstBuffer`, IR block 0, PC `0x24`, with `MemoryInfo` index 0, kind `ScalarBuffer`, `planning_only=false`, 32-bit one-dword data, and offset 0 in the offline reconstruction. Its handle is `GetBufferResource(GetUserData s10, s11, s12, s13)`, and its dynamic offset is `WorkgroupId.x << 2`.

The final `ReadBoundedSrtU32` value has three direct semantic users:

```text
INotEqual32(0xffffffff, value)       validity/sentinel check
ShiftRightLogical32(value, 16)       descriptor dword-1 high field
BitwiseAnd32(value, 0xffff)          descriptor dword-1 low field
```

The attempted layout is `{count=1, flat_offset=0}`. It is not committed because address-source evaluation fails first.

Bounded planning predates R1: the parent of `09d5e94` already contains `PlanBoundedReads`, `m_program.bounded_srt_reads`, and `ApplyBoundedReads`. R1 exposed this blocker by allowing materialization to reach it; it did not create the bounded plan.

## Void origin and plan lifetime

The value passed to `EvaluateDescriptorSource` is `DescriptorSource[0].dword0`, the address/base descriptor dword. It is not the bounded read result or the workgroup selector.

```text
bounded_srt_read[0].address_source=0
→ DescriptorSource[0].dword0
→ GetUserData(s10) producer
→ post-TrackResources DCE invalidates the now-unused GetBufferResource and its four GetUserData arguments
→ saved Value resolves as ValueOpcode::Void
→ Evaluator reports cannot evaluate Void
→ MaterializeBoundedReads reports address is not host-readable
```

The same invalidation was observed for address dwords 1–3. This is V2 (`invalidated/replaced instruction resolves to Void`) caused by a stale plan reference. No specialization-memory callback occurs; the failure is before base-address formation and candidate iteration.

```text
A PlanBoundedReads: proof and source 0 are valid; source 0 points at four descriptor producers.
B ApplyBoundedRootReads: no replacement applies to these GetUserData roots.
C ApplyBoundedReads: original ReadConstBuffer becomes an Identity around ReadBoundedSrtU32; bounded read remains live.
D post-tracking DCE: original GetBufferResource and its GetUserData arguments become dead and are invalidated.
E RefreshShaderSideSrtEligibility: does not repair bounded source 0.
F ExtractResourcePlan: clones the already-invalidated source values as Void.
G MaterializeResources: EvaluateDescriptorSource(source 0) fails on dword 0.
```

## Address error relation

`address is not host-readable` is emitted by the `EvaluateDescriptorSource(read.address_source)` failure branch after count/layout setup and before the candidate loop. There is no guest address, requested byte count, or candidate number. The relation is **A1: the same bounded-read causal chain**, not a second candidate/read or fallback.

## Classification and next action

Primary class: **BS1 — stale bounded plan references replaced/invalidated IR**. The generic blocker is cleared by `dfc7203` in the focused fixture; target-runtime validation is still pending.

Implemented fix: `Tracker::RetainBoundedDescriptorSources` retains the address/count source roots with side-effecting `ReferenceU32` lifetime markers before `ResourcePlan` extraction. The regression proves that post-tracking DCE leaves the source graph evaluable and materializes the expected candidate. This preserves bounded semantics and does not alter BDA/R1.

Audit artifacts:

```text
G:/KytyPS5/notes/bounded-read-audit-09d5e94/offline.log
G:/KytyPS5/notes/bounded-read-audit-09d5e94/final-program.txt
```

## Next runtime gate

Next action: build/install the current `dfc7203` emulator, verify its `Source build` label and exact path, then perform one ASTRO run. If the target shader emits SPIR-V, run `spirv-val` and the numeric undefined-ID scan immediately and reclassify the first new blocker.

This file is volatile state for the `kyty-blocker-debugging` skill. Do not treat slot numbers, hashes, or commits here as general rules.

## Verified installation checkpoint — 2026-09-10

The project copies are now the canonical artifacts at `G:/KytyPS5/repo/docs/kyty-blocker-debugging/`. The system-installed copy at `C:/Users/mneroba/.codex/skills/kyty-blocker-debugging/` remains the Codex skill registration. Project instructions are in `G:/KytyPS5/AGENTS.md`.

Observed repository state during installation:

```text
Repository: G:/KytyPS5/repo
Branch: astro/materialize-resources
Local HEAD: 09d5e94c232934ee5e59d0831e8359772c674bc4
HEAD subject: shader: preserve per-use BDA SRT clones across tracking
Tracked working-tree changes: none
Untracked: _Shaders/
```

The earlier claim of an uncommitted B6 fix is historical, not the current working-tree state. Remote/published HEAD was not checked. No build, tests, executable provenance check, or ASTRO run was performed as part of installation; runtime progress at this HEAD is not established by this checkpoint.

The sections below preserve the supplied earlier debugging checkpoint. Their descriptions of the current blocker, active implementation, and next steps must be reconciled with HEAD and existing evidence before resuming debugging.

## Historical checkpoint supplied with the skill

## Repository

```text
Fork:   https://github.com/MNeroba/KytyPS5
Branch: astro/materialize-resources
Published branch HEAD: c94816c0dbd52be5bffe461f2bc85d175a49026c
```

Current local worktree contains an **uncommitted B6 cache-provenance fix** plus regression unless updated after this checkpoint.

Primary game:

```text
ASTRO BOT EU
Title id: PPSA21567
Game root: G:\PS5 Games\PPSA21567\
```

Primary local repo:

```text
G:\KytyPS5\repo
```

Preferred exact runtime executable when using install tree:

```text
G:\KytyPS5\repo\_Build\windows\install\kyty_emulator.exe
```

## Baseline focused tests

```text
resource_materialization_tests
resource_tracking_tests
shader_cfg_tests
shader_recompiler_compute_tests
scalar_provenance_tests
```

Known unrelated baseline failure:

```text
resource_tracking_tests:
dynamic storage mips
```

Do not misclassify this known failure as a regression unless behavior changes.

## Important published commits in current blocker chain

```text
3b52034  shader: lower runtime PHI resource selects with CFG context
4320bf5  shader: retain producers for shader-side SRT reads
c1f0dde  shader: retain pure scalar SRT reads in BDA
f23d6f6  shader: recover SRT image reads for indirect planning
28cce31  shader: refresh SRT shader-side eligibility after tracking
53081ec  shader: allow scalar SRT values in image write data
c94816c  shader: retain scalar SRT values in runtime resource operands
```

Current published branch HEAD is `c94816c`.

## Closed blockers / proven progress

### BVH opcode decode

Original runtime decode blocker:

```text
MIMG opcode=0xe6
IMAGE_BVH_INTERSECT_RAY
```

A CLI `--stub-bvh` path exists for non-semantic progression testing. `--stub-bvh` is not a real RT/BVH implementation.

### slot114

Host replay proved the first-level address was readable, slots112/113 were zero, the child pointer was zero, and the child read was unreadable. Later resource-tracking eligibility/liveness work moved the game past slot114.

### slot241

Initial failure:

```text
LoadAddressU32
→ SelectU32
→ LogicalNot
→ INotEqual32
→ BitwiseAnd32
→ Phi
→ PHI is not invariant
```

Final sink classes included:

```text
ImageWrite[2]      payload data
ImageSampleRaw[2]  ImageAddress / coordinates
ReadConstBuffer[1] runtime byte offset
```

No resource-identity dependency was present in those final slot241 paths.

Fixes:

```text
53081ec  allow ImageWrite operand 2 only
c94816c  allow ImageSampleRaw operand 2 and ReadConstBuffer operand 1 as transitive runtime edges
```

Runtime result after `c94816c`:

```text
slot241 cleared
next blocker became slot245
```

This is an important negative-control result: safe runtime operands passed while real resource-identity dependency at slot245 remained blocked.

## Current first blocker

Last valid clean ASTRO run:

```text
Build: Source build c94816c-dirty
Shaders: VS 2 / PS 2 / CS 15 = 19
Target shader: 0x78af8e269b528b5c
Target SPIR-V generated: no
```

First blocker:

```text
slot245
LoadAddressU32
→ SelectU32
→ LogicalNot
→ INotEqual32
→ BitwiseAnd32
→ Phi
→ PHI is not invariant
```

Materialization still fails before target SPIR-V generation.

## slot245 / slot246 classification

Root characteristics:

```text
LoadAddressU32
MemoryInfo.kind = ScalarAddress
data_bits = 32
data_dwords = 1
access = Read
planning_only = true
PC = 0x2030
IsRawRead = true
IsShaderSideScalarRoot = true
```

Slot246 is a sibling root in the same address family.

Final source22 descriptor use:

```text
dword0: CompositeExtractU64 over slots245/246 arithmetic
dword1: BitwiseOr32 over same shared envelope
dword2: ReadConst[247]
dword3: immediate 0x00016204
```

Final consumers are `GetBufferResource` / raw `ReadConstBuffer` at PCs `0x2150`, `0x2170`, and `0x21f8` (two component consumers).

This is a **real buffer resource-identity dependency**, not an operand-role false positive. Do not widen `ShaderSideUseGraph` to accept `GetBufferResource`.

## Existing scalar-buffer BDA architecture

Relevant mechanisms:

```text
LowerScalarBufferReadToBda
IsShaderBdaBufferHandle
HasLaneDependentRawRead
ContainsLaneDependentAddress
CloneBdaExpression
ShaderSideSrtReadFlag
```

The source22 scalar-buffer memory shape passes the narrow raw BDA rules. Therefore current work should fix why this existing per-use BDA lowering does not survive, not invent a new descriptor architecture prematurely.

## Uncommitted B6 bug/fix

Proven bug: `CloneBdaExpression` originally cached only `Value`. On a cache hit, clone provenance (`changed`) was lost.

Result:

```text
dword0 clones shared subtree
→ cache populated

dword1 hits cached cloned subtree
→ returns cloned Value
→ but changed=false
→ parent dword1 treated as unchanged
→ stale original descriptor expression survives
```

A focused regression:

```text
fails on c94816c
passes with cache-provenance fix
```

Current local fix stores/propagates `Value + changed`.

This is a real generic bug even though the game did not advance after fixing it alone. Do not discard it.

## Current proven R1 follow-on defect

After the B6 cache fix, focused lifetime tracing proved:

```text
LowerScalarBufferReadToBda
→ creates per-use ReadConst clones with ShaderSideSrtReadFlag
→ complete cloned descriptor survives
→ RefreshShaderSideSrtEligibility scans those clones
→ ShaderSideUseGraph sees GetBufferResource
→ rejects resource identity
→ refresh clears ShaderSideSrtReadFlag
→ slot245/246 remain globally host-side/live
→ MaterializeResources evaluates original raw loop-carried PHI
→ slot245 fails
```

Observed:

```text
after-lower:   BDA clone flag = 1
after-refresh: BDA clone flag = 0
```

Post-refresh:

```text
slot245 shader_side = false
slot245 live_flat   = true
slot246 shader_side = false
slot246 live_flat   = true
```

Original wrappers are semantically dead/reference-only; this is not stale-original-wrapper liveness.

Classification:

```text
SRT4 / BDA5
global slot eligibility is conflated with per-use BDA clone eligibility
```

## Current active implementation

Preferred fix: explicitly track BDA-created `ReadConst` clones and preserve them across global eligibility refresh.

Conceptually:

```text
GLOBAL slot eligibility
  computed from ordinary/original wrappers

PER-USE BDA clone
  explicitly provenance-tagged/tracked
  excluded from global eligibility decision
  retains ShaderSideSrtReadFlag
```

Do not identify special clones solely by checking `ShaderSideSrtReadFlag`, because ordinary globally shader-side wrappers can also carry that flag.

Preferred implementation shape:

```text
Tracker-local set of exact BDA-created ReadConst Inst*
```

Then `RefreshShaderSideSrtEligibility` excludes those pinned clones from the global decision and does not strip their shader-side flag.

## Required next regressions

Keep the B6 shared-cache regression.

### R1 BDA-only

After BDA lowering + refresh:

```text
BDA clone flag remains set
no ordinary host semantic consumer remains
host flattened liveness for slot is false
BDA load remains present
```

### R1 mixed-use

Same slot has an ordinary host consumer plus a BDA-lowered consumer.

Expected:

```text
global slot shader_side = false
ordinary wrapper unflagged
BDA clone flagged
host flat slot remains live because ordinary consumer exists
```

This proves:

```text
GLOBAL host-side != PER-USE BDA host-side
```

Keep existing descriptor-identity negative tests.

## Next runtime validation

Only after:

```text
B6 regression pass
R1 BDA-only pass
R1 mixed-use pass
focused suite acceptable
git diff --check pass
```

perform one clean ASTRO run.

Baseline:

```text
VS 2 / PS 2 / CS 15
slot245 PHI is not invariant
```

Success criterion:

```text
slot245/246 no longer host-evaluated solely because of BDA per-use clones
```

If target shader `0x78af8e269b528b5c` finally emits, immediately run `spirv-val` and the undefined numeric-ID scan.

## Current do-not-do list

```text
DO NOT widen ShaderSideUseGraph for GetBufferResource
DO NOT solve loop-carried PHI on the host
DO NOT force lane0
DO NOT choose one PHI incoming edge
DO NOT add finite candidate caps without proof
DO NOT scan arbitrary memory for descriptors
DO NOT add slot245/246 special cases
DO NOT add shader-hash/PC production exceptions
DO NOT discard B6 cache fix
DO NOT run repeated ASTRO sessions before the R1 lifetime fix is tested
```
