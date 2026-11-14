# IQ3 plugin

The static Open Watcom library registers ABI v1 row decoders and direct packed
x87 matvec kernels for GGML IQ3_XXS and IQ3_S. The two paths remain available
for fallback and comparison.

IQ3_XXS uses a 98-byte block of 256 values. IQ3_S uses a 110-byte block. The decoder includes only the IQ3 codebook data needed by these formats in `iq3_tables.h`; its source is the vendored `ggml-common.h`, under the license in `source/upstream-llama.cpp/LICENSE`.

The direct kernels passed the host differential (maximum absolute error 0),
the QEMU generic 486 self-test, and one-token inference for both formats. In
the recorded Tiny1M benchmark, both direct kernels were faster than
decode+F32; these measurements are specific to that model and guest. See
`../../../docs/ETAPA3-DIRECT-Q2-IQ3.md` and `../../../docs/BENCHMARKS-ETAPA3.md`.
