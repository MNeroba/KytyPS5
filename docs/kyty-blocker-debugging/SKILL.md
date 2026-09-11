---
name: kyty-blocker-debugging
description: Evidence-driven workflow for clearing KytyPS5 shader/resource/runtime blockers with minimal false positives and minimal expensive target-game reruns.
---

# KytyPS5 blocker debugging

Optimize time to the next observable game milestone; count proven blockers cleared, not experiments or report length. Apply only within the user's requested scope.

## Startup and document ownership

Use the canonical project paths in `AGENTS.md`; the installed skill directory is a synchronized mirror.

1. Read `CURRENT_STATE.md`, then `PROJECT_MEMORY.md`.
2. Open only the relevant `REFERENCE.md` section if architecture is still unclear.
3. Inspect current source/tests/saved logs for the exact unknown.
4. Only if an exact semantic question remains, use [DEBUG_REFERENCE_SOURCES.md](G:/KytyPS5/repo/docs/kyty-blocker-debugging/DEBUG_REFERENCE_SOURCES.md) to select the smallest relevant external source set. No mandatory external search per blocker.

| Canonical home | Contents / update trigger |
|---|---|
| SKILL.md | Process and safety rules; change only for reusable lessons. |
| CURRENT_STATE.md | Volatile HEAD/worktree, executable/runtime provenance, target, milestones, P0, last progress, evidence, classification, artifacts, relevant baseline, next cheapest action. Replace stale checkpoints; aim for 50–100 lines. |
| PROJECT_MEMORY.md | Durable proven discoveries, roots, commits, negative evidence and expensive facts; concise fact + evidence + architecture link. |
| REFERENCE.md | Stable architecture/mechanisms; update when they change. |
| docs/DEBUG_REFERENCE_SOURCES.md | On-demand external evidence router. |

Update current state after meaningful source/branch, fix, test/runtime, milestone or P0 changes. Reconcile actual repository/session/artifacts before writing; label observed, inherited and unverified evidence by revision. Move durable results to memory without retaining duplicate investigations in current state.
Do not reopen an answered question unless current source, a regression or new runtime evidence contradicts it or makes it stale. Keep dumps and temporary hypotheses in investigation artifacts, not durable memory.

## GAME PROGRESS STRATEGY

Drive the work toward the target game's next observable milestone. Use these defaults and adapt their names or boundaries in `CURRENT_STATE.md` when the game has a different path:

```text
M0 executable starts
M1 startup shaders compile
M2 first graphics/compute submissions succeed
M3 first visible frame
M4 intro/loading sequence
M5 main menu
M6 gameplay
```

Classify every issue by its position on that path:

- **P0** — blocks the current milestone;
- **P1** — likely to block the next milestone on the same execution path;
- **P2** — correctness issue that is not currently blocking progress;
- **P3** — unrelated cleanup or a known baseline issue.

Always clear P0 first. Keep the critical path narrow: while the target is blocked earlier, do not spend time on unrelated warnings, old baseline failures, future subsystems, or broad completeness work. Prefer a narrow correct implementation that reaches the current milestone; never replace missing semantics with a hack.

Record progress signals separately from code correctness. Meaningful signals include a changed first blocker, increased shader count, a target shader beginning emission, SPIR-V validation, successful pipeline creation, the first GPU submission, the first visible frame, and arrival at a later game milestone. A generic fix may be valid without visible progress, but do not describe it as target-game progress until the runtime proves that step.

When the blocker changes stages, explicitly hand off the investigation and stop debugging the old domain: cleared SRT/materialization moves to SPIR-V validation; valid SPIR-V moves to Vulkan pipeline/runtime; a successful pipeline moves to GPU execution/rendering; a first frame moves to game logic, media, or input.

Primary metric: **time to the next observable game milestone**. Secondary metric: proven blockers cleared per wall-clock time. For each next action, choose the highest expected information gain or progress per unit time, in this order when applicable: existing logs → static proof → focused test → narrow patch → one game run. Avoid broad architecture exploration while a cheaper discriminating action exists.

## Debug tooling is a means, not the project goal

The primary objective is runtime progression of ASTRO toward the menu and gameplay. Extend offline replay, tracing, capture infrastructure, diagnostics, or test harnesses only when they are required to identify, reproduce, or prove a concrete runtime blocker.

Once the required evidence is captured and replay fidelity is established:

1. Stop expanding the diagnostic infrastructure.
2. Return immediately to the runtime path.
3. Identify the next actual blocker.
4. Make the narrowest semantic fix.
5. Validate progression in ASTRO.

Do not keep improving replay/debug infrastructure merely because additional fidelity, logging, metadata, or convenience could be added. For a production-state capture, add further trace fields or tooling only when the current capture proves that information required by `CompileProgram` is missing or ambiguous.

## Priority

The same P0–P3 rules apply to all debugging decisions. Do not reopen an architectural question already answered by `PROJECT_MEMORY.md` unless current source contradicts it, a regression contradicts it, or new runtime evidence makes it stale.

## Progress reporting

