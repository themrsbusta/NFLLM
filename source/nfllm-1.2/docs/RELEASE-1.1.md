# NFLLM 1.1 Experimental Research Release

NFLLM 1.1 is a regular GitHub release, classified as an Experimental Research Release. It packages the NFLLM DOS executable with DOS/32A 26.1.2 bound into the LE image. The final program does **not** require `DOS4GW.EXE` or a separate DOS extender beside it. Open Watcom's `-l=dos4g` option is only the build-time LE compatibility target before `SB /B` binds DOS/32A.

## Release assets

- `NFLLM-1.1-Experimental-Research-Release-DOS.zip`: DOS executables, static plugin libraries and manifests, user instructions, validation evidence, and all required license notices.
- `NFLLM-1.1-Experimental-Research-Release-Source.zip`: NFLLM 1.1 source, scripts, documentation, vendor build tools needed for binding, provenance, and license texts. It excludes build outputs, model weights, and DOS disk images.
- Each ZIP has a `.sha256` sidecar.

## Verification

- `build-dos.cmd`: passed; SVER identified DOS/32A 26.1.2 in the six DOS executables.
- `build-isa-probe.cmd`: passed; SVER identified DOS/32A 26.1.2 in `isa_probe.exe`.
- In NF-DOS 6 V23 under QEMU's generic 486 model with 96 MiB, `NFTEST.EXE` passed and the exact release `NFLLM.EXE` hash generated token ID 419 (`s`) from the TinyStories fixture. The guest share had no external extender tools.
- The exact boot image was mounted read-only and its SHA-256 did not change. The model and DOS image are not included.

## Scope and limitations

- This is a narrow NFLLM inference prototype, not the full upstream `llama.cpp` port.
- The exact 86Box Am486DX4/100 MHz profile has not yet been rerun for 1.1; the QEMU generic 486 result is a compatibility smoke check, not an exact-machine result.
- The vendored DOS/32A source snapshot is v26.1; the DOS/32A/SB/SVER package binaries report 26.1.2. An exact source-to-binary rebuild was not established.
- NFLLM-authored source is MIT. The release also includes the complete DOS/32A, Open Watcom, and upstream IQ3-data license notices; the required DOS/32A acknowledgment is in the included documentation.

The NFLLM 1.0 tree, `main`, and `v1.0` remain unchanged. The 1.1 source is added separately under `source/nfllm-1.1`.
