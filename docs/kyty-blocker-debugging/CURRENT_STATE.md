# Current KytyPS5 debugging state

Last reconciled: 2026-09-14 08:33 Europe/Riga

This is the volatile checkpoint. Durable facts live in PROJECT_MEMORY.md; stable mechanisms live in REFERENCE.md.

## Repository and provenance

Repository/worktree: G:/KytyPS5/Fork/repo
Branch: astro/materialize-resources
Official repository checkout: G:/KytyPS5/OfficialRepo (main)
Game root: G:/PS5_Games
ASTRO BOT input: G:/PS5_Games/PPSA21567/extracted
Demon's Souls input: G:/PS5_Games/PPSA01342/extracted
Repository source HEAD for runtime checkpoint: `9515fd017c38385a35c4f93f06b96bdc50e2c218`
Current source HEAD: `4215519` (path documentation; runtime source remains `9515fd0`)
Last ASTRO runtime source: `9515fd0`
Working tree for this checkpoint: clean
Build: Release, CMake/Ninja, clang-cl, clang-lld_link-64
Executable: G:/KytyPS5/Fork/repo/_Build/windows/install/kyty_emulator.exe
Executable SHA-256 (runtime build): `84A6FAC8B1496E202921FA9CEFA528F560674A64EE949D2041C3CC11ADE0B617`
Matching PDB: G:/KytyPS5/Fork/repo/_Build/windows/install/kyty_emulator.pdb; SHA-256 `B3E84D1E9EA6B070F753D80B980188A8BEC2470F968D881D70459BA77EE6DA74`
Binary provenance: Release rebuild from `9515fd0`; warmed per-title cache remains the runtime input

The system-wide CMake install prefix was not used because it requires administrator access.

## Repository split and AGC work-area fix — 2026-09-14

The fork checkout is now `G:/KytyPS5/Fork/repo`; its linked `test-pr497` worktree is
`G:/KytyPS5/Fork/repo-pr497`. The official upstream checkout is `G:/KytyPS5/OfficialRepo` on
`main`. The root `AGENTS.md` records these paths.

Commit `9515fd0` provisions the official AGC fixed DMEM work area generically: a flexible fixed
mapping at `0x0fe0040000` of size `0x001b0000`, followed by clearing the first `0x60` bytes during
`AgcInit`. The authentic regression failed before the mapping (`KernelVirtualQuery` returned
`0x8002000d`) and passes after it. Focused `shader_cfg_tests` and
`shader_recompiler_compute_tests` also pass.

The single post-fix warm-cache ASTRO run is under
`G:/KytyPS5/logs/AGC_DMEM_ASTRO_20260913_2320/astro-run/`. It proves the fixed mapping call and
still reaches the same MS `0x2b3be82b8235ac05`, `pc=0x3da8`, unsupported SOP1 `0x21` rejection;
the diagnostic still reports unknown `s14:s15`, so mapping provisioning alone did not resolve the
descriptor-chain read. The current P0 remains the unresolved scalar descriptor read; no S_SWAPPC
semantic change is justified.

## Demon’s Souls OfficialRepo launch — 2026-09-14

The clean upstream `OfficialRepo` checkout at `bbebb6419b7891d634931c45e7ec005fc9e6c5ad` was
built as Release and launched from
`G:/KytyPS5/OfficialRepo/_Build/windows/install/kyty_emulator.exe` (SHA-256
`D8714D3A25B7C3347563DBA809A43B61CCA44DC014F4E1D02949AC91BAE846B0`) with
`--game G:/PS5_Games/PPSA01342/extracted`, without quoting the game path. The run initialized
Vulkan and compiled 96 compute shaders before terminating during shader compilation.

The first recorded error is compute shader hash `0xfb0becc9db83db77`, `pc=0x00001044`:
`GetImageResource dword 0 is not a valid runtime value` at
`OfficialRepo/src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp:167`. `stderr` is
empty; the asynchronous launch wrapper did not preserve a numeric process exit code. Full
provenance and logs are under
`G:/KytyPS5/logs/DEMON_SOULS_OFFICIAL_20260914_083900/`.

## Exact MS S_SWAPPC CFG trace — 2026-09-13

The complete warmed-cache capture is under
`G:/KytyPS5/logs/SWAPPC_CFG_TRACE_20260913_2310/astro-run/`; the bounded reconciliation is
`G:/KytyPS5/logs/SWAPPC_CFG_TRACE_20260913_2310/trace-summary.txt`. It reached MS
`0x2b3be82b8235ac05` at guest code `0x0000000500120600`, call `pc=0x3da8`, raw
`0xbe8e210e`, and emitted the requested production diagnostic before CFG rejection.

PROVEN: `s14:s15` is unknown at the call. Its last writer is the expected
`S_BUFFER_LOAD_DWORDX2 s14,s4,+0x60` at `0x3d74` (`0xf4240382`), but the source pair is not
known, so the descriptor base and effective load address are unknown. The static preceding chain
is `0x0000000f_e0040000` -> `S_LOAD_DWORDX4 s4..s7` at `0x3d68` -> the `+0x60` buffer load.

PROVEN: no external target is resolved; target fetch is not attempted, no callee allocation or
return bytes are captured, and `ResolveIndirectCalls` returns no call site. Consequently no
handler validation or splice occurs and the pipeline compiles the raw 4164-dword caller. The
branch-aware executable caller boundary is end-exclusive `0x3db0` (3948 dwords); at `0x3da8`
one unique pending forward target remains, `0x3dac`, discovered by branches at `0x3d54` and
`0x3d84`. The completed back-edge at `0x3dac` clears that pending target and terminates the walk.

The exact CFG rejection is unchanged: unsupported SOP1 opcode `0x21` / raw `0xbe8e210e` at
`0x3da8`. The first divergence is therefore descriptor-chain read/provenance, before target
fetch or splice. No S_SWAPPC semantic change or Aftermath/device-loss diagnostic work is
justified from this trace. The current P0 is now the unresolved scalar descriptor read at the
`S_LOAD_DWORDX4` boundary; no generic integration fix is committed until the read failure's
address-space ownership is proven.

## S_SWAPPC descriptor address ownership audit — 2026-09-13

The bounded audit is `G:/KytyPS5/logs/SWAPPC_ADDRESS_AUDIT_20260913_/address-ownership-audit.txt`;
the negative runtime search is `runtime-pointer-search.txt`, the streaming mapping parse is
`runtime-memory-map-audit.txt`, and the source call-site cross-check is
`source-prt-callsite-audit.txt`. The scalar root preceding the
production `S_LOAD_DWORDX4` is `0x0000000f_e0040000` (`0x0fe0040000`, 0x4000-aligned). It lies
numerically inside the PRT interval `[0x0f00000000, 0xfc00000000)` and below
`HOST_USER_MIN` in the Windows system-reserved interval `[0x0800000000, 0x1000000000)`. This is
consistent with a PRT alias, but fixed flexible mappings can also consume reserved spans, so
numeric range membership is not ownership proof.

