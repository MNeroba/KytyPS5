# Current KytyPS5 debugging state

Last reconciled: 2026-09-12 23:30 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/repo
Branch: astro/materialize-resources
Repository HEAD: `5e2e21995c80a5b29bb84dde8e43d110c1cd6375` (`shader: stop decode at completed unreachable backedge`)
Current semantic source HEAD: `5e2e21995c80a5b29bb84dde8e43d110c1cd6375`
Last ASTRO runtime source HEAD: `44a52db92cc48b32096987b8e619874db0dbb607` (raw-program capture)
Working tree for this checkpoint: clean after the semantic decoder fix
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256: `D22B5CE4874090D6F867D76DECD01A14680CDADBDED26A0EECC8648A50E4142D`
Executable size: 21,055,488 bytes; local install refreshed from `5ebf00a`
Build label: `Source build 5ebf00a`
Matching PDB: G:/KytyPS5/repo/_Build/windows/kyty_emulator.pdb and local install copy; SHA-256 `1E1ED04D7AABD48A54C31BCFF2D821562920473CD0B815CA0D58EDCC0D588646`
Binary provenance: installed executable/PDB are the current bounded-snapshot build above

The system-wide CMake install prefix was not used because it requires administrator access.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5 Games/PPSA21567/extracted
Current milestone: M5 main menu — reached in the validated title-screen progression run
Next milestone: M6 gameplay
Current P0: validate the generic MS decoder traversal fix at runtime; the prior `pc=0x00003e74`, raw `0xc2208080` failure was proven to be unreachable tail data after a completed debug backedge
P0 class: frontend decode stream traversal (static root cause fixed; runtime validation pending)
Last validated progress signal: offline production raw walk reaches `S_ENDPGM` at `0x3d30`, its CDBGSYS target path, and completed `S_BRANCH` backedge at `0x3dac`; focused decoder and compute tests pass. No ASTRO run has been made from `5e2e219` yet.
Known P1 likely blockers: incorrect color/output interpretation remains P1 and is not a current fix target
Known P2: cold-start large dispatcher pipeline compilation latency; successful creates are not a hang. Cache persistence/load is proven, but a substantial warm-time reduction is not.

M1, M2, M3, M4, and M5 are closed/reached. M6 gameplay is not proven. Do not reopen the cleared startup SRT/BDA-lifetime, pipeline-create, submission, visible-frame, bounded resource-remap, scalar-PHI materialization, or TTMP scalar-source conclusions without contradictory evidence.

## MS decoder tail traversal fix — 2026-09-12

Production raw program: `G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/shaders/original/precompile_ms_2b3be82b8235ac05.bin`, 16656 bytes / 4164 dwords, SHA-256 `A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`.
The bounded sequential walk from `0x3c00` reaches `S_ENDPGM` at `0x3d30`, its
`S_CBRANCH_CDBGSYS` target at `0x3d34`, and the unconditional debug backedge
`S_BRANCH 0x3ca4` at `0x3dac`. No branch target enters `[0x3db0,0x3e74]`; the preceding
`0x3e70` is a one-dword `V_SUB_F32`, so `0xc2208080` is unreachable tail data. Kyty and
Prosper both classify high-six-bit `0x30` as unknown; public RDNA2 SMEM uses `0x3d`.

Commit `5e2e219` generically tracks pending forward targets, ignores branch targets beyond
the code span, and stops after a completed unconditional backedge once the post-END target
path is visited. `shader_cfg_tests`, `ctest -R ^shader_cfg$`, and
`shader_recompiler_compute_tests` pass. This is static closure only; runtime validation and
the next genuine blocker remain outstanding. Do not add a Family::0x30 decoder case.

## Prosper-class GDS audit and bounded snapshot result — 2026-09-12

Static audit artifact: `G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_20260912_205915/gds-audit.txt`.
Kyty's GDS lowering, binding/layout assignment, PM4 DMA_DATA GDS handling (including immediate
zero reset), and CPU-mediated PM4 indirect argument visibility are proven by source and focused
tests. Five ASTRO GDS compute modules reached successful emission at compute binding 45; no
rejection, omission, or pipeline failure was logged. Prosper's binding 127 is not applicable.

