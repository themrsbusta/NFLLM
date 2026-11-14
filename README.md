# NFLLM

**NFLLM 0.1 Beta** is an experimental, single-threaded inference prototype for
32-bit DOS and a narrow subset of F32 GGUF models using the LLaMA architecture.
It was developed for legacy x86 hardware and tested in an NF-DOS guest.

This is a small scalar implementation, not a port of the full upstream
`llama.cpp` runtime. The project is a proof of concept; the compatibility
claims below are limited to the configurations that were actually tested.

## Verified configuration

- 86Box 6.0, Windows build 9001
- AMD Am486DX4 at 100 MHz, 96 MiB RAM
- NF-DOS 5 build 5.01.3314 (DOS version 7.10)
- DOS/4GW runtime 1.97
- Open Watcom DOS/4G build, 386 instruction-set baseline, x87 floating point
- GGUF v3, `llama` architecture, F32 tensors

The compatibility probe reported that CPUID was unavailable and did not
execute the CPUID instruction. Its x87 check returned 5.25. The guest reported
a largest free DPMI block of 65,669,120 bytes before inference; this is a
guest-reported value, not a guarantee that a DOS process can allocate that
amount.

## Verified inference

Both tests ran after a clean boot from an isolated clone of the NF-DOS disk.
The saved logs report `Status: PASS` and one generated token for each model.

| Test model | Configuration | Prompt | Output | Guest time |
| --- | --- | --- | --- | ---: |
| TinyStories 260K F32 | vocab 512, dim 64, 5 layers, context 2048 | `Once upon a time` | token 426, `.` | 3.076 s |
| TinyStories 1M F32 | vocab 512, dim 128, 4 layers, context 256 | `Tom and Jerry are ` | token 411, `e` | 12.248 s |

For TinyStories 1M, the measured heap payload was 4,805,696 bytes, excluding
allocator and runtime overhead. Its tested GGUF SHA-256 was
`60FA2675B0531735C9724C9FCB871BAA6E40594F451D1454981034BE47D025D4`.

The same test run checked compatibility probes, a missing-model error, and
truncated-GGUF rejection. The guest logs and a redacted machine-readable summary
are in [`docs/evidence/`](docs/evidence/).

## Build

The build script requires Windows and an installed Open Watcom toolchain with
`wcl386` in `binnt64`. Set `WATCOM` to that toolchain's root, then run:

```bat
set WATCOM=C:\WATCOM
build-dos.cmd
```

The script writes generated files to `build/`. Running the resulting DOS
program requires a compatible DOS/4GW runtime. Neither that runtime nor any
compiled executable is included in this repository.

Example guest command:

```dos
NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1
```

See [`scripts/README.md`](scripts/README.md) for the environment-specific
86Box/NF-DOS verification helpers. Model weights are not included; see
[`models/README.md`](models/README.md).

## Known limits

- Standalone MS-DOS has not been validated; the verified guest used NF-DOS.
- Only the two listed F32 test configurations have been verified.
- Quantized models, other GGUF architectures, and broader model layouts are
  unverified.
- The full upstream `llama.cpp` runtime has not been ported.
- Insufficient-memory behavior and broader CPU compatibility still need work.
- A public binary release is not included in this initial source publication.

## Source provenance and licensing

The project handoff describes `llama.cpp` as a reference and says a complete
source-reuse audit is still pending. The upstream checkout and Git history were
not present in the handoff snapshot, so this repository does not claim to be a
fork or a complete, independent reimplementation. See
[`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

NFLLM-authored source is licensed under the MIT License; see [`LICENSE`](LICENSE).
The upstream `llama.cpp` MIT license reproduced in `third_party/licenses/` is a
separate third-party notice. The source-reuse audit remains pending, so the
project does not claim to be a fork or a complete, independent reimplementation.

## Repository contents

This initial public post contains source code, build and verification scripts,
and selected test evidence. It omits model weights, DOS disk images, generated
executables, and DOS/4GW binaries. Those files were excluded because their
distribution is unnecessary for reviewing the source or because their
redistribution terms have not been confirmed.