During a long investigation, post one concise update when the P0 is confirmed or reclassified, a hypothesis is proven or disproven, fail-before is reproduced, a generic fix is implemented, focused tests complete, a semantic commit is created, runtime provenance is verified, the target advances, the blocker/domain changes, a new exact runtime error is found, or work enters a long build/run. Use:

```yaml
CURRENT P0:
WHAT WAS PROVEN:
WHAT CHANGED:
NEXT ACTION:
```

Do not report every command. When a blocker closes, explicitly report `CLOSED`, `COMMIT`, `EVIDENCE`, and `NEW P0`.

## FAST PATH

Once exact failing path + incorrect decision/pass + correct invariant + focused reproduction are known, stop broad investigation and proceed to regression/fix/validation.

## Replay fidelity

Raw guest shader code does not encode the live compute specialization. Before making a replay capsule, trace the production `ShaderComputeInputInfo` and `CompileOptions` values at the PM4-to-`CompileProgram` boundary, including workgroup dimensions, resource namespace flags, subgroup size, dispatch fields, user-data base, and user-data count. If a crash can occur during resource-plan extraction, persist the raw code and a diagnostic input snapshot before that interval, then record the final state immediately before `CompileProgram`. Mark missing values as unknown; do not infer them from `LocalSize`, IR builtin usage, another shader, or guessed candidates. A shape replay is useful for generic tests but cannot be called an exact production replay until its state and specialization are captured from the target path.

## PATCH THRESHOLD

Patch when:
- root cause is isolated;
- correct invariant is known;
- a focused regression can reproduce it where practical;
- the proposed fix preserves semantic safety boundaries.

Once satisfied, stop collecting redundant evidence. If a regression is impractical, record why and the substitute causal proof; do not waive semantic safety.

## Blocker-clearing sequence

1. Check repository and saved runtime provenance using the contract below. Reuse existing reproduction; do not launch a game merely to start.
2. Record first error, shader/stage/PC, slot/source/resource, shader count and target SPIR-V presence. Assign one primary class.
3. Trace root → wrappers → semantic consumers → incorrect rejection/liveness/pass decision → failing evaluator/emitter.
4. Form one falsifiable hypothesis and choose the cheapest discriminating source inspection, saved-artifact analysis, fixture or narrow diagnostic. If an approach yields no discriminating evidence in roughly 15–20 minutes, change approach.
5. Apply FAST PATH / PATCH THRESHOLD, then follow this order:

```text
prove root cause
→ regression fail-before on parent/baseline
→ minimal generic fix
→ regression pass-after
→ focused tests (including required rewrite lifetime checks)
→ PRE-COMMIT HYGIENE
→ git diff --check
→ semantic commit
→ clean build/install from that exact commit
→ verify Source build / executable provenance
→ ONE target-game validation run
→ validate generated artifacts
→ classify next P0 and checkpoint
```

A game run before the semantic commit is allowed only as explicitly labeled diagnostic evidence collection, never validation of a completed fix. State the missing observation first; use a narrow, semantics-preserving diagnostic and remove temporary instrumentation afterward.
Do not repeat game runs without a source change or a new discriminating diagnostic purpose.

## Causal checks (only where relevant)

- **Host evaluator / PHI:** prove why the expression is host-live and which semantic consumer requires it. Inspect existing per-use lowering before extending host evaluation. A PHI error is a sink, not a root cause.
- **Eligibility:** classify opcode + operand index + semantic role + shortest root path + resource-identity dependency. Enumerate distinct rejection classes for the affected root before widening a rule; key by opcode/operand/block/PC/rule. Use cycle-safe memoized traversal, not exponential PHI unfolding.
- **Existing mechanism:** inspect current helpers, tests, pass ordering and fallback/liveness paths before designing another descriptor, BDA, CFG or materialization subsystem. Ask why the existing path did not fire or survive.
- **Rewrite lifetime:** for pass-ordering, metadata, clone or liveness fixes, regression must inspect after lowering, later tracking/refresh/DCE, ResourcePlan extraction and materialization; check emission when relevant. Check flags, semantic users, descriptor arguments, planning-only state and host flattened liveness. Local rewrite success is insufficient.
- **Regressions:** prefer semantic assertions. For eligibility/resource changes, include positive, negative identity and transitive-negative cases where relevant; mixed-use coverage when global and per-use state differ.
- **No immediate game progress:** retain a proven generic fix. Check later-pass undo, sibling dependencies, fixture coverage and stale executable before dismissing it.

## Semantic safety boundaries