The generic bounded tick-keyed GPU command/resource snapshot is committed in `5ebf00a`. It retains
at most 64 submitted host-tick batches, 128 commands per command buffer, and 96 buffers/images per
command, and is dumped from the existing device-loss path. Focused post-commit checks passed:
`shader_recompiler_compute_tests --scheduler-only`, full `shader_recompiler_compute_tests`, and
`shader_cfg_tests` (exit 0).

The one permitted ASTRO run is `G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_ASTRO_20260912_213444/`, launched
with the command in `command.txt`, source `5ebf00a`, EXE SHA-256
`D22B5CE4874090D6F867D76DECD01A14680CDADBDED26A0EECC8648A50E4142D`, and PDB SHA-256
`1E1ED04D7AABD48A54C31BCFF2D821562920473CD0B815CA0D58EDCC0D588646`. It reached 46 CS / 34 PS /
21 VS / 2 GS and successful HostPresent/Flip activity, then terminated with wrapper status `321`
(`0x141`) while decoding MS `0x2b3be82b8235ac05`: `unknown RDNA2 instruction family` at
`pc=0x00003e74`, raw `0xc2208080`, `ShaderDecoder.cpp:388`. No `GPU_DEVICE_FAULT` or
`GPU_COMMAND_SNAPSHOT_WINDOW` was emitted because device loss was not reached. The snapshot run is
complete; do not rerun ASTRO or fix this decoder blocker in the same checkpoint.

NEXT ACTION: run one narrow runtime validation from `5e2e219`; if the decoder passes, classify only the first later runtime blocker. Do not add a Family::0x30 case.

## Decoder regression comparison — superseded by 5e2e219

`git merge-base --is-ancestor 2a51379 5ebf00a` succeeded. Current
`ShaderDecoder.cpp` still recognizes SOPP through the unchanged family discriminator
(`(word & 0xc0000000) == 0x80000000` and opcode field `0x7f`); the `0x17` entry is present in
`SOPP_OPCODE_LIST`. Runtime raw `0xc2208080` has SOPP gate `0xc0000000` and
`word >> 26 == 0x30`, which is not one of the supported family cases, so it reaches the existing
unknown-family failure. Commit `2a51379` changed the CDBGSYS opcode/CFG handling but did not change
the family discriminator.

The regression `TestNewShaderRecompilerSoppCdbgSys` is called by the normal `shader_cfg_tests`
`main()` and was run separately with a temporary selector (exit 0), then the selector was fully
reverted. Its fixture encodes `0xbf970001` from `EncodeSopp(0x17, 1)`, starts at word 0, and
recompiles as `ShaderType::Compute`; it does not exercise runtime `0xc2208080`, mesh stage, or the
production preceding dword. The runtime failure is mesh stage, hash `0x2b3be82b8235ac05`, PC
`0x00003e74` (4-byte aligned); the preceding dword was not captured. Comparison artifact:
`G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_20260912_205915/decoder-cdbg-comparison.txt`.

The production stream and bounded walk are now preserved in `MS_RAW_CAPTURE_20260912_2240`;
the generic traversal regression and focused tests are recorded in the superseding section above.

## MS decoder boundary audit — superseded by 5e2e219

The earlier artifact `G:/KytyPS5/logs/MS_BOUNDARY_AUDIT_20260912_2205/boundary-audit.txt`
captured only the missing-stream state. The complete target is now preserved at
`G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/shaders/original/precompile_ms_2b3be82b8235ac05.bin`
(16656 bytes; SHA-256 `A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`).

The bounded walk reaches `S_ENDPGM` at `0x3d30`, its CDBGSYS target, and the unconditional
debug backedge `S_BRANCH 0x3ca4` at `0x3dac`; no branch target enters `[0x3db0,0x3e74]`.
The preceding `0x3e70` is a one-dword `V_SUB_F32`, and Prosper independently agrees that
high-six-bit `0x30` is unknown while RDNA2 SMEM uses `0x3d`. The former runtime word is
therefore unreachable tail data (classification C), not a new instruction family.

## Static submit-6256 reconstruction checkpoint — 2026-09-12

