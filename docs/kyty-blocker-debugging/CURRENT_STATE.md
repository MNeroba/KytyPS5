# Current KytyPS5 debugging state

Last reconciled: 2026-09-11 19:24 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
Current source HEAD: edc7d4f4aaef71df6d6dca0c80a8d5ff6d77cb1c (`shader: remap retained bounded descriptor roots`)
Last ASTRO runtime source HEAD: 4374a9d9dbf5224fba68e5c9e3037196a0a13245
Runtime source HEAD at launch: 4374a9d9dbf5224fba68e5c9e3037196a0a13245
Working tree before this checkpoint: source fix committed; documentation checkpoint is pending
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: 4059FF2D5CF62C6065AB754AC18D374D659C1E4F0829CE3BCC50F0CFF11A3D44
Executable size: 20,830,720 bytes; installed 2026-09-11 17:53:04 Europe/Riga
Runtime label: Source build 4374a9d
Matching PDB: G:/KytyPS5/repo/_Build/windows/kyty_emulator.pdb; SHA-256 A441659DB798E6D441E3AF6C7A82E625201EE46F0EE20E53AFEF572E13888EC4; size 23,953,408 bytes; written 2026-09-11 17:53:02 Europe/Riga
Binary provenance: dump/run used the exact installed executable and matching PDB from source HEAD above

The system-wide CMake install prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M4 intro/loading — animated intro/scene rendered and was presented
Next milestone: M5 main menu
Current P0: validate the bounded-root remap fix on ASTRO and classify the first post-intro boundary if termination persists
P0 class: runtime validation of the source fix; the prior shader/recompiler resource-plan invariant is closed by `edc7d4f`
Last validated progress signal: 75 completed SPIR-V modules (38 CS / 22 PS / 14 VS / 1 MS); HostSubmit through tick=337559, HostWait tick=337558 Success, HostPresent/Flip completed, and an animated intro frame was visible
Known P1 likely blockers: incorrect color/output interpretation after termination is classified; do not change color semantics yet
Known P2: cold-start large dispatcher pipeline compilation latency; successful creates are not a hang

M1, M2, and M3 are closed. Do not reopen the cleared startup SRT/BDA-lifetime, pipeline-create, submission, or visible-frame conclusions without contradictory evidence. M4 was reached; termination before M5 remains open.

## Exact runtime evidence

Clean artifact batch:

Run: G:/KytyPS5/logs/ASTRO_CLEAN_20260910_230833/
Source/runtime revision: clean semantic revision 46fa56c (later 2ae7feb and fd3b384 changes are documentation-only)
Artifacts: 44 SPIR-V modules (CS 22 / PS 14 / VS 7 / MS 1)
Validation: spirv-val --target-env vulkan1.3 passed 44/44; numeric disassembly scan found no missing IDs
Target module: 0019_new_shader_cs_78af8e269b528b5c.spv, 941,496 bytes, 45,225 definitions/references, missing 0

Complete latest runtime trace and crash evidence:

Run/archive: G:/KytyPS5/logs/ASTRO_HOSTTRACE_20260911_175340/
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe (exact 4374a9d binary above); matching symbols are in the build tree PDB above
Runtime: ASTRO BOT EU, input G:/PS5 Games/PPSA21567/extracted; final runtime.log write was 2026-09-11 18:02:58 Europe/Riga
Pipeline/command evidence: HostSubmit/HostWait/HostPresent/Flip completed; no Vulkan/device-lost/error is logged. Large compute creates, including 0x530dcd964f29983c (~152661 ms), eventually succeeded.
Termination: Windows Application Error 1000 status 0xc0000409; dump C:/Users/mneroba/AppData/Local/CrashDumps/kyty_emulator.exe.9700.dmp (82,354,794 bytes, SHA-256 6F67CB93120E117E699B4B5CEFC50BFB24791A7BE61D94EC78A3A1E0454FE578; PID 9700, created 2026-09-11 18:02:56 Europe/Riga). Exception record is C++ EH 0xe06d7363 with catchable std::out_of_range; UCRT abort fast-fail subcode 7 (FATAL_APP_EXIT) is the terminal wrapper, not the source attribution.
Throw path: exception thread 32576 (0x7f40); app call at RVA 0x20f021 to vector<IR::BufferResource>::_Xrange (RVA 0x188f90), from ExtractResourcePlan/ResourceControlFlow, source ResourceMaterialization.cpp:1838 (`program.info.buffers.at(memory.resource)`). Program.info.buffers size recovered as 7; numeric memory.resource is not present in the minidump, only proven >= 7.
Final shader context: CS hash 0x657ad04626bf9d55, code_words=2128; Decode/CFG/IR TranslateProgram completed, with no SPIR-V EmitProgram begin/done, PipelineCompile, or .bin/.spv artifact for this hash. Filesystem enumeration of world1_unlock/anim was concurrent; thread/frame evidence supports the recompiler path for the throw.
Last runtime progress: archive contains 75 completed modules (38 CS / 22 PS / 14 VS / 1 MS); final successful HostSubmit tick=337559 and HostWait tick=337558 result=Success elapsed_ms=3. No clean-shutdown or pipeline-cache-save evidence is present.
Exit provenance: the existing launch wrapper recorded no process exit code; stderr has no fatal/assert/exception/device-lost/error text and stdout has no clean shutdown. The dump and Application Error status are the available termination evidence.
Runtime validation caveat: VK_LAYER_KHRONOS_validation was unavailable; this evidence does not provide Vulkan validation-layer coverage.

## Semantic checkpoint

Generic source fix remains in commit 1161113 and does not modify BDA/R1 logic:

src/graphics/shader/recompiler/ShaderRecompiler.cpp — structured loop-exit validation with dispatcher fallback
src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp — metadata/planning-only spill filtering and shader-side SRT alias resolution
tests/shaderCfgTests.cpp — dispatcher shader-side SRT alias regression

Semantic source checkpoint: `edc7d4f` keeps `.at()` and fixes the producer path: retained bounded `ReadConstBuffer` roots now register their descriptor source through `AddBuffer` and receive `AddMemoryPatch`; ordinary bounded reads remain deferred. No catch-all handler, unchecked access, clamp, substitution, or color change was made. Temporary diagnostics and runtime artifacts remain outside the repository under G:/KytyPS5/notes/ and G:/KytyPS5/logs/.

Producer regression: `TestBoundedRootScalarBufferResourceRemap` builds seven ordinary dense buffers, retains a dynamic scalar-buffer root through `PlanBoundedRootReads`/`ApplyBoundedRootReads`, and starts that root with frontend-style `memory.resource=7`. Before `edc7d4f`, `ExtractResourcePlan` failed with `invalid vector subscript`; after it, the root maps to dense buffer 7 and the plan contains eight buffers.

## Focused validation and tomorrow

Passed after `edc7d4f`: `resource_materialization_tests`, `shader_cfg_tests`, `shader_recompiler_compute_tests`, and `scalar_provenance_tests`. `resource_tracking_tests` reaches the known unrelated baseline failure at `dynamic storage mips`; the new bounded-root regression passes before that baseline case.

Next action: build/install the Release emulator from `edc7d4f`, verify executable/PDB provenance, then make one narrowly scoped ASTRO run to test whether the post-intro out-of-range termination is gone. If it persists, capture only the first new boundary; do not reopen M1/M2/M3 or color semantics.