- No game/shader-hash, slot or PC production exceptions.
- Preserve resource identity versus payload/coordinate/offset roles. Never allow an entire opcode because one operand is safe, or bypass identity safety through a sibling operand. Dedicated dynamic-resource lowering needs semantic proof.
- **GLOBAL slot eligibility != PER-USE shader-side/BDA lowering.** Do not promote an entire slot for one safe use or strip a proven per-use clone because another use stays host-side. Use explicit clone provenance; a shared flag alone does not establish origin.
- Do not force lane 0, choose a PHI incoming edge, approximate true loop-carried ReadLane/BVH state with host fixed points, or use arbitrary iteration limits.
- No arbitrary candidate caps, descriptor/guest-memory scans, fallback constants or dummy descriptors. Zero/null fallback requires semantic proof. Bounded plans require a proven finite domain.
- Preserve raw-memory, descriptor base/stride/bounds and active-lane semantics; do not invent unsupported guest behavior.
- Do not repurpose FaultBuffer for general diagnostics. Do not equate BVH miss-stub progress with real BVH semantics.
- A helper returning true or a game advancing does not prove correctness.

## Runtime provenance contract

Before interpreting a run, retain:
- full `git rev-parse HEAD`, branch/worktree path, `git status --short` at build and launch, including relevant submodule/dirty inputs;
- exact build/install commands, compiler/configuration and source revision;
- launched executable absolute path, SHA-256, size and UTC timestamp; compare build/install hashes when using install;
- runtime `Source build ...` label, exact game input/version when known, working directory, arguments/feature flags, relevant environment and log/artifact directory.

For completed-fix validation, commit first and build/install cleanly from that exact commit with no dirty source/configuration inputs; use a separate clean checkout if unrelated work must remain. Verify generated build metadata and executable hash before launch, then confirm the runtime label in the captured log. A label alone is insufficient; an old binary can retain a plausible label.
A stale/mismatched/dirty-uncertain run cannot validate the intended commit. Keep it labeled diagnostic or historical; do not infer provenance from filenames or current HEAD.
Use the same intentional baseline flags, clear stale trace variables and temporary substitutions, and configure required artifact capture before the one validation run.

## Artifact validation and exit

If target SPIR-V is emitted, run `spirv-val` with the applicable target environment and a numeric undefined-ID scan immediately, before broad Vulkan/GPU debugging. Preserve validator version, command, output and original artifact identity.
For GPU execution blockers require runtime validation/capture; for RT/BVH distinguish stub progress from real semantics.
Record old blocker cleared/still failing, new first blocker, counts and target emission. Even identical error text may represent a new causal chain.
Claim only the level proven: offline regression, target-path advancement, valid SPIR-V, GPU execution or later milestone. Do not claim runtime clearance from offline tests alone.

## PRE-COMMIT HYGIENE

Inspect `git status --short`, `git diff`, and `git diff --cached`. Remove/revert only owned temporary instrumentation, debug prints, scratch/build outputs and unnecessary dumps/logs.
Preserve evidence needed for regression, reproduction, provenance or state/memory outside the commit unless intentional test data/documentation.
Stage only the generic fix, regression and directly required docs; verify no unrelated staged files. Run `git diff --check` before committing.
Never use broad destructive cleanup such as `git clean -fdx` without first reviewing exactly what will be removed (matching dry run); preserve evidence and unrelated work.

## Classification lookup (do not investigate every class)

| Family | Classes |
|---|---|
| P | P1 stale executable; P2 wrong tree; P3 dirty build uncertainty; P4 different-revision log |
| D | D1 unsupported instruction; D2 opcode mapping; D3 missing semantics |
| CFG | CFG1 structural branch/diamond; CFG2 loop-carried PHI; CFG3 contextual clone; CFG4 dominance/SSA; CFG5 active-lane semantics |
| SRT | SRT1 host materialization; SRT2 operand-role false positive; SRT3 flat liveness; SRT4 global/per-use conflation; SRT5 stale/planning-only wrapper; SRT6 cross-slot dependency |
| RES | RES1 buffer identity; RES2 image identity; RES3 sampler identity; RES4 missing bounded domain; RES5 indirect shape; RES6 invalidated descriptor source |
| BS | BS1 stale bounded-plan IR reference (RES6 / lifetime) |
| BDA | BDA1 unsupported scalar-buffer semantics; BDA2 lane provenance; BDA3 cloning; BDA4 cache provenance; BDA5 lifetime undo; BDA6 stale host dependency; BDA7 bounds/base/stride |
| SPV | SPV1 undefined ID; SPV2 structure/dominance/SSA; SPV3 type; SPV4 capability/extension; SPV5 omitted producer; SPV6 physical address |
| GPU | GPU1 descriptors; GPU2 BDA/page table; GPU3 images; GPU4 synchronization; GPU5 robustness/null descriptor; GPU6 device-specific |
| RT | RT1 decode/lowering; RT2 node layout; RT3 traversal state; RT4 address calculation; RT5 stub boundary |

## Concise checkpoint/report

Report HEAD/worktree; executable/hash/build label; milestone/P0/class; root and decisive evidence; fix; fail-before/pass-after; focused tests and known baseline; game result; target SPIR-V/validator/ID-scan result; semantic commit; next cheapest action.
Assess false-positive risk only with reasons: proven roles/backend support plus negative regressions lower risk; missing end-to-end evidence raises it; broad eligibility or host approximations are high risk.
Use artifact paths instead of large dumps. For upstream submission, consult contribution policy and disclose AI assistance where required.
