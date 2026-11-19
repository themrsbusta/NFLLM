from pathlib import Path


OUTPUT = Path(__file__).with_name("TINY.NFD")
OUTPUT.write_bytes(b"NFDENSE1" + bytes((3, 0, 0, 3)))
print(f"Wrote {OUTPUT} (12-byte NFDENSE1 model)")
