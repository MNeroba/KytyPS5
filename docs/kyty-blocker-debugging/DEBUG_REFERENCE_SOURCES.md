# KytyPS5 external evidence router

Use this file only when current project evidence leaves an exact semantic question unresolved. It is a router, not a mandatory reading list.

## Normal evidence order

```text
CURRENT_STATE.md
→ PROJECT_MEMORY.md
→ relevant REFERENCE.md section
→ current Kyty source, tests, saved logs, and generated artifacts
→ smallest external source set needed for the remaining semantic question
```

Do not broadly research every source. State one question, choose the primary authority most likely to answer it, add at most one independent implementation when useful, and stop when the question is resolved. Record sources only when the fix materially depends on them.

Runtime evidence establishes what Kyty did. Specifications establish required semantics. Another emulator or driver corroborates an interpretation but does not override the specification or target evidence.

## Route by unresolved question

| Question | Start with | Add only if needed |
|---|---|---|
| RDNA2 opcode, encoding, lane, EXEC/VCC/WQM, SMEM/DS/MIMG semantics | [AMD RDNA2 ISA](https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture) | [GPUOpen PAL](https://github.com/GPUOpen-Drivers/pal) for GFX10 implementation context |
| SPIR-V structure, dominance, typing, capability, or memory semantics | Local `spirv-val` / `spirv-dis`, then [SPIR-V Specification](https://github.com/KhronosGroup/SPIRV-Docs) | [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools), [SPIR-V Registry](https://github.com/KhronosGroup/SPIRV-Registry) |
| Exact Vulkan VUID, layouts, barriers, descriptor/pipeline validity | [Vulkan Specification](https://docs.vulkan.org/spec/latest/index.html) using the exact VUID | [Vulkan Validation Layers](https://github.com/KhronosGroup/Vulkan-ValidationLayers) for implementation of the check |
| AGC, PM4, GFX10.3 registers, DCC/HTILE, shader register setup | Current Kyty code, then [GPUOpen PAL](https://github.com/GPUOpen-Drivers/pal) | [Prosperity](https://github.com/Force67/prosperity) for independent PS5-oriented corroboration |
| PS5 HLE/API name or unknown NID | [sce_symbols](https://github.com/zecoxao/sce_symbols) for identity | [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), [ps5rs](https://github.com/claimore22/ps5rs), or [PS5SDK](https://github.com/PS5Dev/PS5SDK) for ABI hints |
| Old export/library membership | Current symbol sources | [PS5 3.20 libraries](https://github.com/DNNDHH/PS5-3.20_Libs) as historical evidence only |
| PS5 filesystem, module, metadata, device, or community-known structure | [PS5 DevWiki](https://www.psdevwiki.com/ps5/) | Confirm critical behavior with target evidence or another source |
| Sony-derived kernel call, mapping, mutex, socket, errno semantics | Current Kyty implementation, then [FreeBSD manuals](https://man.freebsd.org/) | PS5 SDK/community evidence for Sony differences |
| Windows host memory, SEH, scheduling, file mapping, or API behavior | [Microsoft Win32 documentation](https://learn.microsoft.com/windows/win32/) | Reproducible host trace |
| Guest AMD64 instruction semantics | [AMD64 manuals](https://docs.amd.com/) | Focused Kyty regression |
| GPU stalls or RTX 3090 performance | Target capture and [NVIDIA Nsight Graphics](https://docs.nvidia.com/nsight-graphics/) | Vulkan synchronization rules; PAL for AMD-style command intent |
| Sony/Orbis-derived library behavior | Current Kyty implementation | [shadPS4](https://github.com/shadps4-emu/shadPS4) as PS4 corroboration only |
| Independent PS5 emulator comparison | [Prosperity](https://github.com/Force67/prosperity) | Always reconcile with primary semantics and current Kyty architecture |

## Source limits

### AMD RDNA2 ISA

Primary authority for guest GPU instruction behavior. Search the exact mnemonic/opcode and the fields in dispute. It does not define PS5 resource policy or Kyty pass architecture.

### GPUOpen PAL

Useful for PM4, registers, formats, tiling, barriers, DCC/HTILE, and GFX10 conventions. PAL is a driver implementation, not a PS5 specification.

### Vulkan and SPIR-V

For validation failures, begin with the exact VUID or first validator error and the saved artifact. Do not browse general tutorials. Match the target environment and extensions used by Kyty.

### Prosperity and shadPS4

Use for independent implementation evidence after the semantic question is narrow. Prosperity may contain bugs; shadPS4 targets PS4. Copy neither blindly.

### Symbol and SDK sources

A resolved NID/name does not prove signature, argument types, layouts, ownership, or runtime behavior. Older firmware exports establish historical API surface only.

### FreeBSD and Windows

FreeBSD is a baseline for Sony-derived kernel behavior, not final PS5 authority. Windows documentation controls host API behavior only; keep guest and host semantics separate.

## Minimal evidence note

When external evidence changes a decision, record:

```text
QUESTION: one exact semantic unknown
PRIMARY SOURCE: document/revision/section or symbol
ANSWER: concise required invariant
CORROBORATION: optional independent source
KYTY CONSEQUENCE: exact decision or guard affected
LIMIT: what remains unproven
```

Do not add “all sources checked” boilerplate. If current source, tests, or runtime artifacts already answer the question, record no external-source task.
