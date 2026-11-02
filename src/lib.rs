//! WI-Zoom-style Zoom for Horizon's old WASM mod runtime.
//!
//! Target: `e7614d2197ebe9ffdaa75e1bbb432dc0ce855e70` (`0c9a49390^`),
//! `horizon:mods@0.1.0`, `api = "0.1"`, `side = "client"`. The current
//! `main` branch no longer loads WASM mods.
use horizon_mod_sdk::{
    Mod, Registration,
    input::{self, Event},
    render::{self, Zoom as ZoomView},
    storage,
    ui::{self, ButtonWidget, TextWidget, Widget},
};

const KEY_NAME: &str = "zoom";
const DEFAULT_KEY: &str = "key.keyboard.z";
const KEYS: [&str; 5] = [
    "key.keyboard.z",
    "key.keyboard.c",
    "key.keyboard.v",
    "key.keyboard.b",
    "key.keyboard.x",
];
const LEVELS: [f32; 6] = [2.0, 3.0, 4.0, 6.0, 8.0, 10.0];
const MIN: f32 = 1.25;
const MAX: f32 = 10.0;
const STEP: f32 = 1.25;

const LEVEL_BUTTON: u32 = 2;
const SMOOTH_BUTTON: u32 = 3;
const SENSITIVITY_BUTTON: u32 = 4;
const KEY_BUTTON: u32 = 5;

struct Zoom {
    /// Starting zoom while the key is held (WI-Zoom default: 3x).
    level: f32,
    /// Text used to persist a custom `/zoom level` value.
    level_text: String,
    smooth: bool,
    sensitivity: bool,
    /// `None` when the key is up.
    held: Option<f32>,
    /// Default key; a player's own key binding in Options > Controls wins.
    key: String,
}

impl Zoom {
    fn show(&self) {
        let factor = self.held.unwrap_or(1.0);
        render::set_zoom(ZoomView {
            factor,
            smooth: self.smooth,
            // Slower turning in proportion to the zoom factor.
            turn: if self.sensitivity { 1.0 / factor } else { 1.0 },
        });
    }

    fn page(&self) {
        let on = |b: bool| if b { "ON" } else { "OFF" };
        ui::set_settings(&[
            Widget::Text(TextWidget {
                id: 1,
                text: "Hold the zoom key; use the wheel while held. /zoom for commands.".into(),
                color: 0,
            }),
            Widget::Button(ButtonWidget {
                id: LEVEL_BUTTON,
                label: format!("Starting zoom: {}x", self.level_text),
            }),
            Widget::Button(ButtonWidget {
                id: SMOOTH_BUTTON,
                label: format!("Smooth zoom: {}", on(self.smooth)),
            }),
            Widget::Button(ButtonWidget {
                id: SENSITIVITY_BUTTON,
                label: format!("Slower turning: {}", on(self.sensitivity)),
            }),
            Widget::Button(ButtonWidget {
                id: KEY_BUTTON,
                label: format!("Zoom key: {}", self.key),
            }),
        ]);
    }

    fn save(&self) {
        let flag = |b: bool| if b { &b"true"[..] } else { &b"false"[..] };
        let _ = storage::set("level", self.level_text.as_bytes());
        let _ = storage::set("smooth", flag(self.smooth));
        let _ = storage::set("sensitivity", flag(self.sensitivity));
        let _ = storage::set("key", self.key.as_bytes());
    }

    fn set_level(&mut self, value: f32, text: String) {
        self.level = value.clamp(MIN, MAX);
        self.level_text = text;
    }

    fn cycle_level(&mut self) {
        let i = LEVELS
            .iter()
            .position(|&l| (l - self.level).abs() < 0.0001)
            .unwrap_or(0);
        let next = (i + 1) % LEVELS.len();
        self.set_level(LEVELS[next], LEVELS[next].to_string());
        self.save();
        self.page();
    }

    fn cycle_key(&mut self) {
        let i = KEYS.iter().position(|&k| k == self.key).unwrap_or(0);
        let next = (i + 1) % KEYS.len();
        let _ = self.set_key(KEYS[next]);
    }

    fn set_key(&mut self, key: &str) -> Result<(), String> {
        input::register_key(KEY_NAME, "Zoom key", key)?;
        self.key = key.to_string();
        self.save();
        self.page();
        Ok(())
    }

    fn reset(&mut self) -> Result<(), String> {
        self.set_level(3.0, "3".into());
        self.smooth = true;
        self.sensitivity = true;
        self.set_key(DEFAULT_KEY)?;
        self.save();
        self.page();
        Ok(())
    }
}