Artifact: `G:/KytyPS5/logs/DEVICE_LOSS_STATIC_20260912_2035/submit6256_reconstruction.txt`.
The source artifact is `568f00b`; the exact fault artifact is
`G:/KytyPS5/logs/ASTRO_NON_EOP_DIAG_20260912_1340/`. Offline inspection reconstructed the
recoverable master-timeline order for ticks `329198..329237`: submit `6255` contributes the
329198–329200 lead-in, then submit `6256` contains repeated EOP writes and the bounded
last-non-EOP records expose dispatches at guest code pointers
`0x0000000908e86a00` (3072/65536 groups, mode `0x61`), `0x000000050052fc00` (131072 groups,
mode `0x41`), and `0x000000050052a400` (4096 groups, mode `0x41`), plus a later DrawIndexAuto
record at tick `329223`. Ticks `329215..329220` are absent from the two 16-entry failure windows.

This is a command-buffer summary, not a complete command stream: `CommandProcessor` reuses guest
submit id `6256` across slices, while each progressed slice can produce a new host command buffer
and master tick. `CommandBuffer::SetDebugInfo` retains only the final operation and final
non-EOP Dispatch/Draw metadata. The artifact has no failing-submit shader/hash pairing, descriptor
snapshot, buffer/image/BDA range, allocation lifetime, page-table state, image layout/access state,
or per-submit barrier trace. The 36 device-fault addresses are type-4 instruction-pointer records,
not resolvable host resource addresses. The same-run target hash `0x657ad04626bf9d55` therefore
cannot be assigned to submit `6256`, and no stale resource, invalid range, or synchronization/layout
violation is proven.

The single missing runtime fact is a crash-safe active command-buffer snapshot keyed by host tick,
including same-invocation shader/pipeline identity, descriptor-derived resources and host BDA ranges,
allocation deleted/retired state, and image layout/access state. Do not enable address-binding
diagnostics until static evidence identifies a suspicious host GPU VA. No ASTRO run or semantic
change was made in this checkpoint.

## Submit 6683 correlation checkpoint — 2026-09-12

Run/artifact: `G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_DISPATCH_20260912_2022/`. The temporary
bounded trace was launched from source HEAD `f522eba136e22e42368383bb1acd9e22c7a40131`;
its exact build hashes and command are in `provenance.txt` and `command.txt`. The trace was
reverted afterward, and the installed executable/PDB were restored to the clean hashes recorded
above.

The target guest address `0x000000050052a400` is now proven, in the active dispatch path, to map
to CS `0x657ad04626bf9d55` with groups `4096x1x1`. Its `after_cmd_dispatch` marker precedes
`HostSubmit done ... tick=353333 result=Success`; the corresponding wait began with
`known=353332 current=353334` and completed successfully in 26 ms. Submits carrying
`debug_submit=6683` continued through tick `353496`; the latest observed wait (tick `353490`)
also completed successfully.

This capture contains no `ErrorDeviceLost`, `GPU_DEVICE_FAULT`, or `GPU_CHECKPOINT` record. It
ended with wrapper status `321` (`0x141`) at the known decoder baseline
(`ShaderDecoder.cpp:388`), without a new crash dump or clean-shutdown record. The target
after-rebind snapshot shows the expected 512 MiB page table and 8 MiB fault buffer; every
non-null target host buffer was live (`deleted=false`) with a valid BDA.

Therefore submit `6683`/tick `353333` is not on the failing device-loss chain in this run. The
prior fault artifact still records `ErrorDeviceLost` on submit `6256` (ticks `329214` and
`329237`) with the same guest address, so the address/shader remains a causal lead, but this
successful dispatch path disproves a deterministic target-dispatch-only cause. No new ASTRO run,
address-binding report, or semantic change is justified until static evidence identifies a
specific missing lifetime/range/synchronization fact.

The bounded comparison is preserved in
`G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_DISPATCH_20260912_2022/analysis.txt`.

## Latest device-loss diagnostic checkpoint — 2026-09-12