The warmed runtime log and preserved same-run artifacts contain no `KernelSetPrtAperture`, exact
pointer allocation, or memory-map record. Its eight parsed `out_addr` mappings contain no range
covering the target. The existing `virtual_memory_allocation_tests` run
passes, including `PrtBackingReadPreservesSparseResidency`, which proves registered-PRT sparse
read behavior but does not identify the production pointer's owner. The reader still has no PRT
fallback. The source call-site cross-check finds no in-repository production caller of
`KernelSetPrtAperture`; only the guest NID export can establish that mapping, while the
buffer-image staging path is the sole production consumer of `TryReadPrtBacking`. The evidence
therefore points to missing guest mapping provenance rather than a proven resolver-reader defect.
Address ownership remains unproven, with no preserved host mapping to service the descriptor; no
source change, regression, or ASTRO rerun is justified until this provenance is captured.

## AGC fixed-address provenance audit — 2026-09-13

The official title library `G:/PS5_Games/PPSA21567/extracted/fakelib/libSceAgc.sprx` directly
uses the same fixed `0x0fe0040000` address: its setter/getter at raw offsets `0x8b90` and
`0x8bd0` compare a library-global pointer to that value and access indexed 16-byte slots. Its
initializer at raw `0xe630` obtains a base/size from an imported AGC-driver Dmem query, aligns
the base, clears the first `0x60` bytes, and checks that the base equals `0x0fe0040000`.
The audit is `G:/KytyPS5/logs/SWAPPC_PROVENANCE_20260913_2246/agc-work-area-audit.txt`;
the library SHA-256 is `EACE8AC152404132E632A7E170DDCCA47F230E7C0C02CD4B76F29CA9604037FF`.

The warmed runtime proves HLE `AgcInit(state=0x000000090f371e28, ver=13)` followed by shader
creation, while `src/libs/agc.cpp::AgcInit` only logs and returns. The runtime has no
`KernelSetPrtAperture` or mapping record for `0x0fe0040000`, and the fake library is outside
the directories scanned by `PreloadAdjacentPrograms`. This narrows the provenance gap to AGC
work-area/driver integration, but does not establish the driver call contract, allocation size,
or whether the address is PRT-backed. No generic resolver fix or reader fallback is justified.

## BDA translation diagnostic capture attempt — 2026-09-13

The diagnostic-only source change is committed as `6e379ab`; focused `shader_cfg_tests`,
`shader_recompiler_compute_tests --scheduler-only`, and `shader_recompiler_compute_tests` all
pass. It adds a bounded, opt-in fault-snapshot record for guest BDA page-table translations
referenced by compute push-data pairs without changing execution semantics.

The one authorized Release run used the warmed `PPSA21567` cache and `--stub-bvh`; exact
provenance and command are in
`G:/KytyPS5/logs/BDA_TRANSLATION_DIAGNOSTIC_20260913_1900/astro-run/provenance.txt`.
The run exited 321 (`0x141`) at the deterministic MS `S_SWAPPC_B64` CFG failure
(`0x2b3be82b8235ac05`, `pc=0x3da8`, `raw=0xbe8e210e`) before the target CS dispatch.
Consequently no BDA translation, device-loss, or GPU snapshot record exists for this run;
the capture status is **incomplete**. The bounded report is in
`G:/KytyPS5/logs/BDA_TRANSLATION_DIAGNOSTIC_20260913_1900/astro-run/run-classification.txt`.
Do not run ASTRO again or broaden diagnostics until this recurring production CFG boundary is
reconciled offline.

## Target and milestones

Target game: ASTRO BOT EU
Title ID: PPSA21567
Game input: G:/PS5_Games/PPSA21567/extracted
Current milestone: M5 main menu — reached in the validated title-screen progression run
Next milestone: M6 gameplay
Current P0: establish the AGC work-area/descriptor ownership for production `S_SWAPPC_B64` at MS `0x2b3be82b8235ac05`, `pc=0x3da8`, raw `0xbe8e210e`; external-call resolution remains downstream of that read.
P0 class: production CFG rejects an unresolved external `S_SWAPPC_B64` because its descriptor-chain source is unreadable; the existing diagnostic-only splice path remains separate from production compilation.
Last validated progress signal: the exact Aftermath-wiring run reached MS `0x2b3be82b8235ac05` after compiling CS `0x657ad04626bf9d55`; CFG failed deterministically at `pc=0x3da8` with process result `321 (0x141)`. No GPU device-loss event occurred in that run.
Known P1 likely blockers: incorrect color/output interpretation remains P1 and is not a current fix target
Known P2: cold-start large dispatcher pipeline compilation latency; successful creates are not a hang. A fresh cache removes this cost on subsequent starts.

## NVIDIA Aftermath attribution capture — 2026-09-13

`VK_NV_device_diagnostics_config` and `VK_NV_device_diagnostic_checkpoints` are supported by the
RTX 3090 and were enabled only through the new opt-in `--nvidia-gpu-crash-diagnostic` path. The
single warm-cache run is under
`G:/KytyPS5/logs/AFTERMATH_DIAGNOSTIC_20260913_2200/astro-run/`; exact command and hashes are in
`launch.txt` and `provenance.txt` (post-commit rebuild provenance is in
`provenance-postcommit.txt`). Aftermath initialized successfully, but no GPU crash dump or shader
debug callback was produced because the run stopped first at the deterministic production CFG
blocker: MS `0x2b3be82b8235ac05`, `pc=0x3da8`, raw `0xbe8e210e`, unsupported SOP1 `0x21`.
Process result was `321 (0x141)`; M6 and a new device loss were not reached. The vendor attribution
question is therefore still open and must not be inferred from this run.

Current P0 is the existing generic S_SWAPPC_B64 external-call implementation/CFG boundary. Do not
expand Aftermath or other GPU diagnostics until that blocker is cleared and a later device-loss
capture is actually reached.

## Address-binding IP correlation — 2026-09-13

The exact one-run opt-in capture is in
`G:/KytyPS5/logs/DEVICE_ADDRESS_BINDING_DIAGNOSTIC_20260913_2100/astro-run/`;
the bounded report is `device-fault-address-binding-report.txt`. Runtime support for
`VK_EXT_device_address_binding_report` was confirmed by `vulkaninfo` and the run log.
At device loss (`tick=329990`, `known=329966`, `current=329991`) `VK_EXT_device_fault`
returned 41 type-4 instruction-pointer hints spanning
`0x00000002034e2000..0x00000002034e2520` (precision `0x10`). The tracker retained
`live=339`, `history=2048`, `dropped_history=17043`, `dropped_live=0`; every fault IP had
`matches=0`, and no `GPU_DEVICE_FAULT_IP_PIPELINE` record was emitted. The extension therefore
did not expose the executable/IP cluster or a same-run pipeline/module mapping. Classification
remains **C**; do not infer an invalid resource access or add another diagnostic automatically.

The c0d46a2 bounded audit is recorded in
`G:/KytyPS5/logs/SWAPPC_EXTERNAL_SPLICE_FIX_20260913_/astro-run/device-loss-c0d46a2-report.txt`.
It contains a complete tick-329960 `DispatchDirect` snapshot for CS
`0x657ad04626bf9d55` (4096x1x1), 10 buffer records, 13 image records, 18 push dwords,
and live/non-deleted resource owners with no explicit range/layout violation. Tick
329959 is proven completed and has only submit/last-non-EOP metadata (same-run CS
`0xa572ee17a880e71c`, guest code `0x0000000908e86a00`); its resource/push/BDA snapshot
is absent. Device fault returned 39 type-4 unknown addresses, no vendor records, and
unknown checkpoints. Host observation required a forced reboot after a full-system hang,
but the causal operation remains unproven; no semantic or diagnostic change is justified.

