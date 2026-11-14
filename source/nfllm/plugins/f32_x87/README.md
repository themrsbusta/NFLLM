# F32 x87 static plugin

`f32_x87.lib` packages the existing scalar F32 compatibility kernels for
matvec, RMSNorm, and RoPE as an Open Watcom OMF static library. The host remains
responsible for initializing the registry and explicitly registering the
module through `nfllm_register_reference_plugin()`.

## Build and use

Run `build-dos.cmd` with the configured Open Watcom toolchain. It compiles
`src/nfllm_reference_kernels.c`, archives that object as
`build/plugins/f32_x87.lib`, and links the archive into both `NFLLM.EXE` and
`NFTEST.EXE`. The link map files are emitted beside the executables. The
manifest is copied to `build/plugins/f32_x87-manifest.yaml`.

For explicit selection, run:

```text
NFLLM.EXE model.gguf -p "Once upon a time" -n 1 --backend reference
```

`--backend auto` uses the deterministic registry order. Manual selection must
fail with an error if the requested plugin or its declared capabilities are
unavailable; it must not silently fall back to another plugin.

## Compatibility

This module preserves NFLLM 0.1's scalar F32 kernel operation order. It is
compiled for a 386 baseline and uses x87 floating-point instructions. The host
must explicitly declare both `NFLLM_CAP_386` and `NFLLM_CAP_X87`. The host
selects this platform profile manually and does not enumerate CPU features at
runtime. The compatibility probe did not observe EFLAGS.ID changing under
DOS/4GW and therefore did not execute CPUID; this is inconclusive about CPUID
support in the Am486DX4 guest. AMD documents CPUID on Enhanced Am486DX4
processors. This library does not execute CPUID, MMX, or 3DNow! instructions.

The manifest records ABI v1 and OMF static linking. It is descriptive metadata;
the executable registers compiled descriptors and does not load this YAML at
runtime. This is a packaging extraction of the existing reference backend,
not an optimization. No speedup is expected or claimed. Any future performance
claim requires same-condition measurements; this first plugin's acceptance is
based on link, ABI, selection, equivalence, and inference behavior.

## Validation status

The host self-test checks plugin registration, manual and automatic selection,
ABI rejection, capability rejection, and bitwise equivalence against the
NFLLM 0.1 formulas. Stage 2 acceptance was verified on a clean NF-DOS guest run
in 86Box with one-token inference through this linked archive.
