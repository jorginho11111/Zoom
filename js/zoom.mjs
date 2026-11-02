const KEY = "zoom";
const DEFAULT_KEY = "key.keyboard.z";
const KEYS = [
  "key.keyboard.z", "key.keyboard.c", "key.keyboard.v",
  "key.keyboard.b", "key.keyboard.x",
];
const LEVELS = [2, 3, 4, 6, 8, 10];
const MIN = 1.25, MAX = 10, STEP = 1.25;
const LEVEL_BUTTON = 2, SMOOTH_BUTTON = 3, SENSITIVITY_BUTTON = 4, KEY_BUTTON = 5;

function limit(v) { return Math.max(MIN, Math.min(MAX, v)); }
function onText(b) { return b ? "ON" : "OFF"; }
function levelText(v) { return Number.isInteger(v) ? String(v) : String(Number(v.toFixed(2))); }
function decode(v) {
  if (!v) return null;
  try { return new TextDecoder().decode(v instanceof Uint8Array ? v : new Uint8Array(v)); }
  catch { return null; }
}
function encode(s) { return new TextEncoder().encode(s); }
function stored(k) { return decode(storage.get(k)); }
function flag(k) { const v = stored(k); return v === null || v.trim() !== "false"; }

class Zoom {
  constructor() {
    this.level = 3;
    this.levelText = "3";
    this.smooth = true;
    this.sensitivity = true;
    this.held = null;
    this.key = DEFAULT_KEY;
  }
  init() {
    const level = stored("level");
    if (level !== null && level.trim() !== "" && Number.isFinite(Number(level))) {
      this.level = limit(Number(level));
      this.levelText = level.trim();
    }
    this.smooth = flag("smooth");
    this.sensitivity = flag("sensitivity");
    const key = stored("key");
    if (key && key.trim()) this.key = key.trim();
    try { input.registerKey(KEY, "Zoom key", this.key); }
    catch {
      this.key = DEFAULT_KEY;
      input.registerKey(KEY, "Zoom key", this.key);
    }
    this.page();
    return { commands: ["zoom"], tick: false, frame: true, chat: false };
  }
  show() {
    const factor = this.held ?? 1;
    render.setZoom({
      factor,
      smooth: this.smooth,
      turn: this.sensitivity ? 1 / factor : 1,
    });
  }
  page() {
    ui.setSettings([
      { tag: "text", id: 1, text: "Hold the zoom key; use the wheel while held. /zoom for commands.", color: 0 },
      { tag: "button", id: LEVEL_BUTTON, label: `Starting zoom: ${this.levelText}x` },
      { tag: "button", id: SMOOTH_BUTTON, label: `Smooth zoom: ${onText(this.smooth)}` },
      { tag: "button", id: SENSITIVITY_BUTTON, label: `Slower turning: ${onText(this.sensitivity)}` },
      { tag: "button", id: KEY_BUTTON, label: `Zoom key: ${this.key}` },
    ]);
  }
  save() {
    try {
      storage.set("level", encode(this.levelText));
      storage.set("smooth", encode(String(this.smooth)));
      storage.set("sensitivity", encode(String(this.sensitivity)));
      storage.set("key", encode(this.key));
    } catch {}
  }
  setLevel(value, text) {
    this.level = limit(value);
    this.levelText = text;
  }
  cycleLevel() {
    const i = LEVELS.findIndex(l => Math.abs(l - this.level) < 0.0001);
    const next = (i + 1) % LEVELS.length;
    this.setLevel(LEVELS[next], String(LEVELS[next]));
    this.save(); this.page();
  }
  cycleKey() {
    const i = KEYS.indexOf(this.key);
    this.setKey(KEYS[(i + 1) % KEYS.length]);
  }
  setKey(key) {
    try { input.registerKey(KEY, "Zoom key", key); }
    catch { return false; }
    this.key = key;
    this.save(); this.page();
    return true;
  }
  reset() {
    this.setLevel(3, "3");
    this.smooth = true;
    this.sensitivity = true;
    this.setKey(DEFAULT_KEY);
    this.save(); this.page();
  }
  frame() {
    let changed = false;
    for (const e of input.poll()) {
      if (e.tag === "key-down" && e.val === KEY) {
        this.held = limit(this.level);
        input.claimWheel(true);
        changed = true;
      } else if (e.tag === "key-up" && e.val === KEY) {
        this.held = null;
        input.claimWheel(false);
        changed = true;
      } else if (e.tag === "wheel" && this.held !== null) {
        this.held = limit(this.held * Math.pow(STEP, e.val));
        changed = true;
      }
    }
    if (changed) this.show();
  }
  command(name, args) {
    if (name !== "zoom") return { tag: "err", val: "Unknown command." };
    const parts = args.trim().split(/\s+/).filter(Boolean);
    const verb = parts[0] || "status";
    const arg = parts[1];
    const ok = text => ({ tag: "ok", val: text });
    const err = text => ({ tag: "err", val: text });
    if (verb === "status") {
      return ok(`Zoom: ${this.levelText}x starting level, smooth ${onText(this.smooth)}, slower turning ${onText(this.sensitivity)}, key ${this.key}. Commands: /zoom level <1.25-10>, /zoom key <key>, /zoom smooth on|off, /zoom sensitivity on|off, /zoom reset.`);
    }
    if (verb === "level") {
      if (arg === undefined || !Number.isFinite(Number(arg))) return err("Usage: /zoom level <1.25-10>");
      this.setLevel(Number(arg), arg);
      this.save(); this.page(); if (this.held !== null) this.show();
      return ok(`Starting zoom set to ${this.levelText}x.`);
    }
    if (verb === "key") {
      if (!arg) return err("Usage: /zoom key <key.keyboard.z>");
      if (!this.setKey(arg)) return err("Usage: /zoom key <key.keyboard.z>");
      return ok(`Default zoom key set to ${arg} (your own binding in Options > Controls wins).`);
    }
    if (verb === "smooth" || verb === "sensitivity") {
      const on = arg !== "off";
      if (verb === "smooth") this.smooth = on; else this.sensitivity = on;
      this.save(); this.page(); if (this.held !== null) this.show();
      return ok(`${verb === "smooth" ? "Smooth zoom" : "Slower turning"} set to ${onText(on)}.`);
    }
    if (verb === "reset") {
      this.reset();
      if (this.held !== null) this.show();
      return ok("Zoom reset to 3x and key.keyboard.z.");
    }
    return err("Unknown /zoom option.");
  }
  uiSelect(id) {
    if (id === LEVEL_BUTTON) this.cycleLevel();
    else if (id === SMOOTH_BUTTON) { this.smooth = !this.smooth; this.save(); this.page(); }
    else if (id === SENSITIVITY_BUTTON) { this.sensitivity = !this.sensitivity; this.save(); this.page(); }
    else if (id === KEY_BUTTON) this.cycleKey();
    else return;
    if (this.held !== null) this.show();
  }
  uiSettingsOpened() { this.page(); }
}

const state = new Zoom();
export const events = {
  init: () => state.init(),
  tick: () => {},
  frame: () => state.frame(),
  chat: () => {},
  command: (name, args) => state.command(name, args),
};
export const uiEvents = {
  select: (id, row) => state.uiSelect(id, row),
  closed: () => {},
  settingsOpened: () => state.uiSettingsOpened(),
};