## Target CS BDA/SPIR-V audit — 2026-09-13

The bounded offline audit is recorded in
`G:/KytyPS5/logs/DEVICE_LOSS_AUDIT_C0D46A2_20260913_1825/offline-audit.txt`.
The c0d46a2 snapshot identifies a `DispatchDirect 4096x1x1` for CS
`0x657ad04626bf9d55` (guest code `0x000000050052a400`, pipeline
`0x000000ff6aae46d0`) with 18 push dwords, 10 buffers, and 13 images. The seven
normal buffer allocations are live and allocation-bounded; non-null images are
live and no explicit image layout/extent contradiction is recorded. The replay
module comparison has 124012 words, four guarded physical-address loads, two
image atomics with barriers, two bounds-guarded image stores, and four loops with
explicit `+1` counters and subgroup termination tests. No concrete stale/range/
layout defect or statically provable non-termination is established.

The only unresolved address path is the page-table translation of guest
`0x5034ff4d0`/`0x5034ff4d4` (page index `0x140d3f`) derived from push
`0x00000005034ff4b0`; the snapshot has the page-table buffer identity but not
entry contents or publication state. Keep the classification at C and do not add
diagnostics or rerun ASTRO until that datum is specifically required.

## S_SWAPPC provenance capture — 2026-09-13

The exact `c24f4ba` Release build was installed with the warmed cache and run once using
`--stub-bvh --shader-swappc-diagnostic true`. Provenance and complete runtime artifacts are in
`G:/KytyPS5/logs/SWAPPC_PROVENANCE_20260913_1940/astro-run/`; the bounded classification is in
`capture-report.txt`.

The run produced no diagnostic invocation for target MS `0x2b3be82b8235ac05` (zero matches in
runtime log or stderr). The only MS diagnostic invocation was fused MS `0x4e555b0ebf3b53f8`.
The first terminal boundary was `vkDevice.waitSemaphores` returning `ErrorDeviceLost (-4)` at
ticks `328388` and `328411` (`known=328387`, `current=328412`), process result `321` (`0x141`).
The retained report identifies tick `328388` as CS `0x657ad04626bf9d55`, `DispatchDirect 4096x1x1`,
with complete push/BDA/resource and device-fault records. The target resolver path was not reached;
categories A–E are therefore not applicable, and no source or diagnostic expansion is justified.

## Tick-328388 BDA/page-table audit — 2026-09-13

Offline report: `G:/KytyPS5/logs/SWAPPC_PROVENANCE_20260913_1940/astro-run/bda-tick-328388-audit.txt`.
The complete same-run snapshot for CS `0x657ad04626bf9d55`, `DispatchDirect 4096x1x1`, contains
exactly two BDA translations and no dropped records. Push pairs 0 and 14 both use page index
`0x140b7f`, entry `0x42bbf4000`, and owner allocation slot `35:1` (guest base
`0x502dfc000`, size `0x4000`). They resolve to `0x42bbf74b0`/offset `0x34b0` and
`0x42bbf72e0`/offset `0x32e0`, each with 4-byte range, `within_allocation=1`, `published=1`,
`live=1`, and descriptor owner `deleted=0`.

The previously missing `bda_pagetable[0x140d3f]` / guest `0x5034ff4d0` and `0x5034ff4d4` does not
occur in this run's tick-328388 push data or BDA records; those addresses belong to the separate
c0d46a2 allocation and are not reused. Classification: **C — translation fully valid**. A stale
or unpublished entry and an invalid resolved range are disproven for the captured pairs; no
shader-side invalid access or synchronization defect is proven. The next single missing datum is
GPU-side fault attribution to the first shader memory operation/address or synchronization edge.

## Device-fault type-4 IP audit — 2026-09-13

Offline report: `G:/KytyPS5/logs/SWAPPC_PROVENANCE_20260913_1940/astro-run/device-fault-ip-audit.txt`.
The same-run `VK_EXT_device_fault` record contains 33 `addressType=4` values, all with precision
`0x10`, sparsely spanning `0x00000002034e2000..0x00000002034e2520`. They are instruction-pointer
hints, not invalid-memory ranges. No `VK_EXT_device_address_binding_report` data, shader-module or
pipeline executable GPU-VA allocation, or command checkpoint attribution is present. The known
dispatch guest code (`0x000000050052a400`), captured BDA addresses (`0x04xxxxxxxx`), and pipeline
object handle do not overlap the `0x02xxxxxxxx` hint cluster.

The addresses therefore remain un-attributable to CS `0x657ad04626bf9d55` or another submitted
shader. Classification: **C — insufficient attribution evidence**. The next single missing datum
is the GPU executable-address to pipeline/shader-module mapping for the failing submit; no ASTRO
rerun or shader instrumentation is justified by the existing evidence.

## S_SWAPPC c0/6e runtime divergence audit — 2026-09-13

Offline report: `G:/KytyPS5/logs/SWAPPC_RUNTIME_DIVERGENCE_AUDIT_20260913_/comparison.txt`.
`c0d46a2` is an ancestor of `6e379ab`, and resolver, splice, decoder, and production pipeline
blobs are byte-identical across those commits. Both runs used the same `--stub-bvh` baseline and
the same warmed cache payload. The c0 runtime log contains no compilation of target MS
`0x2b3be82b8235ac05`; it contains only the fused MS `0x4e555b0ebf3b53f8` before its device loss.
The absence of a CFG failure in c0 therefore does not prove a successful target splice.

The `6e379ab` run explicitly compiled the target with `code_words=4164`, completed decode, and
failed in CFG at `0x3da8`/`0xbe8e210e`, proving that its compile span remained the raw caller.
Neither run captured the target invocation's user data, descriptor load, resolver result, handler
fetch, return validation, or splice result. The exact missing datum is one same-invocation,
production resolver/splice provenance record; the existing opt-in S_SWAPPC diagnostic can provide
it. No new ASTRO run or BDA diagnostic expansion is authorized until that evidence is available.

## External S_SWAPPC runtime validation — 2026-09-13

The generic dispatch-side external-call implementation is committed in `4ab899d`, with
branch-aware production traversal and destination-pair validation in `51af33f`, and the
unconditional-resolver guard in `77d499e`. Focused `shader_cfg_tests`,
`shader_recompiler_compute_tests`, `resource_materialization_tests`, and `scalar_provenance_tests`
pass. `resource_tracking_tests` still reports its unrelated dynamic-storage-mips baseline failure.

The final one-run artifact is
`G:/KytyPS5/logs/ASTRO_SWAPPC_M6_20260913_1725/`; its exact command and binary/cache hashes are
in `provenance.txt`. The run produced no prior S_SWAPPC CFG failure or scalar-tail diagnostic and
reached 40 CS shaders. The first new terminal boundary was `vkDevice.waitSemaphores` returning
`ErrorDeviceLost (-4)` for ticks `329976` and `329999` (`known=329975`, `current=330000`), with
the existing retained snapshot report (3 batches, 3 commands, 18 buffer records, and 24 image
records). M6 gameplay is not proven. Do not reopen S_SWAPPC, SPIR-V determinism, or pipeline-cache
work; classify this same-run device-loss chain next.

