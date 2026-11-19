# NFLLM

**1.2.0 Experimental Research Release** is a single-threaded inference runtime
for 32-bit DOS with independently loadable `.NFD` plugins.

The default `main` branch and GitHub **Code → Download ZIP** contain the current
NFLLM 1.2 source under [`source/nfllm-1.2/`](source/nfllm-1.2/). The DOS
executable is available separately from the release assets.

## Downloads

- [DOS runtime ZIP](https://github.com/themrsbusta/NFLLM/releases/download/v1.2.0/NFLLM-1.2.0-Experimental-Research-Release-DOS.zip)
- [Complete source ZIP](https://github.com/themrsbusta/NFLLM/releases/download/v1.2.0/NFLLM-1.2.0-Experimental-Research-Release-Source.zip)
- [Runtime SHA-256](https://github.com/themrsbusta/NFLLM/releases/download/v1.2.0/NFLLM-1.2.0-Experimental-Research-Release-DOS.zip.sha256)
- [Source SHA-256](https://github.com/themrsbusta/NFLLM/releases/download/v1.2.0/NFLLM-1.2.0-Experimental-Research-Release-Source.zip.sha256)
- [GitHub Code ZIP for `main`](https://github.com/themrsbusta/NFLLM/archive/refs/heads/main.zip)
- [Release page and previous releases](https://github.com/themrsbusta/NFLLM/releases)

The repository ZIP contains the current source tree. Use the DOS runtime ZIP
for the prebuilt executable and plugin packages. Neither download includes
model weights, test frames, or DOS disk images.

## Build from source

Build on Windows with Open Watcom 2.0 and CauseWay support. Set `WATCOM` to
your Open Watcom installation, then run the source build script from the
repository root:

```bat
set WATCOM=C:\WATCOM
cd /d source\nfllm-1.2
build-dos.cmd
```

The script creates `build\NFLLM.EXE` and the base plugin modules under
`build\PLUGINS\`. The independently built graph modules can be added with
`plugins\build-nfllm-extensions.cmd` after the host build. The prebuilt runtime
ZIP contains ten modules and omits the experimental SSE2 module.

## Included runtime scope

The DOS release includes ten plugin packages: `DNOW`, `IQ3`, `MMX`, `NFGRAPH`,
`NFINT8OP`, `NFMODEL`, `Q2`, `Q4_0`, `Q8_0`, and `RF_X87`. The graph plugins
support a static INT8 TFLite v3 subset with `PAD`, `CONV_2D`,
`DEPTHWISE_CONV_2D`, `ADD`, `AVERAGE_POOL_2D`, and `RESHAPE`. GGUF format and
operator coverage is also limited; see the [NFLLM 1.2 documentation](source/nfllm-1.2/docs/).

This experimental release is not the complete TinyEngine or MCUNet port.
Plugin modules run in-process and are not sandboxed. The source includes an
SSE2 implementation, but the DOS runtime ZIP omits an SSE2 package because
safe XMM-state handling under DOS/DPMI has not been established.

The three distributed graph-module hashes differ from the hashes of the
validation-build outputs by eight bytes each. The [validation report](source/nfllm-1.2/docs/validation/plugin-sdk-1.2.json)
records both sets; the reason for the difference is unknown.

## Release history

- [1.2.0 Experimental Research Release](https://github.com/themrsbusta/NFLLM/releases/tag/v1.2.0)
- [1.1 Experimental Research Release](https://github.com/themrsbusta/NFLLM/releases/tag/v1.1)
- [1.0 Experimental Research Release](https://github.com/themrsbusta/NFLLM/releases/tag/v1.0)
- [DOS 0.1 Beta](https://github.com/themrsbusta/NFLLM/releases/tag/v0.1.0-beta)

Previous release tags and assets remain available. NFLLM-authored source is
under MIT; see [`LICENSE`](LICENSE) and the
[third-party notices](source/nfllm-1.2/THIRD-PARTY-NOTICES.md).
