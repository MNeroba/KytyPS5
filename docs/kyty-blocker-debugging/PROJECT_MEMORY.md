# KytyPS5 project memory

Durable, non-chronological facts that are expensive to rediscover. Read `CURRENT_STATE.md` first. Put current milestones, P0, worktree, executable, and next action there; stable mechanisms live in `REFERENCE.md`.

## Environment and baselines

- **PROVEN:** Main repository is `G:/KytyPS5/repo`; the fork is `MNeroba/KytyPS5`. The normal installed executable is `G:/KytyPS5/repo/_Build/windows/install/kyty_emulator.exe`.
  Evidence: repository, build, and runtime inspection on 2026-09-10.
- **PROVEN:** Primary host is Windows 11 Pro, Intel i9-12900K, 32 GB RAM, NVIDIA RTX 3090 24 GB. The active Windows build uses CMake/Ninja with `clang-cl`; do not assume the MSVC frontend.
  Evidence: host and CMake inspection on 2026-09-10. See `REFERENCE.md § Build and runtime boundary`.
- **STRONG EVIDENCE:** Secondary validation host is Apple M1 Max with 32 GB RAM.
- **PROVEN:** ASTRO BOT EU is `PPSA21567`; its extraction is already solved. Do not mix Japan `PPSA21559` content into this baseline.
  Evidence: existing extracted game root and project setup records.
- **PROVEN:** Relevant focused suites are `resource_materialization_tests`, `resource_tracking_tests`, `shader_cfg_tests`, `shader_recompiler_compute_tests`, and `scalar_provenance_tests`. `resource_tracking_tests / dynamic storage mips` is a known unrelated failure. An older aggregate `kyty_tests` failure involved obsolete `Log` symbol linkage.
  Evidence: repeated focused runs through `dfc7203`.

## Branch-aware diagnostic traversal — PROVEN

FACT: `Decoder::WalkProgram` now exposes the production branch-aware executable traversal, including
pending forward targets after `S_ENDPGM` and completed-backedge termination. The opt-in S_SWAPPC
scanner consumes this contract for normal shaders; fused-front scans continue to use the same
production-derived front boundary.

WHY IT MATTERS: Diagnostics no longer invent a weaker linear termination rule and therefore do not
walk into post-terminal data such as reserved scalar-source words. Reachable unsupported instructions
remain fail-fast.

EVIDENCE: Authentic pending-target-after-`S_ENDPGM` regression fails before and passes after commit
`623865e`; positive in-front S_SWAPPC and existing fused-front/e0/e5 tests remain green in
`shader_cfg_tests` and `shader_recompiler_compute_tests`. The exact ASTRO validation artifact is
`G:/KytyPS5/logs/SWAPPC_BRANCH_WALK_FIX_20260913_/astro-run/`.

RELATED CODE/COMMIT: `ShaderDecoder.{h,cpp}`, `ShaderSwapPcDiagnostic.cpp`, `tests/shaderCfgTests.cpp`,
commit `623865e`.

## Same-run device-loss window — PROVEN RESULT, CAUSE UNCLASSIFIED

FACT: In the exact `623865e` Release run, `vkDevice.waitSemaphores` returned
`VK_ERROR_DEVICE_LOST (-4)` with `known=329177` and `current=329202`. The first definitely
non-retired tick is `329178`; the artifact does not prove completion for ticks `329179..329201`.

WHY IT MATTERS: The requested MS S_SWAPPC target was not invoked in this run, so no target provenance
or S_SWAPPC semantic conclusion may be drawn from it. The first non-retired dispatch is a candidate,
not a proven cause.

EVIDENCE: Same-run report `G:/KytyPS5/logs/SWAPPC_BRANCH_WALK_FIX_20260913_/astro-run/device-loss-window-report.txt`
contains a tick-329178 `DispatchDirect` for CS `0x657ad04626bf9d55` (`4096x1x1`), 10 buffers,
13 images, all retained resources live/non-deleted, and no explicit range/layout violation. GPU fault
info completed with 39 addresses and no vendor records; checkpoint markers were unknown. Ticks
329185..329201 have only EOP history and one `DrawIndexAuto` predecessor at 329187 without shader/resource
identity. Classification is **C: insufficient same-run evidence to attribute the device loss**.

RELATED CODE/COMMIT: `MasterSemaphore::Wait`, `GpuFaultDiagnostics`; no semantic change is justified.
## S_SWAPPC target-capture retry device loss — PROVEN RESULT, CAUSE UNCLASSIFIED

FACT: A single exact Release run from `5804443` again hit `VK_ERROR_DEVICE_LOST (-4)` in
`MasterSemaphore::Wait` before invoking MS `0x2b3be82b8235ac05` / `S_SWAPPC_B64`.

WHY IT MATTERS: The target provenance remains uncaptured. The repeated device-loss boundary is not
permission to change S_SWAPPC semantics, scheduler/resource ownership, or broaden diagnostics.

EVIDENCE: `G:/KytyPS5/logs/SWAPPC_TARGET_CAPTURE_20260913_135557/astro-run/` reached 40 CS / 22 PS /
14 VS / 1 GS with no prior `e0`/`e5` or branch-walk failure. Wait errors were ticks `329598` and
`329575` (`known=329574`, `current=329599`); GPU fault info completed with 44 addresses,
`vendor_count=0`, `partial=false`, and checkpoint markers were unknown. The log ended at
`GPU_COMMAND_SNAPSHOT_WINDOW` without command/resource records, so it is no better for same-run
attribution than the prior C-class audit. Numeric process exit code was unavailable.

RELATED CODE/COMMIT: no source change; keep the earlier C classification and target-capture objective.
## Durable semantic findings

### Resource identity and operand roles — PROVEN

Fact: `GetBufferResource`, `GetImageResource`, and `GetSamplerResource` carry resource identity and remain a hard safety boundary. Payload data, image coordinates, and byte offsets are separate roles and can be shader-side only when a dedicated path supports them.

Evidence: slot241 fail-before/pass-after regressions and runtime progression. Proven safe cases are `ImageWrite` operand 2, `ImageSampleRaw` operand 2, and `ReadConstBuffer` operand 1.

Why it matters: never broaden an opcode or `ShaderSideUseGraph` because a sibling operand is safe. See `REFERENCE.md § Resource identity and operand roles`.

### Global versus per-use state — PROVEN

Fact: global SRT slot eligibility and per-use shader-side/BDA lowering are distinct. A slot may retain an ordinary host consumer while one explicitly tracked BDA clone remains shader-side.

Evidence: BDA-only and mixed-use regressions committed in `09d5e94`.

Why it matters: refresh passes must preserve provenance-tagged per-use clones without promoting the whole slot. See `REFERENCE.md § Global and per-use state`.

### PHI boundary — PROVEN

Fact: a finite acyclic branch/diamond PHI may lower to a runtime select; a loop-carried PHI containing ReadLane, lane ID, BVH, or other guest execution state is not a host invariant.

Evidence: `3b52034` and the slot241/245 traces.

Why it matters: lane-zero substitution, first-incoming selection, host fixed points, and arbitrary iteration caps change shader semantics. See `REFERENCE.md § Runtime PHI classes`.

### Bounded-plan lifetime — PROVEN

Fact: bounded/indirect plans can hold IR values outside ordinary use edges. After a rewrite removes original users, DCE may invalidate descriptor-source producers before ResourcePlan extraction.

Evidence: the `09d5e94` saved-shader audit and `TestBoundedDescriptorSourceSurvivesDeadCodeElimination`; it fails before and passes after `dfc7203`, including candidate `0x12345678` materialization.

Why it matters: retaining only the bounded read result is insufficient; address/count source roots must survive until extraction. See `REFERENCE.md § Bounded planning and lifetime`.

### Structured CFG loop-exit guard — PROVEN

FACT: A conditional inside an innermost loop cannot be emitted as loop control when one edge leaves the loop through a block that is neither that loop's merge nor continue target. The structured emitter would omit the required selection merge, so the generic safe action is to use the existing dispatcher fallback.

WHY IT MATTERS: This keeps SPIR-V structured-control-flow rules separate from guest branch semantics and avoids target-specific CFG exceptions.

EVIDENCE: Target shader `0x78af8e269b528b5c` mapped to block 51 with an outside edge through block 52 while the loop merge/continue were 153/197. The pre-fix artifact failed `spirv-val` with `Selection must be structured`; the post-check diagnostic run reached dispatcher emission.

RELATED CODE/COMMIT: `ShaderRecompiler.cpp` (`ValidateStructuredLoopControl`), semantic commit `1161113`.

### Dispatcher value boundary — PROVEN

FACT: Dispatcher spill slots may contain native SPIR-V values only. SRT/resource handles (`SrtResource`, buffer/image/sampler resources, and `ImageAddress`) are metadata resolved by their consumers. Planning-only scalar reads are not runtime values unless a shader-side SRT wrapper retains that producer. A shader-side `ReadConst` wrapper aliases `program.srt_reads[slot].value`; cross-block analysis must spill the retained producer rather than the wrapper.

WHY IT MATTERS: Spilling metadata wrappers can create an undefined or forward-referenced SPIR-V ID at a dispatcher edge even though the underlying producer is valid.

EVIDENCE: The pre-fix target module in `ASTRO_DISPATCH_SRT_20260910_2320_debug` stored undefined `%28241` into `%1011`. After the generic metadata/alias handling, `ASTRO_DISPATCH_SRT_20260911_0050_debug` emitted the 941,496-byte target module; `spirv-val --target-env vulkan1.3` passed and a numeric scan found 45,225 definitions, 45,225 references, and zero missing IDs. The runtime reached the target wave64 compute; a clean pipeline/submission result is still unverified.

RELATED CODE/COMMIT: `backend/spirv/spirvEmitterProgram.cpp`, `tests/shaderCfgTests.cpp` dispatcher alias fixture, semantic commit `1161113`.

### Conditional debug-system branch — PROVEN

FACT: RDNA2/GCN SOPP opcode `0x17` is `S_CBRANCH_CDBGSYS`; it branches by `SIMM16 * 4` when the system debug bit is set and otherwise behaves as a NOP. Kyty has no translated shader state for that hardware debug bit, so the generic implementation retains the CFG edge and supplies a constant false condition for normal execution.

WHY IT MATTERS: An ASTRO mesh shader used raw `0xbf970024` at PC `0x3ca0`; the missing decoder entry previously stopped `ShaderCFG::BuildGraph` before emission. Do not treat this as a shader-hash exception or remove the branch edge.

EVIDENCE: fail-before regression `G:/KytyPS5/logs/ASTRO_CFG_P0_20260912_1300/shader_cfg_fail_before_run2.log`; pass-after focused `shader_cfg_tests` and the ASTRO run `G:/KytyPS5/logs/ASTRO_CFG_FIX_20260912_1330/` passed this boundary. ISA reference: [AMD GCN3 Instruction Set Architecture](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/gcn3-instruction-set-architecture.pdf), SOPP opcode 23 (0x17).

RELATED CODE/COMMIT: decoder/CFG/translator support in `2a51379`; next runtime boundary is documented in `CURRENT_STATE.md`.

### Post-CDBGSYS runtime boundary — PROVEN / CLOSED

FACT: The `2a51379` ASTRO run reached 46 CS, 34 PS, 21 VS, and 2 GS shader counts and passed the former MS `S_CBRANCH_CDBGSYS` failure. Its next boundary was `unsupported scalar source operand 0x00000073 at PC 0x00003db0` in `ShaderDecoder.cpp:264` while decoding MS `0x2b3be82b8235ac05`; the later TTMP fix below closes this decoder gap.