## Warm-cache M6 progression run — 2026-09-13

The exact Release build from `e483b45` was installed and run once with the established
`--stub-bvh` baseline. Provenance, command, hashes, and bounded classification are in
`G:/KytyPS5/logs/ASTRO_WARM_CACHE_M6_20260913_/provenance.txt` and
`runtime-classification.txt`; all runtime artifacts remain under that directory.

The run loaded `_PipelineCache\\PPSA21567.bin` (payload `8,725,962` bytes; source cache
SHA-256 `566137417818E49B4592A5D61535C24F7D3B1F27B7B8F8E6A5025BC298A2AA30`). No expensive
compile records were emitted for `0x78af8e269b528b5c`, `0x7bd68261b1bdfb68`,
`0xf3f4e1671b30c1f4`, or `0x530dcd964f29983c`, confirming warm reuse in the progression run.

The first deterministic blocker was MS hash `0x2b3be82b8235ac05`, `code_words=4164`,
`pc=0x3da8`, raw `0xbe8e210e`, unsupported SOP1 opcode `0x21` during CFG BuildGraph
(`ShaderCFG.cpp:40`). Decode completed with 2,881 instructions; no device-loss marker,
MaterializeResources failure, or M6 gameplay evidence occurred. The guest shader address
and numeric process exit code are unavailable in this run and remain explicitly unknown.
Do not reopen cache/SPIR-V work or treat this as a pipeline hang.

## Shader startup profiling — 2026-09-13

The bounded opt-in profiler is committed as `60273f3` and is disabled by default. Its records cover
decode, CFG, structurize, translation, resource/SRT tracking, optimization, SPIR-V emission,
validation, shader-module creation, and Vulkan pipeline creation without changing compile inputs.
Focused suites and replay validation passed before the runtime run. The deterministic 56-word
production-derived fixture produced byte-identical SPIR-V with profiling disabled/enabled.

The one allowed profiling run is under `G:/KytyPS5/logs/SHADER_STARTUP_PERF_20260913_/astro-run-v2/`.
All four known giant CS pipelines completed successfully. The bounded analysis is
`G:/KytyPS5/logs/SHADER_STARTUP_PERF_20260913_/analysis.txt`; it classifies Vulkan pipeline creation
as the dominant cost (98.91–99.02% per shader). Do not optimize frontend/CFG/SPIR-V or add async
compilation from this evidence. The opt-in cache probe below now separates cache reuse from
SPIR-V identity stability before any persistent pipeline-binary work.

## Vulkan pipeline cache probe — 2026-09-13

The opt-in `--pipeline-cache-profile true` probe is committed in `1ecf6d6`; the semantic
fingerprint bookkeeping test is committed in `567180f`. Device support is recorded in
`G:/KytyPS5/logs/vulkaninfo_full_20260912.log`: cache-control rev 3, creation-feedback rev 1,
and `VK_KHR_pipeline_binary` rev 1 with pipeline binaries enabled.

The existing cache was loaded from `_PipelineCache\\PPSA21567.bin` with a `79,150,623`-byte
payload (`cache_handle=true`) in `cache-probe-run-v6` and `-v7`. Ordinary compute probes report
`HIT` with `feedback_flags=0x3` and `app_cache_hit=true`. The first giant CS
`0x78af8e269b528b5c` reports `COMPILE_REQUIRED` in both runs, so the probe avoided another
87-second normal compile. Across v6/v7 specialization hash `0xdf4c4b5ea0432871` and layout
hash `0xf5863b4aec51aeea` are stable, while the SPIR-V hash changes
(`0xf24896587addb349` → `0xd4ab3a5cc5435144`) and the semantic fingerprint changes. This is
diagnostic evidence only; no pipeline-binary implementation is justified yet.

Artifacts: `G:/KytyPS5/logs/VULKAN_PIPELINE_STARTUP_20260913_/cache-probe-run-v3/` through
`-v7/`, `cache-probe-analysis.txt`, and the focused test logs. The probe runs stopped before
normal giant compilation and make no new M6 claim. Next action is a generic source audit of
SPIR-V determinism, followed by the narrowest safe cache design.

M1, M2, M3, M4, and M5 are closed/reached. M6 gameplay is not proven. Do not reopen the cleared startup SRT/BDA-lifetime, pipeline-create, submission, visible-frame, bounded resource-remap, scalar-PHI materialization, or TTMP scalar-source conclusions without contradictory evidence.

## Tick-keyed GPU snapshot reporting fix — 2026-09-13

Source audit: `G:/KytyPS5/logs/DEVICE_LOSS_SNAPSHOT_FIX_20260913_/source-audit.txt`.
The bounded `GpuFaultDiagnostics::m_snapshot_batches` deque is populated before `vkQueueSubmit`
and retired only through the last known GPU tick. The previous dump counted the entire deque in its
header but skipped every batch whose tick exceeded the failing wait tick; a concurrent submit could
therefore produce `retained_batches=3` with no `GPU_COMMAND_SNAPSHOT_BATCH` records. `Log::WriteFatal`
and `Log::Shutdown` flush the logger, so this was not a buffering loss.

Authentic fixture artifacts are `snapshot-fail-before.log` and `snapshot-pass-after-final.log` in
`G:/KytyPS5/logs/DEVICE_LOSS_SNAPSHOT_FIX_20260913_/`. Fail-before prints only the header for
three retained ticks; pass-after prints all three batches, DispatchDirect metadata, and buffer
records. Focused `--gpu-fault-snapshot-only` and `--scheduler-only` runs pass. The generic
diagnostic-only fix is committed as `d0ce0cb`; no scheduler/resource/Vulkan control flow changed.

That snapshot fix was validated by one exact run from `3901b80`; the follow-up barrier task adds no
runtime run. The next runtime attempt should use the exact `1459c38` Release build and only verify
that the complete retained window survives device loss.

## Device-loss report completion barrier — 2026-09-13

The exact post-snapshot-fix ASTRO run is preserved at
`G:/KytyPS5/logs/DEVICE_LOSS_SNAPSHOT_FIX_20260913_/astro-run/` (source build `3901b80`).
It reproduced `vkDevice.waitSemaphores` `ErrorDeviceLost (-4)` at ticks `329171` and `329194`
(`known=329170`, `current=329195`). The first reporter printed a window with three retained
batches and began the CS `0x657ad04626bf9d55` DispatchDirect record; the duplicate waiter logged
`already_reported` and reached the existing fatal path while the first report was still printing.
The file contains one batch, one command, and four of ten buffer records before termination, proving
the remaining gap was report/fail-fast concurrency. The wrapper process-result file was interrupted.

