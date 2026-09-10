# Current KytyPS5 debugging state

Last reconciled: 2026-09-10 23:36 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
Runtime source HEAD at launch: 2ae7febc495a331809307bafea2f92d790988a66
Working tree before this checkpoint: clean
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: 39D69BE6C2956DECBBDC8D1FA1D3FA708B5232F3502F0984EE81BAAD3E5E252E
Executable size: 20,812,800 bytes
Runtime label: Source build 2ae7feb
Binary provenance: built and installed from exact source HEAD above; all later commits in this checkpoint are documentation-only

The system-wide CMake install prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M2 pipeline/command path — pipeline creation, graphics command recording, EndOfPipe signals, and guest flip/video-output path reached
Next milestone: M3 first visible host frame
Current P0: identify the first stable runtime boundary after the proven pipeline/command path
P0 class: M2 post-pipeline runtime; submit completion, GPU wait, and host presentation are not yet separated
Last validated progress signal: complete latest runtime log has matching compute pipeline begin/done pairs, successful graphics pipeline creation, BeginRendering/DrawComplete, QueuePoint entries through submit=7, EndOfPipe signals/events, and guest flip/video-output activity
Known P1 likely blockers: command submission/timeline completion, GPU execution or synchronization, then host Present; no visible frame is proven

M1 is closed: the clean 44-module startup batch passed offline SPIR-V validation and numeric missing-ID scans. Do not reopen SRT/materialization or the cleared pipeline-create hypothesis without contradictory evidence.

## Exact runtime evidence

Clean artifact batch:

Run: G:/KytyPS5/logs/ASTRO_CLEAN_20260910_230833/
Source/runtime revision: clean semantic revision 46fa56c (later 2ae7feb and fd3b384 changes are documentation-only)
Artifacts: 44 SPIR-V modules (CS 22 / PS 14 / VS 7 / MS 1)
Validation: spirv-val --target-env vulkan1.3 passed 44/44; numeric disassembly scan found no missing IDs
Target module: 0019_new_shader_cs_78af8e269b528b5c.spv, 941,496 bytes, 45,225 definitions/references, missing 0

Complete latest runtime trace:

Run: G:/KytyPS5/logs/ASTRO_M2_TRACE_20260910_232151/
Executable: exact source-parent 2ae7feb install binary above
Flags: shader/vulkan validation enabled, GPU-assisted=false, graphics-debug-dump=true, printf-direction=File
Pipeline evidence: 18 vkCreateComputePipelines begin lines and 18 matching done result=Success lines; graphics pipeline creation also returned Success
Command evidence: BeginRendering and DrawComplete reached; QueuePoint DispatchDirect reached with submit indices including submit=7; EndOfPipe signals and event waits were observed
Presentation-path evidence: guest flip/video-output work was reached; no host Present result and no visible frame were proven
Final shader context: address 0x000000050069ec00, hash 0x530dcd964f29983c, SPIR-V EmitProgram words=289489, dispatch groups=1x1x1, local=16x16x1, buffers=10, textures=30, sampled=28, storage=2, samplers=6; the log ends during its descriptor/runtime dump
Latest artifact: G:/KytyPS5/logs/ASTRO_M2_TRACE_20260910_232151/shaders/0022_new_shader_cs_530dcd964f29983c.spv (1,157,956 bytes), spirv-val clean and numeric scan clean (55,709 definitions, 55,709 references, missing 0)
Correction: the previously suspected long vkCreateComputePipelines call eventually completed. No persistent pipeline-create failure or hang is proven. The diagnostic process was stopped while the trace was still making progress or logging this dispatch.
Runtime validation caveat: VK_LAYER_KHRONOS_validation was unavailable; this evidence does not provide Vulkan validation-layer coverage.

## Semantic checkpoint

Generic source fix remains in commit 1161113 and does not modify BDA/R1 logic:

src/graphics/shader/recompiler/ShaderRecompiler.cpp — structured loop-exit validation with dispatcher fallback
src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp — metadata/planning-only spill filtering and shader-side SRT alias resolution
tests/shaderCfgTests.cpp — dispatcher shader-side SRT alias regression

Documentation checkpoint: current repository commit after the runtime capture. Temporary TargetCfgDump diagnostics were removed; saved shader evidence is outside the repository under G:/KytyPS5/notes/.

## Focused validation and tomorrow

Passed: shader_cfg_tests, resource_materialization_tests, shader_recompiler_compute_tests, scalar_provenance_tests, and the dispatcher alias fixture. resource_tracking_tests still reaches the known unrelated baseline failure: dynamic storage mips accepted an inverted range.

Tomorrow first action: read CURRENT_STATE.md, PROJECT_MEMORY.md, and the relevant REFERENCE.md section; inspect the final runtime context for address 0x000000050069ec00/hash 0x530dcd964f29983c and determine whether the stop was diagnostic, logging/descriptor work, slow pipeline processing, or command submission/GPU wait. Prefer existing evidence and source inspection; if a new run is necessary, give it one purpose: find the first unmatched boundary among dispatch preparation, pipeline create, command submission, GPU completion/wait, and flip/present. Add only minimal markers, then build a focused regression before semantic changes.
