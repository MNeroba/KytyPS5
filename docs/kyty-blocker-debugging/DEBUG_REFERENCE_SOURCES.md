# KytyPS5 — Debug / Reverse Engineering Reference Sources

**Purpose:** permanent reference list for debugging, reverse engineering, compatibility work, shader work, HLE work, renderer investigation, synchronization analysis and performance profiling.

**Project instruction:**  
Please **keep this file in the project repository** (recommended path: `docs/DEBUG_REFERENCE_SOURCES.md`) and treat the sources below as the default external reference set.

When a new runtime error, crash, shader failure, unsupported opcode, HLE/NID issue, PM4/AGC problem, Vulkan validation error, memory issue or performance regression is encountered, **consult the relevant sources below before implementing a fix**.

Do not use a title-specific workaround when a generic, evidence-based implementation can be derived from specifications, reference implementations, existing emulator work or documented platform behaviour.

---

# 1. Required debugging workflow

For every new blocker:

```text
1. Capture exact error text
2. Capture file:line / function
3. Reproduce on current clean main
4. Search KytyPS5 source locally
5. Search KytyPS5 Issues / PRs / commits
6. Classify subsystem
7. Consult the relevant external references from this document
8. Compare with independent implementations when available
9. Implement the smallest generic fix
10. Add regression test when practical
11. Re-run affected title
12. Run ctest
13. Run a secondary regression title when practical
14. Record evidence and before/after result
```

Useful local commands:

```powershell
rg -n "exact error text" G:\KytyPS5\repo

git -C G:\KytyPS5\repo log -S "exact error text" --all --oneline

git -C G:\KytyPS5\repo blame path\to\file.cpp
```

---

# 2. KytyPS5 — primary project source

## Repository

https://github.com/KytyPS5/KytyPS5

Always search here first.

Look in:

```text
source code
tests
open PRs
closed PRs
issues
commits
branches/forks when relevant
```

Search keys:

```text
exact error message
function name
shader opcode
NID
guest address
shader hash
Vulkan VUID
PM4 packet name
register name
title ID
```

Examples:

```text
"unsupported sampled depth target"
"GetBufferResource dword 0 is not a valid runtime value"
"IMAGE_BVH_INTERSECT_RAY"
"VK_ERROR_DEVICE_LOST"
"S_WQM_B32"
"vcc_hi"
```

**Use for:** every issue.

---

# 3. AMD RDNA 2 ISA Reference Guide

## Official AMD documentation

https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture

Document family:

```text
AMD RDNA 2 Instruction Set Architecture
Document 70648
```

Use as the **primary semantic authority** for guest shader instruction behaviour.

Use for:

```text
VOP*
SOP*
MIMG*
SMEM
DS
DPP8 / DPP16
SAVEEXEC
EXEC
VCC
WQM
wave32 / wave64
ballot
shuffle
DS_BPERMUTE
IMAGE_ATOMIC_*
IMAGE_BVH_INTERSECT_RAY
instruction encoding
lane semantics
```

Rule:

```text
If the bug is "what should this RDNA2 instruction actually do?",
check AMD ISA before changing Kyty.
```

---

# 4. AMD GPUOpen PAL

## Repository

https://github.com/GPUOpen-Drivers/pal

PAL is extremely useful for understanding AMD GPU programming behaviour around GFX10 / RDNA-class hardware.

Use for:

```text
PM4 packets
GFX10 / GFX10.3 registers
CB_COLOR*
DB_DEPTH*
SPI_SHADER*
DCC
HTILE
tiling
render targets
depth targets
cache flushes
barriers
shader register setup
command submission
format handling
resource transitions
```

Particularly valuable when Kyty reports:

```text
unknown register
unsupported register state
DCC error
HTILE error
PM4 packet error
depth/color target mismatch
```

Important:

```text
PAL is a reference implementation, not the PS5 specification.
Use it together with runtime evidence and PS5-specific behaviour.
```

---

# 5. Vulkan Specification

## Official specification

https://docs.vulkan.org/spec/latest/index.html

Use for host-side renderer correctness.

Typical issues:

```text
VUID-*
VkImage
VkImageView
VkBuffer
VkDescriptorSet
VkPipeline
VkRenderingInfo
layout transitions
barriers
synchronization
storage image
subgroup
format capabilities
memory access
atomics
robustness
```

