# NFLLM 1.1 developer instructions

- This directory is the separate NFLLM 1.1 tree. Keep the NFLLM 1.0 source and `v1.0` release unchanged.
- NFLLM 1.1 does not require `DOS4GW.EXE` at runtime. `DOS/32A` is bound into each final LE executable by `SB /B`; `DOS32A.EXE`, `SB.EXE` and `SVER.EXE` are build-time tools only.
- Open Watcom's `-l=dos4g` option is used to emit the compatible LE input for the binding step. It does not mean the final program loads or requires DOS/4GW.
- Keep the required DOS/32A acknowledgment, all component licenses and provenance in `THIRD-PARTY-NOTICES.md` and `third_party/` in source and binary release packages.
- Do not include model weights or DOS disk images in public source/binary assets. Document model licensing separately when a model is added.
- The 1.1 QEMU 486 smoke result is not an exact 86Box Am486DX4/100 MHz result; retain that limitation in release notes until that profile is tested.
- Publish tag `v1.1` as a regular GitHub release (`prerelease: false`), with the Experimental Research Release classification and documented limitations.