Run/artifact: `G:/KytyPS5/logs/ASTRO_NON_EOP_DIAG_20260912_1340/`.
The exact launch is recorded in `command.txt`; it used the known-good `--stub-bvh` baseline,
`--graphics-debug-dump false`, and direct file printf output. The wrapper did not persist a
`runtime-result.json`; no process exit code is claimed from this run. The emulator process was
gone after the fatal path, and the runtime log ended at 13:45:08 Europe/Riga.

The first recorded Vulkan failure was `vkDevice.waitSemaphores` returning
`ErrorDeviceLost (-4)` for ticks `329237` and `329214` (`known=329213`, `current=329238`).
The fatal path is `masterSemaphore.cpp:149`; no earlier queue-submit, pipeline-create, or Vulkan
error was logged. The fault query returned `count_result=Success`, `info_result=Success`,
`partial=false`, `address_count=36`, `vendor_count=0`, and advertised/allocated vendor capacity
`180000`; all returned addresses have type 4 (`INSTRUCTION_POINTER_UNKNOWN_EXT`). The diagnostic
output is fault evidence only and does not identify the offending GPU command.

The new submit provenance records the last non-EOP operation for the failing tick as
`op=0` (`DispatchDirect`), submit `6256`, args `4096,1,1,65,0x000000050052a400`.
Other recent records include dispatches at guest code pointers `0x0000000908e86a00`,
`0x000000050052fc00`, and `0x000000050052a400`; this metadata is a bounded host-side record and
does not yet prove which command caused the asynchronous device loss. Checkpoint pointers were
already retired/unknown when fault reporting ran.

No semantic fix was made. The next action is static correlation of the preserved dispatch and
its resource/state path; only if that evidence is insufficient should a further bounded diagnostic
be considered. Do not rerun ASTRO in this checkpoint or reopen resource-remap, replay, pipeline-cache,
or color work.

## Latest BDA page-table fix checkpoint — 2026-09-12

Semantic commit: `0c2da1d11537e2f24f1a34593f38d8ec8b4f634e`. The device-local 512 MiB BDA page-table
buffer is now zero-filled once before the first buffer registration and also guarded at `PrepareBda`
for the empty-table case. The authentic fail-before selector
`shader_recompiler_compute_tests.exe --bda-page-table-only` failed before the calls were added;
the post-fix selector passed. Focused suites passed for `resource_materialization_tests`,
`scalar_provenance_tests`, and `shader_recompiler_compute_tests`; `resource_tracking_tests` still
reaches the known unrelated dynamic-storage-mips baseline failure. Logs are under
`G:/KytyPS5/logs/BDA_PAGE_TABLE_FAIL_BEFORE_20260912_1720.log` and
`G:/KytyPS5/logs/BDA_PAGE_TABLE_FOCUSED_20260912_1745/`.

The one post-fix ASTRO run is `G:/KytyPS5/logs/ASTRO_BDA_FIX_20260912_1800/`, launched with the
known-good `--stub-bvh` command from installed executable SHA-256
`BAD51D31214F42F55B16E250378E868BDA11F07C150EE776AC56DCA435B3A9FF`. It reached the target shader
and emitted successfully; the exact MaterializeResources failure did not recur. The first later
failure remained `vkDevice.waitSemaphores` → `ErrorDeviceLost (-4)` for ticks `328465` and `328442`
(`known=328441`, `current=328466`), followed by `MasterSemaphore::Wait` at `masterSemaphore.cpp:149`.
Fault diagnostics returned `address_count=37`, `vendor_count=0`, `count_result=Success`,
`info_result=Success`, and `partial=false`; the last recorded non-EOP dispatch was submit `6242`,
groups `4096,1,1`, mode `65`, guest code pointer `0x000000050052a400`. Wrapper result was `321`
(`0x141`) after 536.975 seconds; no crash dump was produced. This run does not prove the BDA
initialization was the cause of the device loss, so the P0 remains unclassified asynchronous
Vulkan device loss after M5. Do not rerun ASTRO or reopen resource/remap work in this checkpoint.

## Latest progression checkpoint