WHY IT MATTERS: This is the earliest current P0 for M6 progression. No SPIR-V or pipeline result exists for that MS invocation; the scalar-source value must be classified before any further runtime run or semantic change.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_CFG_FIX_20260912_1330/runtime.log` lines 779241–779259; process result `321` (`0x00000141`), no dump. Exact executable SHA-256 is recorded in `CURRENT_STATE.md`.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp`, `2a51379`.

### RDNA2 scalar trap-temporary operands — PROVEN

FACT: RDNA2 scalar source and destination codes 108–123 are the privileged trap-temporary
registers `TTMP0`–`TTMP15`; therefore scalar code `0x73` is `TTMP7`. Kyty previously rejected
these operands in `DecodeScalarSource`. The generic fix adds a `Ttmp` operand kind, carries TTMP
SSA state in a separate IR range (`TtmpBase=106`), and leaves ordinary SGPR numbering and
embedded-fetch tracking unchanged.

WHY IT MATTERS: Trap temporaries are shader execution state, not user-data SGPRs or descriptor
resources. Modeling them separately avoids both the decode failure and accidental resource
materialization/embedded-fetch provenance.

EVIDENCE: Focused fail-before artifact
`G:/KytyPS5/logs/ASTRO_TTMP_P0_20260912_1400/fail_before_run.log` reports the rejected `0x73`;
the same `shader_cfg_tests` fixture passes after the fix. `resource_materialization_tests`,
`scalar_provenance_tests`, and `shader_recompiler_compute_tests` pass afterward, while
`resource_tracking_tests` reaches only its known unrelated `dynamic storage mips` baseline
failure. The exact post-fix ASTRO run
`G:/KytyPS5/logs/ASTRO_TTMP_FIX_20260912_1510/` emits 124012 SPIR-V words for CS
`0x657ad04626bf9d55` and contains no `0x73` decode failure. The operand encoding is defined by the
[AMD RDNA 2 ISA](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna2-shader-instruction-set-architecture.pdf).

RELATED CODE/COMMIT: `ShaderDecoder.{h,cpp}`, `IR/Reg.h`, `IR/Block.h`, `SsaRewrite.cpp`,
`Translator.{h,cpp}`, `Control.cpp`, `Integer.cpp`, `Memory.cpp`, `ShaderCFG.cpp`; semantic
commit `b8faeeb`.

### Post-TTMP runtime device-loss boundary — PROVEN RESULT, ROOT UNCLASSIFIED

FACT: A valid ASTRO run from `b8faeeb` cleared the TTMP decoder boundary, emitted the target CS
`0x657ad04626bf9d55` (`SPIR-V EmitProgram words=124012`), and progressed to 40 CS / 22 PS /
14 VS / 1 GS. The first later fatal boundary was `vkDevice.waitSemaphores` returning
`ErrorDeviceLost (-4)` in `MasterSemaphore::Wait` at `masterSemaphore.cpp:127`, for requested
ticks `328841` and `328864` (`known=328840`, `current=328865`).

WHY IT MATTERS: The wait is the first confirmed blocker after successful shader emission and
pipeline/submit progress, but it is an asynchronous GPU/driver fault observation rather than
proof that the marker wait caused the device loss. Do not reopen the TTMP/resource fixes, treat
slow successful pipeline creation as a hang, or suppress/retry the failed wait.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_TTMP_FIX_20260912_1510/runtime.log` records the exact Vulkan
result and fatal boundary. `GPU_DEVICE_FAULT` reports `address_count=57`, `vendor_count=0`,
advertised and allocated vendor capacity `181328`, `count_result=Success`, and
`info_result=Success` with `partial=false`; no Vulkan error or pipeline-create failure precedes
the wait. The run exited with wrapper status `321` (`0x00000141`) and produced no crash dump.
The installed executable hash was
`F7842BEE9F65308B82ED41F1B277AA6430F527EA340655B876295C54D6B3DFE6` from source `b8faeeb`.

RELATED CODE/COMMIT: `MasterSemaphore::Wait`, `CommandScheduler::Submit`,
`src/graphics/host_gpu/renderer/gpuFaultDiagnostics.cpp`; no device-loss semantic fix was made.

### Host-tick versus guest-submit provenance — PROVEN

FACT: A guest `GuestGpu::Submission` receives one `debug_submit` id, but a sliced submission can
call `BufferFlush` repeatedly; each flush ends one pooled primary command buffer and assigns a new
master timeline tick. `CommandBuffer::SetDebugInfo` keeps only the last operation and the last
non-EOP Dispatch/Draw summary for that command buffer.

WHY IT MATTERS: The fault artifact for submit `6256` (ticks `329198..329237`) cannot reconstruct
all GPU operations or identify the target shader/resources from its ring records. A causal device-
loss capture must key a crash-safe active dispatch/resource snapshot by host tick; do not map a guest
code address to a shader or resource state from another run.

EVIDENCE: `G:/KytyPS5/logs/DEVICE_LOSS_STATIC_20260912_2035/submit6256_reconstruction.txt`;
`ASTRO_NON_EOP_DIAG_20260912_1340` prints only the bounded submit history, omits ticks 329215-329220,
and has no descriptor/BDA/lifetime/layout snapshot. The same run compiled CS
`0x657ad04626bf9d55`, but no active pointer/hash pairing exists for submit `6256`.

RELATED CODE: `src/graphics/guest_gpu/graphicsRun.cpp` (`GuestGpu::Process`),
`src/graphics/host_gpu/renderer/commandScheduler.cpp` (`Submit`),
`src/graphics/host_gpu/renderer/context.cpp` (`SetDebugInfo`).

### Tick-keyed GPU snapshot reporting window — PROVEN

FACT: `GpuFaultDiagnostics::m_snapshot_batches` is a bounded deque of submitted, non-retired
command snapshots. `CommandScheduler::Submit` commits a batch after assigning its master tick and
before `vkQueueSubmit`; `RetireCompleted` removes only batches at or below the last known GPU tick.
The fault dump must therefore emit every batch still retained, including one assigned after the
wait tick that reported the loss.

WHY IT MATTERS: Filtering retained batches by `batch.tick > failing_tick` could print
`retained_batches=3` while emitting no batch, command, or resource records during an asynchronous
device-loss report. That prevented same-run attribution even though the ring and metadata were
present.

EVIDENCE: `G:/KytyPS5/logs/DEVICE_LOSS_SNAPSHOT_FIX_20260913_/source-audit.txt` and its
`snapshot-fail-before.log` / `snapshot-pass-after-final.log` fixture. Three ticks newer than the
reported failure reproduce header-only output before the fix; after commit `d0ce0cb`, all three
DispatchDirect records and representative buffer metadata are emitted. Focused snapshot and
scheduler tests pass. Logger flush is explicit in `Log::WriteFatal` and `Log::Shutdown`.

RELATED CODE/COMMIT: `src/graphics/host_gpu/renderer/gpuFaultDiagnostics.{h,cpp}`,
`tests/ShaderRecompilerComputeTests.cpp`, `d0ce0cb`.

### Device-loss report completion lifecycle — PROVEN

FACT: `GpuFaultDiagnostics::ReportDeviceLost` has an explicit
`NotStarted → Reporting → Complete` lifecycle. One caller owns the dump; duplicate callers wait on
a condition variable while Reporting, then continue the existing caller path after Complete. The
owner flushes the logger before publishing Complete. No Vulkan retry, recovery, scheduler ordering,
or resource semantics are involved.

WHY IT MATTERS: Multiple waiters can observe one device loss. Without serialization, a duplicate
waiter can reach the existing fatal path and terminate the process while the owner is still emitting
the bounded snapshot, leaving an apparently truncated report and an interrupted wrapper result.

EVIDENCE: The exact `3901b80` ASTRO artifact
`G:/KytyPS5/logs/DEVICE_LOSS_SNAPSHOT_FIX_20260913_/astro-run/runtime.log` has failures at ticks
329171 and 329194; `already_reported` is interleaved before the first report's remaining resource
records. Deterministic real-path fixture artifacts in
`G:/KytyPS5/logs/DEVICE_LOSS_REPORT_BARRIER_20260913_/fixture-report.txt` reproduce the race
without timing sleeps: fail-before lets the duplicate leave while the owner is held, while
pass-after holds it at `DuplicateWaiting` and emits one complete report (three batches, commands,
and resources). Focused barrier, snapshot, and scheduler tests exit 0.

RELATED CODE/COMMIT: `src/graphics/host_gpu/renderer/gpuFaultDiagnostics.{h,cpp}`,
`tests/ShaderRecompilerComputeTests.cpp`, `1459c38`.

### BDA page-table initialization contract — PROVEN

FACT: Vulkan device-local allocations do not provide a zero-content contract. Kyty's 512 MiB
BDA page-table buffer therefore must receive one scheduler-active full zero-fill before the first
page-table entry is registered; `PrepareBda` repeats the idempotent initialization for a BDA use
with no registered buffers. Register writes then publish device addresses, while unregister fills
released ranges with zero.

WHY IT MATTERS: The BDA emitter treats zero entries as unmapped and any stale nonzero entry as a
physical address. Initialization must occur before the first `ChangeRegister<true>` write; doing it
after `FindBuffers` would erase entries already recorded in the current command.

EVIDENCE: The authentic fail-before selector failed at first `FindBuffer` with
`BDA page table was not initialized before the first buffer registration` in
`G:/KytyPS5/logs/BDA_PAGE_TABLE_FAIL_BEFORE_20260912_1720.log`; the post-fix selector and focused
shader/resource suites passed in `G:/KytyPS5/logs/BDA_PAGE_TABLE_FOCUSED_20260912_1745/`.
Commit `0c2da1d` implements `InitializeBdaPageTable` in `BufferCache` and calls it from registration
and `GpuResourceManager::PrepareBda`. The subsequent single ASTRO run
`G:/KytyPS5/logs/ASTRO_BDA_FIX_20260912_1800/` still reached `ErrorDeviceLost (-4)` at
`MasterSemaphore::Wait`; this does not prove that the initialization caused or resolved the GPU fault.

RELATED CODE/COMMIT: `bufferCache.{h,cpp}`, `gpuResourceManager.cpp`,
`tests/ShaderRecompilerComputeTests.cpp`, `0c2da1d`.

### GDS and indirect command audit — PROVEN

FACT: Kyty lowers DS GDS operations to one descriptor binding selected by `NativeBinding`; the
compute binding is 45 and the same helper is used by SPIR-V decorations, Vulkan layouts, and
descriptor writes. Binding allocation follows DCE and precedes emission, with no later remap.

WHY IT MATTERS: Prosper's binding 127 is not a Kyty invariant. A missing or renumbered GDS
resource is not supported by current source or the available ASTRO evidence.

EVIDENCE: `Memory.cpp`, `BindingLayout.cpp`, `spirvEmitterModule.cpp`, `pipeline/shaders.cpp`,
`pipeline/descriptors.cpp`; five ASTRO CS hashes in
`G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_20260912_205915/astro-gds-module-census.txt` reached successful
SPIR-V emission and were decorated at binding 45. `GpuCommandLane` covers PM4 immediate, memory,
and immediate-zero GDS reset/readback and passes.

FACT: Kyty has no Vulkan indirect-command consumer. PM4 indirect handlers read guest argument
structures on the CPU; GPU-dirty protection routes a CPU read fault through synchronous download
and scheduler wait before dereference.

WHY IT MATTERS: A missing `eIndirectCommandRead` barrier is not an applicable defect in this
architecture. Continue to investigate the device-loss P0 through the bounded host-tick snapshot.

EVIDENCE: `graphicsRun.cpp`, `runtimeLinker.cpp`, `GpuResourceManager::HandleFault`,
`BufferCache::ReadMemoryOnGpu`, and the existing `GpuCommandLane` dirty-fault checks.

### Bounded snapshot runtime boundary — PROVEN

FACT: The first run using the bounded tick-keyed GPU command/resource snapshot reached 46 CS,
34 PS, 21 VS, and 2 GS with successful pipeline creation, submits, waits, and host flips. It
terminated earlier in MS decode for `0x2b3be82b8235ac05` with `unknown RDNA2 instruction family`
at `pc=0x00003e74`, raw `0xc2208080` (`ShaderDecoder.cpp:388`).

WHY IT MATTERS: The snapshot was not exercised because this decoder boundary precedes the known
post-M5 device-loss window. The next session should classify this exact opcode before any new
runtime run; do not infer a device-loss cause from this capture.

### Non-retired tick coverage and device-loss correlation — PROVEN coverage, root unclassified

FACT: In `G:/KytyPS5/logs/ASTRO_MS_FIX_20260913_0015/runtime_actual/`, the first
`VK_ERROR_DEVICE_LOST (-4)` wait requested tick `329621` with `known=329597` and
`current=329622`; therefore the non-retired interval at failure is `[329598,329621]`.
Ticks `329608..329621` are present as EOP records in the 128-entry MasterSemaphore history.
Their missing `last non-EOP` metadata means those command buffers had no retained Dispatch/Draw
summary; it does not indicate ring truncation. The fixed failure print omits `329600..329604`,
and the bounded snapshot ring retained three batches out of capacity 64.

WHY IT MATTERS: Establish tick coverage before correlating commands or shaders. Do not call an
EOP-only interval absent, and do not infer the root cause from the last marker alone.

FACT: The recoverable same-run target dispatch is tick `329598`, guest submit `6263`, CS
`0x657ad04626bf9d55`, groups `4096x1x1`, guest code `0x000000050052a400`. Its 10 buffers and
13 images are live, descriptor ranges are valid (`violations=0`), and image 0 is R32_UINT,
GENERAL, read/write/atomic, matching SPIR-V binding 41. No stale/freed resource, invalid range,
synchronization/layout, or shader semantic violation is proven because predecessor barrier and
dynamic BDA history are not retained.

EVIDENCE: `device-loss-static-analysis.txt`, `snapshot-window-329621.txt`, and
`snapshot-resource-validation.txt` in the artifact above; source paths are
`gpuFaultDiagnostics.cpp`, `commandScheduler.cpp`, `masterSemaphore.cpp`, `renderCompute.cpp`,
`shaderResourceBarrier.cpp`, and `descriptors.cpp`.

RELATED CODE/COMMIT: bounded snapshot ownership is in `5ebf00a`; the current runtime/documentation
provenance is commit `807a84d`. Do not rerun ASTRO, enable `VK_EXT_device_address_binding_report`,
or change semantics until one exact missing runtime fact is identified.

EVIDENCE: `G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_ASTRO_20260912_213444/runtime.log`, `stdout.txt`,
`stderr.txt`, and `process-result.txt`; source/build `5ebf00a`; wrapper exit `321` (`0x141`).

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp:388`,
bounded diagnostic commit `5ebf00a`.

