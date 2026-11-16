#!/usr/bin/env python3
"""Build padded Tiny1M fixtures for Q2_0, Q2_K, IQ3_XXS and IQ3_S tests.

The original model is read-only. The derived 256-wide F32 baseline and four
quantized fixtures are written atomically beside the source model.
"""

from __future__ import annotations

import argparse
import gc
import os
import subprocess
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
GGUF_PY = ROOT / "upstream" / "llama.cpp" / "gguf-py"
SOURCE = ROOT / "models" / "tiny1m.F32.gguf"
QUANTIZER = ROOT.parent / "tools" / "llama.cpp" / "b11946-vulkan" / "llama-quantize.exe"
IMATRIX_TOOL = ROOT.parent / "tools" / "llama.cpp" / "b11946-vulkan" / "llama-imatrix.exe"
IMATRIX_PATH = ROOT / "build" / "logs" / "tiny1m-256-iq3-imatrix.gguf"
IMATRIX_LOG = ROOT / "build" / "logs" / "tiny1m-256-iq3-imatrix.log"
IMATRIX_CORPUS = ROOT / "build" / "logs" / "tiny1m-256-iq3-imatrix.txt"
sys.path.insert(0, str(GGUF_PY))

import gguf  # noqa: E402


FORMATS = (
    ("Q2_0", gguf.GGMLQuantizationType.Q2_0, gguf.LlamaFileType.MOSTLY_Q2_0, 64),
    ("Q2_K", gguf.GGMLQuantizationType.Q2_K, gguf.LlamaFileType.MOSTLY_Q2_K, 256),
    ("IQ3_XXS", gguf.GGMLQuantizationType.IQ3_XXS, gguf.LlamaFileType.MOSTLY_IQ3_XXS, 256),
    ("IQ3_S", gguf.GGMLQuantizationType.IQ3_S, gguf.LlamaFileType.MOSTLY_IQ3_S, 256),
)
METADATA_UPDATES = {
    "general.file_type": int(gguf.LlamaFileType.ALL_F32),
    "llama.embedding_length": 256,
    "llama.feed_forward_length": 512,
    "llama.attention.head_count": 4,
    "llama.attention.head_count_kv": 4,
}


def copy_metadata(reader: gguf.GGUFReader, writer: gguf.GGUFWriter,
                  updates: dict[str, int]) -> None:
    for key, field in reader.fields.items():
        if key.startswith("GGUF.") or key == "general.architecture":
            continue
        value = updates.get(key, field.contents())
        value_type = field.types[0]
        subtype = field.types[1] if value_type is gguf.GGUFValueType.ARRAY else None
        writer.add_key_value(key, value, value_type, subtype)


def padded_tensor(name: str, source: np.ndarray) -> np.ndarray:
    data = np.asarray(source, dtype=np.float32)
    if data.ndim == 1 and data.shape[0] == 128:
        result = np.ones(256, dtype=np.float32)
        result[:128] = data
        return result
    if data.ndim != 2:
        return data.copy()

    if name in ("output.weight", "token_embd.weight"):
        rows, columns = 512, 256
    elif name.endswith((".attn_q.weight", ".attn_k.weight", ".attn_v.weight", ".attn_output.weight")):
        rows, columns = 256, 256
    elif name.endswith((".ffn_gate.weight", ".ffn_up.weight")):
        rows, columns = 512, 256
    elif name.endswith(".ffn_down.weight"):
        rows, columns = 256, 512
    else:
        raise ValueError(f"No target shape rule for matrix {name}: {data.shape}")

    if data.shape[0] > rows or data.shape[1] > columns:
        raise ValueError(f"Cannot pad {name} from {data.shape} to {(rows, columns)}")
    result = np.zeros((rows, columns), dtype=np.float32)
    result[:data.shape[0], :data.shape[1]] = data
    return result


def write_model(reader: gguf.GGUFReader, output: Path, file_type: int,
                force: bool) -> None:
    temporary = output.with_suffix(output.suffix + ".partial")
    if output.exists() and not force:
        raise FileExistsError(f"Refusing to replace existing fixture: {output}")
    if temporary.exists():
        raise FileExistsError(f"Refusing to replace existing partial file: {temporary}")

    writer = gguf.GGUFWriter(temporary, "llama")
    try:
        updates = dict(METADATA_UPDATES)
        updates["general.file_type"] = int(file_type)
        copy_metadata(reader, writer, updates)
        for tensor in reader.tensors:
            data = padded_tensor(tensor.name, tensor.data)
            writer.add_tensor(tensor.name, data, raw_shape=tuple(data.shape))
        writer.write_header_to_file()
        writer.write_kv_data_to_file()
        writer.write_tensors_to_file()
    except Exception:
        writer.close()
        temporary.unlink(missing_ok=True)
        raise
    else:
        writer.close()

    check = gguf.GGUFReader(temporary)
    counts = {"F32": 0}
    for tensor in check.tensors:
        type_name = tensor.tensor_type.name
        if type_name != "F32":
            temporary.unlink(missing_ok=True)
            raise ValueError(f"Unexpected tensor type {type_name} in {tensor.name}")
        counts[type_name] += 1
        expected_shape = tuple(int(value) for value in tensor.shape)
        if len(expected_shape) == 2 and expected_shape[0] not in (256, 512):
            temporary.unlink(missing_ok=True)
            raise ValueError(f"Unexpected padded shape for {tensor.name}: {expected_shape}")
    del tensor
    del check
    gc.collect()
    os.replace(temporary, output)
    print(f"{output.name}: {output.stat().st_size} bytes; tensors={counts}")


