#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ ! -d node_modules/@bytecodealliance/jco-transpile ]; then
  npm install --silent
fi
ZIG="${ZIG:-$ROOT/.arena/zig/ziglang/zig}"
if [ ! -x "$ZIG" ]; then
  python3 -m pip install --quiet --target "$ROOT/.arena/zig" ziglang==0.17.0
fi

# Freestanding WASM core, no WASI, with a 64 KiB stack below the data/heap.
"$ZIG" cc \
  --target=wasm32-freestanding -std=c11 -nostdlib -ffreestanding -fno-builtin \
  -O1 -g0 \
  -Wl,--no-entry \
  -Wl,--export-memory \
  -Wl,--initial-memory=2097152 \
  -Wl,--global-base=1048576,-z,stack-size=65536 \
  zig/zoom.c -o zig/zoom.core.tmp.wasm

node tools/componentize-core.mjs
node tools/verify.mjs mod.wasm
python3 tools/package.py