Commit `1459c38` replaces the boolean gate with `NotStarted → Reporting → Complete`, waits duplicate
callers on a condition variable, flushes the owner report before publishing Complete, and leaves
Vulkan detection/fail-fast behavior unchanged. Deterministic artifacts are under
`G:/KytyPS5/logs/DEVICE_LOSS_REPORT_BARRIER_20260913_/`: fail-before has duplicate output before
the held owner report; pass-after has exactly one header, three batches, three commands, three
resources, and post-Complete duplicate output. `--gpu-fault-report-barrier-only`,
`--gpu-fault-snapshot-only`, and `--scheduler-only` all exit 0. No ASTRO rerun is authorized in the
barrier task; the next runtime attempt should only verify that the complete retained window survives.

## Branch-aware S_SWAPPC diagnostic validation — 2026-09-13

The generic production/diagnostic branch-aware walker is committed in `623865e` and focused
`shader_cfg_tests` plus `shader_recompiler_compute_tests` pass. The exact Release executable
from that commit is installed with EXE SHA-256 `4D8A2A868E8C98DF42198B671FF950777E1C3F123B46B5ED1622A1E1CAD3DEC4`
and matching PDB SHA-256 `85E4008BE8511ADFA341E5935B78A12849F2D26B404BF211BD21E98EAFD50BC4`.

One permitted ASTRO capture is preserved at `G:/KytyPS5/logs/SWAPPC_BRANCH_WALK_FIX_20260913_/astro-run/`.
It reached 40 CS / 22 PS / 14 VS / 1 GS and the previous `0xe0`/`0xe5` diagnostic failures did not recur.
The first terminal boundary was `vkDevice.waitSemaphores` returning `ErrorDeviceLost (-4)` for ticks
`329178` and `329201` (`known=329177`, `current=329202`); `GPU_DEVICE_FAULT` completed with
`address_count=39`, `vendor_count=0`, `partial=false`, and checkpoints were emitted. The failing snapshot
at tick `329178` is a `DispatchDirect` of CS `0x657ad04626bf9d55` with groups `4096x1x1`; retained
resources were reported live with no stale/range violation. The requested MS target
`0x2b3be82b8235ac05` / raw `0xbe8e210e` / `pc=0x3da8` has no diagnostic record in this run because
termination occurred first. The wrapper did not retain a numeric process exit code. No semantic fix was made.

Next action: classify the same-run device-loss chain before another target capture; keep the branch-aware
walker and dormant S_SWAPPC diagnostics unchanged.
## Same-run device-loss window classification — 2026-09-13

Offline report: `G:/KytyPS5/logs/SWAPPC_BRANCH_WALK_FIX_20260913_/astro-run/device-loss-window-report.txt`.
The exact run from source `623865e` reported `vkDevice.waitSemaphores` `ErrorDeviceLost (-4)` for
requested ticks `329178` and `329201`, with `known=329177` and `current=329202`. Therefore tick
`329177` is the last definitely completed value and `329178` is the first definitely non-retired value;
no completion is proven for `329179..329201`.

Same-run evidence exists only for tick `329178`: an `EopWrite` submit in guest submit `6242` whose
last non-EOP operation is a direct dispatch of CS `0x657ad04626bf9d55` at guest code
`0x000000050052a400`, groups `4096x1x1`, with 10 buffers and 13 images. All retained resources report
live/non-deleted state and no explicit range or layout violation. Ticks `329179..329184` have no per-tick
records; ticks `329185..329201` have only EOP history, with one `DrawIndexAuto` predecessor reported at
`329187` but no shader/resource identity. GPU fault info completed (`address_count=39`, `vendor_count=0`,
`partial=false`), while checkpoint markers were unknown and not attributable to a command.

Classification: **C — insufficient same-run evidence to attribute the device loss**. No semantic or
 diagnostic source change is justified. The MS `S_SWAPPC` target remains unresolved because this run
terminated before its invocation; historical tick/VA/shader identities must not be transferred.

Next action: obtain one separately authorized, narrowly scoped command-to-fault-address provenance fact
before any new runtime capture or semantic change.
## S_SWAPPC target-capture retry — 2026-09-13

The exact Release build from HEAD `5804443` was installed with EXE SHA-256
`2FF864678DC7160AB097D0E7E32675531C843177F0B53E197F3AE86057B87B55` and PDB SHA-256
`FFC530A963797118C54FE1C8F1CA083E77DEF1EB12495AD30D81052A2A87BBC1`.

One permitted ASTRO retry is preserved at `G:/KytyPS5/logs/SWAPPC_TARGET_CAPTURE_20260913_135557/astro-run/`.
It reached 40 CS / 22 PS / 14 VS / 1 GS; the prior `0xe0`/`0xe5` and branch-walk failures did not recur.
The run stopped at `vkDevice.waitSemaphores` `ErrorDeviceLost (-4)` for ticks `329598` and `329575`,
with `known=329574` and `current=329599`. `GPU_DEVICE_FAULT` returned `Success` with
`address_count=44`, `vendor_count=0`, `partial=false`; checkpoint markers were unknown. The log ends
at `GPU_COMMAND_SNAPSHOT_WINDOW` without command/resource records, so this capture provides no materially
better same-run attribution than the prior C-class audit. The MS target
`0x2b3be82b8235ac05` / raw `0xbe8e210e` / `pc=0x3da8` was not invoked; numeric process exit code is
unavailable. No source or scheduling/resource change was made.

Classification remains **C — insufficient evidence to attribute device loss to a specific command/resource**.
Do not expand diagnostics or rerun ASTRO until one exact missing fact is selected.
## Latest Release runtime validation — 2026-09-13

Artifact: `G:/KytyPS5/logs/ASTRO_RELEASE_20260913_102700/`; source and installed Release
binary are exact `b1ef4bf2a058664655b6b5fe6193fb50ffc64e49`. Build and install output, hashes,
command, process result, stdout/stderr, and runtime log are preserved under that directory.
Installed EXE SHA-256 is `A2E986E3EBE2914747CC3B9CA311FDE95185ECF772992D2E04A79F9A2F998EA5`;
matching PDB SHA-256 is `6F6697122C0242F969C1E76362AE6CF20F468C7BFEDEB8D365C72064AA320A01`.
The exact command is in `runtime/command.txt` and uses the established `--stub-bvh` baseline
with shader and graphics debug disabled.

This was the one permitted progression run. It ran from 10:29:28 to 10:38:44 local time,
reached window `frame: 848, fps: 18`, and produced shader counts `VS 21 / PS 34 / CS 46 / GS 2`.
Large compute pipelines completed successfully, including CS `0x530dcd964f29983c` in 154355 ms;
the pipeline cache snapshot reached 19763603 bytes. The process exited with wrapper code `321`
(`0x141`) at the first deterministic new blocker: MS `0x2b3be82b8235ac05`, code size 4164
dwords, Decode completed with 2881 instructions, then CFG BuildGraph reported unsupported
`Family::SOP1 opcode=0x21` raw `0xbe8e210e` at `pc=0x00003da8` from
`src/graphics/shader/recompiler/frontend/cfg/ShaderCFG.cpp:40`. No `VK_ERROR_DEVICE_LOST`,
MaterializeResources error, clean-shutdown record, or crash dump was produced. M6 gameplay is
not proven. Do not attribute this boundary to any individual upstream port until an offline
comparison establishes that connection.

