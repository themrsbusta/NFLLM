# NFLLM neural graph extensions

## Independently implemented modules

`plugins/build-nfllm-extensions.cmd` builds three CauseWay modules with the `.NFD`
plugin-file extension. It does not call `build-dos.cmd` or relink `NFLLM.EXE`:

| Package | Display name | SDK extension | Responsibility |
| --- | --- | --- |
| `NFMODEL.NFD` | NFLLM ModelIO TFLite v3 Adapter | model format | Bounded reader for static-shape TFLite v3 FlatBuffers; returns tensors, affine quantization metadata and operators. |
| `NFGRAPH.NFD` | NFLLM GraphVM INT8 Runtime | neural architecture | Executes an INT8 graph through the NFLLM operator resolver and plans/reuses activation arena ranges according to tensor lifetimes. |
| `NFINT8OP.NFD` | NFLLM Int8Ops Kernel Pack | operator kernels | Scalar implementations of six TFLite INT8 operators, keyed by stable NFLLM operation IDs. |

All modules use `sdk/include/nfllm_graph_sdk.h`. Their C code was written for
NFLLM; the project does not vendor or link TinyEngine, MCUNet, TensorFlow or
LiteRT implementation source. PDR is not a build or runtime dependency.

## Tested model and boundary

The numerical integration fixtures are two official MCUNet VWW TFLite models:

| Model | Input | TFLite operators | LiteRT `BUILTIN_REF` logits |
| --- | --- | --- | --- |
| `mcunet-10fps_vww` / VWW0 | signed INT8 `[1,64,64,3]` | 42 PAD, 29 CONV_2D, 14 DEPTHWISE_CONV_2D, 7 ADD, 1 AVERAGE_POOL_2D, 1 RESHAPE | `[7,-7]` |
| `mcunet-5fps_vww` / VWW1 | signed INT8 `[1,80,80,3]` | 45 PAD, 31 CONV_2D, 15 DEPTHWISE_CONV_2D, 8 ADD, 1 AVERAGE_POOL_2D, 1 RESHAPE | `[-3,2]` |

The downloaded weight files are external validation data; they are not part of
the SDK or plug-in packages. Test frames are deterministic signed INT8 bytes,
so numeric output can be reproduced without a camera pipeline.

The implementation covers these TFLite built-ins:

- `PAD` with constant INT32 paddings and quantized real-zero fill.
- `CONV_2D` with per-output-channel INT8 weights and INT32 bias.
- `DEPTHWISE_CONV_2D` with per-output-channel INT8 weights and INT32 bias.
- `ADD` with affine INT8 input/output scales and broadcasting.
- `AVERAGE_POOL_2D` for matching input/output affine quantization.
- `RESHAPE` for a byte-preserving shape change.

All graph shapes must be static and rank 1–4. Activations and filters use
signed INT8; convolution bias and the `PAD` parameter tensor use INT32. The
plug-ins reject unsupported operators, tensor types, dynamic shapes, malformed
offsets, and invalid quantization metadata. They do not claim full TFLite
coverage, full model-generation pipeline coverage, or every MCUNet architecture.

## Build and install

1. Build the updated NFLLM 1.2 host with `build-dos.cmd` to enable the `.NFD`
   plugin-file extension.
2. Run `plugins/build-nfllm-extensions.cmd` with `WATCOM` set to Open Watcom 2.0.
3. Copy the `.NFD` + `.NFM` pair for each of `NFMODEL`, `NFGRAPH`, and `NFINT8OP`
   from `build/PLUGINS` into the target `PLUGINS` directory alongside any
   other required NFLLM packages.
4. Provide a raw signed INT8 NHWC frame of the size required by the model,
   then run the matching command:

   ```text
   NFLLM.EXE VWW0.TFL -p FRAME.RAW -n 1
   NFLLM.EXE VWW1.TFL -p FRAME.RAW -n 1
   ```

The existing `-p` value is interpreted as the raw frame path only by this
classification architecture. The architecture emits one token with the
predicted class index and the two raw INT8 logits. No host CLI changes are
needed.

To install, replace or remove a package, use the `.NFD` + `.NFM` pair
and `.OFF` disable-marker conventions. The host must be built once with the
`.NFD` loader; subsequent package installs and replacements do not require
rebuilding or modifying the host EXE.

## Numerical test

`docs/validation/graph-inference-1.2-vww0.json` and
`docs/validation/graph-inference-1.2-vww1.json` record model and input hashes,
LiteRT CPU reference outputs, NFLLM DOS outputs, plugin loading, and the host
EXE hash. Both models matched LiteRT 2.3.0 `OpResolverType.BUILTIN_REF`
exactly. For VWW1, LiteRT's
default `AUTO` resolver with XNNPACK returned `[-2,1]`, while its `BUILTIN_REF`
resolver returned `[-3,2]`; NFLLM matched the reference resolver. The report
preserves this optimized-backend rounding difference explicitly.

The local test runtime is DOSBox-X with a 486 instruction profile, 96 MiB RAM,
and FreeDOS 1.4. This demonstrates functional compatibility; it does not
simulate AMD Am486DX4 cycle timing. LiteRT is only the independent test oracle,
not an NFLLM dependency.
