# Q2 plugin

The static Open Watcom library registers row decoders and direct packed x87
matvec kernels for GGML Q2_0 and Q2_K. Both paths remain available under ABI v1.

Q2_0 stores 64 two-bit values and one binary16 scale in an 18-byte block. Q2_K stores 256 values with 16 packed scale/min entries, 64 bytes of quants, and two binary16 super-scales in an 84-byte block.

The direct kernels passed the host differential (maximum absolute error 0),
the QEMU generic 486 self-test, and one-token inference for both formats.
Q2_0's direct path was slower than decode+F32 in the recorded Tiny1M run;
Q2_K was slightly faster. Results and limitations are in
`docs/ETAPA3-DIRECT-Q2-IQ3.md` and `docs/BENCHMARKS-ETAPA3.md`.