Run: `G:/KytyPS5/logs/ASTRO_TTMP_FIX_20260912_1510/`
Launch: installed `kyty_emulator.exe --game "G:/PS5 Games/PPSA21567/extracted" --stub-bvh --shader-debug false --shader-log-direction Silent --graphics-debug-dump false --printf-direction File --printf-output-file "G:/KytyPS5/logs/ASTRO_TTMP_FIX_20260912_1510/runtime.log"`
Result: wrapper exit `321` (`0x00000141`); no crash dump. The run decoded and emitted CS `0x657ad04626bf9d55` (`SPIR-V EmitProgram words=124012`), then reached 40 CS / 22 PS / 14 VS / 1 GS. The first later fatal boundary was `vkDevice.waitSemaphores` returning `ErrorDeviceLost (-4)` for ticks `328841` and `328864` (`known=328840`, `current=328865`), followed by the existing fatal check at `masterSemaphore.cpp:127`. GPU fault diagnostics completed with `address_count=57`, `vendor_count=0`, advertised/allocated vendor capacity `181328`, and `count_result=Success`, `info_result=Success`, `partial=false`. No pipeline-create failure is proven.

## TTMP decoder semantic checkpoint — 2026-09-12

Commit `b8faeeb` adds generic RDNA2 scalar trap-temporary operands: source/destination codes
108–123 decode as `TTMP0`–`TTMP15` (so `0x73` is `TTMP7`). TTMP SSA state uses a separate IR range
from ordinary SGPRs, and embedded-fetch tracking intentionally continues to exclude trap temps.
The focused `shader_cfg_tests`, `resource_materialization_tests`, `scalar_provenance_tests`, and
`shader_recompiler_compute_tests` suites pass; `resource_tracking_tests` still reaches its known
unrelated `dynamic storage mips` baseline failure.

The former `unsupported scalar source operand 0x00000073` boundary is therefore closed. The
post-fix ASTRO run proves the target shader emitted successfully; the current P0 is the later
Vulkan device-loss wait above. Do not add a catch-all handler, unchecked access, or target-specific
TTMP behavior.

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

Semantic source checkpoint: `edc7d4f` keeps `.at()` and fixes the producer path: retained bounded `ReadConstBuffer` roots now register their descriptor source through `AddBuffer` and receive `AddMemoryPatch`; ordinary bounded reads remain deferred. Commit `2a51379` adds generic `S_CBRANCH_CDBGSYS` decode/CFG handling and models the unavailable debug-system bit as clear. No catch-all handler, unchecked access, clamp, substitution, or color change was made. Diagnostic and runtime artifacts remain outside the repository under G:/KytyPS5/logs/.

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

## Focused validation and runtime checkpoint

The scalar SRT image-operand fix was committed as `f6da96f` after an authentic
loop-PHI fail-before regression. `resource_materialization_tests`,
`scalar_provenance_tests`, and `shader_recompiler_compute_tests` passed; the new
resource-tracking regression passed before that suite reached its known unrelated
`dynamic storage mips` baseline failure. The Release build and local install were
refreshed directly from the build tree because the system CMake install prefix needs
administrator access.

Run/artifact: `G:/KytyPS5/logs/ASTRO_MATERIALIZE_FIX_20260912_1200/`.
The exact command, source/executable/PDB provenance, stdout/stderr, and process result
are recorded there. The run reached 46 compute shaders and completed a snapshot for
CS `0xc8aec8bce60cd4a1` (`elapsed_ms=39826`), proving the previous materialization
failure no longer occurs. It then stopped at the first new blocker: MS
`0x2b3be82b8235ac05`, PC `0x00003ca0`, unsupported SOPP opcode `0x17` raw
`0xbf970024` in `ShaderCFG.cpp:40`; process result was `0x00000141`.

Next action: classify this MS CFG opcode from source/ISA evidence before any new
runtime run or semantic change. Keep color correctness P1 and cold pipeline latency
P2; do not reopen the closed resource/remap/PHI findings.

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

## Progression run — post-M4 wait boundary (M5 reached)

Run/artifact: `G:/KytyPS5/logs/ASTRO_PROGRESS_20260911_225139/`.
The one progression launch used the installed executable SHA-256
`F4710707DC611196CD33C62F9BBDC4B7AAC366CAEB1AA9D8172ADAF793B09724` with the known-good
`--stub-bvh` baseline; the exact command is in `command.txt`. The runtime label is `Source build
150a139`; the repository HEAD at launch was `3fe3d2718f154e9d3f8f133a0fb8a176c7004a63`.