### Runtime decoder word versus CDBGSYS regression — PROVEN

FACT: The `2a51379` regression is present in `shader_cfg_tests` and passes as a standalone run,
but it covers synthetic SOPP raw `0xbf970001` (`EncodeSopp(0x17, 1)`) in compute stage. The ASTRO
failure is mesh-stage raw `0xc2208080` at PC `0x00003e74`; it cannot enter the SOPP path because
its top-bit gate is `0xc0000000` and `word >> 26` is `0x30`, which the current discriminator maps
to `Family::Unknown`. The family discriminator itself was unchanged by `2a51379`.

WHY IT MATTERS: A green CDBGSYS regression does not prove coverage of the current runtime word.
Do not reuse that fixture or infer an SOPP/CDBGSYS fix for `0xc2208080`; classify the MS encoding
from source/ISA evidence first. The runtime preceding dword is unavailable, so alignment is only
known to be structurally 4-byte aligned from the logged PC.

EVIDENCE: `G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_20260912_205915/decoder-cdbg-comparison.txt`,
`G:/KytyPS5/logs/GPU_TICK_SNAPSHOT_ASTRO_20260912_213444/runtime.log`, current
`ShaderDecoder.cpp`, and `git show 2a51379`.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp`,
`tests/shaderCfgTests.cpp`, `2a51379`.

### Target MS raw program preservation gap — SUPERSEDED

This entry records the pre-capture state only; the complete stream and its classification are
recorded in `MS decoder unreachable-tail classification and fix — PROVEN` below.

FACT (historical): Before the raw-capture diagnostic, the complete raw MS program for
`0x2b3be82b8235ac05` was not preserved in the project
artifacts. Existing logs provide `code_words=4164`, an old successful decode count of 2858, and
the newer failure word `0xc2208080` at `pc=0x3e74`, but no target `.bin`/`.rdna2` or preceding
dword. `DumpShaderRawBeforeCompile` is called after `TranslateProgram` (`pipelineCache.cpp:564-568`),
so a decoder failure occurs before that sidecar is written.

WHY IT MATTERS: The logged PC is 4-byte aligned but is not proof of an instruction boundary. A
sequential width audit cannot distinguish a genuine first word from a continuation/literal without
the target stream. Do not add a `Family::0x30` case, infer a width fix, or reuse another shader's
same-PC disassembly until raw bytes are captured before decode.

EVIDENCE: `G:/KytyPS5/logs/MS_BOUNDARY_AUDIT_20260912_2205/boundary-audit.txt`; target hash search
across `G:/KytyPS5/logs`, `G:/KytyPS5/repo`, and `G:/KytyPS5/_Shaders`; `ShaderDecoder.cpp` family
dispatch; Prosper `rdna2_decode.cpp` default/SMEM cases. Prosper and public RDNA2 identify SMEM
as high-six-bit `0x3d`; local LLVM has no `llvm-mc.exe` and `llvm-objdump` cannot disassemble Kyty
raw `.bin` files as object inputs. Archive streams at `pc=0x3e74` belong to other hashes and are
not valid target evidence.

RELATED CODE/COMMIT: `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp:564-568`;
`src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp`; audit artifact above.

### BVH and FaultBuffer boundaries — PROVEN

Fact: `--stub-bvh` always misses and is only a downstream-progression aid. `FaultBuffer` is a page-fault bitmap.

Evidence: current CLI and renderer implementation.

Why it matters: neither is evidence of real BVH correctness or a general diagnostics channel. See `REFERENCE.md § BVH and fault tracking`.

### Local validation source — PROVEN

FACT: The Vulkan SDK SPIR-V tools used for target artifacts are under `G:/KytyPS5/tools/VulkanSDK/1.4.357.0/Bin/`; use `spirv-val --target-env vulkan1.3` and `spirv-dis` before investigating Vulkan or GPU behavior.

WHY IT MATTERS: Offline artifact validation is the cheapest handoff from shader emission to runtime pipeline debugging.

EVIDENCE: The 2026-09-10/11 target artifact checks above and `DEBUG_REFERENCE_SOURCES.md`.

RELATED CODE/COMMIT: `REFERENCE.md § SPIR-V emission and validation`.

### Clean target artifact batch — PROVEN

FACT: From clean HEAD `46fa56c` (semantic source fix `1161113`), ASTRO BOT emitted 44 startup modules: CS 22, PS 14, VS 7, MS 1. All 44 passed `spirv-val --target-env vulkan1.3`; numeric disassembly scans found no missing IDs.

WHY IT MATTERS: M1 startup shader/recompiler output is validated for the observed batch. The remaining P0 is runtime classification at M2; no frame or menu claim follows from offline validation.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_CLEAN_20260910_230833/`; target `0019_new_shader_cs_78af8e269b528b5c.spv` is 941,496 bytes, with 45,225 definitions/references and zero missing IDs; build/install executable hash is `AF740B8B2EE49CE957CFD77134FF515E5260D4EAEBA3257908ADAA53EEF29AEE` and runtime label is `Source build 46fa56c`.

RELATED CODE/COMMIT: `1161113`; `CURRENT_STATE.md` runtime checkpoint.

### Graphics trace gating — PROVEN

FACT: `GraphicsRunDebugDumpEnabled()` returns true only when graphics debug dump is enabled and printf direction is not `Silent`.

WHY IT MATTERS: A run with `--graphics-debug-dump true --printf-direction Silent` can emit and execute shaders without recording `QueuePoint`/pipeline traces; absence of those lines is not a pipeline failure.

EVIDENCE: `src/graphics/guest_gpu/graphicsRun.cpp`; clean run `ASTRO_CLEAN_20260910_230833` had `Silent` output and no runtime trace beyond stdout initialization/wave64 warning.

RELATED CODE/COMMIT: `REFERENCE.md § Build and runtime boundary`; no source commit required.

### Complete M2 command-path trace — PROVEN

FACT: The complete latest trace contains 18 vkCreateComputePipelines begin lines with 18 matching done result=Success lines. Graphics pipeline creation also succeeds. The run reaches BeginRendering, DrawComplete, QueuePoint DispatchDirect through submit=7, EndOfPipe signals/events, and guest flip/video-output work.

WHY IT MATTERS: Pipeline creation is not a proven persistent blocker. The active P0 is the first stable boundary after this command path, with command submission/timeline completion, GPU execution/wait, and host presentation still to be separated.

EVIDENCE: G:/KytyPS5/logs/ASTRO_M2_TRACE_20260910_232151/runtime.log. The final shader context is address 0x000000050069ec00, hash 0x530dcd964f29983c, SPIR-V EmitProgram words=289489, and a 1,157,956-byte module 0022 that passes spirv-val --target-env vulkan1.3 and a numeric scan (55,709 definitions, 55,709 references, missing 0). The log ends during descriptor/runtime dumping for that dispatch; no host Present result or visible frame is proven.

RELATED CODE/COMMIT: src/graphics/host_gpu/renderer/pipeline/shaders.cpp CreatePipelineInternal(compute); src/graphics/guest_gpu/graphicsRun.cpp QueuePoint/dispatch path; semantic commit 1161113.

### Post-intro fail-fast throw path — PROVEN

FACT: The exact 4374a9d ASTRO BOT dump contains an uncaught MSVC `std::out_of_range` on thread 32576 (0x7f40). The first project call site is app RVA 0x20f021 in `ResourceControlFlow`, inlined into `ExtractResourcePlan`, calling `std::vector<IR::BufferResource>::_Xrange` at RVA 0x188f90 for `program.info.buffers.at(memory.resource)` (`ResourceMaterialization.cpp:1838`). The captured `Program.info.buffers` vector has 7 entries; the branch proves `memory.resource >= 7`, but the numeric value and `MemoryInfo` element are not in the minidump.

WHY IT MATTERS: The current P0 is a shader/recompiler resource-plan invariant failure after IR translation and before SPIR-V emission. The UCRT `0xc0000409` / fast-fail subcode 7 is the terminal `terminate → abort` wrapper, not evidence of a stack-cookie failure, invalid-parameter guard, device loss, or color root cause. Do not replace the checked access with unchecked indexing or add a catch-all handler.

EVIDENCE: `C:/Users/mneroba/AppData/Local/CrashDumps/kyty_emulator.exe.9700.dmp`; matching Release binary/PDB from source `4374a9d9dbf5224fba68e5c9e3037196a0a13245`; `G:/KytyPS5/logs/ASTRO_HOSTTRACE_20260911_175340/runtime.log`; source `src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp:1838`; disassembly and vector layout recover size 7 and the taken range-check branch. Runtime hash `0x657ad04626bf9d55` completed Decode/CFG/IR TranslateProgram and had no SPIR-V emission artifact. Filesystem enumeration was concurrent, but the throwing frame is in the recompiler thread.

