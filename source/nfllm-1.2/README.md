# NFLLM 1.2 — extensible DOS inference runtime

NFLLM 1.2 is an experimental research release of a loadable plugin architecture
for 32-bit DOS. It keeps the existing NFLLM 1.0 and 1.1 trees intact.

The host executable loads independently built CauseWay modules with the
`.NFD` plugin-file extension from `PLUGINS` beside `NFLLM.EXE`. Operators,
quantizations, model containers,
neural architectures, math-backend descriptors and hardware-optimization
metadata enter through a public, versioned C ABI. A new plugin can be installed,
updated, disabled or removed without rebuilding or modifying the host EXE.

## Public SDK

- Public headers: [`sdk/include/`](sdk/include/)
- ABI and plugin lifecycle: [`docs/PLUGIN-SDK.md`](docs/PLUGIN-SDK.md)
- Standalone example: [`examples/nfdense/`](examples/nfdense/)
- Build script for the host and bundled plugin modules: `build-dos.cmd`
- Build script for the sample plugin module only: `examples/nfdense/build.cmd`

The sample defines a tiny custom `NFDENSE1` container (not GGUF), a small
architecture, a row-quantization validator, an executable operator, a math
backend descriptor and a hardware descriptor. It produces `a` or `b` from a
12-byte test model. This demonstrates the extension route, not production
model support.

## Neural graph extensions

The separately built graph packages in `build/PLUGINS` add three independently
installable NFLLM modules: **NFModelIO** reads TFLite v3, **NFGraphVM** runs INT8 graphs
with a planned activation arena, and **NFInt8Ops** supplies scalar INT8 operators.
Two official MCUNet Visual Wake Words models have been run:
`mcunet-10fps_vww` (64×64) and
`mcunet-5fps_vww` (80×80). They run without converting the model to GGUF and
without rebuilding `NFLLM.EXE` after the host adopts the `.NFD` loader contract.

Build just the three plug-ins with `plugins/build-nfllm-extensions.cmd`. Install
`NFMODEL.NFD/.NFM`, `NFGRAPH.NFD/.NFM` and `NFINT8OP.NFD/.NFM` beside the existing
packages in `PLUGINS`. The tested commands are:

```text
NFLLM.EXE VWW0.TFL -p FRAME.RAW -n 1
NFLLM.EXE VWW1.TFL -p FRAME.RAW -n 1
```

`FRAME.RAW` is one signed INT8 NHWC frame: 12,288 bytes for VWW0 or 19,200
bytes for VWW1. The output token reports the class index and two INT8 logits.

Supported TFLite operators in this plugin release: `PAD`, `CONV_2D`,
`DEPTHWISE_CONV_2D`, `ADD`, `AVERAGE_POOL_2D` and `RESHAPE`, with static shapes,
INT8 activations/weights and INT32 convolution biases. Other TFLite graphs are
rejected as unsupported. This is the independently implemented operator and
format subset needed by the validated VWW graphs, not a claim that all
TFLite graphs or every MCUNet network is supported. See
[`docs/NEURAL-GRAPH-EXTENSIONS.md`](docs/NEURAL-GRAPH-EXTENSIONS.md),
[`sdk/include/nfllm_graph_sdk.h`](sdk/include/nfllm_graph_sdk.h) and the
numeric validation records in `docs/validation/graph-inference-1.2-vww0.json` and
`docs/validation/graph-inference-1.2-vww1.json`.

## Selection and lifecycle

The CPU capability mask rejects an implementation that could execute unsupported
instructions. It does not estimate speed. Among compatible implementations,
lower configured priority wins; ties resolve by plugin and extension IDs. This
allows an older or scalar implementation to remain preferred when it benchmarks
better on the target computer.

A package is one `.NFD` and one `.NFM` manifest sharing a DOS 8.3 basename.
Copy both into `PLUGINS` to install or update. Create `NAME.OFF` to disable it;
deleting the sentinel enables it again. Delete the pair to remove it. NFLLM
must be closed for file replacement, and package changes take effect on its next
start. The loader has no package dependency resolver, signature verification or
GUI installer; only install code you trust because plugin modules run in the host
process.

The source tree includes an SSE2 plugin implementation, but the DOS runtime
release does not ship an SSE2 package: safe XMM-state handling under DOS/DPMI
has not been established. The runtime ZIP lists the packages it actually
contains.

## Build and validation

Set `WATCOM` to the Open Watcom installation and run `build-dos.cmd`. This emits
`build/NFLLM.EXE` and separate `build/PLUGINS/*.NFD` plus `.NFM` files. This
build updates the host loader to locate `.NFD` modules. To build only the sample
plugin afterward, run `examples/nfdense/build.cmd`; it does not rebuild the
host. See the SDK guide for CauseWay module compiler/linker settings.

Curated NFLLM 1.2 evidence is recorded at
[`docs/validation/plugin-sdk-1.2.json`](docs/validation/plugin-sdk-1.2.json):
install/update/disable/remove and two independent custom-format token
inferences passed in DOSBox-X with a 486 instruction profile, 96 MiB RAM and
FreeDOS 1.4. The tests verify the host executable hash remains unchanged.
DOSBox-X is a functional 486-class proxy; it is not cycle-accurate AMD Am486DX4
emulation.

TinyML inference evidence is recorded separately for both models. NFLLM's
logits exactly match LiteRT 2.3.0's `BUILTIN_REF` integer reference for the
deterministic signed-INT8 inputs. On VWW1, LiteRT's default XNNPACK path differs
by one in each logit; both outputs are preserved in its report.

## Scope and limits

NFModelIO, NFGraphVM and NFInt8Ops are independent NFLLM implementations informed by
public neural model interfaces and formats. No upstream source code or
generated kernels are incorporated. This
release validates the MCUNet VWW model and the operator subset listed above;
additional architectures, operators, and quantization forms still require
independent plugins. PDR is not a dependency. The separate `llama.cpp` upstream
port remains a distinct objective.

NFLLM 1.0 and 1.1 information and validation documents retained in `docs/` are
historical unless explicitly labeled 1.2. This source tree corresponds to
`v1.2.0`, titled **NFLLM 1.2.0 — Experimental Research Release**. The release
is available at the [GitHub release page](https://github.com/themrsbusta/NFLLM/releases/tag/v1.2.0).
This source tree is a separate asset from the DOS runtime package. See
[`docs/RELEASE-1.2.0.md`](docs/RELEASE-1.2.0.md) for its exact scope.

## Licensing

See [`LICENSE`](LICENSE) and [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)
for NFLLM and bundled third-party notices. No model weights or DOS disk images
are part of the source tree's intended public SDK package.
