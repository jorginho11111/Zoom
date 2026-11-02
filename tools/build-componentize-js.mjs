import { readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { componentize } from '@bytecodealliance/componentize-js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const witWorld = await readFile(resolve(root, 'wit/zoom.wit'), 'utf8');
const sourcePath = resolve(root, 'js/zoom.mjs');

// `debug: {}` avoids componentize-js 0.23.0's uninitialized debugOutput bug.
// All WASI features are disabled: this is a pure Horizon client component.
const { component } = await componentize({
  sourcePath,
  witWorld,
  disableFeatures: ['stdio', 'random', 'clocks', 'http'],
  debug: {},
});
await writeFile(resolve(root, 'mod.wasm'), component);
console.log(`wrote ${resolve(root, 'mod.wasm')} (${component.length} bytes)`);
