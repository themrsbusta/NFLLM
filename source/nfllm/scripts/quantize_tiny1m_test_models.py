#!/usr/bin/env python3
"""Create reproducible Q4_0/Q8_0 Tiny1M fixtures using the vendored GGUF tools.

The source model is treated as read-only. Quantized files are written beside it
through temporary paths and atomically moved into place after validation.
"""

from __future__ import annotations

import argparse
import gc
import os
import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = ROOT.parents[1]
GGUF_PY = ROOT.parents[0] / "upstream-llama.cpp" / "gguf-py"
MODEL_DIR = PROJECT_ROOT / "models"
SOURCE = MODEL_DIR / "tiny1m.F32.gguf"

sys.path.insert(0, str(GGUF_PY))

import gguf  # noqa: E402


FIXTURES = (
    ("Q8_0", gguf.GGMLQuantizationType.Q8_0, gguf.LlamaFileType.MOSTLY_Q8_0),
    ("Q4_0", gguf.GGMLQuantizationType.Q4_0, gguf.LlamaFileType.MOSTLY_Q4_0),
)


def copy_metadata(reader: gguf.GGUFReader, writer: gguf.GGUFWriter, file_type: int) -> None:
    for key, field in reader.fields.items():
        # GGUFWriter(path, arch) inserts general.architecture itself.
        if key.startswith("GGUF.") or key == "general.architecture":
            continue
        value = field.contents()
        value_type = field.types[0]
        subtype = field.types[1] if value_type is gguf.GGUFValueType.ARRAY else None
        if key == "general.file_type":
            value = int(file_type)
        writer.add_key_value(key, value, value_type, subtype)


def create_fixture(name: str, qtype: gguf.GGMLQuantizationType, file_type: int, force: bool) -> Path:
    output = MODEL_DIR / f"tiny1m.{name}.gguf"
    temporary = output.with_suffix(output.suffix + ".partial")
    if output.exists() and not force:
        raise FileExistsError(f"Refusing to replace existing fixture: {output}")
    if temporary.exists():
        raise FileExistsError(f"Refusing to replace an existing partial file: {temporary}")

    reader = gguf.GGUFReader(SOURCE)
    writer = gguf.GGUFWriter(temporary, "llama")
    converted = 0
    try:
        copy_metadata(reader, writer, file_type)
        for tensor in reader.tensors:
            data = np.asarray(tensor.data)
            if tensor.tensor_type is gguf.GGMLQuantizationType.F32 and data.ndim == 2:
                columns = data.shape[-1]
                if columns % 32 != 0:
                    raise ValueError(f"{tensor.name}: row width {columns} is not divisible by 32")
                packed = gguf.quants.quantize(data.astype(np.float32, copy=False), qtype)
                # raw_shape uses the NumPy row-major order. The writer converts
                # quantized byte rows back to logical element counts itself.
                writer.add_tensor(tensor.name, packed, raw_shape=tuple(packed.shape), raw_dtype=qtype)
                converted += 1
            elif tensor.tensor_type is gguf.GGMLQuantizationType.F32:
                writer.add_tensor(tensor.name, data.astype(np.float32, copy=False), raw_shape=tuple(data.shape))
            else:
                raise ValueError(f"Unexpected source tensor type for {tensor.name}: {tensor.tensor_type}")

        writer.write_header_to_file()
        writer.write_kv_data_to_file()
        writer.write_tensors_to_file()
    except Exception:
        writer.close()
        temporary.unlink(missing_ok=True)
        raise
    else:
        writer.close()

    if converted == 0:
        temporary.unlink(missing_ok=True)
        raise ValueError("No F32 matrix tensors were quantized")

    check = gguf.GGUFReader(temporary)
    counts = {"F32": 0, name: 0}
    for tensor in check.tensors:
        tensor_type = tensor.tensor_type.name
        if tensor_type not in counts:
            temporary.unlink(missing_ok=True)
            raise ValueError(f"Unexpected output tensor type {tensor_type} in {tensor.name}")
        counts[tensor_type] += 1
        source_tensor = next(item for item in reader.tensors if item.name == tensor.name)
        if tuple(int(d) for d in tensor.shape) != tuple(int(d) for d in source_tensor.shape):
            temporary.unlink(missing_ok=True)
            raise ValueError(f"Shape mismatch for {tensor.name}")
    if counts[name] != converted or counts["F32"] + counts[name] != len(reader.tensors):
        temporary.unlink(missing_ok=True)
        raise ValueError(f"Output tensor counts are inconsistent: {counts}")

    # Windows will not rename a file while its NumPy memmap is still open.
    del tensor
    del check
    gc.collect()
    os.replace(temporary, output)
    print(f"{output.name}: {output.stat().st_size} bytes; tensors={counts}; quantized_matrices={converted}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="replace existing output fixtures")
    args = parser.parse_args()
    if not SOURCE.is_file():
        raise FileNotFoundError(f"Missing source model: {SOURCE}")
    for name, qtype, file_type in FIXTURES:
        create_fixture(name, qtype, file_type, args.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
