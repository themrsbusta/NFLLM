> Histórico NFLLM 1.1: o texto abaixo descreve o empacotamento antigo em biblioteca estática. NFLLM 1.2 usa manifestos NFM e carrega DLLs independentes; veja o Plugin SDK em docs/PLUGIN-SDK.md.

# 3DNow! static plugin

This plugin provides an ABI v1 F32 matrix-vector kernel in a separate Open
Watcom OMF library. It computes packed binary32 products and pair reductions
with `PFMUL`, `PFADD` and `PFACC`; an odd trailing element uses the scalar
baseline. `EMMS` restores the x87 state before returning.

The plugin is registered before the x87 reference implementation so automatic
dispatch can prefer it when the manually declared CPU profile includes 3DNow!.
`--backend reference` keeps the F32 baseline. `--backend 3dnow` requests this
kernel and is rejected unless the host profile declares the required ISA.
RMSNorm and RoPE continue to use the x87 reference kernels.

QEMU's Athlon proxy is only a compatibility test for the instructions used; it
does not identify or emulate an exact Geode microarchitecture. The isolated test
and Tiny1M inference passed. Three paired four-token runs produced the same
sequence as x87. The guest did not record prompt ticks in this run, so the
benchmark reports generation-phase ticks only; it makes no total-inference
claim. See `docs/GEODE-READY.md` and `docs/BENCHMARKS-ETAPA3.md`.
