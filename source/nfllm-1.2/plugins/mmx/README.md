> Histórico NFLLM 1.1: o texto abaixo descreve o empacotamento antigo em biblioteca estática. NFLLM 1.2 usa manifestos NFM e carrega DLLs independentes; veja o Plugin SDK em docs/PLUGIN-SDK.md.

# MMX F32 matvec plugin

The MMX plugin quantizes each F32 input vector and each weight row to symmetric
signed int8, computes the dot product with `PMADDWD`, restores the two scales,
and runs `EMMS` before returning to x87 code. It accepts odd vector lengths by
handling the remaining elements in scalar code. The output is approximate; it
is not bitwise equivalent to the x87 reference.

The backend is opt-in with `--backend mmx` and requires a manually declared
MMX-capable CPU profile. The default 386+x87 Am486 profile cannot select or
execute it. The Q8_0/Q4_0 matrix kernels passed guest inference and the isolated
self-test on both the QEMU Athlon ISA proxy and Atom N270 profile. Each
one-token run processed 176,640 direct rows and matched the corresponding
reference token.

The F32 route quantizes activations and rows to int8 first. It passed isolated
numeric checks, but a four-token run diverged from the x87 sequence after the
first token and its measured prompt/generation phases were slower. Keep F32
MMX experimental and manually selected; this result does not invalidate the
independent Q8_0/Q4_0 matrix kernels.

The isolated test covers a non-empty packed group, an odd scalar tail,
quantization error tolerance, rejection of non-finite input without partial
output, and x87 arithmetic after `EMMS`. QEMU validation is an ISA proxy only;
it does not prove behavior on physical Geode hardware. See the
[compatibility summary](../../docs/PLUGIN-COMPATIBILITY.md) for scope and
limitations.