RELATED CODE/COMMIT: `ResourceControlFlow` / `ExtractResourcePlan` in `ResourceMaterialization.cpp`; `PipelineCache::ProgramCache::Get` caller; runtime source HEAD `4374a9d`. The source fix and regression below close this invariant for the observed transformation path; preserve raw guest shader bytes before this interval on a future diagnostic run if needed.

### Production compute input provenance before capture — PROVEN / superseded

FACT: A raw shader binary does not determine the dynamic `ShaderComputeInputInfo` used by `CompileProgram`. The exact historical ASTRO artifacts contain no input dump or replay capsule for CS `0x657ad04626bf9d55`; the target raw `.bin`/`.rdna2` and SPIR-V cannot supply PM4 register values. Do not construct replay candidates from `LocalSize`, IR builtin use, another shader's dump, or defaults.

WHY IT MATTERS: The next exact replay must use production-derived values. The current shape capsule is synthetic and is not a production-equivalent replay.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_HOSTTRACE_20260911_175340/runtime.log` and `G:/KytyPS5/logs/ASTRO_ROOTFIX_20260911_1929/printf.log` contain no target `ShaderDbgDumpInputInfo`/capsule; the only target SPIR-V reports `LocalSize 32x1x1`, which is non-unique. The historical input dump near `0x9e8627388f138c1d` belongs to that different shader.

RELATED CODE/COMMIT: PM4 decoding in `src/graphics/guest_gpu/command_processor/pm4Handlers.cpp`; `ShaderGetStaticInputInfoCS` and `GetShaderParams` in `src/graphics/shader/shader.cpp`; `PipelineCache::GetComputeProgram` and `ProgramCache::Get` in `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`; `RenderExecutor::DispatchDirect` in `src/graphics/host_gpu/renderer/renderCompute.cpp`.

FACT: For the target run, `host_subgroup_size=32` is **PROVEN** by the Vulkan report (`default=32`, no wave64 support) and the `SupportsComputeWave64()` branch. `user_data_base=0` is **PROVEN** as the compute `CompileOptions` default; the compute branch sets `wave_size` but does not set `user_data_base`. `dispatch_threads_num={0,0,0}` at the `CompileProgram` call is **PROVEN by source ordering**: `GetComputeProgram` calls `ProgramCache::Get` (which compiles) before `RenderExecutor::DispatchDirect` writes the dispatch counts back into `input_info`.

FACT: Before the bounded capture, the target-specific values for `threads_num`, `thread_ids_num`, `group_id`, `tg_size_en`, `workgroup_register`, `dispatch_thread_dimensions`, and `user_data count` were **UNAVAILABLE**. Their provenance is nevertheless fixed: `COMPUTE_NUM_THREAD_X/Y/Z` populate `CsStageRegisters.num_thread_*`; `COMPUTE_PGM_RSRC2` supplies TGID/TG_SIZE/TIDIG/USER_SGPR; `ShaderGetStaticInputInfoCS` copies those fields; `GetShaderParams` sizes `options.user_data` from `user_sgpr`.

WHY IT MATTERS: The generic opt-in `ShaderReplayInput` trace is placed before resource-plan extraction and immediately before `CompileProgram`, so one future capture can supply the missing production state without changing semantics. If `PROJECT_MEMORY.md` already answers an architectural question, do not re-investigate it unless current source, a regression, or new runtime evidence contradicts it.

RELATED CODE/COMMIT: diagnostic trace in `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`, initially committed as `3fb0ce7`; the complete production values supersede this unavailable-state checkpoint and are recorded below.

### Exact production replay state for CS 0x657ad04626bf9d55 — PROVEN

FACT: A single ASTRO capture from source `150a1395c7553191a8e5f856b60cdea657034ed8` paired
the target's pre-resource-plan and pre-CompileProgram records with
`replay_invocation_id=76`. Both boundaries contain the complete same state:
`threads_num={32,2,1}`, `thread_ids_num=2`, `group_id={true,true,false}`, `tg_size_en=false`,
`workgroup_register=16`, `host_subgroup_size=32`, `dispatch_thread_dimensions=false`,
`dispatch_threads_num={0,0,0}`, `wave_size=64`, `lds_size_dwords=128`, `scratch_size_dwords=0`,
`user_data_base=0`, and 16 ordered user-data dwords
`[0x02dff4b0,0x00000005,0x055c0100,0xc1400000,0x001fc01f,0x91b00204,0x00000000,0x00000000,0x00000000,0x00000000,0x5ac08000,0x00080005,0x00100000,0x00005204,0x02dff2e0,0x00000005]`.

WHY IT MATTERS: The target replay input is now production-derived and no guessed state is
needed. The checked `.at()` and resource/materialization semantics remain unchanged.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_REPLAY_INPUT_20260911_2223148/runtime.log` lines 828914 and
828922; raw target bytes `shaders/original/precompile_cs_657ad04626bf9d55.bin`; runtime capsule
`shaders/original/precompile_cs_657ad04626bf9d55.capsule.json` (SHA-256
`38C3BAEE5D9D3DEFA65A396D15B3546A7C88F2C5913AE273477F55953AEA5E2F`). Runtime emitted 124012
SPIR-V words and pipeline creation succeeded. Two offline exact replays returned internal
validation PASS and external `spirv-val --target-env vulkan1.3` exit 0, each producing
`LocalSize 32 1 1`, `uses_dma=true`, and identical SPIR-V SHA-256
`0644462056027C84D811B2D621EA5A51FCBFE07B31C629E533373A95DB67DFBC`.

RELATED CODE/COMMIT: diagnostic trace and invocation correlation in
`src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp` and
`src/graphics/shader/recompiler/ShaderRecompiler.h`, commit `150a139`.
The bounded launch used `--stub-bvh` and persisted the printf File sink; the wrapper stopped
after both records were durable, so no exit code is inferred.

### ASTRO M5 title screen progression — PROVEN

FACT: A progression-focused ASTRO run from the `150a139` executable reached the title screen
after the previously proven M4 intro path. The runtime loaded `title_controller_ship [title]`,
repeated `title_screen.spx`, `titlescreen_start_text.jxm`, and `astro_bot_logo_title.jxm`; the
window reached frame 495 at approximately 14 FPS. This is the first proven M5 main-menu/title
milestone.

