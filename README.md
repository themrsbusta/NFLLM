# NFLLM

**1.0 Experimental Research Release** is a small, single-threaded inference
prototype for 32-bit DOS. It reads a narrow subset of GGUF v3 models using the
`llama` architecture. It is **not a port of the full upstream `llama.cpp`**.

The project now includes a static C ABI and registry, an x87 reference backend,
Q8_0/Q4_0/Q2_0/Q2_K/IQ3_XXS/IQ3_S row decoders, and experimental 3DNow!, MMX,
and SSE2 kernels. The quantized formats are decoded to F32 in the DOS inference
path except for the documented direct Q8_0/Q4_0/Q2/IQ3 matvec experiments.
ISA-specific paths require an explicit profile; NFLLM does not select them by
probing CPUID.

The source tree is under [`source/nfllm/`](source/nfllm/). The separate
[`source/upstream-llama.cpp/`](source/upstream-llama.cpp/) directory is an
unmodified reference snapshot, not code ported to DOS. The original upstream
revision is recorded in [`PROJECT.yaml`](PROJECT.yaml).

## Verified configurations

The supplied evidence records these emulator configurations:

- 86Box 6.0, AMD Am486DX4 at 100 MHz, 96 MiB RAM, NF-DOS 5.01.3314 and
  DOS/4GW 1.97 for the baseline 386+x87 path.
- QEMU 11.1 ISA proxies for 3DNow!, MMX, SSE2 and generic 486 experiments.
  These are proxy results, not physical Geode or Atom validation.
- The SSE2 experiment used an isolated Atom N270 guest with `CR4.OSFXSR`
  enabled. General DOS/DPMI XMM-state safety has not been established.

F32, Q8_0, Q4_0, Q2_0, Q2_K, IQ3_XXS and IQ3_S inference results are summarized
in [`docs/ETAPA1-RELATORIO-FINAL.md`](docs/ETAPA1-RELATORIO-FINAL.md),
[`docs/ETAPA2-RELATORIO-FINAL.md`](docs/ETAPA2-RELATORIO-FINAL.md),
[`docs/ETAPA3-RELATORIO-FINAL.md`](docs/ETAPA3-RELATORIO-FINAL.md), and
[`docs/PLUGIN-COMPATIBILITY.md`](docs/PLUGIN-COMPATIBILITY.md). The DOS/4GW
EFLAGS.ID probe was inconclusive; it did not execute CPUID and does not show
that the processor lacks CPUID. Physical Geode/Atom hardware and standalone
MS-DOS have not been validated.

## Models and running

Model weights are **not included** in the repository or release ZIPs. Obtain a
compatible GGUF model separately, review its license, and follow
[`models/README.md`](models/README.md) for the tested TinyStories/Tiny1M
examples. NFLLM does not distribute model weights.

The DOS executable requires DOS/4GW or a compatible extender. That runtime is
not included because its redistribution terms have not been confirmed. Example
command inside a configured DOS environment:

```dos
NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1
```

## Building from source

Builds require Windows and Open Watcom with `wcl386` and `wlib` available. Set
`WATCOM` to the toolchain root, then run the source build script:

```bat
set WATCOM=C:\WATCOM
cmd /d /c source\nfllm\build-dos.cmd
```

The script writes generated binaries and libraries to `source/nfllm/build/`;
these outputs are not committed in the source tree. Verification helpers and
their environment requirements are documented in
[`source/nfllm/scripts/README.md`](source/nfllm/scripts/README.md).

## Downloads

- [1.0 Experimental Research Release](https://github.com/themrsbusta/NFLLM/releases/tag/v1.0)
- The release page provides separate DOS build and source ZIP assets. Neither
  archive contains model weights or DOS/4GW.

## License and provenance

NFLLM project source is licensed under MIT; see [`LICENSE`](LICENSE). The
upstream reference tree retains its own license and third-party notices. A
full source-reuse audit is still pending, so this project does not claim to be
a fork, a complete upstream port, or wholly independent of `llama.cpp`. See
[`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
