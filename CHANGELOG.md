# Changelog

## 1.2.0 Experimental Research Release

- Publish the independently installable `.NFD` plugin runtime for 32-bit DOS.
- Add NFModelIO, NFGraphVM and NFInt8Ops for the validated static INT8 TFLite
  v3 operator subset, while keeping model weights and test frames out of the
  repository and release archives.
- Publish the runtime executable and complete source as separate release ZIPs
  with SHA-256 sidecars.
- Document the distributed graph-module hash difference from the validation
  build outputs and keep the cause marked as unknown.
- Keep the broader TinyEngine/MCUNet port goal open.

## 1.1 Experimental Research Release

- Publish the DOS/32A-based source and runtime as separate release assets.
- Retain the original license, acknowledgments and historical validation
  records in the v1.1 tag and release assets.

## 1.0 Experimental Research Release

- Add Stage 1–3 source, static modules, format decoders and documented QEMU ISA
  proxy experiments.
- Publish the NFLLM source separately from the DOS build artifacts.
- Keep model weights, DOS disk images, generated build outputs, and the DOS/4GW
  runtime out of the source archive and release ZIP.
- Declare the NFLLM-authored project source under MIT; preserve separate
  upstream and third-party notices.
- Record the source-reuse audit and physical-hardware limitations.

## 0.1 Beta — initial public source publication

- Publish the DOS-targeted scalar F32 GGUF inference prototype and compatibility
  probe.
- Publish `NFLLM.EXE` in the DOS `v0.1.0-beta` ZIP release.
- Document the verified NF-DOS/86Box guest configuration and its narrow model
  scope.
- Omit test weights, disk images, and the DOS/4GW runtime from the repository
  and release ZIP.
- Keep the full source-provenance review pending and preserve the upstream
  third-party notice.
