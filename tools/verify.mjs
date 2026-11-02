import { readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { print, componentWit } from '@bytecodealliance/jco-transpile/wasm-tools';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const componentPath = resolve(process.argv[2] ?? resolve(root, 'mod.wasm'));
const bytes = await readFile(componentPath);
const wit = await componentWit(bytes);
const wat = await print(bytes);
const apiSource = await readFile(resolve(root, 'reference/e7614d2/api_level.rs'), 'utf8');

const levelMatch = apiSource.match(/pub const LEVEL: u32 = (\d+);/);
if (!levelMatch) throw new Error('api_level.rs: LEVEL not found');
const apiLevel = Number(levelMatch[1]);
const levels = new Map(
  [...apiSource.matchAll(/\("([^"]+)",\s*(\d+)\)/g)]
    .map(([, name, level]) => [name, Number(level)]),
);

const requiredImports = ['input', 'render', 'storage', 'ui'];
const requiredExports = ['events', 'ui-events'];
for (const name of requiredImports) {
  if (!wit.includes(`import horizon:mods/${name}@0.1.0;`)) {
    throw new Error(`missing import horizon:mods/${name}@0.1.0`);
  }
}
for (const name of requiredExports) {
  if (!wit.includes(`export horizon:mods/${name}@0.1.0;`)) {
    throw new Error(`missing export horizon:mods/${name}@0.1.0`);
  }
}
if (/import\s+wasi:/.test(wit) || /^\s*\(import "wasi:/m.test(wat)) {
  throw new Error('component unexpectedly imports WASI');
}

const importRe = /^\s*\(import "(horizon:mods\/[^"@]+@0\.1\.0)".*\(type (\$[^)]+)\)\)/gm;
const imports = [...wat.matchAll(importRe)].map(([, name, typeId]) => ({ name, typeId }));
if (!imports.length) throw new Error('no horizon:mods imports found in component WAT');

function typeBlock(typeId) {
  const marker = `(type ${typeId}`;
  const start = wat.indexOf(marker);
  if (start < 0) return '';
  let depth = 0;
  for (let i = start; i < wat.length; i++) {
    const ch = wat[i];
    const next = wat[i + 1];
    if (ch === '(' && next === ';') {
      const end = wat.indexOf(';)', i + 2);
      if (end < 0) return '';
      i = end + 1;
      continue;
    }
    if (ch === ';' && next === ';') {
      while (i < wat.length && wat[i] !== '\n') i++;
      continue;
    }
    if (ch === '"') {
      i++;
      while (i < wat.length) {
        if (wat[i] === '\\') { i += 2; continue; }
        if (wat[i] === '"') break;
        i++;
      }
      continue;
    }
    if (ch === '(') depth++;
    else if (ch === ')') {
      depth--;
      if (depth === 0) return wat.slice(start, i + 1);
    }
  }
  return '';
}

const functions = [];
for (const { name, typeId } of imports) {
  const block = typeBlock(typeId);
  if (!block) throw new Error(`could not resolve type ${typeId}`);
  const iface = name.split('/')[1].split('@')[0];
  // Component type exports carry an optional `(;n;)` index before the name,
  // e.g. `(export (;0;) "register-key" (func (type 7)))`.
  for (const [, fn] of block.matchAll(/\(export[^\"]*\"([^\"]+)\"\s+\(func/g)) {
    functions.push(`${iface}.${fn}`);
  }
}
if (!functions.length) throw new Error('no interface functions found');
let needed = 0;
for (const fn of functions) {
  const level = levels.has(fn) ? levels.get(fn) : apiLevel + 1;
  needed = Math.max(needed, level);
}
if (needed > apiLevel) throw new Error(`needed API level ${needed} exceeds host level ${apiLevel}`);
if (!wit.includes('horizon:mods/') || !wit.includes('@0.1.0')) {
  throw new Error('WIT does not use horizon:mods@0.1.0 interfaces');
}

const report = {
  component: componentPath,
  package: 'horizon:mods@0.1.0',
  imports: [...new Set(imports.map(i => i.name))].sort(),
  functions: functions.sort(),
  hostApiLevel: apiLevel,
  neededApiLevel: needed,
  exports: requiredExports,
  wasi: false,
};
console.log(JSON.stringify(report, null, 2));
