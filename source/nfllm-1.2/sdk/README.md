# NFLLM Plugin SDK

This directory is the public plugin-author surface for NFLLM 1.2.

- Include `include/nfllm_plugin_sdk.h`; it includes `nfllm_abi.h`.
- See [`../docs/PLUGIN-SDK.md`](../docs/PLUGIN-SDK.md) for ABI rules, the
  manifest format and install/update/disable/remove steps.
- Build and inspect the standalone example in [`../examples/nfdense/`](../examples/nfdense/).

The SDK is source-level and targets 32-bit DOS CauseWay DLLs built with Open
Watcom. The host and plugin share a versioned C ABI. C++ objects, exceptions,
allocator-owned containers and compiler-specific class layouts must not cross
the boundary.