The latest user-reviewed evidence proves target shader emission and 40 compute shaders. M5 was
already proven by the title-screen progression run recorded in commit `4b0deac`; these later
diagnostic branches are classified as post-M5 outcomes.

The process terminated through the existing fatal-error path at runtime.log lines 780571–
780572 and stdout lines 107–125: `Not implemented (result != vk::Result::eSuccess)` in
`src/graphics/host_gpu/renderer/masterSemaphore.cpp:69`, the `vkDevice.waitSemaphores` result check
in `MasterSemaphore::Wait`. The run wrapper recorded wall time `550.8255477 s`, PID `26180`, and
no reliable process exit code (`exit_code` was unavailable; do not treat the reported `0x00000000`
placeholder as a clean exit). No new crash dump was created. The exact Vulkan result and source
cause remain unclassified; no semantic fix was made.

## Timeline-wait diagnostic checkpoint — 2026-09-11

Static symbolization against the 150a139 map/PDB identifies two saved fatal paths: one enters
`MasterSemaphore::Wait` from `CommandScheduler::PriorityOperationsThread`; the other enters it
from `CommandScheduler::Finish` via `BufferCache::DownloadBufferMemory` and
`GpuResourceManager::HandleFault`. The current source submits the master timeline value in
`CommandScheduler::Submit`; its wait caller and requested tick are not logged by the saved
`ASTRO_PROGRESS` run because `--graphics-debug-dump=false`. Existing full host traces show
successful submits through tick 337559 and a successful wait for tick 337558, but contain no
non-success wait result for the failing run.

Failure-only logging was added without changing control flow in commit `55788cb`:
`MasterSemaphore::Wait` now logs numeric/result-string `vkDevice.waitSemaphores` status together
with requested tick, `KnownGpuTick()`, and `CurrentTick()` only when the result is non-success.
`shader_recompiler_compute_tests.exe --scheduler-only` passed (exit 0).

The one diagnostic launch using the rebuilt `55788cb` executable is
`G:/KytyPS5/logs/ASTRO_WAIT_DIAG_20260911_2009/`, exit `0x141`. It reached target CS
`0x657ad04626bf9d55` and emitted 124012 words, then stopped at the already-known
`MaterializeResources(...)` fatal in `pipelineCache.cpp:573`; no wait-result diagnostic was
reached. This run is diagnostic evidence only and does not reclassify or reopen the resource
blocker. The saved P0 wait result remains unknown and requires a future run only if no existing
artifact can provide it. A later `86073bb` cache run recorded `ErrorDeviceLost (-4)` for the
wait boundary; this identifies the result only and does not classify its cause.

## Pipeline-cache persistence checkpoint — 2026-09-11

Source/build provenance: source HEAD `86073bb2f196c703e6652a6705ff33e9c054e885`, branch
`astro/materialize-resources`; installed Release executable SHA-256
`99274EDE87D67FE55BE07321FE2FE3E2C04904F2636985F29898A4880F749E23`; matching PDB SHA-256
`B99F47087F9A9FCD285EEFF9BBF7DFAC98BECAAD96D977397CD5CFA24E7E6E02`. The source change is
commit `86073bb`: expensive graphics/compute pipeline snapshots use the existing
`SnapshotDriverCacheLocked()` path whenever elapsed time exceeds 1000 ms, independent of
shader/graphics debug flags; failure remains non-fatal.

Cold run: `G:/KytyPS5/logs/ASTRO_PIPELINE_CACHE_COLD_20260911_2325/`, exact command in
`command.txt`, debug flags off, `--stub-bvh`, wall `542.8841709 s`, exit `0x00000141`.
The cache was absent at launch and ended at 9,674,239 bytes, SHA-256
`D89D0B934FB40F5B33672356C3E01F872EDDE70CFBFC06055E0FD95EF6C8DF88`; snapshots were saved
for compute shaders `0x78af8e269b528b5c` (85,675 ms), `0x7bd68261b1bdfb68` (112,727 ms),
`0xf3f4e1671b30c1f4` (129,791 ms), and `0x530dcd964f29983c` (152,333 ms).

