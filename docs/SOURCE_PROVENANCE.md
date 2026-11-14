# Source provenance

The NFLLM handoff identifies `ggml-org/llama.cpp` as the reference project and
describes the current code as a small scalar F32 backend for a narrow GGUF
subset. It also says the upstream checkout was not modified and that a full
source-reuse/adaptation audit has not been completed.

The handoff snapshot supplied for this initial publication contained no
`.git` metadata and excluded the upstream checkout. It therefore does not
provide the source history or exact upstream revision needed to identify every
copied or adapted line. This repository makes no claim that NFLLM is a fork, a
complete port, or wholly independent of upstream code.

NFLLM-authored source is published under the MIT License in the repository
root. The upstream project also publishes an MIT license; its current license
text is preserved under `third_party/licenses/llama.cpp-MIT.txt` for separate
attribution and notice review. The source-reuse audit remains pending.

Before treating the provenance review as complete or making stronger claims
about the relationship to upstream:

1. Recover and review the original NFLLM development history and upstream
   revision used as reference.
2. Identify any copied or adapted source and retain the required notices.
3. Confirm redistribution terms for DOS/4GW and every other bundled component.
4. Confirm that the source license and notices cover all authored and reused
   material.

Model weights are excluded from this repository. Their licenses are separate
from the NFLLM source license.
