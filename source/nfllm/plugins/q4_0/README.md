# Q4_0 static plugin

This ABI v1 Open Watcom OMF plugin provides two separate operations:

- `NFLLM_OP_DEQUANTIZE_ROW_Q4_0` decodes 16 packed bytes and one little-endian
  FP16 scale per 32-value block. Low nibbles are elements 0–15, high nibbles
  are 16–31, and values are centered by subtracting 8.
- `NFLLM_OP_MATVEC_Q4_0_ROW` computes a packed-row dot product directly and
  avoids creating a temporary F32 row.

Both current kernels are baseline 386+x87 C. The direct kernel is not an MMX,
3DNow!, or SSE implementation. Inference still uses the F32 backend for other
transformer operations. If the direct operation is unavailable or the user
selects `--backend reference`, NFLLM uses the earlier decode-then-F32 path.
The manifest is descriptive metadata. The project owner declares
NFLLM-authored plugin source under MIT; the broader source-reuse audit remains
pending.

## Verification

Self-tests cover nibble order/centering, FP16 normal/zero/subnormal scales,
invalid ABI and bounds. Differential tests compare the direct operation with
decode-then-dot. DOS inference and the full quantization regressions pass on
QEMU's generic 486 CPU model with 96 MiB.

In three interleaved QEMU samples, Q4_0 direct matvec had a 6,372-tick median;
the reference decode-then-F32 path had 10,381 ticks. This is a 1.63x whole-run
improvement over that fallback on the same emulator, but the direct run remained
slightly slower than F32 (6,262 ticks). This is not a physical Am486DX4 result.
See [`ETAPA3-DIRECT-Q8-Q4.md`](../../../../docs/ETAPA3-DIRECT-Q8-Q4.md) and the
artifact paths recorded there.
