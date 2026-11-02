#!/usr/bin/env python3
"""Create the installable .hmod zip for Horizon's old WASM loader."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "zoom-wasm-e7614d2.zip"
FILES = ["horizon-mod.toml", "mod.wasm"]

for name in FILES:
    if not (ROOT / name).is_file():
        raise SystemExit(f"missing deliverable: {name}")

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in FILES:
        z.write(ROOT / name, arcname=name)

with zipfile.ZipFile(OUT) as z:
    names = z.namelist()
    if names != FILES:
        raise SystemExit(f"unexpected zip layout: {names}")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")
    for info in z.infolist():
        print(f"  {info.filename}: {info.file_size} bytes")
