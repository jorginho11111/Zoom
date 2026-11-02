#!/usr/bin/env bash
# Canonical Horizon e7614d2 build path. Requires a real Rust toolchain.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

rustup target add wasm32-unknown-unknown
cargo build --lib --target wasm32-unknown-unknown \
  --message-format=json-render-diagnostics --release
CORE="$ROOT/target/wasm32-unknown-unknown/release/zoom.wasm"
cargo run --release -p componentize -- "$CORE" "$ROOT/mod.wasm"
node "$ROOT/tools/verify.mjs" "$ROOT/mod.wasm"
python3 "$ROOT/tools/package.py"
