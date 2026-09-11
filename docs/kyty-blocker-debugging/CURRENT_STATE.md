# Current KytyPS5 debugging state

Last reconciled: 2026-09-11 23:02 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
Repository HEAD at this docs checkpoint: `3fe3d27` (docs-only commits after the runtime build; source/build revision remains `150a139`)
Current source HEAD: 150a1395c7553191a8e5f856b60cdea657034ed8 (`debug: persist complete shader replay input trace`)
Last ASTRO runtime source HEAD: 150a1395c7553191a8e5f856b60cdea657034ed8
Runtime source HEAD at launch: 150a1395c7553191a8e5f856b60cdea657034ed8
Working tree before this checkpoint: clean at `150a139`; local install tree was refreshed from this build
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: F4710707DC611196CD33C62F9BBDC4B7AAC366CAEB1AA9D8172ADAF793B09724
Executable size: 21,020,160 bytes; local install refreshed 2026-09-11 22:25:00 Europe/Riga
Build label: Source build 150a139 (from generated `kytyGitVersion.h`); diagnostic-only replay trace fields are enabled by the launch flags below
Matching PDB: G:/KytyPS5/repo/_Build/windows/kyty_emulator.pdb and local install copy; SHA-256 7F661D1EECEE0C3CCCE5BF3878D19191A4A928E9940F0FBB885399392A6B078A; size 24,059,904 bytes
Binary provenance: the historical dump/run used 4374a9d above; this bounded capture used the exact 150a139 executable/PDB above

The system-wide CMake install prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M5 main menu — title screen loaded and rendered
Next milestone: M6 gameplay
Current P0: classify the first post-M5 runtime failure at `MasterSemaphore::Wait` (`masterSemaphore.cpp:69`)
P0 class: GPU synchronization/runtime result; the exact `vk::Result` is not logged in this run
Last validated progress signal: M5 title screen reached after frame 495 at approximately 14 FPS; `LevelDocument Loaded: title_controller_ship [title]` and title-screen assets were logged
Known P1 likely blockers: incorrect color/output interpretation remains P1 and is not a current fix target
Known P2: cold-start large dispatcher pipeline compilation latency; successful creates are not a hang

M1, M2, M3, and M4 are closed. M5 was reached in the progression run below. Do not reopen the cleared startup SRT/BDA-lifetime, pipeline-create, submission, or visible-frame conclusions without contradictory evidence.

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

Before the bounded capture, the target raw `.bin`/`.rdna2` and SPIR-V could not supply PM4
register state; the historical `ShaderDbgDumpInputInfo` near `0x9e8627388f138c1d` belonged to
another shader. The source provenance remains: PM4 `COMPUTE_NUM_THREAD_X/Y/Z` and
`COMPUTE_PGM_RSRC2` populate `CsStageRegisters`; `ShaderGetStaticInputInfoCS` copies those
fields; `GetShaderParams` copies `cs_user_sgpr` into `options.user_data`; `ProgramCache::Get`
passes the state into `CompileProgram`.

The complete production values and paired trace are recorded in **Bounded production capture —
complete** below. No values were inferred from LocalSize and no replay candidates were guessed.

## Focused validation and tomorrow

The diagnostic source change passed `git diff --check` and was committed as `150a139`. A serialized
Release build (`ninja -C _Build/windows -j1 kyty_emulator shader_recompiler_compute_tests
shader_replay_tests`) completed; the generated CMake install target still fails at the
administrator-only system prefix, so the local install executable/PDB were copied directly from
the build tree. `shader_recompiler_compute_tests` passed (`EXIT_CODE=0`, wall `8.1447 s`, log
`G:/KytyPS5/logs/shader_recompiler_compute_tests_150a139.log`), and the replay test executable
help path also returned 0 (`G:/KytyPS5/logs/shader_replay_tests_help_150a139.log`).

Next action: classify the post-M5 semaphore failure from the preserved run artifact before any fix. Keep color correctness P1 and cold pipeline latency P2.

## Bounded production-capture attempt

The single authorized capture attempt from this checkpoint is incomplete and must not be
treated as target-game evidence. Run/artifact: `G:/KytyPS5/logs/ASTRO_REPLAY_INPUT_20260911_221647/`.
Provenance: source `3fb0ce7bf477ccda6f75593ed95926d6a785adec`, repository HEAD
`84853bcd9ce4b133ecd54e556754b883c3c9a9b8`, installed executable SHA-256
`F4334F4D39476C95BFA1ABB301D8C25D053DA3331B0B31E8F867823FEA63121C`.
The exact command is in `command.txt`; the wrapper recorded `exit_code=321`
(`0x00000141`) after 3.738 s. The process stopped at the known unsupported MIMG BVH
opcode `0xe6` because `--stub-bvh` was not supplied, before
`0x657ad04626bf9d55` was encountered. No `ShaderReplayInput` A/B record or exact capsule
was produced; `--printf-direction Silent` also omitted the `LOGF` trace lines. Do not
count this as a target capture or update M1–M4 from it.