WHY IT MATTERS: The resource-remap and target-shader replay work is no longer on the runtime
critical path. Future work should target progression from M5 toward gameplay.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_PROGRESS_20260911_225139/`; runtime.log lines 132658 and
750548, title assets at lines 133922 and 133940, stdout line 97 build label `Source build
150a139`, and final shader count `VS 14 | PS 22 | CS 40 | GS 1`.

RELATED CODE/COMMIT: runtime build source `150a139`; no semantic fix was made for this run.

FACT: After M5 was reached, the same run terminated through the existing fatal-error path at
`src/graphics/host_gpu/renderer/masterSemaphore.cpp:69`, the non-success check after
`vkDevice.waitSemaphores` in `MasterSemaphore::Wait`. The exact Vulkan result was not logged and
no new crash dump was created; this is an unclassified next P0, not evidence against M5.

WHY IT MATTERS: Do not reclassify this as a cold pipeline hang or reopen earlier shader/resource
blockers. The next investigation must first identify the exact semaphore result and its runtime
cause before changing semantics.

EVIDENCE: The same run's runtime.log lines 780571–780572 and stdout lines 107–125; wrapper
wall time `550.8255477 s`, PID `26180`, with no reliable process exit code.

RELATED CODE/COMMIT: `MasterSemaphore::Wait`; runtime artifact above; no fix made in this session.

### Bounded descriptor root resource remap — PROVEN

FACT: `PlanBoundedRootReads` can retain a raw scalar-buffer `ReadConstBuffer` as a descriptor root. `ApplyBoundedRootReads` keeps that original instruction alive with `ReferenceU32`, but the old `ResourceTracking::Collect` returned before `AddBuffer` and `AddMemoryPatch`. Its frontend resource id therefore remained in the guest/SGPR namespace while `program.info.buffers` was dense, producing the post-intro `std::out_of_range`.

WHY IT MATTERS: This is an upstream missing-registration/remap bug, not a valid-input case for clamping, unchecked indexing, catch-all handling, or graceful fallback. Keep the checked `.at()` as the invariant boundary. Ordinary bounded reads are still replaced later and remain deferred.

EVIDENCE: `TestBoundedRootScalarBufferResourceRemap` builds seven ordinary dense buffers, retains a dynamic root with `memory.resource=7`, and reaches `ExtractResourcePlan`. With the pre-fix `Collect` skip it fails with `invalid vector subscript`; with `edc7d4f` the root registers as dense buffer 7, `program.info.buffers` has eight entries, and the root remains represented in `srt_reads`.

RELATED CODE/COMMIT: `ResourceTracking.cpp::Collect`, `PlanBoundedRootReads`, `ApplyBoundedRootReads`; `tests/ResourceTrackingTests.cpp`; semantic commit `edc7d4f`.

### Scalar SRT image value operands — PROVEN

FACT: `ShaderSideUseGraph` must distinguish image descriptor identity from value
operands. `ImageResource` and `SamplerResource` remain host-tracked identities;
image addresses, coordinates, payloads, predicates, and atomic data are ordinary
shader values. Non-void image results are walked transitively so a later descriptor
identity use still rejects the root.

WHY IT MATTERS: A loop-carried scalar-address SRT value used only through an image
value operand must not be host-materialized. Treating every image use as a sink
caused `MaterializeResources` to evaluate a non-invariant PHI and stopped ASTRO
before SPIR-V emission.

EVIDENCE: The authentic regression in `tests/ResourceTrackingTests.cpp` fails on
the parent implementation and passes after `f6da96f`; `resource_materialization_tests`,
`scalar_provenance_tests`, and `shader_recompiler_compute_tests` pass. The fix keeps
the resource-identity rejection cases green and does not alter `.at()` or Vulkan
error handling.

RELATED CODE/COMMIT: `SrtWalker.cpp::IsShaderSideImageOperand` and
`ShaderSideUseGraph`; semantic commit `f6da96f`.

### Post-fix runtime boundary — PROVEN

FACT: After `f6da96f`, ASTRO progressed past CS `0xc8aec8bce60cd4a1` and reached
46 compute shaders. Its first new fatal boundary is MS shader
`0x2b3be82b8235ac05`, PC `0x00003ca0`, decoded raw `0xbf970024`: unsupported SOPP
opcode `0x17` in `ShaderCFG.cpp:40`. The run did not reach M6 and no fix was made
for this next blocker.

WHY IT MATTERS: The materialization invariant is closed for the observed path.
Future work should classify this frontend CFG/ISA gap first; colors remain P1 and
pipeline-cache latency remains P2.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_MATERIALIZE_FIX_20260912_1200/runtime.log`
lines 778545–778565; stdout ends at `VS 21 | PS 34 | CS 46 | GS 1`; process result
is `0x00000141`. Runtime provenance is source `f6da96f7d89536fc931033f049a97e32d5baa1cd`,
installed executable SHA-256
`11135EDE0163F58E7B84E9C7EEEB82E63A8345EAA7B24CFCEED33B3855A6ED43`.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/frontend/cfg/ShaderCFG.cpp:40`;
no semantic follow-up commit yet.

## Confirmed blocker history and commits

The later semantic and documentation checkpoints are also durable:

- 1161113 — generic structured-loop dispatcher fallback and native-value-only SRT spill handling; BDA/R1 untouched.
- 2ae7feb — documentation/runtime checkpoint for valid SPIR-V and the M2 command-path trace.

| Checkpoint | Durable result |
|---|---|
| `3b52034` | Lowered finite CFG-context resource PHIs to runtime selects without legalizing loop-carried execution state. |
| `4320bf5` | Retained producers needed for shader-side SRT reads. |
| `c1f0dde` | Retained pure scalar SRT reads used by BDA. |
| `f23d6f6` | Recovered SRT image reads for indirect planning. |
| `28cce31` | Refreshed shader-side SRT eligibility after tracking. |
| `53081ec` | Allowed scalar SRT values specifically in `ImageWrite` payload data. |
| `c94816c` | Allowed `ImageSampleRaw` coordinates and `ReadConstBuffer` byte offsets as runtime edges. |
| `09d5e94` | Preserved BDA clone provenance and exact per-use clones across eligibility refresh. |
| `dfc7203` | Retained bounded descriptor address/count sources through post-tracking DCE. |

- **BVH decode — PROVEN:** MIMG `0xe6` is `IMAGE_BVH_INTERSECT_RAY`; `0xe7` is the related BVH64 opcode. The miss stub exposed downstream work but did not implement semantics.
- **slot114 family — STRONG EVIDENCE:** involved SRT producer/liveness and shader-side planning. Later eligibility/lifetime work moved runtime past it; reopen only if current evidence regresses.
- **slot241 — PROVEN:** the reported `PHI is not invariant` came from safe runtime operand roles, not resource identity. `53081ec` and `c94816c` cleared it while negative identity cases remained rejected.
- **slot245/246 B6 — PROVEN:** `CloneBdaExpression` cached only `Value`; a shared-subtree cache hit lost `changed` provenance and left stale host-side IR. The regression fails before and passes after `09d5e94`.
- **slot245/246 R1 — PROVEN:** BDA applicability passed, but eligibility refresh treated per-use BDA clones as ordinary wrappers, cleared their flags after seeing `GetBufferResource`, and restored host evaluation of loop-carried state. Explicit `m_bda_srt_clones` provenance plus mixed-use regression fixed the generic defect in `09d5e94`.
- **BS1 bounded source — PROVEN:** `PlanBoundedReads` saved descriptor values; `ApplyBoundedReads` removed ordinary uses; DCE invalidated `GetUserData` producers; extraction copied `Void`; materialization failed before base-address formation or candidate reads. `RetainBoundedDescriptorSources` uses side-effecting `ReferenceU32` lifetime markers. `dfc7203` passed offline regression and the later target run reached SPIR-V emission.

## Known negative evidence

Do not retry these without contradictory current source, regression, or runtime evidence:

- “slot245 means BDA applicability failed” is false; its guards passed.
- “B6 alone explains slot245” is incomplete; refresh lifetime was a separate R1 defect.
- Allowing `GetBufferResource` shader-side does not solve dynamic identity safely.
- Source22 loop-carried state cannot be made host-invariant by choosing lane 0, an incoming PHI edge, or a host fixed point.
- Promoting a whole SRT slot loses mixed-use semantics.
- Arbitrary candidate caps, guest-memory scans, zero/dummy/null descriptors, and game/hash/slot/PC exceptions lack semantic proof.
- A `c94816c-dirty` runtime label does not validate later revisions; provenance must be captured for each run.
- Repeated game runs without a source change or one stated discriminating observation are expensive and non-informative.

## High-value code and evidence locations

```text
src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp
src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp
src/graphics/shader/recompiler/ir/passes/SrtWalker.cpp
src/graphics/shader/recompiler/ir/Value.cpp
src/graphics/shader/recompiler/ShaderRecompiler.cpp
src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp
G:/KytyPS5/logs/
```

Useful existing mechanisms: `LowerScalarBufferReadToBda`, `CloneBdaExpression`, `ShaderSideSrtReadFlag`, `m_bda_srt_clones`, `RefreshShaderSideSrtEligibility`, `PlanBoundedReads`, `ReadBoundedSrtU32`, `RuntimePhiSelect`, `ExtractResourcePlan`, and `MaterializeResources`. Their stable roles are in `REFERENCE.md`.

### Driver pipeline snapshots in normal runs — PROVEN

FACT: Expensive graphics and compute pipeline creation now snapshots the existing Vulkan driver
cache whenever elapsed time exceeds 1000 ms and a cache is available, regardless of shader or
graphics debug flags. Both paths hold `PipelineCache::m_mutex` and reuse
`SnapshotDriverCacheLocked()` with the existing flush and atomic-replace behavior; snapshot
failure remains non-fatal.

WHY IT MATTERS: Normal ASTRO diagnostics can persist and reload the driver cache without adding
new capture infrastructure, but persistence alone does not prove a warm compile-time benefit.

EVIDENCE: Source commit `86073bb`; cold run
`G:/KytyPS5/logs/ASTRO_PIPELINE_CACHE_COLD_20260911_2325/` created a 9,674,239-byte cache
and saved four expensive compute snapshots (85,675–152,333 ms). Warm run
`G:/KytyPS5/logs/ASTRO_PIPELINE_CACHE_WARM_20260911_2335/` loaded that payload, saved an
updated 17,565,856-byte cache, and repeated the landmarks in 85,791–154,026 ms with wall
times 542.884 s and 543.670 s; both ended at the existing `MaterializeResources` fatal.

RELATED CODE/COMMIT: `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`, `86073bb`.
The active checkpoint remains M4/post-intro with M5 unproven; the known `ErrorDeviceLost (-4)`
timeline-wait result remains the runtime P0 and was not investigated by this cache experiment.

FACT: The failure-only wait diagnostic in the cold cache run records
`vkDevice.waitSemaphores` returning `ErrorDeviceLost (-4)` for ticks 328465 and 328442
(`known=328441`, `current=328466`). This is the exact Vulkan result, not yet its causal root.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_PIPELINE_CACHE_COLD_20260911_2325/runtime.log` lines
778120–778121. Do not retry or suppress the wait failure; classify its source in the next
runtime investigation.

### Post-M5 device loss — PROVEN RESULT, ROOT UNCLASSIFIED

FACT: `MasterSemaphore::Wait` can return `vk::Result::eErrorDeviceLost` after ASTRO reaches
the post-M5 runtime path. In two `bd131e8` diagnostic runs, the failure was observed for
ticks 327685/327708 and 352888/352911; the process ended with wrapper status `0x141`.

WHY IT MATTERS: The exact Vulkan result is settled, but the wait is only the first host-side
observation of an asynchronous GPU fault. Do not treat the last marker write as the cause, ignore
or retry the error, or reopen the earlier resource-remap/replay work.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_DIAG_20260912_0005/runtime.log` and
`G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_HISTORY_20260912_0018/runtime.log` contain the failure
records and submit metadata. Both runs reached 40 compute shaders. Windows System Event 153
from `nvlddmkm` coincided with each termination (first at
`2026-09-11T21:07:56.4265059Z`, second at local `2026-09-12 00:29:33`). NVIDIA driver-event
evidence indicates a GPU/driver fault but does not identify the offending command.

RELATED CODE/COMMIT: `MasterSemaphore::Wait`, `CommandScheduler::Submit`, and the bounded
submit ring in `masterSemaphore.{h,cpp}`; diagnostic commits `a7b7bb5` and `bd131e8`.

### Submit metadata interpretation — PROVEN MECHANISM

FACT: The failed wait records are predominantly `debug_op=3` (`EopWrite`) marker submissions,
with one preceding `debug_op=5` (`EopWriteBack`); `CommandProcessor::WriteAtEndOfPipe` performs
the guest marker write through the host synchronization path and records metadata. The ring did
not retain a direct draw/dispatch immediately before the failure.

WHY IT MATTERS: EOP metadata identifies the synchronization point at which device loss was
detected, not necessarily the GPU command that caused it. The next investigation must recover the
last non-EOP operation and its descriptor/resource state before proposing a semantic fix.

EVIDENCE: `ASTRO_DEVICE_LOSS_HISTORY_20260912_0018/runtime.log` lines 826063–826100; enum
definitions in `src/graphics/guest_gpu/command_processor/commandProcessor.h`; EOP paths in
`src/graphics/guest_gpu/graphicsRun.cpp` and `src/graphics/host_gpu/renderer/sync.cpp`.

RELATED CODE/COMMIT: diagnostic submit ring from `bd131e8`; no semantic change made.

### Optional Vulkan fault diagnostics — PROVEN

FACT: On the Windows RTX 3090 test host, `VK_EXT_device_fault` and
`VK_NV_device_diagnostic_checkpoints` are available and enabled by the diagnostic build
`624fb5d` without changing required device features or synchronization behavior.

WHY IT MATTERS: Future device-loss runs can query driver fault data and bounded command markers
at the existing fatal boundary; the extensions themselves are not the blocker.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_GPU_FAULT_DIAG_20260912_0115/runtime.log` logs both extensions
as enabled. The same run reached the pre-existing `MaterializeResources` fatal before the wait
boundary, so it produced no fault/checkpoint payload and does not classify the separate
`ErrorDeviceLost (-4)` cause.

RELATED CODE/COMMIT: `src/graphics/host_gpu/graphicContext.cpp`,
`src/graphics/host_gpu/renderer/gpuFaultDiagnostics.{h,cpp}`, commit `624fb5d`.

### Fault-diagnostic ownership and partial results — PROVEN

FACT: Checkpoint batches are retained as soon as a submission receives its timeline tick and
before `vkQueueSubmit`; fault collection keeps the original vendor-binary size separately,
passes the allocated capacity on the second query, and preserves address/vendor records when
the driver returns `VK_INCOMPLETE`.

WHY IT MATTERS: A submit or asynchronous device-loss failure can now resolve its last command
markers without exposing a short vendor buffer to the Vulkan driver. This changes diagnostics
only; failed Vulkan results are still fatal and are never retried or ignored.

EVIDENCE: `gpuFaultDiagnostics.cpp`, `commandScheduler.cpp`, semantic diagnostic commit
`50bb5ad`; scheduler-only and full `shader_recompiler_compute_tests` both exited 0.

### Non-EOP submit provenance — PROVEN MECHANISM

FACT: The command buffer records the most recent non-EOP `DispatchDirect`, `DrawIndex`, or
`DrawIndexAuto` tuple and copies it into the submit-history record before a wait can report
device loss. EOP marker metadata remains separate, so the recorded non-EOP operation is a
causal lead rather than proof of the faulting GPU command.

WHY IT MATTERS: `MasterSemaphore::Wait` can now report the last ordinary GPU operation that was
queued before the observed timeline failure without changing submission or wait semantics.

EVIDENCE: diagnostic commit `568f00b`; focused scheduler-only and full
`shader_recompiler_compute_tests` both exited 0. The run
`G:/KytyPS5/logs/ASTRO_NON_EOP_DIAG_20260912_1340/` recorded submit `6256`,
`DispatchDirect` groups `4096,1,1`, mode `65`, and guest code pointer
`0x000000050052a400` before the failing wait.

RELATED CODE/COMMIT: `context.cpp`, `commandScheduler.cpp`, and
`masterSemaphore.{h,cpp}`, `568f00b`.

### Device-loss fault payload — PROVEN RESULT, ROOT UNCLASSIFIED

FACT: In the `568f00b` diagnostic run, `vkDevice.waitSemaphores` returned
`VK_ERROR_DEVICE_LOST (-4)` for ticks `329237` and `329214` (`known=329213`,
`current=329238`). `VK_EXT_device_fault` returned `Success` with 36 address records,
no vendor records, and `partial=false`; every address had type 4,
`VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT`.

