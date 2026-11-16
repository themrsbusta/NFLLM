# Experimental SSE2 F32 matvec plugin

The plugin performs four-wide F32 multiply/add operations and handles a
non-multiple-of-four tail in scalar code. It is compiled for the 386 baseline,
with SSE instructions encoded as bytes, and can execute only after an explicit
Atom profile confirms `CR4.OSFXSR`.

The QEMU N270 profile passed the OSFXSR helper and the isolated SSE2 tests.
Three paired F32 runs emitted the same four-token sequence as x87; Q8_0 and
Q4_0 matrix kernels passed inference and three repeated comparisons against the
scalar direct-row kernels. The measured medians and scope are in
`docs/BENCHMARKS-ETAPA3.md`.

`OSFXSR.COM` is a test-only real-mode helper that sets CR4.OSFXSR in an isolated
QEMU Atom guest. Never run it on the Am486 or a physical DOS system. This
single-process VM experiment does not prove that a general DOS/DPMI host saves
XMM state across tasks, interrupts, or context switches. Keep the plugin
experimental and manually selected until that contract is established.