Next action: use the bounded same-run device-loss report to select one exact missing provenance fact; do not rerun ASTRO or alter the dormant S_SWAPPC path in this checkpoint.

## S_SWAPPC dispatch-side diagnostic capture — 2026-09-13

The diagnostic-only resolver and synthetic descriptor-chain regression are committed in
`3f7a30c`. `shader_cfg_tests` rebuilt and passed (exit 0). The single permitted ASTRO run
used the established `--stub-bvh` command plus `--shader-swappc-diagnostic true`, source
`ed310ed42975cac2ffd1e824b3d4654265bdb280` with the diagnostic working tree, EXE SHA-256
`F97F5DCEA4A3461F8D192A304D615665604055E650B8BACE8CA4F7B191C8951D`, and PDB SHA-256
`D2D761463D595EAFBAF2252A2F4342790AA4256068035D1B396B3134B68D10F0`.

Artifact: `G:/KytyPS5/logs/SWAPPC_DIAGNOSTIC_20260913_/`. The run loaded the warm pipeline
cache and terminated with wrapper status `321` (`0x141`) at CS `0x0000000908e86a00` (hash
`0xa572ee17a880e71c`), `unsupported scalar source operand 0x000000e0` at `pc=0x370` in
`ShaderDecoder.cpp:269`. No `ShaderSwapPcDiagnostic` record for MS
`0x2b3be82b8235ac05` / raw `0xbe8e210e` was emitted because that shader was not reached.
No second runtime run was performed. The requested production target remains unresolved.

## CS scalar-source 0xe0 classification — 2026-09-13

Artifact: `G:/KytyPS5/logs/CS_E0_CLASSIFICATION_20260913_/analysis.txt`. The runtime log reports
address `0x0000000908e86a00` and a successful `a572ee17a880e71c` compile with `code_words=60`,
so `pc=0x370` lies outside that 240-byte stream. The only preserved raw stream containing the
reported offset is the older hash-keyed `fb948a435a4e295e` artifact (1104 bytes): raw
`0x881000e0` at `0x370` decodes by local VOP2 rules as `V_SUB_F32 v8, scalar source 0xe0, v0`.
Its reachable decode ends at `S_ENDPGM` at `0x2dc`; all words through `0x370` are post-END
padding/metadata. This independently demonstrates the trailing-data shape (B) for that old
artifact, but cannot classify the current run because guest-address/hash provenance does not
match. No traversal or decoder change is justified.

## SOP1 S_SWAPPC call-contract audit — 2026-09-13

Artifact: `G:/KytyPS5/logs/SOP1_CALL_AUDIT_20260913_/analysis.txt` (also appended to
`SOP1_LSHR_AUDIT_20260913_/analysis.txt`). The preserved raw MS stream is 4164 dwords,
SHA-256 `A3D856918B88B05D05CFFC079E625D109261920FABBF4C7E5D32E03D7584DF49`.

The bounded window `0x3d30..0x3dac` decodes without continuation/literal ambiguity. The
last writer of both s14 and s15 before `0x3da8` is the two-dword
`S_BUFFER_LOAD_DWORDX2 s14, s4, offset=96` at `0x3d74`; the old pair is therefore loaded
from a descriptor chain rooted at scalar memory `0x0000000f_e0040000`, not a literal.
The exact 64-bit value and allocation are absent from available same-run artifacts.

**PROVEN:** The instruction contract snapshots old `s[14:15]`, writes `PC+4 = 0x3dac` to
the overlapping pair, and jumps to the old target. **STRONG EVIDENCE:** the target is an
external guest code allocation because this MS has no `S_SETPC_B64` return instruction.
The current CFG only accepts local/in-binary `S_SETPC_B64` targets, so the matching
architecture is dispatch-side target resolution plus callee splicing, as in KytyPS5 PR #427
(`a2cafb2f2c4a7fec745caf47e4e230bd0d1b6785`). No source change or runtime run is justified
until the numeric target and return bytes are captured or otherwise proven.

## Latest tick coverage and static device-loss correlation — 2026-09-13

Artifact: `G:/KytyPS5/logs/ASTRO_MS_FIX_20260913_0015/`; static report:
`G:/KytyPS5/logs/ASTRO_MS_FIX_20260913_0015/device-loss-static-analysis.txt`.
The first wait error requested tick `329621` with `known=329597` and `current=329622`, so the
inclusive non-retired interval is `[329598,329621]`. Ticks `329608..329621` are present in the
128-entry MasterSemaphore submit history as EOP writes; their missing `last non-EOP` metadata and
absence of snapshots indicate EOP-only command buffers, not ring truncation. Ticks `329600..329604`
are outside the fixed 16-entry failure print and must not be treated as absent. The snapshot ring
retained three batches with capacity 64.

The recoverable target dispatch is tick `329598`, guest submit `6263`, CS
`0x657ad04626bf9d55`, groups `4096x1x1`, guest code `0x000000050052a400`. All captured owners are
live and descriptor ranges are valid (`violations=0`); image 0 is `VK_FORMAT_R32_UINT`, GENERAL,
read/write/atomic and matches SPIR-V binding 41. The source ordering is
`FindBuffers -> PrepareBda -> Rebind/CommitBindings -> ShaderWriteHazardBarrier -> dispatch ->
ShaderAccessBarrier`, but the artifact lacks a complete predecessor barrier/BDA history.
No stale/freed resource, invalid range, synchronization/layout, or shader semantic violation is
proven. Do not rerun ASTRO or enable address-binding diagnostics until one exact missing fact is
selected.

Next action: use existing source and artifacts to select one concrete missing fact at the earliest
device-loss boundary; only then add a bounded diagnostic or focused regression. Keep color, replay,
resource-remap, and pipeline-cache work closed.

## Selective upstream correctness audit — 2026-09-13

No ASTRO run was performed. Accepted generic ports are `1be44d2` (upstream `e020fab` plus
coherent-read prerequisite `e47eb4b`), `7d4ead9` (`515d644`), `d43c1b7` (`f1de661`),
`75e4726` (`03d3f2a`), `83d9f32` (`7bcd43d`), and `2298df0` (`a21ffde`). They cover checked
GPU-written indirect argument snapshots, owned submitted PM4 bytes, indirect-buffer chain
control, dword-aligned conditional predicates, `S_MUL_HI_I32`, `V_FRACT_F16`, and
`V_CMPX_LT_U16`.

`ded853b` (A1) was not ported: its global EOP/flip synchronization failed the existing
nonblocking packet regression and would change local boundary semantics. `12c855e` (A4) was
not ported: synchronized indirect-register snapshots reject the current host-pointer PM4 test
contract; no production evidence requires adapting that boundary yet. `c354657` (B3) is already
represented by the local `S_WQM_B32` implementation. `c913951` (B4) remains deferred: the
upstream subvector-loop regression failed on the local CFG/mask model, so no blind adaptation was
made without target evidence.

Focused validation logs are under `G:/KytyPS5/logs/UPSTREAM_AUDIT_20260913_/`. Passing suites:
`resource_materialization_tests`, `scalar_provenance_tests`, `shader_recompiler_compute_tests`,
and `shader_cfg_tests`; `resource_tracking_tests` still reaches the known unrelated
`dynamic storage mips` baseline failure.