WHY IT MATTERS: The exact host result and driver payload are durable evidence, but they do not
identify the asynchronous GPU command or justify a semantic fix. Do not classify the last EOP or
last non-EOP marker as the root cause without resource/state correlation.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_NON_EOP_DIAG_20260912_1340/runtime.log` lines 778865–778949;
no earlier queue-submit, pipeline-create, or Vulkan error was logged. The runtime log's generated
build label is stale (`246ea51-dirty`); use source HEAD `568f00bcc0f1086383c39705216ec35c06c47942`
and executable/PDB hashes in `CURRENT_STATE.md` for provenance.

RELATED CODE/COMMIT: `MasterSemaphore::Wait`, `gpuFaultDiagnostics.cpp`, `568f00b`.

### Guest dispatch address records are not active-shader identity — PROVEN

FACT: AGC records keyed by a guest code address can be stale, reused, or interleaved with
later shader mappings. In `ASTRO_BDA_DISPATCH_STATE_20260912_1945`, address
`0x00000005007b7300` had an earlier AGC record advertising `shader_size=0x13a0`, but a later
active `GraphicsDispatchState` record for the same address reported shader hash
`0x9e3c6093e9c20738` and `code_words=160`.

WHY IT MATTERS: A guest VA or AGC code size alone cannot identify the pipeline or resource
state that caused an asynchronous GPU fault. Require an active dispatch-side pointer-to-hash
record before investigating BDA addresses, storage-image bindings, lifetime, or synchronization.

EVIDENCE: The bounded generic trace in
`G:/KytyPS5/logs/ASTRO_BDA_DISPATCH_STATE_20260912_1945/` emitted 8192 dispatch records but
did not reach target address `0x000000050052a400`; the run ended at the known decoder baseline
before that dispatch. No device-loss, BDA, or resource causal claim follows from this capture.

RELATED CODE/COMMIT: `AgcCreateShader`, `ShaderMapUserData`, and
`RenderExecutor::DispatchDirect`; diagnostic source was temporary and reverted.

### Target dispatch provenance and non-causal success — PROVEN

FACT: In the active dispatch path, guest code address `0x000000050052a400` maps to CS hash
`0x657ad04626bf9d55` for a `4096x1x1` dispatch. In the bounded capture, that dispatch was
submitted as `debug_submit=6683` at timeline tick `353333`; its submit and waits completed
successfully, and the same submit stream continued through tick `353496`.

WHY IT MATTERS: The address/hash pair is a valid lead from the earlier fault run, but a successful
production dispatch with the same target path means the shader or dispatch alone is not proven to
be the deterministic cause of `VK_ERROR_DEVICE_LOST`. Keep the P0 at the asynchronous device-loss
boundary and require a concrete lifetime, range, synchronization, or GPU fault correlation before
changing semantics.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_DEVICE_LOSS_DISPATCH_20260912_2022/analysis.txt` and its
`runtime.log`; the fault comparison is
`G:/KytyPS5/logs/ASTRO_NON_EOP_DIAG_20260912_1340/runtime.log`, where submit `6256` (ticks
`329214`/`329237`) returned `ErrorDeviceLost`. The target after-rebind snapshot recorded a 512 MiB
page table, an 8 MiB fault buffer, and live non-null host buffers with valid BDAs.

RELATED CODE/COMMIT: temporary trace in `renderCompute.cpp` (reverted); submit/wait ownership in
`commandScheduler.cpp` and `masterSemaphore.cpp`; documentation checkpoint `f522eba`.

### MS decoder unreachable-tail classification and fix — PROVEN

FACT: The complete production mesh-shader stream for `0x2b3be82b8235ac05` is preserved at
`G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/shaders/original/precompile_ms_2b3be82b8235ac05.bin`
(4164 dwords; SHA-256 `A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`).
A sequential walk from a known boundary reaches `S_ENDPGM` at `0x3d30`, the
`S_CBRANCH_CDBGSYS` target path, and the completed unconditional backedge `S_BRANCH 0x3ca4`
at `0x3dac`. There is no branch target in `[0x3db0,0x3e74]`; `0x3e70` is a one-dword
`V_SUB_F32`, so `0xc2208080` is unreachable post-backedge tail data, not an instruction start.
Kyty and Prosper agree that high-six-bit `0x30` is unsupported/unknown, while public RDNA2
SMEM uses `0x3d`; no PS5-specific `0x30` encoding is established.

WHY IT MATTERS: Do not add a Family::0x30 decoder case or reuse the isolated word as an ISA
lead. `DecodeProgram` now tracks unvisited forward targets, ignores out-of-span targets, and
stops after a completed unconditional backedge once the post-END target path is visited. This
preserves required post-END CDBGSYS control flow while avoiding linear decode of unreachable
metadata/tail bytes.

EVIDENCE: `G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/target-sequential-decode.clean.txt`,
`branch-targets.txt`, `target-prosper-decode.txt`, and `boundary-audit-final.txt`. The
production-derived regression `TestDecoderStopsAfterCompletedBackedgeBeforeTailData` and
`shader_cfg_tests`, `ctest -R ^shader_cfg$`, and `shader_recompiler_compute_tests` all pass.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.cpp`,
`tests/shaderCfgTests.cpp`, semantic commit `5e2e219`.

### MS decoder regression provenance — PROVEN

FACT: The production-derived tail regression is independently runnable and passes. The
`2a51379` CDBGSYS regression remains useful for SOPP/CFG coverage but is synthetic and does
not validate raw `0xc2208080` or the mesh-stage stream.

WHY IT MATTERS: A green CDBGSYS unit test must not be mistaken for proof that the old runtime
failure word was a genuine instruction. The exact tail fixture proves the generic traversal
boundary without adding a `Family::0x30` decoder case.

EVIDENCE: `G:/KytyPS5/logs/MS_DECODER_EXACT_REGRESSION_20260912_2355/analysis.txt`; isolated
`KYTY_EXACT_MS_REGRESSION=1 shader_cfg_tests.exe` exit 0; restored full `shader_cfg_tests.exe`
exit 0; `git merge-base --is-ancestor 2a51379 5ebf00a` success. The old runtime record has
stage MS/hash `0x2b3be82b8235ac05`, `code_words=4164`, `pc=0x3e74`, raw `0xc2208080`, but did
not capture the preceding dword or explicit mode. The preserved raw stream supplies
`0x08183d08` at `0x3e70` and one-dword width. The latest f55195 ASTRO run did not re-enter this
MS hash, so exact runtime re-entry remains unproven.

RELATED CODE/COMMIT: `tests/shaderCfgTests.cpp`, `5e2e219`, runtime artifacts
`MS_RAW_CAPTURE_20260912_2240` and `ASTRO_DECODER_FIX_20260912_2340`.

### Prosper-class runaway-indirect/GDS audit — PROVEN ruled out on current artifact

FACT (**PROVEN**): Local DS lowering is separate for GDS and LDS. `MemoryOps.cpp:381-407`
decodes the DS opcode, 16-bit offset, and GDS selector; `Memory.cpp:99-101,114-145,850-856`
selects `ResourceKind::Gds` and passes M0/EXEC, while LDS remains its own resource kind.
`spirvEmitterMemory.cpp:726-800` uses `base=(M0>>16)`, `size=M0&0xffff`,
`address=base+offset`, `raw_index=address>>2`; GDS append/consume is a device-scope atomic
against the runtime array. `spirvEmitterMemoryHelpers.cpp:108-124,159-164` checks
`index < OpArrayLength`. `bufferCache.cpp:27,209` supplies a zeroed 64 KiB (16384-dword)
backing allocation. No explicit post-add modulo is emitted, so the code proves the 64 KiB backing allocation,
M0 16-bit fields, and dword indexing but does not prove a separate host-side wrap operation.

FACT (**PROVEN for recoverable records; unavailable for unseen indirect values**): In
`ASTRO_MS_FIX_20260913_0015`, host-tick snapshots retain DispatchDirect 329598
(`0x657ad04626bf9d55`, 4096x1x1), DispatchDirect 329599
(`0x338b450551457250`, 131072x1x1), and DrawIndexAuto 329607. No DispatchIndirect execution
record contains dereferenced x/y/z. The renderer's capped direct sample repeats bounded
per-frame dimensions through frame 39 without monotonic growth.

FACT (**PROVEN absence of a recoverable chain**): `AgcAcbDispatchIndirect` logged 2280
pointer-only records using two repeated addresses (`0x...63c0` and `0x...63e0`). They both
fall within the retained allocation `0x...4000 + 0x4000`, but the snapshot records only a
vertex subrange; it does not establish an indirect resource binding, argument values, host tick,
or last writer. The artifact has no DS_APPEND/DS_CONSUME, DMA_DATA, fill/copy, or
DispatchIndirect execution text; its 128 `gds_offset/gds_size` matches are PM4 metadata dumps.

WHY IT MATTERS: The Prosper semantic class (runaway or accumulating indirect group counts
caused by missing GDS/counter producer state) is ruled out as a proven explanation here. Do not
add a cap, skip a dispatch, change GDS semantics, or enable address-binding diagnostics from
this evidence. If a future capture is justified, request only actual indirect x/y/z immediately
before dispatch and last-writer provenance keyed by host tick and command ordinal.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_MS_FIX_20260913_0015/gds-indirect-census.txt`;
`gds-provenance-search.txt`; source paths above; `snapshot-window-329621.txt`.

RELATED CODE/COMMIT: `AgcAcbDispatchIndirect` (`src/libs/agc.cpp:3302-3331`), PM4
handlers (`src/graphics/guest_gpu/command_processor/pm4Handlers.cpp:1319-1345`),
CPU dereference (`src/graphics/guest_gpu/graphicsRun.cpp:1131-1143`), and existing bounded
snapshot commit `5ebf00a`.

### Selective upstream correctness ports — PROVEN

FACT: The local branch accepts six generic upstream fixes after source/test audit: indirect
dispatch arguments are read through synchronized GPU-clean backing (`e020fab` with prerequisite
`e47eb4b`, local `1be44d2`); submitted graphics/compute PM4 spans own immutable packet bytes
(`515d644`, local `7d4ead9`); indirect-buffer chain control is honored, including an empty chain
(`f1de661`, local `d43c1b7`); conditional IB predicates preserve dword bit 2 and use an unaligned-safe
64-bit copy (`03d3f2a`, local `75e4726`); and RDNA2 `S_MUL_HI_I32`, `V_FRACT_F16`, and
`V_CMPX_LT_U16` decode/translate through existing IR (`7bcd43d`, `a21ffde`, local `83d9f32` and
`2298df0`).

WHY IT MATTERS: These are generic queue/PM4/ISA correctness improvements, but they do not
classify or close the current ASTRO post-M5 device-loss P0. Do not infer runtime progress from
these offline fixes or reopen the closed resource-remap/GDS findings.

EVIDENCE: Focused runs in `G:/KytyPS5/logs/UPSTREAM_AUDIT_20260913_/`: `shader_cfg_tests`,
`shader_recompiler_compute_tests`, `resource_materialization_tests`, and
`scalar_provenance_tests` passed; `resource_tracking_tests` reaches its known unrelated
`dynamic storage mips` baseline failure. `CheckGpuCommandLane`, `Pm4WaitResume`, and the new
conditional-predicate regression pass.

RELATED CODE/COMMIT: `GuestGpu::Submit/SubmitCompute`, `CommandProcessor::DispatchIndirectAddress`,
`ProcessIndirectBuffer`, `CpOpBranch`, scalar/vector decoder tables and translators; local commits
`1be44d2`, `7d4ead9`, `d43c1b7`, `75e4726`, `83d9f32`, `2298df0`.

