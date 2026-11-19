# NFDENSE plugin SDK example

This standalone CauseWay DLL demonstrates how an external developer can build
and install a plugin without rebuilding `NFLLM.EXE`. It uses only the public
headers `sdk/include/nfllm_abi.h` and `sdk/include/nfllm_plugin_sdk.h`.

The sample bundle registers all six extension categories: an operator,
quantization descriptor, model-file format, neural architecture, math-backend
descriptor and hardware-optimization descriptor. Its custom `NFDENSE1` model
container is not GGUF. The toy architecture reads a 2×2 signed-byte transition
matrix and emits `a` or `b`; this is an SDK integration fixture, not a
production language model or an implementation of an external neural framework.

## Build and install

Run `build.cmd` with the Open Watcom toolchain configured by `WATCOM`. It writes
`build/PLUGINS/NFDENSE.NFD` and `NFDENSE.NFM`; it does not invoke the NFLLM host
build. Place those two files in `PLUGINS` beside the existing `NFLLM.EXE`.
Generate the 12-byte sample model with `python make_model.py`, then run:

```text
NFLLM.EXE TINY.NFD -p A -n 1
NFLLM.EXE TINY.NFD -p B -n 1
```

The sample emits `a` for prompt `A` and `b` for prompt `B`. The manifest
priority is 1000, so the scalar reference backend remains preferred for normal
automatic F32 dispatch. The sample kernel can be explicitly selected by its
numeric plugin id, `0x4E464431`, if exercising that operation through GGUF.

## Package management

- Install: copy `NFDENSE.NFD` and `NFDENSE.NFM` into `PLUGINS`.
- Update: replace both files while NFLLM is closed, keeping their base name.
- Disable: create `PLUGINS\NFDENSE.OFF`; remove that sentinel to re-enable.
- Remove: delete `NFDENSE.NFD`, `NFDENSE.NFM` and any `NFDENSE.OFF` file.

All package filenames must use DOS 8.3 names. The loader checks the public ABI
version and CPU capability mask before registering a module. Install plugins
only from developers you trust; a loaded DLL runs inside the NFLLM process.
