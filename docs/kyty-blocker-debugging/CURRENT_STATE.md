# Current KytyPS5 debugging state

Last reconciled: 2026-09-10 23:06 Europe/Riga

This file is the volatile checkpoint. Durable facts live in `PROJECT_MEMORY.md`; stable mechanisms live in `REFERENCE.md`.

## Repository and provenance

```text
Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
HEAD at semantic source checkpoint: 1161113cb88d26355c1248c04632691b27f94726
Working tree: canonical debugging documentation is pending; source/test tree is clean at the semantic checkpoint
Compiler/configuration: clang-cl, Ninja, Release
Executable used for diagnostic run: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Diagnostic executable SHA-256: 3C430E1853381212F9DDBF64EC91CEB011FFDA26D84DFC23C8D385766BD0EE51
Diagnostic runtime label: Source build 3dc3e19; Vulkan pipeline cache disabled (dirty build)
```

The diagnostic executable was built before `1161113`; it must not be treated as completed-fix validation. The intended validation must be rebuilt and installed from the exact semantic commit with no dirty source/configuration inputs.

## Target and game milestones

```text
Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M1 startup shaders compile — target CS now emits SPIR-V and passes offline validation
Next milestone: M2 first graphics/compute submissions succeed
Current P0: prove the target shader pipeline and first submission on a clean exact-commit ASTRO run
Class: SPV2 was cleared diagnostically; active P0 is Vulkan pipeline/runtime (M2)
Last validated progress signal: diagnostic run emitted 21 modules (CS 17 / PS 2 / VS 2), reached the target wave64 compute, and did not reach a menu or visible frame
Known P1 likely blockers: descriptor/pipeline runtime compatibility, then GPU execution/rendering; media/input/game logic only after a frame
```

A generic fix may be correct without visible game progress. No claim is made for M2, M3, M4, M5, or M6 until the clean runtime observes the corresponding signal.

## Proven blocker transition

The bounded descriptor-source lifetime blocker (BS1/RES6) was cleared by `dfc7203`: the target path moved past materialization and reached target SPIR-V emission. The next blocker was target compute shader `0x78af8e269b528b5c` structured-control-flow validation. The generic CFG validation now routes a conditional loop exit through the existing dispatcher when its outside target is neither the loop merge nor continue block. The dispatcher emitter now keeps metadata-only handles and planning-only reads out of native spills and follows shader-side `ReadConst` aliases to their retained producer.

The earlier bounded-SRT evidence remains available at:

```text
G:/KytyPS5/notes/bounded-read-audit-09d5e94/
G:/KytyPS5/notes/ASTRO_BS1_SHADER_ARCHIVE_20260910/
```

## Current exact evidence

Diagnostic run (dirty source; evidence only):

```text
Run: G:/KytyPS5/logs/ASTRO_DISPATCH_SRT_20260911_0050_debug/
Target SPIR-V: G:/KytyPS5/logs/ASTRO_DISPATCH_SRT_20260911_0050_debug/shaders/0019_new_shader_cs_78af8e269b528b5c.spv
Target disassembly: ...spv.spvasm
Target size: 941,496 bytes
spirv-val --target-env vulkan1.3: PASS (exit 0)
Numeric disassembly scan: definitions=45,225, references=45,225, missing=0
Runtime stdout: initialized graphics, then `warning: executing wave64 compute shader cs=0x0000000908e86a00`
```

The process was stopped manually after the target emitted and validated; no pipeline-creation, submission, frame, menu, or gameplay signal was captured. The old diagnostic artifact in `ASTRO_DISPATCH_SRT_20260910_2320_debug` failed because it still contained the undefined ID `%28241`; do not use it as current evidence.

## Semantic source checkpoint

```text
src/graphics/shader/recompiler/ShaderRecompiler.cpp
  generic post-structurization check for non-merge loop exits; dispatcher fallback
src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp
  metadata/planning-only spill filtering and shader-side SRT alias resolution
 tests/shaderCfgTests.cpp
  dispatcher shader-side SRT alias emission regression
```

Commit `1161113` contains these generic changes and does not modify BDA/R1 logic. Temporary `TargetCfgDump` diagnostics and their CMake entry were removed; the saved shader archive was moved outside the repository.

## Focused validation

Passed after the current source changes: `shader_cfg_tests`, `resource_materialization_tests`, `shader_recompiler_compute_tests`, and `scalar_provenance_tests`. The new dispatcher alias fixture passes. `resource_tracking_tests` still reaches the known unrelated baseline failure: `dynamic storage mips: an inverted dynamic storage mip range was accepted or mutated output`.

Before a clean target run:

```text
run git diff --check and inspect staged paths
build/install Release from that exact commit
verify executable hash and Source build label
run exactly one ASTRO validation with the intentional shader/artifact flags
run spirv-val and numeric undefined-ID scan on the target module immediately
inspect first pipeline/submission/frame signal and checkpoint this file
```