FACT (**PROVEN deferred candidates**): A1 `ded853b` global EOP/flip synchronization is not
portable to the current nonblocking packet contract; A4 `12c855e` synchronized indirect-register
snapshots reject existing host-pointer PM4 fixtures; B3 `c354657` is already locally implemented;
B4 `c913951` subvector-loop semantics failed its upstream regression on the local CFG/mask model.

WHY IT MATTERS: Do not retry these ports without a new source contradiction, a regression that
proves the local contract has changed, or target runtime evidence tying the behavior to a blocker.

### Exact Release validation and first post-audit blocker — PROVEN

FACT: A clean Release build/install from source `b1ef4bf2a058664655b6b5fe6193fb50ffc64e49`
was run once with the established ASTRO `--stub-bvh` baseline. The installed executable and
matching PDB were SHA-256 `A2E986E3EBE2914747CC3B9CA311FDE95185ECF772992D2E04A79F9A2F998EA5`
and `6F6697122C0242F969C1E76362AE6CF20F468C7BFEDEB8D365C72064AA320A01`.

WHY IT MATTERS: The accepted upstream ports have now been exercised together in a production
run. The run progressed to frame 848 at 18 FPS and shader counts `VS 21 / PS 34 / CS 46 / GS 2`,
well beyond the old executable's device-loss point. No attribution to an individual port is
proven; runtime validation must treat the next decoder boundary as the active P0.

EVIDENCE: `G:/KytyPS5/logs/ASTRO_RELEASE_20260913_102700/runtime/result-summary.txt`,
`runtime/stdout.log`, `runtime/stderr.log`, and `runtime/runtime.log`. The process exited with
`321 (0x141)` after MS `0x2b3be82b8235ac05` completed Decode (`2881` instructions) and failed
in CFG BuildGraph at `pc=0x3da8` on raw `0xbe8e210e`, `Family::SOP1`, opcode `0x21`, with
`SOP1 opcode is not implemented` at `src/graphics/shader/recompiler/frontend/cfg/ShaderCFG.cpp:40`.
No device loss, MaterializeResources failure, or crash dump occurred in this run.

RELATED CODE/COMMIT: `ShaderDecoder.cpp`, `ShaderCFG.cpp`,
`G:/KytyPS5/logs/ASTRO_RELEASE_20260913_102700/runtime/runtime.log`.

### SOP1/SOP2 opcode-family reconciliation — PROVEN

FACT: Production MS `0x2b3be82b8235ac05` reaches raw `0xbe8e210e` at
`pc=0x3da8`. Its family discriminator is `0x7d` (`Family::SOP1`), its SOP1
opcode field is `0x21`, and both encoded register fields are `s14`. Local and
fetched `origin/main` source map `S_LSHR_B64` only in the SOP2 table at opcode
`0x21`; the enum, scalar translator, IR `ShiftRightLogical64` opcode, and
SPIR-V lowering already exist. SOP1 decoding has one source and one
destination, while `ShiftRightLogical64` requires a `U64` source pair plus a
`U32` shift count, so an SOP1 alias is not semantically valid.

WHY IT MATTERS: No generic `S_LSHR_B64` implementation change is justified by
this failure. Do not add a SOP1 alias, infer a second operand, or alter the
family discriminator. The active P0 is a distinct unsupported SOP1 encoding
that needs an independent ISA/source contract before semantic work.

EVIDENCE: Exact runtime failure in
`G:/KytyPS5/logs/ASTRO_RELEASE_20260913_102700/runtime/runtime.log`; preserved
production binary
`G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/shaders/original/precompile_ms_2b3be82b8235ac05.bin`
(4164 dwords, SHA-256
`A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`);
local/upstream table comparison and field decoding in
`G:/KytyPS5/logs/SOP1_LSHR_AUDIT_20260913_/analysis.txt`; focused
`shader_cfg_tests` and `shader_recompiler_compute_tests` both exit `0`.

RELATED CODE/COMMIT: `frontend/decode/ScalarAluOps.cpp` (SOP1/SOP2 tables and
operand widths), `frontend/decode/ShaderDecoder.cpp` (family selection),
`frontend/translate/Scalar.cpp`, `frontend/translate/Translate.cpp`
(`ReadU32Pair`), `ir/opcodes/ValueOpcodes.inc`, and
`backend/spirv/spirvEmitterAlu*.cpp`; this documentation checkpoint is the
current repository tip.

### Production S_SWAPPC external-call contract — PROVEN / target value unresolved

FACT (**PROVEN**): In production MS `0x2b3be82b8235ac05`, raw `0xbe8e210e` at
`pc=0x3da8` is `S_SWAPPC_B64 s[14:15], s[14:15]`. Its semantics require an old-value
snapshot: the instruction reads the old 64-bit pair as the target, writes `PC+4`
(`0x3dac`) into the same pair, then jumps to the old target. Any implementation must
preserve that overlap ordering explicitly.

FACT (**PROVEN**): The bounded stream walk has no width ambiguity. Both s14 and s15 are
last written by `S_BUFFER_LOAD_DWORDX2 s14, s4, offset=96` at `0x3d74`; no later instruction
before `0x3da8` writes either register. The load chain is scalar memory
`0x0000000f_e0040000` -> `S_LOAD_DWORDX4` into s4..s7 -> descriptor payload at `+0x60`.
The exact old pair, host allocation, and target code bytes are not present in the available
same-run artifacts, so direct numeric target provenance remains unresolved.

FACT (**STRONG EVIDENCE**): This is an external guest-code call. The current MS contains no
`S_SETPC_B64` instruction to consume the saved return pair, while the continuation at
`0x3dac` is exactly the saved-return location. The callee therefore must be outside the
current MS binary and return through `S_SETPC_B64 s[14:15]` (or equivalent saved-pair form).

WHY IT MATTERS: Local CFG resolution in `ShaderCFG.cpp` accepts only local PC-relative or
validated in-binary jump-table targets. A dynamic external target cannot be represented as
an ordinary local CFG edge or fake fallthrough. The semantically appropriate design is
dispatch-side target resolution and callee-body splicing, matching KytyPS5 PR #427 commit
`a2cafb2f2c4a7fec745caf47e4e230bd0d1b6785`; no source was ported in this audit.

EVIDENCE: `G:/KytyPS5/logs/SOP1_CALL_AUDIT_20260913_/analysis.txt`,
`G:/KytyPS5/logs/MS_RAW_CAPTURE_20260912_2240/shaders/original/precompile_ms_2b3be82b8235ac05.bin`
(SHA-256 `A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`), and the
bounded decode `G:/KytyPS5/logs/SOP1_CALL_AUDIT_20260913_/decode_full.txt`.

RELATED CODE/COMMIT: `frontend/decode/ScalarAluOps.cpp`,
`frontend/decode/MemoryOps.cpp`, `frontend/translate/Translate.cpp` (`ReadU32Pair`,
`WriteU32Pair`), `frontend/translate/Control.cpp`, and `frontend/cfg/ShaderCFG.cpp`
(`ResolveSetpcTargets`, local-target validation); comparison only:
`https://github.com/KytyPS5/KytyPS5/commit/a2cafb2f2c4a7fec745caf47e4e230bd0d1b6785`.

### S_SWAPPC provenance diagnostic boundary — PROVEN

FACT (**PROVEN**): Commit `3f7a30c` contains the generic, opt-in dispatch-side resolver and
bounded synthetic descriptor-chain regression; `shader_cfg_tests` exits 0. It runs before
translation/CFG failure, reads only live user-data/guest memory through the existing bounded
read cache, and does not alter `S_SWAPPC_B64` semantics or suppress CFG errors.

FACT (**PROVEN**): The one authorized ASTRO capture with the resolver enabled did not reach the
target MS. With the warm pipeline cache it stopped first at CS `0x0000000908e86a00` (hash
`0xa572ee17a880e71c`), scalar source `0x000000e0` at `pc=0x370`,
`ShaderDecoder.cpp:269`, wrapper status `321` (`0x141`). No target record was emitted.

WHY IT MATTERS: The target `S_SWAPPC_B64 s[14:15],s[14:15]` descriptor value remains
unresolved; no runtime target, allocation, callee words, or return pair may be inferred from
this run. The earlier CS decoder boundary is now the first runtime blocker for any future
capture.

EVIDENCE: `G:/KytyPS5/logs/SWAPPC_DIAGNOSTIC_20260913_/capture-report.txt`,
`runtime/runtime.log`, `runtime/process-result.txt`, and `shader_cfg_tests.log`.

RELATED CODE: `src/graphics/shader/recompiler/ShaderSwapPcDiagnostic.{h,cpp}` and
`src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`.

### CS scalar-source 0xe0 provenance audit — PROVEN for preserved artifact, current run unresolved

FACT (**PROVEN**): The only preserved raw stream containing the reported offset `pc=0x370`
is the older `fb948a435a4e295e` compute artifact (`1104` bytes). Its dword at `0x370` is
`0x881000e0`, which local VOP2 field rules decode as opcode `0x04` (`V_SUB_F32 v8, src0=0xe0,
v0`). Scalar source `0xe0` is reserved by the RDNA2 source-code ranges.

FACT (**PROVEN**): That artifact's reachable stream ends at `S_ENDPGM` raw `0xbf810000` at
`0x2dc`; all bytes through `0x370` are post-END padding/embedded metadata. No branch target
enters this region, and current `DecodeProgram` returns at an END with no pending target.

WHY IT MATTERS: This is the same broad trailing-data shape as the closed MS case, but without
a backedge. It does not justify another traversal change. The current diagnostic run reports
address `0x908e86a00`, hash `a572ee17a880e71c`, and `code_words=60`; its persisted 240-byte
stream cannot contain `pc=0x370`. Therefore the current failure remains a provenance mismatch,
and the older `fb948` bytes/hash/guest address must not be transferred to it.

EVIDENCE: `G:/KytyPS5/logs/CS_E0_CLASSIFICATION_20260913_/analysis.txt` and
`G:/KytyPS5/logs/SWAPPC_DIAGNOSTIC_20260913_/runtime/runtime.log`.

### Diagnostic resolver terminal-boundary provenance — PROVEN

FACT: The opt-in `ResolveSwapPcDiagnostics` path used by `ProgramCache::Get` was scanning `params.code` linearly before `TranslateProgram` and called `Decoder::DecodeInstruction` after `S_ENDPGM` when no pending branch target remained. This violated the executable traversal contract for terminal streams and could decode post-END metadata such as production raw `0x881000e0` at `pc=0x370`, causing `DecodeScalarSource(0xe0)` to fail without the current shader's exact span/hash context. A later same-hash MS stream proves that production `Decoder::DecodeProgram` may retain a post-END path when pending targets exist, so this fix is not a complete branch-aware traversal contract.

WHY IT MATTERS: A reported decoder PC can be impossible for the stage/hash/code span shown by the normal phase log when a pre-Translate diagnostic path fails first. Do not add support for reserved scalar source `0xe0` or infer a shader identity from that mismatch. Diagnostic scans must stop at terminal shader boundaries unless production decoding has a pending branch-target path to retain.

