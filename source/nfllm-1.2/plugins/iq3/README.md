> Histórico NFLLM 1.1: o texto abaixo descreve o empacotamento antigo em biblioteca estática. NFLLM 1.2 usa manifestos NFM e carrega DLLs independentes; veja o Plugin SDK em docs/PLUGIN-SDK.md.

# IQ3 plugin

The static Open Watcom library registers ABI v1 row decoders and direct packed
x87 matvec kernels for GGML IQ3_XXS and IQ3_S. The two paths remain available
for fallback and comparison.

IQ3_XXS uses a 98-byte block of 256 values. IQ3_S uses a 110-byte block. The decoder includes only the IQ3 codebook data needed by these formats in `iq3_tables.h`; the tables were transcribed from `ggml-common.h` at revision `b79696f1f556cb30b0c184838115007d929baca8`. The MIT attribution is in `../../THIRD-PARTY-NOTICES.md` and `../../third_party/llama.cpp/LICENSE`.

See the [compatibility summary](../../docs/PLUGIN-COMPATIBILITY.md) for the
verified scope and limitations of this format implementation.