Rule:

```text
If Vulkan validation emits a VUID, search the exact VUID first.
```

---

# 6. Vulkan Validation Layers

## Repository

https://github.com/KhronosGroup/Vulkan-ValidationLayers

Use when:

```text
the VUID text is unclear
the validation condition needs to be understood
the relevant Vulkan requirement is difficult to locate
```

Search:

```text
exact VUID
validation error text
function name
```

---

# 7. SPIR-V Specification

## Repository

https://github.com/KhronosGroup/SPIRV-Docs

Use for SPIR-V emitter correctness.

Relevant areas:

```text
OpPhi
OpImage*
OpAtomic*
OpGroupNonUniform*
OpControlBarrier
OpImageTexelPointer
structured control flow
dominance
subgroup scope
memory semantics
type rules
capabilities
```

---

# 8. SPIRV-Tools

## Repository

https://github.com/KhronosGroup/SPIRV-Tools

Important tools:

```text
spirv-val
spirv-dis
spirv-as
spirv-opt
```

Recommended shader-debug path:

```text
guest RDNA2 ISA
    ↓
Kyty decoded IR
    ↓
generated SPIR-V
    ↓
spirv-val
    ↓
spirv-dis
```

Rule:

```text
If generated SPIR-V fails spirv-val, fix that before investigating rendering correctness.
```

---

# 9. SPIR-V Registry

## Repository

https://github.com/KhronosGroup/SPIRV-Registry

Use for:

```text
SPIR-V extensions
vendor extensions
capabilities
memory models
subgroup extensions
new instructions
```

---

# 10. Prosperity — independent PS4/PS5 emulator

## Repository

https://github.com/Force67/prosperity

Use as an **independent implementation reference**.

Particularly useful for:

```text
PS5 HLE
AGC
shader behaviour
resource tracking
descriptor handling
command processing
runtime call observations
PS5 title-specific traces
```

Comparison workflow:

```text
Kyty implementation
        ↕
Prosperity implementation
        ↕
AMD / Vulkan / platform documentation
```

Important:

```text
Prosperity is not an authority.
It may also contain bugs.
Use it as independent evidence, not as specification.
```

---

# 11. PS5 DevWiki

## Site

https://www.psdevwiki.com/ps5/

Useful areas:

```text
file structures
system software
sysmodules
title metadata
devices
memory notes
filesystem
error codes
PUP structure
game layout
param metadata
```

Use for:

```text
PS5-specific naming
filesystem paths
module names
known structures
community reverse-engineering notes
```

Important:

```text
Community documentation.
Critical behaviour should be confirmed elsewhere when possible.
```

---

# 12. sce_symbols — SCE name / NID database

## Repository

https://github.com/zecoxao/sce_symbols

Use primarily for:

```text
NID -> function name
SCE symbol lookup
module/function identification
```

Example:

```text
vuSXe69VILM
    ↓
sceAgcDcbGetLodStats
```

Use when logs contain unknown values such as:

```text
vuSXe69VILM
F0Y42t-3e18
1q1titRBL6o
```

Important limitation:

```text
A resolved name does NOT prove:
- exact function signature
- argument types
- structure layout
- runtime semantics
```

It is primarily a symbol resolver.

---

# 13. PS5 Payload SDK

## Repository

https://github.com/ps5-payload-dev/sdk

Useful for:

```text
sce_stubs
module names
exports
NIDs
headers
dynamic linking
stub generation
PS5 module surface
```

Use for errors such as:

```text
unresolved libSce*
unknown module
unknown NID
missing export
dynamic-linking mismatch
```

---

# 14. PS5 3.20 library/export material

## Repository

https://github.com/DNNDHH/PS5-3.20_Libs

Useful primarily as historical/API-surface evidence.

Use for:

```text
module exports
NIDs
function names
library membership
older API surface
```

Important:

```text
Do NOT treat this repository as definitive modern PS5 runtime behaviour.
Firmware 3.20 is old and much of the useful material is export/stub-oriented.
```

---

# 15. ps5rs

## Repository

https://github.com/claimore22/ps5rs

Useful for automation around:

```text
PS5 ELF / PRX parsing
imports
exports
NIDs
symbol resolution
offline export databases
module analysis
```

Potential workflow:

```text
game eboot / PRX
    ↓
ps5rs
    ↓
imports + NIDs
    ↓
sce_symbols
    ↓
Kyty registration / implementation
```