EVIDENCE: `G:/KytyPS5/logs/PROVENANCE_AUDIT_20260913_/source-audit.txt`; fail-before `shader_cfg_tests` exit `321` at `pc=0x370`/`0x881000e0`; pass-after exit `0`; `shader_recompiler_compute_tests` exit `0`.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/ShaderSwapPcDiagnostic.cpp`, `tests/shaderCfgTests.cpp`, commit `191cc73`.

### Post-fix e5 failure ownership — PROVEN diagnostic path, identity unavailable

FACT (**PROVEN**): On the exact `f47c496` Release run, the first post-fix fail-fast was `unsupported scalar source operand 0xe5 at pc=0x7c`. Matching PDB symbolization shows `DecodeScalarSource → DecodeSop2 → DecodeInstruction → ResolveSwapPcDiagnostics → ProgramCache::Get<ShaderVertexInputInfo> → GetGraphicsPrograms → DrawAuto`. The error therefore originates in the opt-in diagnostic resolver before `TranslateProgram`.

WHY IT MATTERS: The prior `e0@0x370` issue is closed, but this new reserved source must not be added or suppressed. The last normal PS phase log is a completed prior invocation; it is not the failing shader identity.

EVIDENCE: `G:/KytyPS5/logs/E5_OWNERSHIP_20260913_/ownership.txt` and `stack-symbolized.txt`; runtime `G:/KytyPS5/logs/PROVENANCE_AUDIT_20260913_/runtime-validation/astro-run/` (`exit=321`, build label `f47c496`).

FACT (**PROVEN**): The same run did not log the failing invocation's selected VS/Mesh stage, guest address, hash, code_words/span, or raw word because `DumpShaderRawBeforeCompile` and any pre-resolver marker were disabled by the launch flags. Exact tail/reachability classification is therefore unresolved and must not be inferred from historical shader files.

RELATED CODE/COMMIT: `pipelineCache.cpp:552-560,640-645`, `ShaderSwapPcDiagnostic.cpp:458-500`, commit `191cc73`.

## Diagnostic fused-front boundary provenance — PROVEN (2026-09-13)

FACT: The first post-`191cc73` `0xe5 @ pc=0x7c` is a diagnostic-only traversal failure in an MS/GS-front fused invocation, not evidence that reserved scalar source `0xe5` is reachable production code.
WHY IT MATTERS: The opt-in S_SWAPPC scanner must stop at the same front boundary as fused production decoding before any semantic decoder change is considered.
EVIDENCE: Exact clean `53996c0` run artifact `G:/KytyPS5/logs/E5_PROVENANCE_RUNTIME_20260913_/astro-run/`; invocation 29 header identifies stage MS, guest shader `0x00000005007d1700`, hash `0x4e555b0ebf3b53f8`, 56 words. Raw `0x99e758e5` at pc `0x7c` is SOP2 `S_PACK_LH_B32_B16` with reserved `ssrc0=0xe5`; preceding pc `0x3c` is `S_SETPC_B64 s6`. AGC logs show the same front/back fusion operation, and `ShaderRecompiler.cpp::DecodeFusedProgram` stops at S_SETPC before splicing `back_code`.
RELATED CODE/COMMIT: `src/graphics/shader/recompiler/ShaderRecompiler.cpp` (`DecodeFusedProgram`); `src/graphics/shader/recompiler/ShaderSwapPcDiagnostic.cpp` (`ResolveSwapPcDiagnostics`); diagnostic provenance commit `53996c0`.

### Fused-front diagnostic contract — PROVEN (2026-09-13)

FACT (**PROVEN**): Commit `0ca59901d51d2a083cee8ba3732651e7fa8258f3` exposes the exact
production fused-front boundary as `FusedFrontWordCount`. `DecodeFusedProgram` and the opt-in
`ResolveSwapPcDiagnostics` scanner share this result when `back_code` is present, so the
scanner stops at `S_SETPC_B64 s6` without changing scalar decoding, production CFG, or
S_SWAPPC semantics.

FACT (**PROVEN**): The invocation-29 fixture from hash `0x4e555b0ebf3b53f8` fails before the
fix at raw `0x99e758e5`, `pc=0x7c`, reserved source `0xe5`, then passes after the boundary
trim; an S_SWAPPC inside the valid front remains discoverable. Focused `shader_cfg_tests` and
`shader_recompiler_compute_tests` both exit 0.

FACT (**PROVEN**): The one Release validation run from `0ca5990` (artifact
`G:/KytyPS5/logs/E5_FUSED_FRONT_FIX_20260913_/astro-run/`) passed the former e0/e5
diagnostic boundaries and reached MS hash `0x2b3be82b8235ac05`, then stopped at production
CFG `SOP1 opcode=0x21`, raw `0xbe8e210e`, `pc=0x3da8`, exit `321`/`0x141`. No S_SWAPPC record
was emitted because the scanner's non-fused linear walk stops at `S_ENDPGM` `pc=0x3d30`;
production `DecodeProgram` retains pending branch-target paths and reaches `0x3da8`. The
next diagnostic question is this normal-shader boundary contract; do not infer S_SWAPPC
target values from this run or rerun ASTRO until that boundary is classified offline.

WHY IT MATTERS: The fused-front fix is closed. A future generic scanner change must preserve
the distinction between fused handoff trimming and branch-aware normal-shader traversal; do
not add a global `S_SETPC`/`S_ENDPGM` break or scalar-source workaround.

EVIDENCE: `G:/KytyPS5/logs/E5_FUSED_FRONT_FIX_20260913_/fail-before-v2.log`,
`shader_cfg_tests-pass-after-v2.log`, `shader_recompiler_compute_tests-pass-after-v2.log`,
`astro-run/runtime.log`, and
`G:/KytyPS5/logs/SOP1_CALL_AUDIT_20260913_/decode_full.txt`.

RELATED CODE/COMMIT: `src/graphics/shader/recompiler/ShaderRecompiler.cpp`,
`ShaderSwapPcDiagnostic.cpp`, `tests/shaderCfgTests.cpp`, commit `0ca5990`.

FACT: The bounded same-run decode window is `pc=0x00..0x7c`; both paths from `S_CBRANCH_EXECZ` at `0x08` converge at `S_WAITCNT 0x38`, then terminate the fused front at `S_SETPC_B64 0x3c`. Words after `0x3c` are tail data for this front span; `0xe5` is not a production scalar-source contract.
WHY IT MATTERS: Do not add scalar-source table entries, fake fallthrough, or production CFG changes for this failure.
EVIDENCE: `G:/KytyPS5/logs/E5_PROVENANCE_RUNTIME_20260913_/diagnostic-boundary-analysis.txt` lists all captured raw words and boundary reasoning.
RELATED CODE/COMMIT: `ShaderDecoder.cpp` SOP2 decode; `ShaderRecompiler.cpp` fused front/back handoff.

## Vulkan pipeline cache reuse and key stability — PROVEN (2026-09-13)

FACT (**PROVEN**): The active NVIDIA device supports `VK_EXT_pipeline_creation_cache_control`
(rev 3), `VK_EXT_pipeline_creation_feedback` (rev 1), and `VK_KHR_pipeline_binary` (rev 1),
with pipeline-cache-control and pipeline-binary features reported enabled.

FACT (**PROVEN**): The existing per-title Vulkan cache loaded a `79,150,623`-byte payload with a
live cache handle. Ordinary compute pipelines returned `HIT`, `feedback_flags=0x3`, and
`app_cache_hit=true`, while giant CS `0x78af8e269b528b5c` returned
`COMPILE_REQUIRED` in two identical cache-probe runs.

FACT (**PROVEN**): For that giant CS, specialization hash
`0xdf4c4b5ea0432871` and descriptor-layout hash `0xf5863b4aec51aeea` remained stable, but the
SPIR-V hash changed from `0xf24896587addb349` to `0xd4ab3a5cc5435144`; the full semantic
fingerprint changed with it.

WHY IT MATTERS: A loaded `VkPipelineCache` does not currently prove warm reuse for the giant
path. Do not normalize generated SPIR-V, key by shader hash, or add pipeline binaries until the
generic source of SPIR-V nondeterminism is understood. Keep the profile opt-in.

EVIDENCE: `G:/KytyPS5/logs/VULKAN_PIPELINE_STARTUP_20260913_/cache-probe-run-v3/` through
`-v7/`, `G:/KytyPS5/logs/VULKAN_PIPELINE_STARTUP_20260913_/cache-probe-analysis.txt`,
and `G:/KytyPS5/logs/vulkaninfo_full_20260912.log` (extension/features). The giant cold timings
are in `G:/KytyPS5/logs/SHADER_STARTUP_PERF_20260913_/analysis.txt`.

RELATED CODE/COMMIT: `src/graphics/host_gpu/renderer/pipeline/pipelineCacheFingerprint.h`,
`pipelineCache.cpp`, `shaders.cpp`, `vulkanWindow.cpp`; commits `1ecf6d6` and `567180f`.

## SPIR-V dispatcher spill determinism — PROVEN (2026-09-13)

FACT: Giant CS `0x78af8e269b528b5c` uses the dispatcher fallback with `lane_count=2` (wave64,
host subgroup32) and 520 spills. Before `f56a8fb`, three independent processes emitted the
same 1040 spill `OpVariable` IDs in different orders, producing different SPIR-V hashes despite
identical replay inputs.
WHY IT MATTERS: Pointer-keyed unordered-map iteration changed emitted SPIR-V identity and
prevented a loaded Vulkan cache from reusing the giant pipeline. This was a generic emitter
ordering defect, not shader-specific semantics.
EVIDENCE: fail-before hashes `8266A755...`, `C71B6394...`, `0E3CA4B5...`; first divergence
was the spill-variable region. After `f56a8fb`, three giant runs match
`386480C1F768721CF41D7888DB93F5921CD224E9562BBA3B2A8F5A7DFD63B3D6` and three small
production-fixture runs match `F5EDA4E9B9EE5F65A6442384E89225D74F3410A08143C2BD7AC1ED65A92A83CF`.
`spirv-val --target-env vulkan1.3` passes.
RELATED CODE/COMMIT: `src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp`,
commit `f56a8fb`; artifacts `G:/KytyPS5/logs/SPIRV_DETERMINISM_20260913_/`.

FACT: The cache-only probe after the fix loaded the existing 79,150,623-byte cache but returned
`COMPILE_REQUIRED` for the stable giant identity (`spirv_hash=0x271420d97ec99650`,
fingerprint `0xb259b45ff04fb40b`).
WHY IT MATTERS: The cache predates the deterministic identity and does not yet prove warm giant
reuse. Do not infer a cache defect or add pipeline binaries without a fresh cache/progression
experiment using this stable SPIR-V.
EVIDENCE: `G:/KytyPS5/logs/SPIRV_DETERMINISM_20260913_/cache-probe-after/`; probe stopped
before normal compilation.

## Persistent Vulkan cache reuse after deterministic SPIR-V — PROVEN (2026-09-13)

FACT (**PROVEN**): A fresh isolated per-title `_PipelineCache/PPSA21567.bin` populated by exact
Release `f56a8fb` reused all four giant compute pipelines. Cache-only probes returned `HIT` for
`0x78af8e269b528b5c`, `0x7bd68261b1bdfb68`, `0xf3f4e1671b30c1f4`, and `0x530dcd964f29983c` with
the same SPIR-V hashes, specialization hashes, layout hashes, and fingerprints as the successful
cold population.
WHY IT MATTERS: Ordinary persistent `VkPipelineCache` is sufficient once emitted SPIR-V is
deterministic; a `VK_KHR_pipeline_binary` prototype is not justified for this blocker.
EVIDENCE: the first giant compiled in `85482 ms`; the remaining three compiled in `114048`,
`131535`, and `152900 ms`. The persisted fresh cache is `8726040` bytes with SHA-256
`566137417818E49B4592A5D61535C24F7D3B1F27B7B8F8E6A5025BC298A2AA30`. A warm process reached all
four `HIT` probes in 8 seconds wall and was stopped before normal pipeline creation, a measured
lower-bound saving of `475.965 s` (98.35%).
RELATED CODE/COMMIT: `pipelineCache.cpp`, `shaders.cpp`, commit `f56a8fb`; full artifacts
`G:/KytyPS5/logs/PIPELINE_CACHE_REUSE_20260913_/`.
