> Histórico NFLLM 1.1: o texto abaixo descreve o empacotamento antigo em biblioteca estática. NFLLM 1.2 usa manifestos NFM e carrega DLLs independentes; veja o Plugin SDK em docs/PLUGIN-SDK.md.

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
NFLLM-authored plugin code is covered by the project MIT license in `../../LICENSE`.
The IQ3 lookup-table attribution and third-party license texts are in
`../../THIRD-PARTY-NOTICES.md`.

## Verification

Self-tests cover nibble order/centering, FP16 normal/zero/subnormal scales,
invalid ABI and bounds. Differential tests compare the direct operation with
decode-then-dot. DOS inference and the full quantization regressions pass on
QEMU's generic 486 CPU model with 96 MiB.

See the [compatibility summary](../../docs/PLUGIN-COMPATIBILITY.md) for the
verified scope and performance limitations.
