# Current KytyPS5 debugging state

Last reconciled: 2026-09-10 23:20 Europe/Riga

This file is the volatile checkpoint. Durable facts live in `PROJECT_MEMORY.md`; stable mechanisms live in `REFERENCE.md`.

## Repository and provenance

```text
Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
HEAD at documentation/runtime checkpoint: 46fa56c61052d3916836863e4a8afc4d3c649388
Working tree: this checkpoint update is pending; source/test tree is clean at the semantic checkpoint
Compiler/configuration: clang-cl, Ninja, Release
Executable used for diagnostic run: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Clean executable SHA-256 (build/install): AF740B8B2EE49CE957CFD77134FF515E5260D4EAEBA3257908ADAA53EEF29AEE
Clean runtime label: Source build 46fa56c; Release, clang-lld_link-64
```

The executable was built and copied into the local install tree from clean HEAD `46fa56c`; the system-wide CMake install prefix was not used because it requires administrator access.

## Target and game milestones

```text
Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M1 startup shaders compile — clean run emitted 44 modules and all passed offline validation
Next milestone: M2 first graphics/compute submissions succeed
Current P0: classify the stable post-emission runtime stall at the pipeline/submit/wait boundary before the first observable frame
Class: SPV2 is cleared; active P0 is Vulkan pipeline/runtime (M2)
Last validated progress signal: clean run emitted 44 modules (CS 22 / PS 14 / VS 7 / MS 1), reached target wave64 compute, and produced no fatal/validation error before bounded stop
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

Clean exact-commit run (runtime trace intentionally incomplete):

```text
Run: G:/KytyPS5/logs/ASTRO_CLEAN_20260910_230833/
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Arguments: --stub-bvh, shader/vulkan validation=true, GPU-assisted=false, graphics-debug-dump=true, shader logs in run/shaders
Artifacts: 44 `.spv` + 44 `.bin` + 44 `.rdna2`
spirv-val --target-env vulkan1.3: 44/44 PASS (exit 0)
Numeric disassembly scan: 44/44 clean; target 0019 definitions=45,225, references=45,225, missing=0
Runtime stdout: initialized graphics, then `warning: executing wave64 compute shader cs=0x0000000908e86a00`
```

The process was force-stopped after roughly nine minutes and 44 artifacts; no fatal/validation error, pipeline trace, submission, frame, menu, or gameplay signal was captured. Because `GraphicsRunDebugDumpEnabled()` is gated by non-silent printf output, this run cannot classify pipeline versus submit/wait. The prior dirty artifact in `ASTRO_DISPATCH_SRT_20260910_2320_debug` failed on undefined `%28241`; do not use it as current evidence.

Next discriminating runtime capture: repeat one bounded run only when needed with `--printf-direction File --printf-output-file <run>/runtime.log` and `--graphics-debug-dump true`, then inspect the first `QueuePoint`, `vkCreate*Pipeline`, submit, wait, or fatal line. This changes observability, not shader semantics.

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

Before the next instrumented target run:

```text
verify git status, HEAD, executable hash, and Source build label
use printf-direction File plus printf-output-file so graphics debug traces are enabled
run one bounded ASTRO validation with the intentional shader/artifact flags
inspect the first pipeline/submit/wait signal; then checkpoint this file
```
