/* WI-Zoom-style Horizon WASM mod, core module for horizon:mods@0.1.0
 * (Horizon revision e7614d2197ebe9ffdaa75e1bbb432dc0ce855e70).
 *
 * This is a freestanding WASM core module: no WASI, no libc. The canonical
 * ABI shims are supplied by wit-component when tools/componentize-core.mjs
 * wraps it into mod.wasm.
 */
#define EXPORT(name) __attribute__((export_name(name)))
#define IMPORT(mod, name) __attribute__((import_module(mod), import_name(name)))

extern void h_register_key(int, int, int, int, int, int, int)
    IMPORT("cm32p2|horizon:mods/input@0.1", "register-key");
extern void h_poll(int) IMPORT("cm32p2|horizon:mods/input@0.1", "poll");
extern void h_claim_wheel(int) IMPORT("cm32p2|horizon:mods/input@0.1", "claim-wheel");
extern void h_set_zoom(float, int, float)
    IMPORT("cm32p2|horizon:mods/render@0.1", "set-zoom");
extern void h_get(int, int, int) IMPORT("cm32p2|horizon:mods/storage@0.1", "get");
extern void h_set(int, int, int, int, int)
    IMPORT("cm32p2|horizon:mods/storage@0.1", "set");
extern void h_set_settings(int, int)
    IMPORT("cm32p2|horizon:mods/ui@0.1", "set-settings");

extern unsigned char __heap_base;
static unsigned int heap_used = 0;