## Exact MS regression verification — 2026-09-12

Artifact: `G:/KytyPS5/logs/MS_DECODER_EXACT_REGRESSION_20260912_2355/analysis.txt`.
`git merge-base --is-ancestor 2a51379 5ebf00a` succeeded. The current discriminator still
routes SOPP through opcode `0x7f`, while SMEM remains high-six-bit `0x3d`; no `Family::0x30`
case was added. `TestNewShaderRecompilerSoppCdbgSys` from `2a51379` is in the normal
`shader_cfg_tests` main but is synthetic (`EncodeSopp(0x17, ...)`) and does not exercise
production raw `0xc2208080`. The production-derived `TestDecoderStopsAfterCompletedBackedgeBeforeTailData`
was run alone (exit 0), then the restored full `shader_cfg_tests` suite also passed (exit 0).

The old failing runtime record is stage MS, hash `0x2b3be82b8235ac05`, `code_words=4164`,
`pc=0x3e74`, raw `0xc2208080`. Runtime did not persist the preceding dword or explicit mode;
the exact preserved raw file supplies preceding `0x08183d08` at `0x3e70` and one-dword width.
The latest f55195 ASTRO run did not log this MS hash, so exact runtime re-entry remains unproven.

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
Launch: installed `kyty_emulator.exe --game G:/PS5_Games/PPSA21567/extracted --stub-bvh --shader-debug false --shader-log-direction Silent --graphics-debug-dump false --printf-direction File --printf-output-file "G:/KytyPS5/logs/ASTRO_TTMP_FIX_20260912_1510/runtime.log"`
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
Executable: G:/KytyPS5/Fork/repo/_Build/windows/install/kyty_emulator.exe (exact 4374a9d binary above); matching symbols are in the build tree PDB above
Runtime: ASTRO BOT EU, input G:/PS5_Games/PPSA21567/extracted; final runtime.log write was 2026-09-11 18:02:58 Europe/Riga
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

## Offline Prosper-class GDS/indirect audit — 2026-09-13

Artifact: `G:/KytyPS5/logs/ASTRO_MS_FIX_20260913_0015/`; reports:
`gds-indirect-census.txt` and `gds-provenance-search.txt`. No ASTRO run or source/semantic
change was made.

The local DS path is explicit: `MemoryOps.cpp:381-407` decodes DS opcode/offset/GDS bit;
`Memory.cpp:99-101,114-145,850-856` maps the GDS bit to `ResourceKind::Gds`, keeps LDS
separate, and supplies M0 plus EXEC operands. `spirvEmitterMemory.cpp:726-800` computes
`base=(M0>>16)`, `size=M0&0xffff`, `address=base+offset`, `raw_index=address>>2`, then
performs a device-scope atomic append/consume against the GDS runtime array.
`spirvEmitterMemoryHelpers.cpp:108-124,159-164` bounds the index by `OpArrayLength`;
`bufferCache.cpp:27,209` allocates and zeroes a 64 KiB GDS buffer (16384 dwords). There is
no explicit post-add modulo in the emitter, so this source proves a 64 KiB backing allocation,
M0 16-bit fields, and dword indexing, not an additional host-side wrap operation. LDS uses workgroup/function storage.

The retained host-tick census has DispatchDirect at tick 329598 (guest submit 6263,
CS `0x657ad04626bf9d55`, 4096x1x1) and tick 329599 (same submit, CS
`0x338b450551457250`, 131072x1x1); tick 329607 is DrawIndexAuto. Ticks 329600..329621
have no retained non-EOP dispatch metadata, and no DispatchIndirect execution record carries
dereferenced x/y/z. The renderer's capped direct sample repeats bounded per-frame dimensions
(14 calls/frame through frame 38; 8 in frame 39) without monotonic growth.

`AgcAcbDispatchIndirect` records 2280 pointer-only entries with two repeated addresses
(`0x00000005074063c0`, 760 calls; `0x00000005074063e0`, 1520 calls). Both addresses fall
inside the one retained allocation base `0x0000000507404000`, size `0x4000`, but the window
identifies only a vertex subrange and has no indirect binding, x/y/z value, host tick, or
last-writer provenance. Runtime text contains no DS_APPEND/DS_CONSUME, DMA_DATA, fill/copy,
or DispatchIndirect execution records; 128 `gds_offset/gds_size` matches are PM4 metadata dumps.

Result: all recoverable launch dimensions are bounded/sane and the Prosper runaway-indirect/GDS
counter class is ruled out as a proven explanation for this artifact. The unseen indirect values
remain unknown. If another capture is later justified, the single missing fact is the actual
indirect x/y/z immediately before dispatch plus last-writer provenance keyed by host tick and
command ordinal.

## Diagnostic provenance fix — 2026-09-13

Commit `191cc73` (`diagnostics: honor shader terminal boundary`) fixes the source-level provenance defect in the opt-in S_SWAPPC resolver for streams whose `S_ENDPGM` has no pending branch targets. `ResolveSwapPcDiagnostics` now stops after that terminal boundary; production `Decoder::DecodeProgram` can still retain a post-END branch-target path when pending targets exist, as shown by the later MS target capture. It does not alter shader semantics, CFG, or scalar-source decoding.

Artifact: `G:/KytyPS5/logs/PROVENANCE_AUDIT_20260913_/`. The production-derived fixture contains `S_ENDPGM` (`0xbf810000`) and the exact preserved post-END dword `0x881000e0` at `pc=0x370`. Pre-fix `shader_cfg_tests` exited `321` with the same reserved scalar-source error; post-fix exited `0`. `shader_recompiler_compute_tests` exited `0`. Source proof and ownership analysis are in `source-audit.txt`.

The clean Release validation run from `f47c496` is complete; it reached this new failure. Do not rerun ASTRO, add scalar-source support, or reopen S_SWAPPC semantics until invocation provenance is captured.
## Post-fix scalar-source 0xe5 ownership — 2026-09-13

Artifact: `G:/KytyPS5/logs/E5_OWNERSHIP_20260913_/`; runtime: `G:/KytyPS5/logs/PROVENANCE_AUDIT_20260913_/runtime-validation/astro-run/`.

The exact Release binary was `f47c496` (EXE SHA-256 `54C4A4BB2919F0D4491AECF601585F24A52446A4EDCEFE9DD853DA873E9715AB`, matching PDB SHA-256 `778BEB7F6E10C2D288CFDABCB8D590A51FFA72EFA83C59F1C41607B7036F7EB3`). The run passed the old `pc=0x370`/`src=0xe0` point and completed CS `0x530dcd964f29983c` SPIR-V emission (289489 words). It then exited `321 (0x141)` with `unsupported scalar source operand 0xe5 at pc=0x7c`.

PDB symbolization proves ownership by the diagnostic path: `DbgExitHandler → DecodeScalarSource → DecodeSop2 → DecodeInstruction → ResolveSwapPcDiagnostics → ProgramCache::Get<ShaderVertexInputInfo> → GetGraphicsPrograms → DrawAuto`. This is failure class A, before `TranslateProgram`; it is not a normal production decode failure. The stack identifies a graphics vertex-input template (VS-or-Mesh selection), but the exact selected stage, guest address, shader hash, code_words/span, and raw word at `pc=0x7c` were not logged before the resolver and no raw precompile dump was enabled. The last normal PS hash `0xee4a30dd74f51f5f` is a prior completed invocation and must not be attributed to this error.

