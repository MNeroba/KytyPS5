# Current KytyPS5 debugging state

Last reconciled: 2026-09-11 00:40 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
HEAD: 2a58297ae798127c5a3f544c0e9d835c93638626
Working tree: clean at this documentation checkpoint; runtime binary was built from source parent 2ae7feb
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: 39D69BE6C2956DECBBDC8D1FA1D3FA708B5232F3502F0984EE81BAAD3E5E252E
Executable size: 20,812,800 bytes
Runtime label: Source build 2ae7feb

The install-tree binary was built from exact HEAD with the local CMake install script. The system-wide prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M1 startup shaders compile — 44-module batch is offline-valid
Next milestone: M2 first graphics/compute submissions succeed
Current P0: stable Vulkan runtime stall at vkCreateComputePipelines for shader address 0x000000050069ec00, module 0022 hash 0x530dcd964f29983c
P0 class: M2 Vulkan pipeline/runtime; the call has no completion signal in the bounded trace
Last validated progress signal: 44 startup modules emitted in the clean batch; all passed SPIR-V validation and numeric ID scans. The latest exact-HEAD trace reached 23 modules, 37 QueuePoints, and successful CS/graphics pipeline creates before the current create call.
Known P1 likely blockers: first explicit vkQueueSubmit/timeline completion, GPU execution or synchronization, then presentation; three DrawFramebufferSkip records had no color/depth target, but this is not yet the P0.

M1 is closed diagnostically: the shader/recompiler path reaches valid artifacts. No M2, M3, M4, M5, or M6 claim is made until the corresponding runtime signal is observed.

## Exact runtime evidence

Clean artifact batch:

Run: G:/KytyPS5/logs/ASTRO_CLEAN_20260910_230833/
Source/runtime revision: clean semantic revision 46fa56c (2ae7feb contains documentation-only follow-up)
Artifacts: 44 SPIR-V modules (CS 22 / PS 14 / VS 7 / MS 1)
Validation: spirv-val --target-env vulkan1.3 passed 44/44; numeric disassembly scan found no missing IDs
Target module: 0019_new_shader_cs_78af8e269b528b5c.spv, 941,496 bytes, 45,225 definitions/references, missing 0
Runtime: initialized graphics and reached the wave64 compute warning; no fatal/validation error before bounded stop

Discriminating pipeline trace:

Run: G:/KytyPS5/logs/ASTRO_M2_TRACE_20260910_232151/
Executable: exact-HEAD install binary above; flags included shader/vulkan validation, GPU-assisted=false, graphics-debug-dump=true, printf-direction=File
Observed: 23 modules emitted before stop; 37 QueuePoints; 18 compute and 2 graphics pipeline creates completed with result=Success; 9 Equeue wait timeouts; no vkQueueSubmit, Flip done, or present marker was logged
Current boundary: module 0022 (SPIR-V size 1,157,956 bytes) passed spirv-val and numeric scan (55,709 definitions, 55,709 references, missing 0). The runtime log ends at PipelineTrace: vkCreateComputePipelines begin for that module; after more than 20 seconds the process showed no log growth and was force-stopped.
Runtime validation caveat: VK_LAYER_KHRONOS_validation was unavailable, so this run proves offline SPIR-V validity and pipeline-create return values only; it does not provide Vulkan validation-layer coverage.

The existing PipelineTrace instrumentation is enough to identify this call-site boundary. Do not add submit/wait instrumentation or launch another target run until this create either completes or a narrower static/isolated test shows it is not the current P0.

## Semantic checkpoint

Generic source fix remains in commit 1161113 and does not modify BDA/R1 logic:

src/graphics/shader/recompiler/ShaderRecompiler.cpp — structured loop-exit validation with dispatcher fallback
src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp — metadata/planning-only spill filtering and shader-side SRT alias resolution
tests/shaderCfgTests.cpp — dispatcher shader-side SRT alias regression

Documentation/runtime checkpoint commit: 2a58297 (runtime binary source parent 2ae7feb). Temporary TargetCfgDump diagnostics were removed; saved shader evidence is outside the repository under G:/KytyPS5/notes/.

## Focused validation and next action

Passed: shader_cfg_tests, resource_materialization_tests, shader_recompiler_compute_tests, scalar_provenance_tests, and the dispatcher alias fixture. resource_tracking_tests still reaches the known unrelated baseline failure: dynamic storage mips accepted an inverted range.

Next action: use existing logs and static source/module comparison to determine why valid module 0022 does not return from vkCreateComputePipelines. If that boundary clears, add the smallest scheduler Submit/MasterSemaphore wait markers and run one new bounded target capture to classify M2; then checkpoint this file.