Warm run: `G:/KytyPS5/logs/ASTRO_PIPELINE_CACHE_WARM_20260911_2335/`, same command and
provenance, wall `543.6700286 s`, exit `0x00000141`. It loaded the cold cache payload
(`9,674,161` bytes, SHA-256 `D89D0B...C8DF88`) and ended at 17,565,856 bytes, SHA-256
`18E0265F0213F9C74D80F9FE7F0178C61AB0E6B9F8F92D9585E120FEE18948BF`. The same four
landmark pipelines took 85,791, 113,127, 130,659, and 154,026 ms, respectively; no
substantial warm compile reduction or overall wall-time reduction was proven. Both runs
reached `CS 40` and then the existing `MaterializeResources(...)` fatal at `pipelineCache.cpp:573`.

Cache persistence and load are therefore proven, while cache performance benefit remains
unproven. Do not expand cache tooling. Return the runtime critical path to the known P0 exact
`vkDevice.waitSemaphores` `ErrorDeviceLost (-4)` classification; keep colors P1.

## Device-loss diagnosis checkpoint — 2026-09-12

Source/build provenance: source HEAD `bd131e82212f9d60c2b23ed1e05850d9eee4fcf7`, branch
`astro/materialize-resources`; installed Release executable SHA-256
`D5522E70E52A437A7C267E9B2503E42F18716D0AD9E4B2EAC32D0ECCA48BA05D`; matching PDB SHA-256
`BC29B98CBE80A4C8B84E53BD2A1255EB41E1DCCD9FCA7E548F993490DF1467EF`. The exact build and
launch command are recorded in each artifact directory below.

Two diagnostic launches used the known-good `--stub-bvh` baseline and reached the post-M5
runtime path. Both terminated with wrapper exit `321` (`0x00000141`) after
`vkDevice.waitSemaphores` returned `ErrorDeviceLost (-4)`:

- `G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_DIAG_20260912_0005/`: ticks 327685 and 327708;
  `nvlddmkm` System Event 153 at `2026-09-11T21:07:56.4265059Z`.
- `G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_HISTORY_20260912_0018/`: ticks 352888 and 352911;
  `nvlddmkm` System Event 153 at local `2026-09-12 00:29:33`.

The bounded submit ring identifies the failed waits as `debug_op=3` EOP marker submissions
(with one preceding `debug_op=5` write-back), all on submit 6676 in the second run. This is a
detection point for an earlier asynchronous GPU fault, not proof that the marker write itself is
the root cause. The last 16 records contain no direct draw/dispatch operation, so the offending
GPU command remains unclassified. No Vulkan device-lost error is ignored or retried, and no
semantic fix was made.

Focused validation after the diagnostic commits passed:
`ninja -C _Build/windows -j1 kyty_emulator shader_recompiler_compute_tests`, followed by
`shader_recompiler_compute_tests.exe --scheduler-only` (exit 0). The current worktree is clean
before this documentation checkpoint.

Next action: in the next session, use the existing logs/source to identify the last non-EOP GPU
operation and its resource state; add further bounded diagnostics only if that evidence cannot be
recovered statically. Then build a focused regression before any semantic change. Do not rerun
ASTRO tonight; do not reopen resource-remap, replay, pipeline-cache, or color work.

## GPU fault diagnostics checkpoint — 2026-09-12

Diagnostic source commit: `624fb5d657b23ac2bece3e5448b88b1f97fa580a` (`diag: capture Vulkan
device-loss provenance`). The Release executable and matching PDB were built from this exact
HEAD and copied to the local install tree. EXE SHA-256 is
`978341D098E4302CB4DE4FC279FF13BEC62024C1C23046AFBD11480B4D56139E`; PDB SHA-256 is
`322B8212BA6E647C6529763069E4F149B161B6033406210892D67305FB962D84`.