## Bounded production capture — complete

Run/artifact: `G:/KytyPS5/logs/ASTRO_REPLAY_INPUT_20260911_2223148/`.
The one launch used source `150a1395c7553191a8e5f856b60cdea657034ed8`, executable SHA-256
`F4710707DC611196CD33C62F9BBDC4B7AAC366CAEB1AA9D8172ADAF793B09724`, matching PDB SHA-256
`7F661D1EECEE0C3CCCE5BF3878D19191A4A928E9940F0FBB885399392A6B078A`, and the exact command
is recorded in `command.txt`. It used the known-good `--stub-bvh` baseline, enabled only the
existing replay trace, and wrote the printf trace to `runtime.log` through the File sink.

Target CS `0x657ad04626bf9d55` was paired by `replay_invocation_id=76`: boundary A
`pre_resource_plan` is runtime.log line 828914 and boundary B `pre_compile` is line 828922.
Both records are complete and byte-for-byte identical after removing the phase name. Values:
`threads_num={32,2,1}`, `thread_ids_num=2`, `group_id={true,true,false}`, `tg_size_en=false`,
`workgroup_register=16`, `host_subgroup_size=32`, `dispatch_thread_dimensions=false`,
`dispatch_threads_num={0,0,0}`, `wave_size=64`, `lds_size_dwords=128`, `scratch_size_dwords=0`,
`user_data_base=0`, and ordered `user_data` count 16:
`[0x02dff4b0,0x00000005,0x055c0100,0xc1400000,0x001fc01f,0x91b00204,0x00000000,0x00000000,0x00000000,0x00000000,0x5ac08000,0x00080005,0x00100000,0x00005204,0x02dff2e0,0x00000005]`.

The target emitted 124012 SPIR-V words and pipeline creation completed successfully
(`runtime.log` line 830773, elapsed_ms=614). The wrapper stopped intentionally after both
records became durable at 2026-09-11 22:41:24; wall time was 545.1619612 s and no process exit
code is claimed. The immutable runtime capsule is
`shaders/original/precompile_cs_657ad04626bf9d55.capsule.json`, SHA-256
`38C3BAEE5D9D3DEFA65A396D15B3546A7C88F2C5913AE273477F55953AEA5E2F`.

Two offline exact replays both returned exit code 0 and internal validation PASS. Each emitted
124012 words with `OpExecutionMode %main LocalSize 32 1 1`, `uses_dma=true`, and SPIR-V SHA-256
`0644462056027C84D811B2D621EA5A51FCBFE07B31C629E533373A95DB67DFBC`; wall times were 114 ms
and 108 ms. External `spirv-val --target-env vulkan1.3` returned 0 for both outputs. This proves
production-state equivalence for the captured recompiler input, without changing M1–M4.

## Progression run — M5 reached

Run/artifact: `G:/KytyPS5/logs/ASTRO_PROGRESS_20260911_225139/`.
The one progression launch used the installed executable SHA-256
`F4710707DC611196CD33C62F9BBDC4B7AAC366CAEB1AA9D8172ADAF793B09724` with the known-good
`--stub-bvh` baseline; the exact command is in `command.txt`. The runtime label is `Source build
150a139`; the repository HEAD at launch was `3fe3d2718f154e9d3f8f133a0fb8a176c7004a63`.

M5 is proven logically: runtime.log records `LevelDocument Loaded: title_controller_ship [title]`
at lines 132658 and 750548, repeated `title_screen.spx` loads, and title assets
`titlescreen_start_text.jxm` and `astro_bot_logo_title.jxm` (lines 133922 and 133940). The process
window reached frame 495 at approximately 14 FPS; stdout reached `VS 14 | PS 22 | CS 40 | GS 1`.

After M5, the process terminated through the existing fatal-error path at runtime.log lines 780571–
780572 and stdout lines 107–125: `Not implemented (result != vk::Result::eSuccess)` in
`src/graphics/host_gpu/renderer/masterSemaphore.cpp:69`, the `vkDevice.waitSemaphores` result check
in `MasterSemaphore::Wait`. The run wrapper recorded wall time `550.8255477 s`, PID `26180`, and
no reliable process exit code (`exit_code` was unavailable; do not treat the reported `0x00000000`
placeholder as a clean exit). No new crash dump was created. The exact Vulkan result and a source
root cause remain unclassified for the next session; no fix was made in this run.