Use when doing HLE/API audits.

---

# 16. PS5SDK

## Repository

https://github.com/PS5Dev/PS5SDK

Useful for:

```text
headers
libkernel declarations
basic ABI hints
constants
structures
dlsym-related declarations
Sony-derived API names
```

Important:

```text
Treat as WIP/reference material.
Do not assume every declaration is exact for current firmware.
```

---

# 17. FreeBSD documentation

## Manual pages

https://man.freebsd.org/

Use as baseline semantics for Sony-derived kernel/HLE behaviour.

Especially:

```text
mmap
munmap
mprotect
_umtx_op
pthread-related behaviour
filesystem calls
socket API
errno
synchronization
```

Useful when debugging:

```text
MAP_FIXED
MAP_EXCL
memory replacement
thread synchronization
mutex/futex-like behaviour
mapping semantics
```

Important:

```text
PS5 is not stock FreeBSD.
Use FreeBSD as baseline semantics, not final authority.
```

---

# 18. AMD64 Architecture Programmer's Manuals

## AMD documentation

https://docs.amd.com/

Use for guest CPU/x86 instruction semantics.

Relevant examples:

```text
SSE4a
EXTRQ
INSERTQ
AVX
exceptions
instruction encoding
register semantics
```

Use when Kyty reports:

```text
unsupported CPU instruction
illegal instruction
guest opcode failure
CPU semantic mismatch
```

---

# 19. Microsoft Learn / Win32 documentation

## Site

https://learn.microsoft.com/windows/win32/

Use for Windows host-side issues:

```text
VirtualQuery
VirtualAlloc
VirtualProtect
VirtualFree
file mapping
SEH
0xC0000005
thread scheduling
WaitFor*
ETW
memory protection
```

Typical bugs:

```text
access violation
mapping failure
host memory race
VirtualQuery overhead
thread scheduling
P/E-core migration
```

---

# 20. NVIDIA Nsight Graphics documentation

## Documentation

https://docs.nvidia.com/nsight-graphics/

Use for RTX 3090 performance analysis:

```text
GPU Trace
subchannel switches
barriers
draw/dispatch transitions
GPU idle
occupancy
register pressure
pipeline stalls
```

Especially important on Ampere for:

```text
Draw
→ Dispatch
→ Draw
```

because Graphics/Compute subchannel switches may introduce expensive implicit synchronization.

---

# 21. shadPS4

## Repository

https://github.com/shadps4-emu/shadPS4

Useful as a Sony/Orbis-derived implementation reference for:

```text
memory model concepts
libkernel
AVPlayer
audio/video
filesystem behaviour
some libSce APIs
```

Important:

```text
PS4 != PS5
```

Do not use shadPS4 as authority for PS5 AGC/GPU semantics.

---

# 22. Local runtime evidence is a first-class source

Always collect runtime evidence before making assumptions.

Record where applicable:

```text
exact error
full log
Kyty commit SHA
game title ID
game version
shader hash/address
guest PC
NID
PM4 packet
register values
generated SPIR-V
Vulkan validation output
Nsight capture
ETW capture
before/after behaviour
```

Evidence hierarchy should prefer:

```text
reproducible runtime observation
+
authoritative specification
+
independent implementation/reference
+
regression test
```

---

# 23. Source selection by error type

## Shader opcode / semantics

Use:

```text
1. Kyty source/tests
2. AMD RDNA2 ISA
3. AMD PAL
4. Prosperity
5. SPIR-V spec/tools
```

---

## SPIR-V generation error

Use:

```text
1. spirv-val
2. spirv-dis
3. SPIR-V specification
4. SPIRV-Tools
5. Kyty emitter/tests
```

---

## Vulkan validation / renderer error

Use:

```text
1. exact VUID
2. Vulkan specification
3. ValidationLayers
4. Kyty renderer code
5. AMD PAL
```

---

## AGC / PM4 / register error

Use:

```text
1. Kyty source
2. Prosperity
3. AMD PAL
4. RDNA2 ISA where applicable
5. PS5 DevWiki / PS5 API material
```

---

## NID / libSce / HLE error

Use:

```text
1. sce_symbols
2. ps5-payload-dev/sdk
3. ps5rs
4. PS5-3.20 exports
5. PS5SDK
6. Prosperity
7. Kyty implementation
```