No second opcode-specific terminal break, scalar-source table change, CFG change, or S_SWAPPC change is justified. The next discriminating action is a generic pre-resolver invocation provenance/raw capture; only after exact identity/span is available can executable reachability and any second boundary class be classified.

## Diagnostic scalar-source 0xe5 same-run provenance — 2026-09-13

Artifact: `G:/KytyPS5/logs/E5_PROVENANCE_RUNTIME_20260913_/`; exact clean Release source
HEAD `53996c064b29d5e7cd3ae9e3422205804484dc61`, EXE SHA-256
`2B2CE85F1108A561CF2389D469E29B5CDF26F121684398D1E8B399698CBAF846`, matching PDB SHA-256
`89EF9AC7AC482251D0C450EF336797760A876DF6255257934AA18AB04EDD6A3E`. One run used the
known `--stub-bvh` command with `--shader-swappc-diagnostic true`; process exited `321`
(`0x141`) at 2026-09-13 09:46:09Z.

Invocation 29 proves the owner: stage `MS`, guest shader `0x00000005007d1700`, hash
`0x4e555b0ebf3b53f8`, `code_words=56`, `code_bytes=224`, caller
`ProgramCache::Get<ShaderVertexInputInfo>`. The exact pre-decode record is pc `0x7c`, raw
`0x99e758e5`, followed by the reserved-source fatal. The raw word is SOP2 opcode `0x33`
(`S_PACK_LH_B32_B16`), `sdst=103`, `ssrc0=0xe5`, `ssrc1=0x58`.

Offline control-flow classification is B for this diagnostic span. Both sides of
`S_CBRANCH_EXECZ` at pc `0x8` converge on `S_WAITCNT` at `0x38`; `S_SETPC_B64 s6` at
`0x3c` is the indirect fused-front boundary. The runtime AGC records the corresponding
front/back pair near this code, and `DecodeFusedProgram` stops the front at S_SETPC before
splicing the back shader. The diagnostic resolver currently scans linearly past that boundary
and reaches embedded tail data at `0x40..0x7c`; `0xe5` is therefore not proven reachable
production code. No decoder/scalar/CFG semantic change was made. A separate task must add the
same fused-boundary contract to the opt-in diagnostic walk with an authentic regression.

## Fused-front diagnostic fix and validation — 2026-09-13

Semantic/diagnostic commit: `0ca59901d51d2a083cee8ba3732651e7fa8258f3`. The shared
`FusedFrontWordCount` contract is used by both production fused decoding and the opt-in
S_SWAPPC scanner; `shader_cfg_tests` and `shader_recompiler_compute_tests` pass. The
production-derived invocation-29 fixture fails before the fix at `pc=0x7c`/raw
`0x99e758e5` (reserved source `0xe5`, exit `0x141`) and passes after without decoding the
tail; an in-front S_SWAPPC remains discoverable.

The exact Release run artifact is
`G:/KytyPS5/logs/E5_FUSED_FRONT_FIX_20260913_/astro-run/`. Source HEAD is `0ca5990`;
installed EXE/PDB SHA-256 values are recorded in `release-provenance.txt`. The run reached
MS invocation 104, hash `0x2b3be82b8235ac05`, and then stopped at the existing production CFG
failure `SOP1 opcode=0x21`, raw `0xbe8e210e`, `pc=0x3da8` (exit `321`/`0x141`). The corrected
diagnostic emitted no e0/e5 traversal failure, but no S_SWAPPC record was produced: its
linear non-fused walk stops at `S_ENDPGM` `pc=0x3d30`, while production decoding retains a
pending branch-target path through `0x3dac` to the later call site. No second runtime run is
authorized by this checkpoint; next action is offline classification of that normal-shader
diagnostic boundary before any further capture.

## SPIR-V cross-process determinism — 2026-09-13

The authentic production capsule for giant CS `0x78af8e269b528b5c` proves the dispatcher
fallback path (`blocks=198`, `wave_size=64`, `host_subgroup_size=32`, therefore
`lane_count=2`). The fail-before replay emitted three different SPIR-V SHA-256 values
(`8266A755...`, `C71B6394...`, `0E3CA4B5...`) while the same 1040 spill `OpVariable` IDs
were merely permuted (spill count `520`). The first divergence was the variable-order region,
not shader inputs or semantics.

Commit `f56a8fb` records deterministic low-half spill discovery in `spill_order` and allocates
the high-half spill IDs in that order; unordered maps remain lookup-only. Three independent
giant replays now match SHA-256 `386480C1F768721CF41D7888DB93F5921CD224E9562BBA3B2A8F5A7DFD63B3D6`,
and three runs of the small production fixture match `F5EDA4E9B9EE5F65A6442384E89225D74F3410A08143C2BD7AC1ED65A92A83CF`.
`spirv-val --target-env vulkan1.3` passes and byte comparison of giant runs A/B is clean.

The post-fix cache-only probe loaded the existing `79,150,623`-byte cache and returned
`COMPILE_REQUIRED` for the giant shader with stable SPIR-V hash `0x271420d97ec99650`,
specialization `0xdf4c4b5ea0432871`, layout `0xf5863b4aec51aeea`, and fingerprint
`0xb259b45ff04fb40b`. It stopped before normal compilation; the old cache was created from
the unstable identity, so warm reuse is not yet proven. Artifacts:
`G:/KytyPS5/logs/SPIRV_DETERMINISM_20260913_/`.

M6 gameplay remains unproven. Do not add pipeline binaries or rerun ASTRO until the next
runtime/cache decision is made from this stable identity.

## Persistent Vulkan cache reuse — 2026-09-13

The old per-title cache was preserved at `G:/KytyPS5/logs/PIPELINE_CACHE_REUSE_20260913_/historical-old-cache/`
(SHA-256 `4F75FC651723824AD2BDFAE727A6EE6DE275124A8530B0E2A9C78C2FBC3147CC`). A fresh isolated
`_PipelineCache/PPSA21567.bin` namespace was populated by the exact `f56a8fb` Release binary.
The four giant compute pipelines compiled successfully once (`85482`, `114048`, `131535`, and
`152900 ms`) and the cache persisted at `8726040` bytes.

A second process using that same cache and deterministic state returned `HIT` for all four giant
hashes (`0x78af8e269b528b5c`, `0x7bd68261b1bdfb68`, `0xf3f4e1671b30c1f4`, and
`0x530dcd964f29983c`) in 8 seconds wall, before normal pipeline creation. This proves ordinary
persistent `VkPipelineCache` reuse after the SPIR-V ordering fix; no `VK_KHR_pipeline_binary`
prototype is justified. The measured lower-bound saving versus the `483.965 s` cold giant compile
total is `475.965 s` (98.35%).

Artifacts: `G:/KytyPS5/logs/PIPELINE_CACHE_REUSE_20260913_/cache-reuse-report.txt` and the
cold/warm run directories. M6 gameplay remains unproven; next action is runtime progression with
this warmed deterministic cache.
