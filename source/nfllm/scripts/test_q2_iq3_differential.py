#!/usr/bin/env python3
"""Compare the DOS-plugin Q2/IQ3 decoders with the vendored GGUF reference."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
from pathlib import Path

import numpy as np
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WATCOM = ROOT.parents[2] / "NF-DOS-6.0" / "tools" / "watcom2026"
WATCOM = Path(os.environ.get("WATCOM", DEFAULT_WATCOM))
sys.path.insert(0, str(ROOT.parents[0] / "upstream-llama.cpp" / "gguf-py"))
import gguf  # noqa: E402


FORMATS = {
    "Q2_0": (gguf.GGMLQuantizationType.Q2_0, 64, 18),
    "Q2_K": (gguf.GGMLQuantizationType.Q2_K, 256, 84),
    "IQ3_XXS": (gguf.GGMLQuantizationType.IQ3_XXS, 256, 98),
    "IQ3_S": (gguf.GGMLQuantizationType.IQ3_S, 256, 110),
}


def compile_host(output: Path) -> None:
    env = os.environ.copy()
    env["WATCOM"] = str(WATCOM)
    env["PATH"] = str(WATCOM / "binnt64") + os.pathsep + env.get("PATH", "")
    env["INCLUDE"] = str(WATCOM / "h")
    env["LIB"] = str(WATCOM / "lib386")
    compiler = WATCOM / "binnt64" / "wcl386.exe"
    sources = [
        ROOT / "tests" / "quant_decoder_host.c",
        ROOT / "src" / "nfllm_registry.c",
        ROOT / "plugins" / "q2" / "q2.c",
        ROOT / "plugins" / "iq3" / "iq3.c",
    ]
    subprocess.run([str(compiler), "-q", "-bt=nt", "-3", "-fpi87", "-ot",
                    *map(str, sources), f"-fe={output}"], check=True, env=env)


def make_packed(name: str, rng: np.random.Generator) -> tuple[np.ndarray, int]:
    qtype, block_elements, block_bytes = FORMATS[name]
    columns = block_elements * 3
    raw = rng.integers(0, 256, size=(3, block_bytes), dtype=np.uint8)
    if name == "Q2_0":
        scales = rng.uniform(0.02, 1.0, size=3).astype(np.float16).view(np.uint16)
        raw[:, :2] = np.column_stack((scales & 0xff, scales >> 8)).astype(np.uint8)
    elif name == "Q2_K":
        d = rng.uniform(0.02, 1.0, size=3).astype(np.float16).view(np.uint16)
        dmin = rng.uniform(0.02, 0.5, size=3).astype(np.float16).view(np.uint16)
        raw[:, 80:82] = np.column_stack((d & 0xff, d >> 8)).astype(np.uint8)
        raw[:, 82:84] = np.column_stack((dmin & 0xff, dmin >> 8)).astype(np.uint8)
    else:
        scales = rng.uniform(0.02, 1.0, size=3).astype(np.float16).view(np.uint16)
        raw[:, :2] = np.column_stack((scales & 0xff, scales >> 8)).astype(np.uint8)
    return raw, columns


def main() -> int:
    evidence = ROOT / "build" / "logs" / "q2-iq3-differential"
    evidence.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="nfllm-q2-iq3-") as temporary:
        temp = Path(temporary)
        host = temp / "quant_decoder_host.exe"
        compile_host(host)
        rng = np.random.default_rng(20261114)
        for name, (qtype, block_elements, _block_bytes) in FORMATS.items():
            packed, columns = make_packed(name, rng)
            input_path = temp / f"{name}.packed"
            output_path = temp / f"{name}.f32"
            packed.tofile(input_path)
            subprocess.run([str(host), name, str(columns), str(input_path), str(output_path)],
                           check=True)
            actual = np.fromfile(output_path, dtype=np.float32)
            expected = gguf.quants.dequantize(packed, qtype).astype(np.float32, copy=False).reshape(-1)
            if actual.shape != expected.shape:
                raise AssertionError(f"{name}: shape mismatch {actual.shape} != {expected.shape}")
            np.testing.assert_allclose(actual, expected, rtol=2e-6, atol=2e-6)
            results.append({
                "format": name,
                "columns": columns,
                "blocks": columns // block_elements,
                "max_abs_error": float(np.max(np.abs(actual - expected))),
                "status": "PASS",
            })
    result_path = evidence / "summary.json"
    result_path.write_text(json.dumps({"seed": 20261114, "results": results}, indent=2) + "\n",
                           encoding="utf-8")
    for item in results:
        print(f"{item['format']}: PASS; columns={item['columns']}; max_abs_error={item['max_abs_error']:.8g}")
    print(f"Evidence: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