def quantize_fixture(source: Path, name: str,
                     qtype: gguf.GGMLQuantizationType,
                     file_type: int, imatrix: Path | None,
                     force: bool) -> None:
    output = ROOT / "models" / f"tiny1m-256.{name}.gguf"
    temporary = output.with_suffix(output.suffix + ".partial")
    if output.exists() and not force:
        raise FileExistsError(f"Refusing to replace existing fixture: {output}")
    if temporary.exists():
        raise FileExistsError(f"Refusing to replace existing partial file: {temporary}")
    if not QUANTIZER.is_file():
        raise FileNotFoundError(f"Missing native llama.cpp quantizer: {QUANTIZER}")
    try:
        command = [str(QUANTIZER), "--pure"]
        if imatrix is not None:
            command.extend(["--imatrix", str(imatrix)])
        command.extend([str(source), str(temporary), name, "1"])
        with (ROOT / "build" / "logs" / f"tiny1m-256-{name}-quantize.log").open("w", encoding="utf-8") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        check = gguf.GGUFReader(temporary)
        counts: dict[str, int] = {}
        for tensor in check.tensors:
            tensor_type = tensor.tensor_type.name
            counts[tensor_type] = counts.get(tensor_type, 0) + 1
            if tensor_type not in ("F32", name):
                raise ValueError(f"Unexpected tensor type {tensor_type} in {tensor.name}")
            shape = tuple(int(value) for value in tensor.shape)
            if len(shape) == 2 and shape[0] not in (256, 512):
                raise ValueError(f"Unexpected padded shape for {tensor.name}: {shape}")
        if counts.get(name, 0) == 0 or counts.get("F32", 0) == 0:
            raise ValueError(f"Quantized fixture has incomplete tensor coverage: {counts}")
        general_type = int(check.fields["general.file_type"].contents())
        if general_type != int(file_type):
            raise ValueError(f"Unexpected general.file_type={general_type} for {name}")
        del tensor
        del check
        gc.collect()
        os.replace(temporary, output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    print(f"{output.name}: {output.stat().st_size} bytes; tensors={counts}")


def build_iq3_imatrix(source: Path, force: bool) -> Path:
    temporary = IMATRIX_PATH.with_suffix(IMATRIX_PATH.suffix + ".partial")
    story = (
        "Once upon a time, a little fox found a blue key beside a quiet river. "
        "She carried it to the old mill, where a kind baker opened a small wooden door. "
        "Inside, a family of mice had saved seeds for the winter. The fox shared bread, "
        "the mice shared seeds, and everyone planted a garden before the first snow. "
        "In spring, green leaves covered the hill and the river sang beneath the bridge. "
    )
    if not IMATRIX_TOOL.is_file():
        raise FileNotFoundError(f"Missing native llama.cpp importance-matrix tool: {IMATRIX_TOOL}")
    IMATRIX_PATH.parent.mkdir(parents=True, exist_ok=True)
    if IMATRIX_PATH.exists() and not force:
        raise FileExistsError(f"Refusing to replace existing imatrix: {IMATRIX_PATH}")
    if temporary.exists():
        raise FileExistsError(f"Refusing to replace existing partial imatrix: {temporary}")
    IMATRIX_CORPUS.write_text(story * 20, encoding="utf-8")
    command = [str(IMATRIX_TOOL), "-m", str(source), "-f", str(IMATRIX_CORPUS),
               "-o", str(temporary), "--output-format", "gguf", "--no-ppl",
               "-t", "1", "-tb", "1", "-ngl", "0"]
    try:
        with IMATRIX_LOG.open("w", encoding="utf-8") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        data = temporary.read_bytes()
        if len(data) < 8 or data[:4] != b"GGUF":
            raise ValueError("Generated importance matrix is not a GGUF file")
        os.replace(temporary, IMATRIX_PATH)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    print(f"IQ3 importance matrix: {IMATRIX_PATH.stat().st_size} bytes")
    return IMATRIX_PATH


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="replace existing output fixtures")
    args = parser.parse_args()
    if not SOURCE.is_file():
        raise FileNotFoundError(f"Missing source model: {SOURCE}")

    reader = gguf.GGUFReader(SOURCE)
    baseline = ROOT / "models" / "tiny1m-256.F32.gguf"
    write_model(reader, baseline, int(gguf.LlamaFileType.ALL_F32), args.force)
    imatrix = build_iq3_imatrix(baseline, args.force)
    for name, qtype, file_type, _block_elements in FORMATS:
        quantize_fixture(baseline, name, qtype, int(file_type),
                         imatrix if name.startswith("IQ3_") else None, args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
