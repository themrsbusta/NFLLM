# Third-party notices — NFLLM 1.2 source and runtime release

## NFLLM-authored source

NFLLM-authored source in this tree is licensed under MIT; see [`LICENSE`](LICENSE).
That license does not replace terms for third-party material listed below.

## llama.cpp / ggml lookup-table data

`plugins/iq3/iq3_tables.h` contains IQ3 codebook data transcribed from
`ggml/src/ggml-common.h` in `ggml-org/llama.cpp`, revision
`b79696f1f556cb30b0c184838115007d929baca8`. The upstream MIT license is
reproduced at [`third_party/llama.cpp/LICENSE`](third_party/llama.cpp/LICENSE).
No broader source reuse is claimed here.

## Open Watcom and CauseWay runtime

The 1.2 build uses the Open Watcom 2.0 toolchain and its CauseWay DOS extender
libraries: the host links with `-l=CauseWay`; `.NFD` plugin modules use the
`CWDLLS` system type internally. See the [Open Watcom CauseWay User's Guide](https://open-watcom.github.io/open-watcom-v2-wikidocs/cw.html)
for the upstream module format and calling convention.

Open Watcom's Sybase Open Watcom Public License 1.0 is included at
[`third_party/open-watcom/LICENSE.txt`](third_party/open-watcom/LICENSE.txt).
Build provenance and the license-file hash are recorded in
[`third_party/open-watcom/PROVENANCE.yaml`](third_party/open-watcom/PROVENANCE.yaml).
The toolchain itself is not bundled as part of the NFLLM plugin SDK. Review and
preserve the included license and notices with any distribution that contains
linked runtime components. The Open Watcom source and license are available at
the [official Open Watcom repository](https://github.com/open-watcom/open-watcom-v2);
the included binaries use its Watcom-1.0-licensed runtime components. The DOS
runtime release documentation identifies this source location and the included
license file.

The DOS/32A source, documentation, build wrapper and license retained at
`third_party/dos32a/` are historical NFLLM 1.1 material; the 1.2
`build-dos.cmd` does not invoke or link DOS/32A and does not require DOS4GW.
No DOS/32A binary tools are included. The DOS/32A license requires this
acknowledgment in documentation distributed with the source:

> This product uses DOS/32 Advanced DOS Extender technology.

## Package contents and exclusions

The intended SDK source package contains no model weights or DOS disk images.
`NFDENSE1` is a tiny generated test fixture and is not a pretrained model.

## TinyML reference projects and validation model

NFLLM NFModelIO (format reader), NFGraphVM (graph executor and activation planner),
and NFInt8Ops (scalar operators) are original NFLLM code. No source files, generated kernels, or
binary libraries from TinyEngine, MCUNet, TensorFlow, or LiteRT are included or
linked. The implementation consumes the documented TFLite v3 FlatBuffer
layout and quantized tensor metadata through NFLLM's own plugin ABI.

The external `mcunet-10fps_vww.tflite` and `mcunet-5fps_vww.tflite` weights
files were downloaded from the official MIT HAN Lab MCUNet release solely to
validate real MCUNet models. They are not included in the NFLLM tree or plugin
packages. The upstream MCUNet repository is MIT-licensed; retain its
attribution/license when separately redistributing those model files.
TinyEngine is also MIT-licensed. The local validation used the LiteRT CPU
runtime as an independent numerical oracle; it is test-only and not an NFLLM
build or runtime dependency.

- MCUNet project and license: <https://github.com/mit-han-lab/mcunet>
- TinyEngine project and license: <https://github.com/mit-han-lab/tinyengine>
- MCUNet model zoo / official release: <https://github.com/mit-han-lab/mcunet/blob/master/model_zoo.py>
- LiteRT validation runtime: <https://github.com/google-ai-edge/LiteRT>

