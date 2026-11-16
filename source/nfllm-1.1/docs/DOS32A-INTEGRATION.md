# NFLLM 1.1 — DOS/32A integration

## Integration method

Open Watcom emits the DOS Linear Executable format with its `-l=dos4g` target
library. `bind-dos32a.cmd` runs the DOS/32A SUNSYS Bind Utility on each raw LE
output using the supported `/B` operation and a distinct `/BN<name>` output.
`SVER.EXE` then verifies that the final executable contains a DOS/32A stub.

The linker setting retains the DOS/4G-compatible LE format. It does not require
an external `DOS4GW.EXE` at runtime. The final executables contain the bound
DOS/32A extender and run without external `DOS4GW.EXE`, `DOS32A.EXE`, `SB.EXE`
or `SVER.EXE` files beside them. The build copies the binder and extender into a
temporary directory for binding, then removes those staged tools after success.

This is the DOS/32A executable-binding workflow, not textual amalgamation of
the extender assembly into `NFLLM.cpp`. DOS/32A remains separately identified
and licensed under `third_party/dos32a/LICENSE`.

The build-time Open Watcom runtime modules are statically linked into the DOS
executables; the Open Watcom Public License and source location are recorded in
`THIRD-PARTY-NOTICES.md` and `third_party/open-watcom/LICENSE.txt`.

## Build outputs

From this directory, run:

```bat
build-dos.cmd
build-isa-probe.cmd
```

The first command compiles and binds `NFLLM.EXE`, `NFTEST.EXE`, `compat_probe.exe`,
and the MMX, 3DNow and SSE2 test executables. The second command builds and binds
`isa_probe.exe`. The binder checks each output with `SVER.EXE`; logs and the
target-to-log mapping are kept separately under
`docs/validation/dos32a-bind/all/` and `docs/validation/dos32a-bind/isa/` in this source release; fresh local logs are written under `build/logs/`.

The build consumes the DOS/32A source snapshot at tag `v26.1` for provenance
and the `DOS32A.EXE`, `SB.EXE` and `SVER.EXE` files from the local Open Watcom
package `dos32a-26.1.2.zip`. The packaged binaries report version 26.1.2.
The exact source-to-binary build match is unverified: the source tag is v26.1,
and its historical build also requires TASM 5.3, which is not installed here.
See `third_party/dos32a/PROVENANCE.yaml` for hashes and origins.

## Verification performed

- `build-dos.cmd` completed successfully; `SVER.EXE` identified DOS/32A 26.1.2
  in all six DOS outputs (`build/logs/dos32a-bind/all/`).
- `build-isa-probe.cmd` completed successfully; its `isa_probe.exe` was bound
  and recognized by `SVER.EXE` (`build/logs/dos32a-bind/isa/`).
- `NFTEST.EXE` reported `SELFTEST: PASS` in the guest.
- `NFLLM.EXE` loaded `STORY.GGU` and computed one token: token ID 419, text `s`.
  The guest reported `Status: PASS`.
- Guest: NF-DOS 6 V23 boot image, QEMU generic 486 CPU, TCG single-thread,
  96 MiB RAM. The FAT test volume contained only `NFLLM.EXE`, `NFTEST.EXE`,
  `PROBE.EXE` and the 1.18 MB model; it contained no external extender or
  binder utilities.
- The V23 boot image was attached read-only. Its SHA-256 remained
  `53C7345445F70B71D93F2094CAE406BF5E79E4B1AE042C94551B6DE6C9B18C62` before
  and after the run.
- Detailed output is in `build/logs/dos32a-bind/` and
  `build/logs/nfdos-v23-dos32a/nfdos-v23-smoke-20261116-b/`.

This smoke test proves that the 1.1 binary starts under DOS/32A and performs a
real inference token without an external DOS/4GW file in that guest. The CPU is
QEMU's generic 486 model; this is not yet a repeat of the earlier 86Box
Am486DX4/100 MHz configuration. The NF-DOS 5/DOS/4GW logs in the stage reports
remain historical 1.0 baseline evidence.

## License conditions observed

The redistribution terms allow source and binary redistribution if the
copyright, conditions and disclaimer are retained. Binary materials include
`THIRD-PARTY-NOTICES.md`; the user-facing acknowledgment appears in the README
and notice file. The product remains named NFLLM and does not use either
restricted DOS/32A product name as its own name. This records the terms applied
to this packaging and is not a legal opinion.
