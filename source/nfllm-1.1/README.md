# NFLLM 1.1 — NF-DOS / Am486DX4 inference prototype

This separate 1.1 tree adds DOS/32A-bound DOS executables to the NFLLM 1.0
inference prototype. The inference and compatibility results below describe the
1.0 baseline unless a result is explicitly labeled 1.1.

## 1.1 extender packaging

Open Watcom still emits a DOS Linear Executable using its `-l=dos4g` target
library. The build then runs the DOS/32A SUNSYS Bind Utility (`SB /B`) to bind
the DOS/32A extender into each final executable. The generated `NFLLM.EXE`,
`NFTEST.EXE`, compatibility probe and ISA probes therefore do not need an
external `DOS4GW.EXE`, `DOS32A.EXE` or `SB.EXE` beside them at runtime. The
Watcom linker target is an LE compatibility format; the final runtime is
DOS/32A embedded by the vendor binder.

`-l=dos4g` is a build-time LE compatibility target only. NFLLM 1.1 does not
load or require `DOS4GW.EXE` at runtime.

This uses DOS/32A as its intended executable extender, rather than merging its
low-level assembly runtime into `NFLLM.cpp`. The vendored source, original
license, build tools and provenance are under `third_party/dos32a/`. The
required acknowledgment and binary redistribution notice are in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

This product uses DOS/32 Advanced DOS Extender technology.

The original `NFLLM/` tree remains the 1.0 baseline and is not modified by this
branch. Prior 86Box inference logs and performance tables below are baseline
evidence made with the external DOS/4GW runtime; they do not by themselves
prove the 1.1 binder path.

### 1.1 smoke verification

The Watcom build and DOS/32A binder passed. In an NF-DOS 6 V23 guest on QEMU's
generic 486 CPU with 96 MiB RAM, `NFTEST.EXE` passed and `NFLLM.EXE` generated
token 419 (`s`) from the 1.18 MB TinyStories fixture. The test volume contained
no external extender tools, and the read-only V23 boot image hash was unchanged.
This is a generic QEMU 486 verification; the exact 86Box Am486DX4/100 MHz
profile has not yet been rerun with 1.1. See
[`docs/DOS32A-INTEGRATION.md`](docs/DOS32A-INTEGRATION.md) for build and log details.

This is an NFLLM implementation and a compatibility proof for a narrow model
subset. It is **not yet a port of the full upstream `llama.cpp`**. The upstream
checkout is reference-only and must remain unmodified; its own `AGENTS.md`
governs that directory. Current evidence covers GGUF v3, `llama` architecture,
F32 and the six listed quantized formats on TinyStories/Tiny1M fixtures. It
does not establish support for other architectures, other GGML formats, or the
full upstream runtime.

## NFLLM 1.0 baseline guest configuration

- 86Box 6.0, Windows build 9001
- AMD Am486DX4 at 100 MHz (`cpu_family=am486dx4`, `cpu_speed=100000000`)
- 96 MiB configured RAM (`mem_size=98304`; BIOS memory test reported 98304K)
- NF-DOS 5 build 5.01.3314, DOS 7.10, DOS/4GW runtime 1.97
- Open Watcom DOS/4G build with a 386 ISA baseline and x87; inference is scalar
  and single-threaded

The probe tried the standard EFLAGS.ID writability check under DOS/4GW. The bit
did not change in that context, so the probe did not execute CPUID and did not
establish whether CPUID is exposed to this guest. AMD documents CPUID support
for Enhanced Am486DX4 processors, so the earlier claim that this CPU lacks
CPUID is withdrawn. NFLLM itself uses an explicit 386+x87 profile and does not
enumerate the CPU at runtime. The x87 arithmetic probe returned 5.25. DPMI
AX=0500h reported a largest free block of 65,669,120 bytes before inference;
that measured value does not mean the DOS process can allocate all of it.

## Verified NFLLM 1.0 baseline guest inference

Both runs were made after a clean guest boot from an isolated clone of the
NF-DOS disk. `NFLLM.LOG` and `T1NFLLM.LOG` report `Status: PASS`, with logits and
token IDs computed from the loaded GGUF weights.

