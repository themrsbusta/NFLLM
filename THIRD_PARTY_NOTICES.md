# Third-party notices

## llama.cpp

The separately located [`source/upstream-llama.cpp/`](source/upstream-llama.cpp/)
tree is an unmodified reference snapshot of
[`ggml-org/llama.cpp`](https://github.com/ggml-org/llama.cpp), at revision
`b79696f1f556cb30b0c184838115007d929baca8`. Its upstream `LICENSE` and
third-party notices remain in that subtree. It has not been ported to NF-DOS.

Some NFLLM code uses quantization lookup-table data from the upstream project;
the relevant source retains its upstream attribution. A complete source-reuse
audit has not been completed. NFLLM-authored source is licensed under MIT by
the root [`LICENSE`](LICENSE); this notice does not replace the upstream or
other third-party terms.

No model weights, DOS disk images, DOS/4GW binaries, or compiled executables
are included in the source repository. The separate DOS release ZIP contains
compiled NFLLM project builds and notices, but no model weights, disk images,
or DOS/4GW runtime.
