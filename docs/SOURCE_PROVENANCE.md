# Source provenance

The project is a small DOS inference prototype for a narrow subset of GGUF; it
is **not a port of the full upstream `llama.cpp`**. The upstream reference
snapshot is stored separately at `source/upstream-llama.cpp/`, unchanged at
revision `b79696f1f556cb30b0c184838115007d929baca8`. The upstream `.git`
directory and model assets were omitted from the published source snapshot;
the source revision is recorded here so the reference can be identified.

The original NFLLM development history and a full source-reuse/adaptation
audit have not been completed. Some quantization lookup-table data is derived
from upstream and retains its attribution in the source. This release does
not claim that NFLLM is a fork, a complete port, or wholly independent of
upstream code.

NFLLM-authored project source is licensed under MIT in the repository root.
The upstream tree keeps its own `LICENSE` and third-party notices. Model
weights and the DOS/4GW runtime are not included; they have separate licensing
terms.

Before making stronger claims about the relationship to upstream, complete a
file-level source audit, confirm all required notices, and verify the
redistribution terms for every component intended for binary distribution.
