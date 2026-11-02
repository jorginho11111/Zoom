import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const out = resolve(root, '.arena/mock');
execFileSync(
  resolve(root, 'node_modules/.bin/jco'),
  ['transpile', resolve(root, 'mod.wasm'), '-o', out,
   '--no-typescript', '--no-wasi-shim', '-I', 'sync', '-q'],
  { cwd: root, stdio: 'inherit' },
);
const { instantiate } = await import(pathToFileURL(resolve(out, 'mod.js')).href);
const modules = new Map([
  ['mod.core.wasm', new WebAssembly.Module(readFileSync(resolve(out, 'mod.core.wasm')))],
  ['mod.core2.wasm', new WebAssembly.Module(readFileSync(resolve(out, 'mod.core2.wasm')))],
  ['mod.core3.wasm', new WebAssembly.Module(readFileSync(resolve(out, 'mod.core3.wasm')))],
]);
const getCoreModule = name => {
  const m = modules.get(name);
  if (!m) throw new Error(`unknown core module ${name}`);
  return m;
};

const store = new Map();
const calls = { registerKey: [], claimWheel: [], poll: 0, setZoom: [], setSettings: [], set: [] };
let pending = [];
const dec = new TextDecoder();
const specificImports = {
  'horizon:mods/input': {
    registerKey: (name, label, key) => { calls.registerKey.push({ name, label, key }); },
    poll: () => { calls.poll++; const value = pending; pending = []; return value; },
    claimWheel: on => { calls.claimWheel.push(on); },
  },
  'horizon:mods/render': {
    setZoom: z => { calls.setZoom.push({ factor: +z.factor.toFixed(6), smooth: z.smooth, turn: +z.turn.toFixed(6) }); },
  },
  'horizon:mods/storage': {
    get: k => store.get(k),
    set: (k, v) => {
      const bytes = new Uint8Array(v);
      store.set(k, bytes);
      calls.set.push([k, dec.decode(bytes)]);
    },
  },
  'horizon:mods/ui': { setSettings: widgets => { calls.setSettings.push(widgets); } },
};
// A full SDK world may declare interfaces this mod never calls. Give them
// no-op functions so both the trimmed C component and the Rust SDK component
// instantiate with the same mock host.
const genericImport = new Proxy({}, { get: () => () => undefined });
const imports = new Proxy({}, {
  get: (_, name) => {
    if (typeof name !== 'string' || !name.includes('horizon:mods/')) return undefined;
    const key = name.replace(/@0\.1\.0$/, '');
    return specificImports[key] ?? genericImport;
  },
});
const { events, uiEvents } = await instantiate(getCoreModule, imports);
const results = [];
const check = (label, actual, expected) => {
  const a = JSON.stringify(actual), e = JSON.stringify(expected);
  results.push({ label, ok: a === e, actual, expected });
};

check('init registration', events.init(), { commands: ['zoom'], tick: false, frame: true, chat: false });
check('register default key', calls.registerKey.at(-1), { name: 'zoom', label: 'Zoom key', key: 'key.keyboard.z' });
check('initial settings widgets', calls.setSettings[0]?.length, 5);
check('initial settings text', calls.setSettings[0]?.[0]?.tag, 'text');
check('initial settings buttons', calls.setSettings[0]?.slice(1).map(w => [
  w.tag, w.val.id, typeof w.val.label === 'string' && w.val.label.length > 0,
]), [
  ['button', 2, true], ['button', 3, true],
  ['button', 4, true], ['button', 5, true],
]);
uiEvents.settingsOpened(); events.tick(1n); events.chat('hello'); uiEvents.closed();

pending = [{ tag: 'key-down', val: 'zoom' }]; events.frame(0.016);
check('key-down zoom', calls.setZoom.at(-1), { factor: 3, smooth: true, turn: 0.333333 });
check('key-down wheel claim', calls.claimWheel.at(-1), true);
pending = [{ tag: 'wheel', val: 1 }]; events.frame(0.016);
check('wheel up zoom', calls.setZoom.at(-1), { factor: 3.75, smooth: true, turn: 0.266667 });
pending = [{ tag: 'wheel', val: -1 }]; events.frame(0.016);
check('wheel down zoom', calls.setZoom.at(-1), { factor: 3, smooth: true, turn: 0.333333 });
pending = [{ tag: 'key-up', val: 'zoom' }]; events.frame(0.016);
check('release zoom', calls.setZoom.at(-1), { factor: 1, smooth: true, turn: 1 });
check('release wheel claim', calls.claimWheel.at(-1), false);
check('wheel claimed true then false', calls.claimWheel.slice(-2), [true, false]);

check('command status', events.command('zoom', 'status'),
  'Zoom: 3x starting level, smooth ON, slower turning ON, key key.keyboard.z. Commands: /zoom level <1.25-10>, /zoom key <key>, /zoom smooth on|off, /zoom sensitivity on|off, /zoom reset.');
check('command level', events.command('zoom', 'level 5'), 'Starting zoom set to 5x.');
check('command level storage', dec.decode(store.get('level')), '5');
check('command key', events.command('zoom', 'key key.keyboard.c'),
  'Default zoom key set to key.keyboard.c (your own binding in Options > Controls wins).');
check('command key registered', calls.registerKey.at(-1)?.key, 'key.keyboard.c');
check('command key storage', dec.decode(store.get('key')), 'key.keyboard.c');
check('command smooth off', events.command('zoom', 'smooth off'), 'Smooth zoom set to OFF.');
check('command sensitivity off', events.command('zoom', 'sensitivity off'), 'Slower turning set to OFF.');
check('flags stored', [dec.decode(store.get('smooth')), dec.decode(store.get('sensitivity'))], ['false', 'false']);

uiEvents.settingsOpened();
uiEvents.select(2, 0); check('cycle level storage', dec.decode(store.get('level')), '2');
uiEvents.select(3, 0); check('smooth toggled back', dec.decode(store.get('smooth')), 'true');
uiEvents.select(4, 0); check('sensitivity toggled back', dec.decode(store.get('sensitivity')), 'true');
uiEvents.select(5, 0); check('cycle key storage', dec.decode(store.get('key')), 'key.keyboard.v');
check('cycle key registered', calls.registerKey.at(-1)?.key, 'key.keyboard.v');

let error1 = '', error2 = '';
try { events.command('zoom', 'level nope'); } catch (e) { error1 = String(e); }
try { events.command('other', ''); } catch (e) { error2 = String(e); }
check('invalid level errors', error1.includes('Usage: /zoom level'), true);
check('unknown command errors', error2.includes('Unknown command.'), true);
check('command reset', events.command('zoom', 'reset'), 'Zoom reset to 3x and key.keyboard.z.');
check('reset storage', [
  dec.decode(store.get('level')), dec.decode(store.get('smooth')),
  dec.decode(store.get('sensitivity')), dec.decode(store.get('key')),
], ['3', 'true', 'true', 'key.keyboard.z']);

const failed = results.filter(r => !r.ok);
console.log(JSON.stringify({ passed: results.length - failed.length, total: results.length, failed }, null, 2));
if (failed.length) process.exit(1);
console.log(`PASS ${results.length}/${results.length} checks`);