fn stored(key: &str) -> Option<String> {
    storage::get(key).and_then(|v| String::from_utf8(v).ok())
}

impl Mod for Zoom {
    fn init() -> Result<(Self, Registration), String> {
        // A stored key is preferred; fall back to Z if the host rejects it.
        let mut key = stored("key").unwrap_or_else(|| DEFAULT_KEY.to_string());
        if input::register_key(KEY_NAME, "Zoom key", &key).is_err() {
            key = DEFAULT_KEY.to_string();
            input::register_key(KEY_NAME, "Zoom key", &key)?;
        }

        let level_text = stored("level").unwrap_or_else(|| "3".to_string());
        let level = level_text
            .trim()
            .parse::<f32>()
            .unwrap_or(3.0)
            .clamp(MIN, MAX);
        let flag = |k: &str| stored(k).map(|v| v.trim() != "false").unwrap_or(true);
        let zoom = Zoom {
            level,
            level_text,
            smooth: flag("smooth"),
            sensitivity: flag("sensitivity"),
            held: None,
            key,
        };
        zoom.page();
        Ok((zoom, Registration::new().command("zoom").on_frame()))
    }

    fn frame(&mut self, _seconds: f32) {
        let mut changed = false;
        for event in input::poll() {
            match event {
                Event::KeyDown(k) if k == KEY_NAME => {
                    self.held = Some(self.level.clamp(MIN, MAX));
                    input::claim_wheel(true);
                }
                Event::KeyUp(k) if k == KEY_NAME => {
                    self.held = None;
                    input::claim_wheel(false);
                }
                Event::Wheel(steps) => {
                    if let Some(z) = &mut self.held {
                        *z = (*z * STEP.powf(steps)).clamp(MIN, MAX);
                    } else {
                        continue;
                    }
                }
                _ => continue,
            }
            changed = true;
        }
        if changed {
            self.show();
        }
    }

    fn command(&mut self, name: &str, args: &str) -> Result<Option<String>, String> {
        if name != "zoom" {
            return Err("Unknown command.".into());
        }
        let mut words = args.split_whitespace();
        let verb = words.next().unwrap_or("status");
        match verb {
            "status" => Ok(Some(format!(
                "Zoom: {}x starting level, smooth {}, slower turning {}, key {}. Commands: /zoom level <1.25-10>, /zoom key <key>, /zoom smooth on|off, /zoom sensitivity on|off, /zoom reset.",
                self.level_text,
                if self.smooth { "ON" } else { "OFF" },
                if self.sensitivity { "ON" } else { "OFF" },
                self.key,
            ))),
            "level" => {
                let raw = words.next().ok_or("Usage: /zoom level <1.25-10>")?;
                let value = raw
                    .parse::<f32>()
                    .map_err(|_| "Usage: /zoom level <1.25-10>")?;
                self.set_level(value, raw.to_string());
                self.save();
                self.page();
                if self.held.is_some() {
                    self.show();
                }
                Ok(Some(format!("Starting zoom set to {}x.", self.level_text)))
            }
            "key" => {
                let key = words.next().ok_or("Usage: /zoom key <key.keyboard.z>")?;
                self.set_key(key)?;
                Ok(Some(format!(
                    "Default zoom key set to {} (your own binding in Options > Controls wins).",
                    key
                )))
            }
            "smooth" | "sensitivity" => {
                let on = !matches!(words.next(), Some("off"));
                if verb == "smooth" {
                    self.smooth = on;
                } else {
                    self.sensitivity = on;
                }
                self.save();
                self.page();
                if self.held.is_some() {
                    self.show();
                }
                Ok(Some(format!(
                    "{} set to {}.",
                    if verb == "smooth" { "Smooth zoom" } else { "Slower turning" },
                    if on { "ON" } else { "OFF" }
                )))
            }
            "reset" => {
                self.reset()?;
                Ok(Some("Zoom reset to 3x and key.keyboard.z.".into()))
            }
            _ => Err("Unknown /zoom option.".into()),
        }
    }

    fn ui_select(&mut self, id: u32, _row: u32) {
        match id {
            LEVEL_BUTTON => self.cycle_level(),
            SMOOTH_BUTTON => {
                self.smooth = !self.smooth;
                self.save();
                self.page();
            }
            SENSITIVITY_BUTTON => {
                self.sensitivity = !self.sensitivity;
                self.save();
                self.page();
            }
            KEY_BUTTON => self.cycle_key(),
            _ => return,
        }
        if self.held.is_some() {
            self.show();
        }
    }

    fn ui_settings_opened(&mut self) {
        self.page();
    }
}

horizon_mod_sdk::export_mod!(Zoom);
