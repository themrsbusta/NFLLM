> Histórico NFLLM 1.1: o texto abaixo descreve o empacotamento antigo em biblioteca estática. NFLLM 1.2 usa manifestos NFM e carrega DLLs independentes; veja o Plugin SDK em docs/PLUGIN-SDK.md.

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

The plugin's NFLLM-authored code is licensed under MIT; see the root `LICENSE`
and [`THIRD-PARTY-NOTICES.md`](../../THIRD-PARTY-NOTICES.md). The manifest
identifies the plugin for NFLLM's extension system.

## Verification

Host self-tests check signed values, FP16 scales and invalid input. Differential
tests compare the direct path with decode-then-dot. DOS inference and the full
quantization regressions pass on QEMU's generic 486 CPU model with 96 MiB.

See the [compatibility summary](../../docs/PLUGIN-COMPATIBILITY.md) for the
verified scope and performance limitations.