---

## Kernel / memory / synchronization error

Use:

```text
1. Kyty implementation
2. FreeBSD docs
3. Microsoft Win32 docs for host behaviour
4. PS5SDK
5. PS5 DevWiki
6. independent emulator behaviour
```

---

## Guest CPU instruction error

Use:

```text
1. AMD64 manuals
2. Kyty CPU implementation
3. focused regression test
```

---

## RTX 3090 performance issue

Use:

```text
1. Nsight Graphics
2. WPR/WPA
3. NVIDIA documentation
4. Vulkan synchronization rules
5. Kyty profiling/instrumentation
```

---

# 24. Evidence / trust ranking

Recommended ranking:

| Level | Source |
|---|---|
| **A** | AMD RDNA2 ISA, AMD64 manuals, Vulkan spec, SPIR-V spec, Microsoft docs |
| **A-/B+** | AMD PAL |
| **B** | reproducible Kyty runtime evidence, Kyty regression tests, independent Prosperity result |
| **B-/C+** | FreeBSD semantics for Sony-derived APIs |
| **C+** | sce_symbols, ps5-payload-sdk, ps5rs |
| **C** | PS5 DevWiki, PS5SDK |
| **C-/historical** | PS5 3.20 export/stub material |

---

# 25. Preferred standard for a high-quality fix

A strong generic fix should ideally have:

```text
1. Reproducible title/runtime failure
2. Exact failing Kyty code path
3. Authoritative or high-quality semantic reference
4. Independent supporting evidence where available
5. Minimal generic implementation
6. Regression test
7. ASTRO BOT / affected-title before-after result
8. Secondary regression title or test
```

Ideal evidence chain:

```text
Game reproduces bug
        +
AMD / Vulkan / FreeBSD / Microsoft spec explains expected semantics
        +
Kyty implementation differs
        +
PAL / Prosperity / PS5 API evidence supports interpretation
        +
regression test fails before
        +
regression test passes after
        +
game proceeds further
```

This is preferred over:

```text
"title crashes, return 0 here and it boots"
```

---

# 26. Project requirement

**Please keep this document committed in the project and use it during debugging.**

Recommended repository location:

```text
docs/DEBUG_REFERENCE_SOURCES.md
```

When a new issue is opened in local notes or a development branch, add a small section:

```markdown
## References checked

- [ ] Kyty source / Issues / PRs
- [ ] AMD RDNA2 ISA
- [ ] AMD PAL
- [ ] Vulkan / SPIR-V documentation
- [ ] Prosperity
- [ ] PS5 symbol / NID sources
- [ ] FreeBSD / Microsoft docs where relevant
- [ ] Other:
```

Not every source needs to be consulted for every problem.

The requirement is:

```text
Identify the subsystem first,
then consult the relevant authoritative/reference sources
before implementing a speculative fix.
```

---

# 27. Rule for Codex / AI-assisted debugging

If Codex or another AI coding assistant is used in this repository:

```text
1. Read this file before investigating a new emulator blocker.
2. Search the relevant sources listed here as needed.
3. Do not invent undocumented semantics.
4. Prefer specifications and runtime evidence.
5. Compare independent implementations when useful.
6. Clearly mark assumptions.
7. Add tests whenever practical.
8. Do not claim a fix is correct only because one game proceeds further.
9. Preserve links/evidence in notes or PR preparation.
10. The human contributor must review and understand the resulting change.
```

Suggested instruction to include in project-level AI guidance:

```text
For KytyPS5 debugging and reverse-engineering tasks, consult
docs/DEBUG_REFERENCE_SOURCES.md.

Use the reference sources appropriate to the failing subsystem before
implementing speculative emulator behaviour. Prefer authoritative
specifications, current Kyty source/history, reproducible runtime evidence,
and independent implementations. Record which references were used when
the reasoning materially depends on them.
```

---

# 28. Summary

Default debug path:

```text
ERROR
  ↓
Kyty source/history
  ↓
classify subsystem
  ↓
relevant authoritative spec
  ↓
PAL / Prosperity / PS5 reference material
  ↓
minimal generic fix
  ↓
regression test
  ↓
game A/B
  ↓
secondary regression
```

**This reference list should remain part of the project and be consulted whenever the current evidence is insufficient to explain a newly discovered error.**
