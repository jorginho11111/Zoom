import { readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  print,
  parse,
  componentEmbed,
  componentNew,
  componentWit,
} from '@bytecodealliance/jco-transpile/wasm-tools';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const corePath = resolve(root, 'zig/zoom.core.tmp.wasm');
const witPath = resolve(root, 'wit/zoom.wit');
const outPath = resolve(root, 'mod.wasm');

const core = await readFile(corePath);
const wat = await print(core);
const renamed = wat.replace(
  '(export "memory" (memory 0))',
  '(export "cm32p2_memory" (memory 0))',
);
if (renamed === wat) throw new Error('memory export not found in core module');
const renamedCore = await parse(renamed);

const embedded = await componentEmbed({
  binary: renamedCore,
  witPath,
  world: 'zoom-mod',
  stringEncoding: 'utf8',
  dummy: false,
});
const component = await componentNew(embedded, []);
await writeFile(outPath, component);

const wit = await componentWit(component);
console.log(`wrote ${outPath} (${component.length} bytes)`);
console.log(wit.trim());
