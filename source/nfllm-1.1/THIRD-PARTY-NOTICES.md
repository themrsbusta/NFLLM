# Third-party notices — NFLLM 1.1

## NFLLM-authored source

NFLLM-authored source in this version is licensed under MIT; see [`LICENSE`](LICENSE).
That license does not replace terms for third-party material listed below.

## llama.cpp / ggml lookup-table data

`plugins/iq3/iq3_tables.h` contains IQ3 codebook data transcribed from
`ggml/src/ggml-common.h` in `ggml-org/llama.cpp`, revision
`b79696f1f556cb30b0c184838115007d929baca8`. The relevant upstream MIT license
is reproduced at [`third_party/llama.cpp/LICENSE`](third_party/llama.cpp/LICENSE).
The full upstream tree is reference material in the repository and is not part
of the standalone NFLLM 1.1 source archive. A complete file-level source-reuse
audit remains pending; no broader source reuse is claimed here.

## DOS/32 Advanced DOS Extender

The 1.1 DOS executables include DOS/32A 26.1.2 through the vendor SUNSYS Bind
Utility. The source snapshot and the complete license for that implementation
are at [`third_party/dos32a/`](third_party/dos32a/). The 8.00 historical
reference documentation retains its separate 1996–2002 Supernar Systems
copyright and the Liberty Edition license page at
[`third_party/dos32a/dos32a_800/docs/html/gnrl/0c.html`](third_party/dos32a/dos32a_800/docs/html/gnrl/0c.html).
Those historical documents are not used by the NFLLM build or runtime.

The end-user documentation includes the required acknowledgment:

> This product uses DOS/32 Advanced DOS Extender technology.

NFLLM is not named “DOS/32A” or “DOS/32 Advanced”.

## Open Watcom runtime

The release executables were built with Open Watcom 2.0 and statically include
referenced C/C++ runtime library modules. The compiler toolchain itself is not
bundled. Its Sybase Open Watcom Public License 1.0 is included at
[`third_party/open-watcom/LICENSE.txt`](third_party/open-watcom/LICENSE.txt).
Build provenance and the license-file hash are recorded in
[`third_party/open-watcom/PROVENANCE.yaml`](third_party/open-watcom/PROVENANCE.yaml). The Open Watcom source is available at
[open-watcom/open-watcom-v2](https://github.com/open-watcom/open-watcom-v2).

## Package contents and exclusions

The DOS runtime ZIP contains no model weights, DOS disk images, NF-DOS image,
DOS/4GW binary, or separate extender utility. The source ZIP includes the
DOS/32A `DOS32A.EXE`, `SB.EXE`, and `SVER.EXE` package tools solely for binding
and verification during a rebuild; they are not required beside the final
NFLLM executables. NFLLM 1.1 does not require an external `DOS4GW.EXE` at
runtime. Neither ZIP includes model weights or disk images.
