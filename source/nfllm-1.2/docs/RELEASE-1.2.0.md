# NFLLM 1.2.0 — Experimental Research Release

Release tag: `v1.2.0`; release type: regular GitHub release (not a prerelease).

NFLLM 1.2 introduces an independently installable `.NFD` plugin platform for
32-bit DOS. The host loads plugin modules from `PLUGINS`; each package pairs an
`.NFD` module with an `.NFM` manifest. New modules can be installed, updated,
disabled or removed without rebuilding `NFLLM.EXE`.

## Release assets

- `NFLLM-1.2.0-Experimental-Research-Release-DOS.zip`: runtime executable,
  ten plugin packages, SDK headers and example, documentation, validation
  records, and required license notices.
- `NFLLM-1.2.0-Experimental-Research-Release-Source.zip`: complete standalone
  NFLLM 1.2 source tree, scripts, tests, documentation, licenses and
  provenance. It does not require the NFLLM 1.1 tree.
- Each ZIP has a `.sha256` sidecar.

The runtime ZIP contains these ten modules: `DNOW.NFD`, `IQ3.NFD`, `MMX.NFD`,
`NFGRAPH.NFD`, `NFINT8OP.NFD`, `NFMODEL.NFD`, `Q2.NFD`, `Q4_0.NFD`, `Q8_0.NFD`
and `RF_X87.NFD`. The three graph packages provide the following functionality:

- `NFMODEL.NFD` (`NFModelIO`) reads the supported static TFLite v3 format
  subset.
- `NFGRAPH.NFD` (`NFGraphVM`) executes supported INT8 graphs and plans the
  activation arena.
- `NFINT8OP.NFD` (`NFInt8Ops`) supplies six scalar INT8 operators.

The source tree also contains an SSE2 plugin implementation. An SSE2 runtime
package is omitted because safe XMM-state handling under DOS/DPMI has not been
established.

## Verification and artifact identity

The validation report records successful clean-source builds and DOSBox-X
checks. Its three graph-module hashes identify the validation-build outputs;
they do not match the corresponding modules in the DOS release ZIP. Each
distributed graph module is eight bytes larger. The exact distributed hashes
are listed in `docs/validation/plugin-sdk-1.2.json`. The supplied handoff also
reports that the graph packages loaded and the VWW0/VWW1 logits matched LiteRT,
but it does not tie those results to the distributed module hashes. The cause
of the size and hash difference is not confirmed, so the validation-build
hashes must not be treated as hashes of the release modules.

The emulator checks use a functional 486 profile, not cycle-accurate AMD
Am486DX4 emulation.

## Scope and limitations

This is an experimental research release of the NFLLM plugin architecture and
the tested model/operator subset. It is not the complete TinyEngine or MCUNet
port. The demonstrated graph path supports static INT8 graphs using `PAD`,
`CONV_2D`, `DEPTHWISE_CONV_2D`, `ADD`, `AVERAGE_POOL_2D` and `RESHAPE`.
Additional operators, architectures, model formats and quantization forms
remain future plugin work. No model weights, test frames or disk images are
included. Plugin modules run in-process; this release does not provide plugin
sandboxing, signature verification or dependency resolution. PDR is not a
dependency.

The complete TinyEngine/MCUNet support goal remains open until its broader
required coverage is implemented and validated; this release does not mark
that goal complete.

## Licensing

NFLLM-authored source is distributed under MIT; see `LICENSE`. The DOS runtime
also includes third-party IQ3-data attribution and license information, plus
the Open Watcom Public License 1.0 and provenance for linked runtime
components. The Open Watcom source is available at
<https://github.com/open-watcom/open-watcom-v2>. Preserve the included license
and notices when redistributing the runtime ZIP. The source package also
contains historical DOS/32A source and its required acknowledgment; DOS/32A is
not included in the NFLLM 1.2 runtime ZIP.
