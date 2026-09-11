# Current KytyPS5 debugging state

Last reconciled: 2026-09-11 22:04 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
Current source HEAD: de7d8b67b21adfe96ecdbd8573ea82b5418712c4 (`docs: reconcile compute replay checkpoint`)
Last ASTRO runtime source HEAD: 4374a9d9dbf5224fba68e5c9e3037196a0a13245
Runtime source HEAD at launch: 4374a9d9dbf5224fba68e5c9e3037196a0a13245
Working tree before this checkpoint: clean at `de7d8b6`; local install tree was refreshed from this build
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: 83CBAE098F0980B68971BE92179E6BF38B443C9328B7D29EB42729F071E1993F
Executable size: 21,020,160 bytes; local install refreshed 2026-09-11 22:04:26 Europe/Riga
Build label: Source build de7d8b6 (from generated `kytyGitVersion.h`); no ASTRO runtime launched from this build yet
Matching PDB: G:/KytyPS5/repo/_Build/windows/kyty_emulator.pdb and local install copy; SHA-256 CF2239F85E878EA9F71C2B18205829DA1BE1E49E7FDBF8554594B79CAB6CF79E; size 24,059,904 bytes; built 2026-09-11 22:04:11 Europe/Riga
Binary provenance: the historical dump/run used 4374a9d above; the current local install is a separate de7d8b6 build for the future capture

The system-wide CMake install prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M4 intro/loading — animated intro/scene rendered and was presented
Next milestone: M5 main menu
Current P0: obtain production-derived `ShaderComputeInputInfo` for CS `0x657ad04626bf9d55` so an exact replay can be built without guessed state
P0 class: replay provenance / pre-resource-plan observation; the bounded-root producer invariant is fixed generically by `edc7d4f` and must not be reopened without contradictory evidence
Last validated progress signal: 75 completed SPIR-V modules (38 CS / 22 PS / 14 VS / 1 MS); HostSubmit through tick=337559, HostWait tick=337558 Success, HostPresent/Flip completed, and an animated intro frame was visible
Known P1 likely blockers: classify any next post-intro boundary after the exact target state is captured; incorrect color/output interpretation remains P1 and is not a current fix target
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

## Prior focused validation

Passed after `edc7d4f`: `resource_materialization_tests`, `shader_cfg_tests`, `shader_recompiler_compute_tests`, and `scalar_provenance_tests`. The current `de7d8b6` build re-ran `shader_recompiler_compute_tests` successfully (`EXIT_CODE=0`, wall `8.867 s`; log `G:/KytyPS5/logs/shader_recompiler_compute_tests_ce32234.log`). `resource_tracking_tests` reaches the known unrelated baseline failure at `dynamic storage mips`; the new bounded-root regression passes before that baseline case.

## Production compute-state provenance

The existing target artifacts do not contain an authentic `ShaderComputeInputInfo` snapshot or capsule for `0x657ad04626bf9d55`. The target raw `.bin`/`.rdna2` and SPIR-V contain shader code/output, not PM4 register state. The only target-run field directly proven from existing evidence is `host_subgroup_size=32`, from the Vulkan subgroup report and `GetComputeProgram` selection. `user_data_base=0` is the compute `CompileOptions` default, and source ordering proves `dispatch_threads_num={0,0,0}` at the `CompileProgram` call because `RenderExecutor::DispatchDirect` assigns it only after `GetComputeProgram` returns.

The remaining target values are **UNAVAILABLE** without a new production capture: `threads_num`, `thread_ids_num`, `group_id`, `tg_size_en`, `workgroup_register`, `dispatch_thread_dimensions`, and `user_data count`. Their provenance is known: PM4 `COMPUTE_NUM_THREAD_X/Y/Z` and `COMPUTE_PGM_RSRC2` populate `CsStageRegisters`; `ShaderGetStaticInputInfoCS` copies those fields; `GetShaderParams` copies `cs_user_sgpr` into `options.user_data`; `ProgramCache::Get` passes the state into `CompileProgram`. The historical `ShaderDbgDumpInputInfo` near `0x9e8627388f138c1d` belongs to that other shader and cannot be reused.

No guessed replay candidate or new ASTRO run was made for this checkpoint. The new opt-in `ShaderReplayInput` markers record the actual state both before resource-plan extraction and immediately before `CompileProgram`; the next run must use them only to capture production values and then build one exact capsule.

## Focused validation and tomorrow

The diagnostic source change passed `git diff --check` and was committed as `ce32234`. A serialized Release build (`ninja -C _Build/windows -j1 kyty_emulator`) completed; the generated CMake install target still fails at the administrator-only system prefix, so the local install executable/PDB were copied directly from the build tree. `shader_recompiler_compute_tests` was rebuilt and passed (`EXIT_CODE=0`, wall `8.867 s`, log `G:/KytyPS5/logs/shader_recompiler_compute_tests_ce32234.log`).

Next action: when the no-run constraint is lifted, launch the current local install once with shader-debug enabled solely to capture the target's pre-resource-plan fields, then build one exact capsule from that production state. Do not generate a new exact capsule or change color semantics until those values are production-derived.
