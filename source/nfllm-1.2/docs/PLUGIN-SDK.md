# NFLLM 1.2 Plugin SDK

NFLLM 1.2 loads CauseWay plugin modules from `.NFD` files in `PLUGINS` at process
startup. A plugin can be built, installed, updated, disabled or removed independently of
`NFLLM.EXE`. ABI v1 is a public C interface for 32-bit DOS: it passes fixed
versioned structures and function pointers, not C++ objects, exceptions or
compiler-owned containers.

## SDK files

- `sdk/include/nfllm_abi.h` — shared status codes, operation IDs, capability
  bits, argument structures and the host ABI.
- `sdk/include/nfllm_plugin_sdk.h` — plugin bundles, extension kinds,
  quantization/model/architecture interfaces and the query-entry macro.
- `sdk/include/nfllm_graph_sdk.h` — neutral tensor-graph and operator-call ABI
  used by the NFModelIO, NFGraphVM and NFInt8Ops modules.
- `examples/nfdense/` — an independently built `.NFD` module and custom model
  fixture. It does not rebuild the host when compiled.

The copies in `src/` are forwarding headers for NFLLM's internal source. The
SDK headers under `sdk/include/` are the public include path for plugin authors.

## What plugins can extend

| Extension | Runtime contract |
|---|---|
| Operator | `nfllm_kernel_descriptor_v1` supplies an executable callback for an operation ID. The host exposes each bundle kernel to `resolve_extension(NFLLM_EXT_OPERATOR, operation_id, ...)` automatically; plugin authors do not add a duplicate extension descriptor. |
| Quantization | `nfllm_quantization_v1` identifies a packed type and supplies row validation; decoding and direct math are operator callbacks. |
| Model file format | `probe/open/close` recognizes a file and exposes a format-neutral model/tensor/token view. |
| Neural architecture | `supports/generate` implements the model's inference path and emits tokens through the host callback. |
| Math backend | A backend descriptor identifies the implementation; executable math is supplied by that plugin's operator callbacks. |
| Hardware optimization | A hardware descriptor documents the target; operator and plugin capability masks gate execution of the actual implementation. |

Format and architecture plugins are resolved independently by their IDs. A
format can therefore provide a `model_view` for an architecture from another
`.NFD` module. The `NFDENSE` fixture deliberately packages its small format, architecture,
quantization validator, operator and backend descriptors together so one sample
shows all registration types. Its matrix kernel is executable, and its
architecture really emits a token; the descriptors are not a substitute for
those callbacks.

## CPU safety and implementation choice

The loader rejects a plugin whose required CPU capability bits are not present
in the active CPU profile. This is a compatibility guard, not a benchmark.
Among compatible implementations the runtime uses the manifest priority (lower
number wins); equal priorities resolve deterministically by plugin ID and
extension ID. A newer ISA is never assumed to be faster. Developers can publish
several implementations, compare them on the target machines, and adjust
priority or disable one package without changing the host executable.

`NFLLM.EXE --list-plugins` lists loaded extension IDs and priorities. For the
legacy GGUF operator path, `--backend 0xPLUGIN_ID` requests a plugin for the
core F32 operations; `--backend auto` uses the configured priority. Other
operators, model formats and architectures are resolved by their extension ID
and compatible priority. The currently implemented CLI is intentionally small;
it is not a general benchmark or plugin manager UI.

## Build an independent plugin

The on-disk plugin filename uses `.NFD`; its loadable module follows CauseWay's
Open Watcom dynamic-library conventions: compile plugin code with
`-bd -s`, and link using `system cwdlls` for stack-based calls. The official
[CauseWay user guide](https://open-watcom.github.io/open-watcom-v2-wikidocs/cw.html#using-dlls-with-open-watcom-c-c)
documents this module format and calling convention. NFLLM's sample also uses `-3s` for the
386+stack calling convention and exports `_NFLLM_PluginQueryV1`.

Build the updated host once with `build-dos.cmd` to enable `.NFD` loading. Then
run `examples/nfdense/build.cmd`; set `WATCOM` to the Open Watcom installation.
It produces `NFDENSE.NFD` and `NFDENSE.NFM` in `build/PLUGINS` without relinking
`NFLLM.EXE`. Generate its 12-byte
fixture with `python examples/nfdense/make_model.py` and run:

```text
NFLLM.EXE TINY.NFD -p A -n 1
NFLLM.EXE TINY.NFD -p B -n 1
```

The first prompt emits `a`; the second emits `b`. `NFDENSE1` is deliberately a
custom format rather than GGUF. This tiny transition fixture proves that an
external format and architecture can reach the token-emission API; it is not a
production network or proof of broad model support.

## Package manifest

Ship one `.NFD` and one `.NFM` file with the same DOS 8.3 basename. The manifest
is UTF-8/ASCII `key=value`, one field per line:

```ini
plugin_id=0x4E464431
name=nfdense-toy-neural
version=1.0.0
abi_version=1
required_capabilities=0x00000003
priority=1000
entrypoint=_NFLLM_PluginQueryV1
```

The module's returned bundle must match the manifest's plugin ID, name, version
and capability requirements. Use unique plugin IDs. Operation IDs can be
shared by alternative implementations; extension IDs identify the public
format, architecture or descriptor. The loader checks the ABI and capability
requirements before registration.

## Install, update, disable and remove

Close NFLLM before replacing package files; modules are loaded for the lifetime
of each NFLLM process.

1. **Install:** copy the pair, for example `NFDENSE.NFD` and `NFDENSE.NFM`, to
   `PLUGINS` beside `NFLLM.EXE`.
2. **Update:** replace both files with the new package version, keeping the
   basename. The next NFLLM process loads the new manifest and `.NFD` module.
3. **Disable:** create an empty `PLUGINS\NFDENSE.OFF` file. Delete the sentinel
   to enable the package again.
4. **Remove:** delete `PLUGINS\NFDENSE.NFD` and `PLUGINS\NFDENSE.NFM` (and any
   `.OFF` sentinel).

These actions touch package files only. They do not modify, relink or rebuild
`NFLLM.EXE`. A package has no automatic dependency installer or digital
signature verification in this prototype; install only `.NFD` modules from
developers you trust. A loaded module runs in the same DOS process with the
host's memory and privileges.

## Current limits

- DOS 8.3 filenames and a local `PLUGINS` directory are required.
- ABI v1 is for the 32-bit CauseWay DOS runtime; Windows shared libraries and 64-bit host
  plugins are not compatible.
- The model view currently bounds tensor names, dimensions and file sizes using
  the fixed-width v1 structures in the header.
- There is no package dependency resolver, signature system, hot reload or GUI
  installer. Update/remove takes effect on the next process start.
- The external architecture path and 1.2 plugin lifecycle were tested in
  DOSBox-X with a generic 486 instruction profile and 96 MiB RAM. DOSBox-X is
  not a cycle-accurate AMD Am486DX4/100 MHz simulation.
- NFLLM 1.2 includes an independently authored neural graph compatibility
  subset for static TFLite v3 INT8 graphs and two external MCUNet VWW models. It does not claim full
  model-generator or architecture coverage.
- PDR is not a required dependency.