/* Satisfy any compiler-generated memory helpers without importing libc. */
void *memcpy(void *dst, const void *src, unsigned long n) {
    volatile unsigned char *d = (volatile unsigned char *)dst;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memmove(void *dst, const void *src, unsigned long n) {
    return memcpy(dst, src, n);
}
void *memset(void *dst, int c, unsigned long n) {
    volatile unsigned char *d = (volatile unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

static void zero(volatile unsigned char *p, unsigned int n) {
    while (n--) *p++ = 0;
}
static void copy_bytes(volatile unsigned char *dst, const volatile unsigned char *src, unsigned int n) {
    while (n--) *dst++ = *src++;
}
static unsigned int slen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
static int eq_n(const unsigned char *p, int len, const char *lit) {
    unsigned int n = slen(lit), i;
    if (len < 0 || (unsigned int)len != n) return 0;
    for (i = 0; i < n; i++) if (p[i] != (unsigned char)lit[i]) return 0;
    return 1;
}
static void put_i32(unsigned char *base, int off, unsigned int v) {
    *(volatile unsigned int *)(base + off) = v;
}
static unsigned int get_i32(const unsigned char *base, int off) {
    return *(const volatile unsigned int *)(base + off);
}
static float get_f32(const unsigned char *base, int off) {
    return *(const volatile float *)(base + off);
}
static float clamp_level(float v) {
    if (v < 1.25f) return 1.25f;
    if (v > 10.0f) return 10.0f;
    return v;
}
static float pow_step(float s) {
    const float ln125 = 0.2231435513f;
    int n = (int)s;
    float frac = s - (float)n;
    float r = 1.0f;
    int i;
    if (n > 32) n = 32;
    if (n < -32) n = -32;
    if (n >= 0) { for (i = 0; i < n; i++) r *= 1.25f; }
    else { for (i = 0; i < -n; i++) r /= 1.25f; }
    if (frac > 0.0f) r *= 1.0f + ln125 * frac;
    else if (frac < 0.0f) r /= 1.0f - ln125 * frac;
    return r;
}
static float parse_float(const unsigned char *p, int len, int *ok) {
    float sign = 1.0f, val = 0.0f, scale = 0.1f;
    int i = 0, digits = 0;
    *ok = 0;
    if (len <= 0) return 0.0f;
    if (p[0] == '-') { sign = -1.0f; i = 1; }
    while (i < len && p[i] >= '0' && p[i] <= '9') {
        val = val * 10.0f + (float)(p[i] - '0'); digits++; i++;
    }
    if (i < len && p[i] == '.') {
        i++;
        while (i < len && p[i] >= '0' && p[i] <= '9') {
            val += (float)(p[i] - '0') * scale; scale *= 0.1f; digits++; i++;
        }
    }
    if (!digits || i != len) return 0.0f;
    *ok = 1;
    return sign * val;
}
static int token(const unsigned char *s, int len, int idx, const unsigned char **out) {
    int start = 0, count = 0, i = 0;
    while (i <= len) {
        if (i == len || s[i] == ' ') {
            if (i > start) {
                if (count == idx) { *out = s + start; return i - start; }
                count++;
            }
            start = i + 1;
        }
        i++;
    }
    return 0;
}

static const char ZOOM[] = "zoom";
static const char LABEL[] = "Zoom key";
static const char DEFAULT_KEY[] = "key.keyboard.z";
static const char KEY_LEVEL[] = "level";
static const char KEY_SMOOTH[] = "smooth";
static const char KEY_SENSITIVITY[] = "sensitivity";
static const char KEY_KEY[] = "key";
static const char *const KEYS[] = {
    "key.keyboard.z", "key.keyboard.c", "key.keyboard.v",
    "key.keyboard.b", "key.keyboard.x"
};
static const float LEVEL_VALUES[] = { 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f };
static const char *const LEVEL_TEXTS[] = { "2", "3", "4", "6", "8", "10" };

static const char HELP[] =
    "Zoom: hold the key; wheel changes the level while held. /zoom for commands.";
static const char BTN_LEVEL[] = "Cycle starting zoom";
static const char BTN_SMOOTH[] = "Toggle smooth zoom";
static const char BTN_SENSITIVITY[] = "Toggle slower turning";
static const char BTN_KEY[] = "Cycle default key";

static const char STATUS[] =
    "Zoom commands: /zoom level <1.25-10>, /zoom key <key>, /zoom smooth on|off, /zoom sensitivity on|off, /zoom reset.";
static const char OK_LEVEL[] = "Starting zoom updated.";
static const char OK_KEY[] = "Default zoom key updated (your own binding wins).";
static const char OK_SMOOTH[] = "Smooth zoom updated.";
static const char OK_SENSITIVITY[] = "Slower turning updated.";
static const char OK_RESET[] = "Zoom reset to 3x and key.keyboard.z.";
static const char ERR_USAGE_LEVEL[] = "Usage: /zoom level <1.25-10>";
static const char ERR_USAGE_KEY[] = "Usage: /zoom key <key.keyboard.z>";
static const char ERR_UNKNOWN[] = "Unknown /zoom option.";
static const char ERR_UNKNOWN_CMD[] = "Unknown command.";
static const char ERR_KEY[] = "Could not register the zoom key.";

static float level = 3.0f;
static int smooth = 1;
static int sensitivity = 1;
static float held = 1.0f;
static int held_active = 0;
static char key[64] = "key.keyboard.z";
static char level_text[32] = "3";
static unsigned char storage_buf[128];

static unsigned char init_ret[16] __attribute__((aligned(4)));
static unsigned char cmd_ret[16] __attribute__((aligned(4)));
static unsigned char poll_ret[8] __attribute__((aligned(4)));
static unsigned char get_ret[16] __attribute__((aligned(4)));
static unsigned char set_ret[16] __attribute__((aligned(4)));
static unsigned char register_ret[16] __attribute__((aligned(4)));
static int commands_list[2] __attribute__((aligned(4)));
static unsigned char widgets[5 * 24] __attribute__((aligned(4)));

static void copy_key(const unsigned char *p, int len) {
    if (len < 0) len = 0;
    if (len > 63) len = 63;
    copy_bytes((volatile unsigned char *)key, (const volatile unsigned char *)p, (unsigned int)len);
    key[len] = 0;
}
static void copy_level_text(const unsigned char *p, int len) {
    if (len < 0) len = 0;
    if (len > 31) len = 31;
    copy_bytes((volatile unsigned char *)level_text, (const volatile unsigned char *)p, (unsigned int)len);
    level_text[len] = 0;
}
static int read_setting(const char *name) {
    zero((volatile unsigned char *)get_ret, sizeof(get_ret));
    h_get((int)name, (int)slen(name), (int)get_ret);
    if (get_ret[0] != 1) return -1;
    {
        unsigned int ptr = get_i32(get_ret, 4);
        unsigned int len = get_i32(get_ret, 8);
        if (len > sizeof(storage_buf)) len = sizeof(storage_buf);
        copy_bytes((volatile unsigned char *)storage_buf,
                   (const volatile unsigned char *)ptr, len);
        return (int)len;
    }
}
static void store_setting(const char *name, const unsigned char *value, int len) {
    zero((volatile unsigned char *)set_ret, sizeof(set_ret));
    h_set((int)name, (int)slen(name), (int)value, len, (int)set_ret);
}
static void save_all(void) {
    store_setting(KEY_LEVEL, (const unsigned char *)level_text, (int)slen(level_text));
    store_setting(KEY_SMOOTH, (const unsigned char *)(smooth ? "true" : "false"),
                  smooth ? 4 : 5);
    store_setting(KEY_SENSITIVITY,
                  (const unsigned char *)(sensitivity ? "true" : "false"),
                  sensitivity ? 4 : 5);
    store_setting(KEY_KEY, (const unsigned char *)key, (int)slen(key));
}
static void show(void) {
    float factor = held_active ? held : 1.0f;
    float turn = sensitivity ? 1.0f / factor : 1.0f;
    h_set_zoom(factor, smooth, turn);
}
static void page(void) {
    unsigned char *w = widgets;
    zero((volatile unsigned char *)widgets, sizeof(widgets));
    w[0] = 0; put_i32(w, 4, 1); put_i32(w, 8, (unsigned int)HELP);
    put_i32(w, 12, slen(HELP)); put_i32(w, 16, 0);
    w += 24;
    w[0] = 1; put_i32(w, 4, 2); put_i32(w, 8, (unsigned int)BTN_LEVEL);
    put_i32(w, 12, slen(BTN_LEVEL));
    w += 24;
    w[0] = 1; put_i32(w, 4, 3); put_i32(w, 8, (unsigned int)BTN_SMOOTH);
    put_i32(w, 12, slen(BTN_SMOOTH));
    w += 24;
    w[0] = 1; put_i32(w, 4, 4); put_i32(w, 8, (unsigned int)BTN_SENSITIVITY);
    put_i32(w, 12, slen(BTN_SENSITIVITY));
    w += 24;
    w[0] = 1; put_i32(w, 4, 5); put_i32(w, 8, (unsigned int)BTN_KEY);
    put_i32(w, 12, slen(BTN_KEY));
    h_set_settings((int)widgets, 5);
}
static int register_current_key(void) {
    zero((volatile unsigned char *)register_ret, sizeof(register_ret));
    h_register_key((int)ZOOM, 4, (int)LABEL, (int)slen(LABEL),
                   (int)key, (int)slen(key), (int)register_ret);
    return register_ret[0] == 0;
}
static int set_key(const unsigned char *p, int len) {
    char old[64];
    int i, oldlen = (int)slen(key);
    if (len <= 0 || len >= 64) return 0;
    copy_bytes((volatile unsigned char *)old, (const volatile unsigned char *)key,
               (unsigned int)oldlen + 1);
    copy_key(p, len);
    if (!register_current_key()) {
        copy_bytes((volatile unsigned char *)key, (const volatile unsigned char *)old,
                   (unsigned int)oldlen + 1);
        return 0;
    }
    (void)i;
    save_all();
    page();
    return 1;
}
static void set_level(float v, const unsigned char *raw, int rawlen) {
    level = clamp_level(v);
    copy_level_text(raw, rawlen);
}
static void cycle_level(void) {
    int i = 0, found = -1;
    for (i = 0; i < 6; i++) if (LEVEL_VALUES[i] == level) { found = i; break; }
    i = (found + 1) % 6;
    set_level(LEVEL_VALUES[i], (const unsigned char *)LEVEL_TEXTS[i],
              (int)slen(LEVEL_TEXTS[i]));
}
static void cycle_key(void) {
    int i = 0, found = -1;
    for (i = 0; i < 5; i++) if (eq_n((const unsigned char *)key, (int)slen(key), KEYS[i])) {
        found = i; break;
    }
    i = (found + 1) % 5;
    set_key((const unsigned char *)KEYS[i], (int)slen(KEYS[i]));
}

static int init_err(void) {
    zero((volatile unsigned char *)init_ret, sizeof(init_ret));
    init_ret[0] = 1;
    put_i32(init_ret, 4, (unsigned int)ERR_KEY);
    put_i32(init_ret, 8, slen(ERR_KEY));
    return (int)init_ret;
}
static int cmd_ok(const char *msg) {
    zero((volatile unsigned char *)cmd_ret, sizeof(cmd_ret));
    cmd_ret[0] = 0;
    cmd_ret[4] = 1;
    put_i32(cmd_ret, 8, (unsigned int)msg);
    put_i32(cmd_ret, 12, slen(msg));
    return (int)cmd_ret;
}
static int cmd_err(const char *msg) {
    zero((volatile unsigned char *)cmd_ret, sizeof(cmd_ret));
    cmd_ret[0] = 1;
    put_i32(cmd_ret, 4, (unsigned int)msg);
    put_i32(cmd_ret, 8, slen(msg));
    return (int)cmd_ret;
}

/* Small static string builder: no snprintf/libc needed. */
static char msg[256];
static int msg_len = 0;
static void msg_reset(void) { msg_len = 0; msg[0] = 0; }
static void msg_ch(char c) {
    if (msg_len < (int)sizeof(msg) - 1) { msg[msg_len++] = c; msg[msg_len] = 0; }
}
static void msg_n(const unsigned char *p, int len) {
    int i;
    for (i = 0; i < len; i++) msg_ch((char)p[i]);
}
static void msg_str(const char *s) { msg_n((const unsigned char *)s, (int)slen(s)); }
static void msg_int(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) msg_ch(buf[--i]);
}
static void msg_float(float v) {
    int whole = (int)v;
    int frac = (int)((v - (float)whole) * 100.0f + 0.5f);
    char buf[3];
    if (frac >= 100) { whole++; frac = 0; }
    msg_int(whole);
    if (frac) {
        buf[0] = '.';
        buf[1] = (char)('0' + frac / 10);
        buf[2] = (char)('0' + frac % 10);
        msg_n((const unsigned char *)buf, 3);
    }
}

EXPORT("cm32p2|horizon:mods/events@0.1|init")
int init(void) {
    int n, ok;
    float v;
    n = read_setting(KEY_LEVEL);
    if (n > 0) {
        v = parse_float(storage_buf, n, &ok);
        if (ok) set_level(v, storage_buf, n);
    }
    n = read_setting(KEY_SMOOTH);
    if (n > 0) smooth = eq_n(storage_buf, n, "false") ? 0 : 1;
    n = read_setting(KEY_SENSITIVITY);
    if (n > 0) sensitivity = eq_n(storage_buf, n, "false") ? 0 : 1;
    n = read_setting(KEY_KEY);
    if (n > 0) copy_key(storage_buf, n);
    if (!register_current_key()) {
        copy_key((const unsigned char *)DEFAULT_KEY, (int)slen(DEFAULT_KEY));
        if (!register_current_key()) return init_err();
    }
    page();
    zero((volatile unsigned char *)init_ret, sizeof(init_ret));
    commands_list[0] = (int)ZOOM;
    commands_list[1] = 4;
    init_ret[0] = 0;
    put_i32(init_ret, 4, (unsigned int)commands_list);
    put_i32(init_ret, 8, 1);
    init_ret[12] = 0;
    init_ret[13] = 1;
    init_ret[14] = 0;
    return (int)init_ret;
}
EXPORT("cm32p2|horizon:mods/events@0.1|init_post")
void init_post(int ret) { (void)ret; }
EXPORT("cm32p2|horizon:mods/events@0.1|tick")
void tick(long long t) { (void)t; }
EXPORT("cm32p2|horizon:mods/events@0.1|tick_post")
void tick_post(void) {}
EXPORT("cm32p2|horizon:mods/events@0.1|frame")
void frame(float seconds) {
    unsigned int ptr, len, i;
    int changed = 0;
    (void)seconds;
    zero((volatile unsigned char *)poll_ret, sizeof(poll_ret));
    h_poll((int)poll_ret);
    ptr = get_i32(poll_ret, 0);
    len = get_i32(poll_ret, 4);
    for (i = 0; i < len; i++) {
        unsigned char *e = (unsigned char *)(ptr + i * 24);
        unsigned char tag = e[0];
        if (tag == 2) {
            unsigned int p = get_i32(e, 4), l = get_i32(e, 8);
            if (eq_n((const unsigned char *)p, (int)l, ZOOM)) {
                held = clamp_level(level);
                held_active = 1;
                h_claim_wheel(1);
                changed = 1;
            }
        } else if (tag == 3) {
            unsigned int p = get_i32(e, 4), l = get_i32(e, 8);
            if (eq_n((const unsigned char *)p, (int)l, ZOOM)) {
                held = 1.0f;
                held_active = 0;
                h_claim_wheel(0);
                changed = 1;
            }
        } else if (tag == 4 && held_active) {
            held = clamp_level(held * pow_step(get_f32(e, 4)));
            changed = 1;
        }
    }
    if (changed) show();
}
EXPORT("cm32p2|horizon:mods/events@0.1|frame_post")
void frame_post(void) {}
EXPORT("cm32p2|horizon:mods/events@0.1|command")
int command(int name_ptr, int name_len, int args_ptr, int args_len) {
    const unsigned char *name = (const unsigned char *)name_ptr;
    const unsigned char *args = (const unsigned char *)args_ptr;
    const unsigned char *verb, *arg;
    int vlen, alen, ok;
    float v;
    if (!eq_n(name, name_len, ZOOM)) return cmd_err(ERR_UNKNOWN_CMD);
    vlen = token(args, args_len, 0, &verb);
    if (vlen == 0 || eq_n(verb, vlen, "status")) {
        msg_reset();
        msg_str("Zoom: "); msg_str(level_text); msg_str("x starting level, smooth ");
        msg_str(smooth ? "ON" : "OFF"); msg_str(", slower turning ");
        msg_str(sensitivity ? "ON" : "OFF"); msg_str(", key "); msg_str(key);
        msg_str(". Commands: /zoom level <1.25-10>, /zoom key <key>, /zoom smooth on|off, /zoom sensitivity on|off, /zoom reset.");
        return cmd_ok(msg);
    }
    alen = token(args, args_len, 1, &arg);
    if (eq_n(verb, vlen, "level")) {
        if (alen == 0) return cmd_err(ERR_USAGE_LEVEL);
        v = parse_float(arg, alen, &ok);
        if (!ok) return cmd_err(ERR_USAGE_LEVEL);
        set_level(v, arg, alen);
        save_all(); page(); if (held_active) show();
        msg_reset(); msg_str("Starting zoom set to "); msg_str(level_text); msg_str("x.");
        return cmd_ok(msg);
    }
    if (eq_n(verb, vlen, "key")) {
        if (alen == 0) return cmd_err(ERR_USAGE_KEY);
        if (!set_key(arg, alen)) return cmd_err(ERR_USAGE_KEY);
        msg_reset(); msg_str("Default zoom key set to "); msg_n(arg, alen);
        msg_str(" (your own binding in Options > Controls wins).");
        return cmd_ok(msg);
    }
    if (eq_n(verb, vlen, "smooth") || eq_n(verb, vlen, "sensitivity")) {
        int is_smooth = eq_n(verb, vlen, "smooth");
        int on = !(alen > 0 && eq_n(arg, alen, "off"));
        if (is_smooth) smooth = on; else sensitivity = on;
        save_all(); page(); if (held_active) show();
        msg_reset(); msg_str(is_smooth ? "Smooth zoom" : "Slower turning");
        msg_str(" set to "); msg_str(on ? "ON" : "OFF"); msg_str(".");
        return cmd_ok(msg);
    }
    if (eq_n(verb, vlen, "reset")) {
        level = 3.0f;
        copy_level_text((const unsigned char *)"3", 1);
        smooth = 1; sensitivity = 1;
        copy_key((const unsigned char *)DEFAULT_KEY, (int)slen(DEFAULT_KEY));
        if (!register_current_key()) return cmd_err(ERR_KEY);
        save_all(); page(); if (held_active) show();
        return cmd_ok(OK_RESET);
    }
    return cmd_err(ERR_UNKNOWN);
}
EXPORT("cm32p2|horizon:mods/events@0.1|command_post")
void command_post(int ret) { (void)ret; }
EXPORT("cm32p2|horizon:mods/events@0.1|chat")
void chat(int ptr, int len) { (void)ptr; (void)len; }
EXPORT("cm32p2|horizon:mods/events@0.1|chat_post")
void chat_post(void) {}

EXPORT("cm32p2|horizon:mods/ui-events@0.1|select")
void select(int id, int row) {
    (void)row;
    if (id == 2) cycle_level();
    else if (id == 3) smooth = !smooth;
    else if (id == 4) sensitivity = !sensitivity;
    else if (id == 5) { cycle_key(); return; }
    else return;
    save_all(); page(); if (held_active) show();
}
EXPORT("cm32p2|horizon:mods/ui-events@0.1|select_post")
void select_post(void) {}
EXPORT("cm32p2|horizon:mods/ui-events@0.1|closed")
void closed(void) {}
EXPORT("cm32p2|horizon:mods/ui-events@0.1|closed_post")
void closed_post(void) {}
EXPORT("cm32p2|horizon:mods/ui-events@0.1|settings-opened")
void settings_opened(void) { page(); }
EXPORT("cm32p2|horizon:mods/ui-events@0.1|settings-opened_post")
void settings_opened_post(void) {}

EXPORT("cm32p2_realloc")
int realloc(int old_ptr, int old_size, int align, int new_size) {
    unsigned int base = (unsigned int)&__heap_base;
    unsigned int a = (unsigned int)align;
    unsigned int cur, mask, aligned, end, bytes, pages;
    (void)old_ptr; (void)old_size;
    if (a == 0) a = 1;
    cur = base + heap_used;
    mask = a - 1u;
    aligned = (cur + mask) & ~mask;
    end = aligned + (unsigned int)new_size;
    bytes = (unsigned int)__builtin_wasm_memory_size(0) * 65536u;
    if (end > bytes) {
        pages = (end - bytes + 65535u) / 65536u;
        if ((int)__builtin_wasm_memory_grow(0, pages) < 0) return 0;
    }
    heap_used = end - base;
    return (int)aligned;
}
EXPORT("cm32p2_initialize")
void initialize(void) {}