| Model | Guest configuration | Prompt | Verified output | Guest time |
|---|---|---|---|---:|
| `stories260K-f32.gguf` | 1,185,376 bytes; vocab 512; dim 64; 5 layers; 8 heads; 4 KV heads; FFN 172; context 2048 | `Once upon a time` | token 426, `.` | 3,076 ticks / 1000 Hz = 3.076 s |
| [`tiny1m.F32.gguf`](https://huggingface.co/shibatch/tiny1m) | 3,754,048 bytes; vocab 512; dim 128; 4 layers; 2 heads; 2 KV heads; FFN 352; context 256 | `Tom and Jerry are ` | token 411, `e` | 12,248 ticks / 1000 Hz = 12.248 s |

For TinyStories 1M, the guest reported 30 prompt tokens. The heap payload
estimate was 4,805,696 bytes: 3,754,048 model + 1,048,576 KV cache + 3,072
auxiliary bytes, excluding allocator and runtime overhead. The model's SHA-256
is `60FA2675B0531735C9724C9FCB871BAA6E40594F451D1454981034BE47D025D4`.

The same boot also verified both compatibility probes, the missing-model
error, and truncated-GGUF rejection (expected exit code 2). `NFLLM.DON` marks
completion of the guest script. The original NF-DOS disk remained unchanged at
SHA-256 `32A264038D161D7F10A8465C084A6895C558FC33ED02221B0D262A1EE9E6CEE3`;
models and test scripts were placed on the isolated clone.

### Stage 2 quantized Tiny1M runs

Q8_0 and Q4_0 inference also passed on clean 86Box clones. Their estimated
heap payloads were 2,062,400 B and 1,595,456 B, respectively, versus
4,805,696 B for F32. The Q8_0 run generated token 411 (`e`); Q4_0 generated
token 402 (`ch`). In three interleaved one-token benchmark samples, medians
were 32.461 s for Q8_0, 29.989 s for Q4_0, and 11.479 s for F32. These
row-dequantize-then-F32 paths reduce storage but are slower on this guest.
Details and log paths are in [`docs/ETAPA2-RELATORIO-FINAL.md`](docs/ETAPA2-RELATORIO-FINAL.md).

### Stage 3 Q2 and IQ3 format add-on

The `q2_x87.lib` and `iq3_x87.lib` plugins add row decoders for Q2_0, Q2_K,
IQ3_XXS and IQ3_S while retaining ABI C v1 and the 386+x87 baseline. The DOS
path decodes a row to F32 and then uses the reference F32 matvec; these are
format-support plugins, not direct packed dot-product optimizations.

All four formats passed compatibility checks and generated one token after a
clean NF-DOS boot on the 86Box Am486DX4/100 MHz/96 MiB profile. The fixture has
vocabulary 512, dimension 256, four layers, four heads, FFN 512 and a 30-token
prompt. Guest-reported heap payload includes model, 2 MiB KV cache and 3,072
auxiliary bytes, but excludes DOS/4GW allocator/runtime overhead.

| Format | GGUF size | Heap payload | Generated token | Guest time |
|---|---:|---:|---|---:|
| Q2_0 | 834,112 B | 2,934,336 B | id 413 (`t`) | 98.646 s |
| Q2_K | 969,280 B | 3,069,504 B | id 414 (`o`) | 101.886 s |
| IQ3_XXS | 1,127,296 B | 3,227,520 B | id 317 (` Lily`) | 176.750 s |
| IQ3_S | 1,262,464 B | 3,362,688 B | id 353 (` on`) | 169.555 s |

The reported guest time is the DOS clock at 1,000 ticks/s. Each output log
reports `Status: PASS` and says token IDs were computed from GGUF weights.
Host differential decoding against the vendored GGUF reference had maximum
absolute error 0 for all four formats. These timings show that the current
scalar row-decoding path is slow on the 486; they do not claim a speedup.

The historical raw guest logs and differential summaries cited in this
section remain in the development workspace and are not included in the
release archive. The release's DOS/32A binding and QEMU smoke evidence is in
[`docs/validation/`](docs/validation/).

### Stage 3 proxy-validated kernels

Direct Q8_0/Q4_0 row matvecs and the measured comparison against the previous
decode-then-F32 route are documented in
[`docs/ETAPA3-DIRECT-Q8-Q4.md`](docs/ETAPA3-DIRECT-Q8-Q4.md). A separate
3DNow! F32 matvec passed vector, odd-tail, x87-cleanup and four-token comparison
checks on a QEMU Athlon ISA proxy. MMX Q8_0/Q4_0 and SSE2 F32/Q8_0/Q4_0 passed
their QEMU proxy tests; SSE2 uses an isolated OSFXSR-enabled N270 guest. Direct
packed x87 kernels for Q2_0, Q2_K, IQ3_XXS and IQ3_S passed the QEMU generic 486
checks. These results do not establish physical Geode/Atom behavior or general
DOS/DPMI XMM-state safety. MMX F32 remains experimental because its four-token
sequence diverged and it was slower in the recorded proxy run.

The acceptance results, lab profiles, plugin matrix and measurements are in
[`docs/ETAPA3-RELATORIO-FINAL.md`](docs/ETAPA3-RELATORIO-FINAL.md),
[`docs/GEODE-READY.md`](docs/GEODE-READY.md),
[`docs/ATOM-READY.md`](docs/ATOM-READY.md),
[`docs/PLUGIN-COMPATIBILITY.md`](docs/PLUGIN-COMPATIBILITY.md) and
[`docs/BENCHMARKS-ETAPA3.md`](docs/BENCHMARKS-ETAPA3.md). This prototype work
does not finish the separate objective of porting upstream llama.cpp to DOS.

## Evidence and next work

- Packaged 1.1 binding and smoke evidence: [`docs/validation/`](docs/validation/)
- Stage reports and compatibility documents: [`docs/`](docs/)
- Raw historical guest logs cited by older reports are kept in the development
  workspace and are intentionally omitted from the public source archive.
- DOS build entry point: `build-dos.cmd`
- Target probe: `src/compat_probe.cpp`
- Inference backend: `NFLLM.cpp`

The next product objective is the upstream `llama.cpp` port. One successful
inference proves only the tested NFLLM model, kernel and guest path; it does not
prove full application or model support. The DOS/4GW EFLAGS.ID probe was
inconclusive, so ISA selection remains manually declared rather than automatic.
