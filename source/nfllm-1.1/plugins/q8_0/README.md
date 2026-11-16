# Q8_0 static plugin

This ABI v1 Open Watcom OMF plugin provides two separate operations:

- `NFLLM_OP_DEQUANTIZE_ROW_Q8_0` decodes blocks of 32 signed 8-bit values and
  their little-endian FP16 scale into F32.
- `NFLLM_OP_MATVEC_Q8_0_ROW` computes a packed-row dot product directly and
  avoids creating a temporary F32 row.

Both current kernels are baseline 386+x87 C. The direct kernel is not an MMX,
3DNow!, or SSE implementation. Inference still uses the F32 backend for other
transformer operations. If the direct operation is unavailable or the user
selects `--backend reference`, NFLLM uses the earlier decode-then-F32 path.

The plugin is statically linked as `q8_0_x87.lib`; the manifest is descriptive
metadata. License status remains undeclared in project sources.

## Verification

Host self-tests check signed values, FP16 scales and invalid input. Differential
tests compare the direct path with decode-then-dot. DOS inference and the full
quantization regressions pass on QEMU's generic 486 CPU model with 96 MiB.

In three interleaved QEMU samples, Q8_0 direct matvec had a 6,481-tick median;
the reference decode-then-F32 path had 10,436 ticks. This is a 1.61x whole-run
improvement over that fallback on the same emulator, but the direct run remained
slightly slower than F32 (6,262 ticks). This is not a physical Am486DX4 result.
See [`ETAPA3-DIRECT-Q8-Q4.md`](../../docs/ETAPA3-DIRECT-Q8-Q4.md) and the
artifact paths recorded there.