Focused validation passed: `shader_recompiler_compute_tests.exe --scheduler-only` and the full
`shader_recompiler_compute_tests.exe` both exited 0. The targeted run is
`G:/KytyPS5/logs/ASTRO_GPU_FAULT_DIAG_20260912_0115/`; the exact command and provenance are in
`command.txt` and `prelaunch-provenance.json`. Both `VK_EXT_device_fault` and
`VK_NV_device_diagnostic_checkpoints` logged as enabled.

The run lasted 555.987 s and exited naturally with wrapper status `321` (`0x00000141`). It
reached 40 compute shaders, then stopped at the existing
`MaterializeResources(...)` fatal in `pipelineCache.cpp:573`. No `GPU_DEVICE_FAULT` or
`GPU_CHECKPOINT` block was emitted, no new crash dump appeared, and the device-loss wait was
not reached. This is evidence that the diagnostic extensions initialize correctly on the
test machine; it does not classify the separate post-M5 `ErrorDeviceLost (-4)` cause.

Next action: preserve this run and return to static evidence for the existing device-loss P0.
Do not start a second ASTRO run in this checkpoint and do not change shader, resource, sync, or
render semantics.

## GPU fault diagnostic ownership correction — 2026-09-12

Source commit `50bb5ad` moves `CommitSubmit(tick, markers)` to immediately after timeline-tick
allocation and before `vkQueueSubmit`, preserving checkpoint pointers when submission fails or
device-loss reporting runs before normal post-submit bookkeeping. `DumpDeviceFault` now keeps
the advertised vendor-binary size, passes the capped allocated capacity through
`VkDeviceFaultCountsEXT` for the second query, and treats `VK_INCOMPLETE` as partial evidence
while still logging returned address/vendor records. No ASTRO run was performed for this
diagnostic correction.

Focused validation artifacts: `G:/KytyPS5/logs/GPU_FAULT_DIAG_FIX_20260912_scheduler-only-fresh.log`
and `G:/KytyPS5/logs/GPU_FAULT_DIAG_FIX_20260912_full-tests-fresh.log`; both exit code 0.

## Dispatch-state correlation checkpoint — 2026-09-12

The static narrowing task did not establish a production mapping for guest code address
`0x000000050052a400`. The exact AGC record at that address advertises a 160-word compute
shader, but AGC address records are not authoritative for the active invocation: in the same
capture, address `0x00000005007b7300` later reached `GraphicsDispatchState` as shader hash
`0x9e3c6093e9c20738`, `code_words=160`, despite its earlier AGC record advertising
`shader_size=0x13a0`. This proves address metadata can be stale/reused/interleaved; no
pointer-to-hash or resource-state inference is allowed from that record alone.

The valid bounded diagnostic capture is
`G:/KytyPS5/logs/ASTRO_BDA_DISPATCH_STATE_20260912_1945/`. Its temporary generic trace emitted
the capped 8192 dispatch-state records, but the target address never reached a
`GraphicsDispatchState` or `GraphicsDispatchResources` record. The run ended at the known
decoder baseline (`unknown RDNA2 instruction family`, `ShaderDecoder.cpp:388`) with wrapper
status `321` (`0x141`), before the target dispatch; it produced no device-loss or BDA/resource
causal evidence. The malformed earlier wrapper attempt
`G:/KytyPS5/logs/ASTRO_BDA_DISPATCH_STATE_20260912_1930/` is not runtime evidence.

No semantic source, renderer, scheduler, or address-binding diagnostic change was made. The
temporary trace was reverted and the clean install now uses EXE SHA-256
`8326FBF6AC6426763BAE5BBBD33B2753273EC0C44347B1B03C52CAEF6F821E90` with matching PDB SHA-256
`CC54F3FDD0C14BB728494A4708EC9B8BCB0AD9BE01726CB46235D20983CBDAEE` from current HEAD
`5191c74`. The async `VK_ERROR_DEVICE_LOST (-4)` remains the P0; no new runtime run or
`VK_EXT_device_address_binding_report` capture is justified until an exact pointer-to-hash and
resource snapshot is available.

Next action: use existing source/log evidence to obtain one exact target dispatch-state record
or identify a different stable causal boundary. If a future capture is required, keep it
generic and bounded, record provenance, and stop at the first exact missing fact; do not reopen
closed resource/remap, replay, pipeline-cache, or color findings.
