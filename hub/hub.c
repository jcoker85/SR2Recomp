/*
 * SR2Recomp hub (ported from the SR1Recomp hub, itself from the MC1Recomp and TwistedMetal1234Recomp hubs).
 *
 * A small menu for the PS2 games recompiled with ps2-native-recomp. Each row
 * has two buttons: Play starts the game straight away with its saved
 * settings, and Settings opens this hub's own settings page for that game.
 * The settings are kept in settings.ini beside this executable, one [<id>]
 * section per game. Every option is off until it is switched on, so a fresh
 * install plays the original game.
 *
 * The list comes from games.txt beside this executable, one game per line:
 *   Title|Subtitle|path/to/ps2run.exe|Cover name|id|ELF name
 * The path is relative to this file and lies inside the toolkit folder, as
 * <toolkit>/build/native-<id>/ps2run.exe; the toolkit folder is the game's
 * working directory and holds games/<id>/ (patches.txt, the memory card, keys.ini). The disc image is the player's own: the
 * first Play asks for it (a standard Open dialog) and its path is kept in settings.ini (disc=); Settings > Disc Image changes
 * it. The game boots from that image (ps2run reads the game's program from it), so nothing of the game is kept beside the
 * launcher. A folder <id>-options beside the toolkit folder means the
 * game has the switchable widescreen / 60 fps patches (widescreen.txt, 60fps.txt):
 * the hub copies the lines of the options that are on into <id>-options/boot.txt before each
 * start. Without that folder those two options are greyed out.
 *
 * Play starts the game like this:
 *   ps2run.exe --disc "<your disc image>" --window|--fullscreen
 *     --game-dir "games\<id>" --memcard "games\<id>\<id>.ps2" --patches "games\<id>\patches.txt"
 *     --patches "<options>\boot.txt" [--widescreen]
 * (the memory card is the toolkit app's own file for the game, so saves are shared with it)
 * with PS2_SCALE, PS2_TRUE_COLOR, PS2_DITHER, PS2_LOOK, PS2_PACE, PS2_PRESENT_MODE,
 * PS2_WIDE_ONLY_IF/PS2_WIDE_WORD (4:3 Menus) and PS2_WINDOW_TITLE set for it
 * ("SR2Recomp.exe --check [id]" prints all of it without starting anything).
 *
 * The window keeps the shape of a PS2 DVD-case front. The cover of the
 * highlighted game (covers/<id>.png beside the executable) fills the
 * background and cross-fades as the highlight moves. Covers are not shipped:
 * missing ones are downloaded in the background from the libretro-thumbnails
 * project (Sony - PlayStation 2) by the cover name in games.txt.
 *
 * Controls: arrow keys + Enter, mouse, or a controller (D-pad + A, B back).
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define MAX_GAMES 16
#define CLIENT_W 540          /* PS2 box fronts (libretro scans are 512x726) */
#define CLIENT_H 766
#define ROW_H 62
#define ROW_GAP 8
#define MARGIN 28
#define LIST_BOTTOM 58        /* space under the list for the hint line */
#define BTN_W 98
#define BTN_GAP 8
#define COL_PLAY 0
#define COL_SETTINGS 1
#define FADE_MS 160
#define TICK_MS 16

/* Settings page layout. */
#define S_TOP 112
#define S_ROW_H 62
#define S_ROW_GAP 6
#define S_VIEW_TOP 104        /* the rows scroll between these two lines */
#define S_VIEW_BOTTOM (CLIENT_H - 56)
#define S_WHEEL_STEP (S_ROW_H + S_ROW_GAP)
#define S_VAL_W 74            /* ON/OFF box */
#define S_LOOK_W 190          /* the picture mode and texture filtering boxes */
#define S_DISC_W 230          /* the disc image box */
#define S_SEG_W 38            /* one of the 1x..4x boxes */
#define S_SEG2_W 70           /* one of the 4:3 / 16:9 and 16-bit / 32-bit boxes */
#define S_SEG3_W 104          /* Controller / Keyboard */
#define S_SEG_GAP 4

/* The orange of the game's own menus. */
#define ACCENT RGB(222, 140, 44)
#define ACCENT_DIM RGB(116, 70, 18)
#define WHITE RGB(245, 245, 245)
#define GREY RGB(170, 170, 170)
#define DARK_GREY RGB(105, 105, 105)

typedef struct {
    int wide, hud43, menus43, sixty, scale, look, texfilter, smooth, zfrac, truecolor, dither, pacing, vsync, fullscreen, skipintro;
} Options;

typedef struct {
    wchar_t title[96];
    wchar_t subtitle[128];
    wchar_t exe[MAX_PATH];      /* full path of ps2run.exe */
    wchar_t tool[MAX_PATH];     /* toolkit folder: working directory */
    wchar_t exe_rel[MAX_PATH];  /* ps2run.exe relative to the toolkit folder */
    wchar_t opts[MAX_PATH];     /* <id>-options beside the toolkit folder */
    wchar_t id[32];
    wchar_t elf[64];
    DWORD crc;                  /* CRC-32 of the ELF on the right disc (games.txt, optional) */
    int has_crc;
    wchar_t cover_name[96];     /* libretro-thumbnails box art name, or empty */
    wchar_t disc[MAX_PATH];     /* the player's disc image (settings.ini disc=), or empty */
    int available;              /* ps2run.exe is built */
    int toggles;                /* <id>-options exists: widescreen/60 fps/HUD patches */
    int menus;                  /* <id>-options/menus43.txt exists: 4:3 menus and videos */
    int hud;                    /* <id>-options/hud43.txt exists: the Fixed Widescreen HUD is built into the game program */
    Options o;
    HBITMAP cover;              /* cover scaled to the client area, or NULL */
} Game;

static Game g_games[MAX_GAMES];
static int g_count;
static int g_sel;
static int g_col;       /* COL_PLAY or COL_SETTINGS */
static HANDLE g_child;
static int g_child_game;      /* the game g_child runs */
static DWORD g_child_start;   /* GetTickCount() when it started */
static wchar_t g_dir[MAX_PATH];
static wchar_t g_ini[MAX_PATH];
static HFONT g_font_item, g_font_small, g_font_btn, g_font_head, g_font_set, g_font_tab;

/* Pages: the game list, or the settings of one game. */
enum { PAGE_LIST, PAGE_SETTINGS };
/* The picture looks of the toolkit's window (host/keymap.cpp; PS2_LOOK takes the word): how the PS2 picture is enlarged
 * for the window. Crisp is closest to the console; the toolkit's own default is sharp_edges. */
/* 6: FSR (AMD FidelityFX Super Resolution 1, PS2_FSR=1, gpu.cpp), which takes the place of the look; its name word is fsr. */
#define LOOK_FSR 6
static const wchar_t* k_look_words[7] = { L"crisp", L"crisp_edges", L"soft", L"soft_edges", L"sharp", L"sharp_edges", L"fsr" };
/* Texture filtering (PS2_TEX_FILTER, gpu.cpp make_setup), as PCSX2 names them; 0 = the game's own choice. */
static const wchar_t* k_filter_words[3] = { L"ps2", L"nearest", L"bilinear" };
static const wchar_t* k_filter_names[3] = { L"Bilinear (PS2)", L"Nearest", L"Bilinear (Forced)" };
static const wchar_t* k_look_names[7] = { L"Crisp", L"Crisp + edges", L"Soft", L"Soft + edges", L"Sharp", L"Sharp + edges", L"FSR" };

/* The Graphics tab's rows, in the order they are shown. */
enum { S_FULLSCREEN, S_SCALE, S_VSYNC, S_PACING, S_LOOK, S_TEXFILTER, S_SIXTY, S_WIDE, S_HUD43, S_MENUS43, S_TRUECOLOR, S_DITHER, S_SMOOTH, S_ZFRAC, S_SKIPINTRO, S_DISC, S_BACK, S_COUNT };
static int g_page = PAGE_LIST;
static int g_srow;      /* highlighted settings row */
static int g_sscroll;   /* how far the settings rows are scrolled up, in pixels */

/* Background cross-fade: g_bg_from is fully shown, g_bg_to fades in over it. */
static int g_bg_from, g_bg_to;
static int g_fade = 255;    /* 0..255 */

/* XInput is loaded at run time so the hub starts on systems without it. */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } PadState;
typedef struct { DWORD packet; PadState pad; } XState;
typedef DWORD (WINAPI *XGetState)(DWORD, XState*);
static XGetState g_xget;
static WORD g_pad_prev;

/* The few GDI+ flat-API calls used to decode and scale the cover PNGs. */
typedef struct { UINT32 version; void* debug_cb; BOOL no_thread; BOOL no_codecs; } GdipStartupInput;
int WINAPI GdiplusStartup(ULONG_PTR* token, const GdipStartupInput* input, void* output);
int WINAPI GdipLoadImageFromFile(const WCHAR* file, void** image);
int WINAPI GdipDisposeImage(void* image);
int WINAPI GdipCreateFromHDC(HDC dc, void** graphics);
int WINAPI GdipDeleteGraphics(void* graphics);
int WINAPI GdipSetInterpolationMode(void* graphics, int mode);
int WINAPI GdipDrawImageRectI(void* graphics, void* image, INT x, INT y, INT w, INT h);

static HBITMAP load_cover(const wchar_t* id) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\covers\\%ls.png", g_dir, id);
    void* image = NULL;
    if (GdipLoadImageFromFile(path, &image) != 0 || !image) return NULL;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, CLIENT_W, CLIENT_H);
    HGDIOBJ old = SelectObject(mem, bmp);
    void* g = NULL;
    if (GdipCreateFromHDC(mem, &g) == 0 && g) {
        GdipSetInterpolationMode(g, 7);     /* high-quality bicubic */
        GdipDrawImageRectI(g, image, 0, 0, CLIENT_W, CLIENT_H);
        GdipDeleteGraphics(g);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    GdipDisposeImage(image);
    return bmp;
}

/* --- settings.ini ----------------------------------------------------------- */

static void load_options(Game* g) {
    Options* o = &g->o;
    o->wide = GetPrivateProfileIntW(g->id, L"widescreen", 0, g_ini) != 0;
    o->hud43 = GetPrivateProfileIntW(g->id, L"hud43", 0, g_ini) != 0;
    o->menus43 = GetPrivateProfileIntW(g->id, L"menus43", 0, g_ini) != 0;
    o->sixty = GetPrivateProfileIntW(g->id, L"60fps", 0, g_ini) != 0;
    o->scale = GetPrivateProfileIntW(g->id, L"scale", 1, g_ini);
    if (o->scale < 1) o->scale = 1;
    if (o->scale > 4) o->scale = 4;
    o->truecolor = GetPrivateProfileIntW(g->id, L"truecolor", 0, g_ini) != 0;
    o->dither = GetPrivateProfileIntW(g->id, L"dither", 1, g_ini) != 0;   /* on: the game turns the PS2's dither on */
    o->look = GetPrivateProfileIntW(g->id, L"look", 0, g_ini);
    o->smooth = GetPrivateProfileIntW(g->id, L"smoothtextures", 0, g_ini) != 0;
    o->zfrac = GetPrivateProfileIntW(g->id, L"zfight", 0, g_ini) != 0;
    if (o->look < 0 || o->look > LOOK_FSR) o->look = 0;
    o->texfilter = GetPrivateProfileIntW(g->id, L"texfilter", 0, g_ini);
    if (o->texfilter < 0 || o->texfilter > 2) o->texfilter = 0;
    o->pacing = GetPrivateProfileIntW(g->id, L"framepacing", 0, g_ini) != 0;
    o->vsync = GetPrivateProfileIntW(g->id, L"vsync", 0, g_ini) != 0;
    o->fullscreen = GetPrivateProfileIntW(g->id, L"fullscreen", 0, g_ini) != 0;
    o->skipintro = GetPrivateProfileIntW(g->id, L"skipintro", 0, g_ini) != 0;
    GetPrivateProfileStringW(g->id, L"disc", L"", g->disc, MAX_PATH, g_ini);
}

static void save_options(const Game* g) {
    const Options* o = &g->o;
    struct { const wchar_t* key; int v; } kv[] = {
        { L"widescreen", o->wide }, { L"hud43", o->hud43 }, { L"menus43", o->menus43 }, { L"60fps", o->sixty },
        { L"scale", o->scale }, { L"look", o->look }, { L"texfilter", o->texfilter }, { L"smoothtextures", o->smooth }, { L"zfight", o->zfrac }, { L"truecolor", o->truecolor }, { L"dither", o->dither },
        { L"framepacing", o->pacing }, { L"vsync", o->vsync },
        { L"fullscreen", o->fullscreen }, { L"skipintro", o->skipintro },
    };
    for (size_t i = 0; i < sizeof kv / sizeof kv[0]; i++) {
        wchar_t v[16];
        swprintf(v, 16, L"%d", kv[i].v);
        WritePrivateProfileStringW(g->id, kv[i].key, v, g_ini);
    }
}

/* --- keys.ini: the controls ---------------------------------------------------
 * The game's keys.ini (games\<id>\keys.ini, host/keymap.cpp in the toolkit) says which key or mouse button, and which
 * controller button ('pad_cross = a'), works each PS2 button. The game reads it at start and again when its window gets
 * the focus back. The Controls pages read and write its lines; the other lines and the comments are left as they are.
 * Player 2's controller has its own lines, 'pad2_cross = a' and so on, with the same defaults as player 1's. */

/* One row of the Controls page: the keys.ini word, the PS2 button or stick direction it is, the game's default key(s) and
 * controller input. Every row takes a controller input: a button, a trigger or a stick direction (the original game steers
 * on the left stick and can drive with the right stick's up and down, so both sticks can be moved anywhere). */
typedef struct { const char* word; const wchar_t* label; int pad; const char* key_default; const char* pad_default; } Bind;
static const Bind k_binds[] = {
    { "dpad_up", L"D-pad Up", 1, "Up", "dpup" },
    { "dpad_down", L"D-pad Down", 1, "Down", "dpdown" },
    { "dpad_left", L"D-pad Left", 1, "Left", "dpleft" },
    { "dpad_right", L"D-pad Right", 1, "Right", "dpright" },
    { "left_up", L"Left Stick Up", 1, "W", "leftstick_up" },
    { "left_down", L"Left Stick Down", 1, "S", "leftstick_down" },
    { "left_left", L"Left Stick Left", 1, "A", "leftstick_left" },
    { "left_right", L"Left Stick Right", 1, "D", "leftstick_right" },
    { "right_up", L"Right Stick Up", 1, "I", "rightstick_up" },
    { "right_down", L"Right Stick Down", 1, "K", "rightstick_down" },
    { "right_left", L"Right Stick Left", 1, "J", "rightstick_left" },
    { "right_right", L"Right Stick Right", 1, "L", "rightstick_right" },
    { "cross", L"Cross", 1, "X", "a" },
    { "circle", L"Circle", 1, "C", "b" },
    { "square", L"Square", 1, "Z", "x" },
    { "triangle", L"Triangle", 1, "V", "y" },
    { "l1", L"L1", 1, "B", "leftshoulder" },
    { "r1", L"R1", 1, "N", "rightshoulder" },
    { "l2", L"L2", 1, "M", "lefttrigger" },
    { "r2", L"R2", 1, "key 54", "righttrigger" },
    { "l3", L"L3", 1, "Left Alt", "leftstick" },
    { "r3", L"R3", 1, "Space", "rightstick" },
    { "start", L"Start", 1, "Enter", "start" },
    { "select", L"Select", 1, "Tab", "back" },
};
#define BIND_COUNT ((int)(sizeof k_binds / sizeof k_binds[0]))
/* keys.ini lines the page does not show, which only lose a key when it is given to a row here (else one key would press two
 * buttons): God of War's roll and rage, the key card and quick menu keys. */
static const char* k_hidden_words[] = { "roll", "rage", "help", "menu" };

enum { DEV_CONTROLLER, DEV_KEYBOARD };
static char g_keys_text[65536];         /* keys.ini as read */
static char g_kb[BIND_COUNT][160];      /* each row's keys, as written in keys.ini ("Space, Enter") */
static char g_pad[2][BIND_COUNT][32];   /* each row's controller button word ("a", "lefttrigger"), for player 1 and player 2 */
static int g_player;                    /* the controller the Controls page shows: 0 player 1, 1 player 2 */
static int g_p1_keyboard;               /* keys.ini 'p1_device = keyboard': the keyboard is player 1, not a controller */
static const char* k_pad_prefix[2] = { "pad_", "pad2_" };   /* the keys.ini names of player 1's and player 2's lines */
static char g_hidden[sizeof k_hidden_words / sizeof k_hidden_words[0]][160];

static void keys_path(const Game* g, wchar_t* out) {
    swprintf(out, MAX_PATH, L"%ls\\games\\%ls\\keys.ini", g->tool, g->id);
}

/* Lower case, letters and digits only: how the game compares names (keymap.cpp squash). */
static void squash(const char* in, char* out, size_t n) {
    size_t k = 0;
    for (; *in && k + 1 < n; in++)
        if ((*in >= 'a' && *in <= 'z') || (*in >= '0' && *in <= '9')) out[k++] = *in;
        else if (*in >= 'A' && *in <= 'Z') out[k++] = (char)(*in - 'A' + 'a');
    out[k] = 0;
}

static void trim(char* s) {
    size_t a = 0, b = strlen(s);
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    memmove(s, s + a, b - a);
    s[b - a] = 0;
}

/* The value of the last line 'name = value' (the game lets a later line win), or 0 when there is none. */
static int keys_get(const char* name, char* out, size_t n) {
    char want[64], line[512], key[64];
    int found = 0;
    squash(name, want, sizeof want);
    for (const char* p = g_keys_text; *p;) {
        const char* e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p += e ? (size_t)(e - p) + 1 : strlen(p);
        trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        char* hash = strstr(line, " #");
        if (hash) *hash = 0;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        squash(line, key, sizeof key);
        if (strcmp(key, want) != 0) continue;
        snprintf(out, n, "%s", eq + 1);
        trim(out);
        found = 1;
    }
    return found;
}

/* keys.ini with the line of `name` set to 'name = value' (the last one, when it is there more than once), else added at the end. */
static void keys_set(const char* name, const char* value) {
    static char out[sizeof g_keys_text];
    char want[64], line[512], key[64], repl[256];
    squash(name, want, sizeof want);
    snprintf(repl, sizeof repl, "%s = %s", name, value);
    /* find the last matching line */
    const char* last = NULL;
    size_t last_len = 0;
    for (const char* p = g_keys_text; *p;) {
        const char* e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        size_t cl = len < sizeof line ? len : sizeof line - 1;
        memcpy(line, p, cl);
        line[cl] = 0;
        trim(line);
        char* eq = line[0] && line[0] != '#' && line[0] != ';' ? strchr(line, '=') : NULL;
        if (eq) {
            *eq = 0;
            squash(line, key, sizeof key);
            if (!strcmp(key, want)) { last = p; last_len = len; }
        }
        p += e ? len + 1 : len;
    }
    size_t used = strlen(g_keys_text);
    if (last) {
        if (last_len && last[last_len - 1] == '\r') last_len--;
        snprintf(out, sizeof out, "%.*s%s%s", (int)(last - g_keys_text), g_keys_text, repl, last + last_len);
    } else {
        snprintf(out, sizeof out, "%s%s%s\r\n", g_keys_text, used && g_keys_text[used - 1] != '\n' ? "\r\n" : "", repl);
    }
    snprintf(g_keys_text, sizeof g_keys_text, "%s", out);
}

static void keys_load(const Game* g) {
    wchar_t path[MAX_PATH];
    keys_path(g, path);
    g_keys_text[0] = 0;
    FILE* f = _wfopen(path, L"rb");
    if (f) {
        size_t n = fread(g_keys_text, 1, sizeof g_keys_text - 1, f);
        g_keys_text[n] = 0;
        fclose(f);
    }
    for (int i = 0; i < BIND_COUNT; i++) {
        char name[48];
        if (!keys_get(k_binds[i].word, g_kb[i], sizeof g_kb[i])) snprintf(g_kb[i], sizeof g_kb[i], "%s", k_binds[i].key_default);
        if (!k_binds[i].pad) continue;
        for (int p = 0; p < 2; p++) {
            snprintf(name, sizeof name, "%s%s", k_pad_prefix[p], k_binds[i].word);
            if (!keys_get(name, g_pad[p][i], sizeof g_pad[p][i])) snprintf(g_pad[p][i], sizeof g_pad[p][i], "%s", k_binds[i].pad_default);
        }
    }
    static const char* hidden_defaults[] = { "Left Ctrl, Mouse X1", "none", "none", "none" };
    for (int i = 0; i < (int)(sizeof k_hidden_words / sizeof k_hidden_words[0]); i++)
        if (!keys_get(k_hidden_words[i], g_hidden[i], sizeof g_hidden[i])) snprintf(g_hidden[i], sizeof g_hidden[i], "%s", hidden_defaults[i]);
    /* 'p1_device': what plays as player 1, the P1 Controls tab's Device choice (keyboard: the first controller is player 2) */
    char dev[32] = "", c[32];
    keys_get("p1_device", dev, sizeof dev);
    squash(dev, c, sizeof c);
    g_p1_keyboard = !strcmp(c, "keyboard");
}

static int keys_save(const Game* g) {
    wchar_t path[MAX_PATH];
    keys_path(g, path);
    if (!g_keys_text[0])    /* no keys.ini yet (the game writes the full one on its first start): a short one with a heading */
        snprintf(g_keys_text, sizeof g_keys_text,
                 "# keys.ini: the controls. The launcher's Settings > P1 and P2 Controls pages write these lines; any line that is not\r\n"
                 "# here keeps the game's default. Delete the file to get every default back.\r\n");
    FILE* f = _wfopen(path, L"wb");
    if (!f) return 0;
    fwrite(g_keys_text, 1, strlen(g_keys_text), f);
    fclose(f);
    return 1;
}

/* A key as one name, the way the game compares keys: "return" and "enter" are the same key, and so on. */
static void key_canon(const char* token, char* out, size_t n) {
    static const char* same[][2] = {
        { "return", "enter" }, { "escape", "esc" }, { "kpenter", "keypadenter" }, { "numenter", "keypadenter" }, { "numpadenter", "keypadenter" },
        { "shift", "leftshift" }, { "lshift", "leftshift" }, { "rshift", "rightshift" }, { "ctrl", "leftctrl" }, { "control", "leftctrl" },
        { "lctrl", "leftctrl" }, { "rctrl", "rightctrl" }, { "alt", "leftalt" }, { "lalt", "leftalt" }, { "ralt", "rightalt" },
        { "del", "delete" }, { "ins", "insert" }, { "pgup", "pageup" }, { "pgdn", "pagedown" }, { "caps", "capslock" },
        { "lmb", "leftmouse" }, { "mouse1", "leftmouse" }, { "rmb", "rightmouse" }, { "mouse2", "rightmouse" }, { "mmb", "middlemouse" },
        { "mouse3", "middlemouse" }, { "mouse4", "mousex1" }, { "mouse5", "mousex2" }, { "comma", "key54" }, { "period", "key55" },
        { "semicolon", "key51" }, { "minus", "key45" }, { "plus", "key46" }, { "equals", "key46" },
    };
    squash(token, out, n);
    for (size_t i = 0; i < sizeof same / sizeof same[0]; i++)
        if (!strcmp(out, same[i][0])) { snprintf(out, n, "%s", same[i][1]); return; }
}

/* Does the list "Space, Enter" have the key `canon` (key_canon)? Removes it when `remove`. */
static int list_has(char* list, const char* canon, int remove) {
    char copy[160], outl[160] = "", c[48];
    int has = 0;
    snprintf(copy, sizeof copy, "%s", list);
    for (char* t = strtok(copy, ","); t; t = strtok(NULL, ",")) {
        char tok[64];
        snprintf(tok, sizeof tok, "%s", t);
        trim(tok);
        key_canon(tok, c, sizeof c);
        if (!strcmp(c, canon)) { has = 1; continue; }
        if (!strcmp(c, "none") || !tok[0]) continue;
        if (outl[0]) strncat(outl, ", ", sizeof outl - strlen(outl) - 1);
        strncat(outl, tok, sizeof outl - strlen(outl) - 1);
    }
    if (has && remove) snprintf(list, 160, "%s", outl[0] ? outl : "none");
    return has;
}

/* Gives the key `token` to row `row`. A row that had it gets this row's old keys (a swap); a hidden line only loses it. */
static void bind_key(int row, const char* token) {
    char canon[48], old[160];
    key_canon(token, canon, sizeof canon);
    snprintf(old, sizeof old, "%s", g_kb[row]);
    list_has(old, canon, 1);
    for (int i = 0; i < BIND_COUNT; i++) {
        if (i == row || !list_has(g_kb[i], canon, 1)) continue;
        if (strcmp(old, "none") != 0) {
            if (!strcmp(g_kb[i], "none")) snprintf(g_kb[i], sizeof g_kb[i], "%s", old);
            else { strncat(g_kb[i], ", ", sizeof g_kb[i] - strlen(g_kb[i]) - 1); strncat(g_kb[i], old, sizeof g_kb[i] - strlen(g_kb[i]) - 1); }
        }
        keys_set(k_binds[i].word, g_kb[i]);
    }
    for (int i = 0; i < (int)(sizeof k_hidden_words / sizeof k_hidden_words[0]); i++)
        if (list_has(g_hidden[i], canon, 1)) keys_set(k_hidden_words[i], g_hidden[i]);
    snprintf(g_kb[row], sizeof g_kb[row], "%s", token);
    keys_set(k_binds[row].word, g_kb[row]);
}

/* A controller button word as the game reads it, in one spelling (lb -> leftshoulder, ...). */
static void pad_canon(const char* word, char* out, size_t n) {
    static const char* same[][2] = {
        { "lb", "leftshoulder" }, { "rb", "rightshoulder" }, { "l1", "leftshoulder" }, { "r1", "rightshoulder" }, { "lt", "lefttrigger" },
        { "rt", "righttrigger" }, { "l2", "lefttrigger" }, { "r2", "righttrigger" }, { "zl", "lefttrigger" }, { "zr", "righttrigger" },
        { "ls", "leftstick" }, { "rs", "rightstick" }, { "l3", "leftstick" }, { "r3", "rightstick" }, { "view", "back" }, { "select", "back" },
        { "menu", "start" }, { "dpadup", "dpup" }, { "dpaddown", "dpdown" }, { "dpadleft", "dpleft" }, { "dpadright", "dpright" }, { "", "none" },
    };
    squash(word, out, n);
    for (size_t i = 0; i < sizeof same / sizeof same[0]; i++)
        if (!strcmp(out, same[i][0])) { snprintf(out, n, "%s", same[i][1]); return; }
}

/* Gives the controller button `word` to row `row` of the shown player's controller; a row that had it gets this row's old button. */
static void bind_pad(int row, const char* word) {
    char want[32], c[32], name[48];
    char (*pad)[32] = g_pad[g_player];
    pad_canon(word, want, sizeof want);
    for (int i = 0; i < BIND_COUNT; i++) {
        if (i == row || !k_binds[i].pad) continue;
        pad_canon(pad[i], c, sizeof c);
        if (strcmp(c, want) != 0 || !strcmp(want, "none")) continue;
        snprintf(pad[i], sizeof pad[i], "%s", pad[row]);
        snprintf(name, sizeof name, "%s%s", k_pad_prefix[g_player], k_binds[i].word);
        keys_set(name, pad[i]);
    }
    snprintf(pad[row], sizeof pad[row], "%s", want);
    snprintf(name, sizeof name, "%s%s", k_pad_prefix[g_player], k_binds[row].word);
    keys_set(name, pad[row]);
}

/* How a controller button is shown: the Xbox names, which is what XInput pads (and SDL's positions) use. */
static const wchar_t* pad_display(const char* word) {
    static const struct { const char* w; const wchar_t* d; } names[] = {
        { "a", L"A" }, { "b", L"B" }, { "x", L"X" }, { "y", L"Y" }, { "back", L"View" }, { "start", L"Menu" }, { "guide", L"Guide" },
        { "leftstick", L"LS (press)" }, { "rightstick", L"RS (press)" }, { "leftshoulder", L"LB" }, { "rightshoulder", L"RB" },
        { "lefttrigger", L"LT" }, { "righttrigger", L"RT" }, { "dpup", L"D-pad Up" }, { "dpdown", L"D-pad Down" },
        { "dpleft", L"D-pad Left" }, { "dpright", L"D-pad Right" }, { "misc1", L"Share" }, { "touchpad", L"Touchpad" },
        { "paddle1", L"Paddle 1" }, { "paddle2", L"Paddle 2" }, { "paddle3", L"Paddle 3" }, { "paddle4", L"Paddle 4" }, { "none", L"None" },
        { "leftstickup", L"LS Up" }, { "leftstickdown", L"LS Down" }, { "leftstickleft", L"LS Left" }, { "leftstickright", L"LS Right" },
        { "rightstickup", L"RS Up" }, { "rightstickdown", L"RS Down" }, { "rightstickleft", L"RS Left" }, { "rightstickright", L"RS Right" },
    };
    char c[32];
    pad_canon(word, c, sizeof c);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(c, names[i].w)) return names[i].d;
    return L"?";
}

/* A key list as shown: keys written as SDL scancode numbers get their symbol back. */
static void kb_display(const char* list, wchar_t* out, size_t n) {
    static const struct { const char* k; const char* d; } sym[] = {
        { "key 45", "-" }, { "key 46", "=" }, { "key 47", "[" }, { "key 48", "]" }, { "key 49", "\\" }, { "key 51", ";" },
        { "key 52", "'" }, { "key 53", "`" }, { "key 54", "," }, { "key 55", "." }, { "key 56", "/" },
        { "key 84", "Keypad /" }, { "key 85", "Keypad *" }, { "key 86", "Keypad -" }, { "key 87", "Keypad +" }, { "key 99", "Keypad ." },
    };
    char buf[200] = "";
    char copy[160];
    snprintf(copy, sizeof copy, "%s", list);
    for (char* t = strtok(copy, ","); t; t = strtok(NULL, ",")) {
        char tok[64];
        snprintf(tok, sizeof tok, "%s", t);
        trim(tok);
        const char* show = tok;
        for (size_t i = 0; i < sizeof sym / sizeof sym[0]; i++)
            if (!_stricmp(tok, sym[i].k)) show = sym[i].d;
        if (buf[0]) strncat(buf, ", ", sizeof buf - strlen(buf) - 1);
        strncat(buf, show, sizeof buf - strlen(buf) - 1);
    }
    if (!buf[0] || !_stricmp(buf, "none")) snprintf(buf, sizeof buf, "None");
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)n);
}

/* The keys.ini name of a key from WM_KEYDOWN / WM_SYSKEYDOWN, or NULL for a key that cannot be given (F1, F2, F5, F8, F9 and
 * F11 belong to the game window: quick menu, picture, quick save and load, problem report, full screen). */
static const char* vk_token(WPARAM vk, LPARAM lp) {
    static char buf[24];
    int ext = (lp >> 24) & 1, scan = (lp >> 16) & 0xFF;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) {
        int f = (int)(vk - VK_F1) + 1;
        if (f == 1 || f == 2 || f == 5 || f == 8 || f == 9 || f == 11) return NULL;
        snprintf(buf, sizeof buf, "F%d", f);
        return buf;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(buf, sizeof buf, "Keypad %d", (int)(vk - VK_NUMPAD0)); return buf; }
    switch (vk) {
    case VK_SPACE: return "Space";
    case VK_RETURN: return ext ? "Keypad Enter" : "Enter";
    case VK_ESCAPE: return "Esc";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_CAPITAL: return "CapsLock";
    case VK_SHIFT: return scan == 0x36 ? "Right Shift" : "Left Shift";
    case VK_CONTROL: return ext ? "Right Ctrl" : "Left Ctrl";
    case VK_MENU: return ext ? "Right Alt" : "Left Alt";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    /* the punctuation keys (by their place on a US keyboard) and the keypad's operator keys: written as SDL scancode numbers,
     * since names like "/" or "Keypad *" are not ones the game reads back as that one key */
    case VK_DIVIDE: return "key 84";
    case VK_MULTIPLY: return "key 85";
    case VK_SUBTRACT: return "key 86";
    case VK_ADD: return "key 87";
    case VK_DECIMAL: return "key 99";
    case VK_OEM_MINUS: return "key 45";
    case VK_OEM_PLUS: return "key 46";
    case VK_OEM_4: return "key 47";
    case VK_OEM_6: return "key 48";
    case VK_OEM_5: return "key 49";
    case VK_OEM_1: return "key 51";
    case VK_OEM_7: return "key 52";
    case VK_OEM_3: return "key 53";
    case VK_OEM_COMMA: return "key 54";
    case VK_OEM_PERIOD: return "key 55";
    case VK_OEM_2: return "key 56";
    }
    return NULL;
}

/* --- games.txt -------------------------------------------------------------- */

static int is_dir(const wchar_t* p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int exists(const wchar_t* p) {
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* Split off the next '|' field; returns the rest, or NULL at the end. */
static wchar_t* next_field(wchar_t* s) {
    wchar_t* bar = wcschr(s, L'|');
    if (!bar) return NULL;
    *bar = 0;
    return bar + 1;
}

static void load_games(void) {
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\games.txt", g_dir);
    FILE* f = _wfopen(path, L"rt, ccs=UTF-8");
    if (!f) return;
    wchar_t line[1024];
    while (g_count < MAX_GAMES && fgetws(line, 1024, f)) {
        size_t n = wcslen(line);
        while (n && (line[n - 1] == L'\n' || line[n - 1] == L'\r')) line[--n] = 0;
        if (!n || line[0] == L'#') continue;
        wchar_t* sub = next_field(line);
        wchar_t* exe = sub ? next_field(sub) : NULL;
        wchar_t* cover = exe ? next_field(exe) : NULL;
        wchar_t* id = cover ? next_field(cover) : NULL;
        wchar_t* elf = id ? next_field(id) : NULL;
        if (!elf || !*id || !*elf) continue;

        Game* g = &g_games[g_count];
        memset(g, 0, sizeof *g);
        wcsncpy(g->title, line, 95);
        wcsncpy(g->subtitle, sub, 127);
        wcsncpy(g->cover_name, cover, 95);
        wcsncpy(g->id, id, 31);
        wchar_t* crc = next_field(elf);   /* optional; splits it off the ELF name */
        if (crc && *crc) g->crc = (DWORD)wcstoul(crc, NULL, 16), g->has_crc = 1;
        wcsncpy(g->elf, elf, 63);

        wchar_t joined[MAX_PATH];
        swprintf(joined, MAX_PATH, L"%ls\\%ls", g_dir, exe);
        for (wchar_t* c = joined; *c; c++) if (*c == L'/') *c = L'\\';
        if (!GetFullPathNameW(joined, MAX_PATH, g->exe, NULL)) continue;
        /* The toolkit folder is everything before \build\ in the path. */
        wchar_t* build = wcsstr(g->exe, L"\\build\\");
        if (!build) continue;
        size_t tl = (size_t)(build - g->exe);
        wcsncpy(g->tool, g->exe, tl);
        g->tool[tl] = 0;
        wcsncpy(g->exe_rel, build + 1, MAX_PATH - 1);
        swprintf(joined, MAX_PATH, L"%ls\\..\\%ls-options", g->tool, g->id);
        GetFullPathNameW(joined, MAX_PATH, g->opts, NULL);

        g->available = exists(g->exe);
        g->toggles = is_dir(g->opts);
        swprintf(joined, MAX_PATH, L"%ls\\menus43.txt", g->opts);
        g->menus = exists(joined);
        swprintf(joined, MAX_PATH, L"%ls\\hud43.txt", g->opts);
        g->hud = exists(joined);
        load_options(g);
        g->cover = load_cover(g->id);
        g_count++;
    }
    fclose(f);
}

/* --- cover download ----------------------------------------------------------
 * Runs on its own thread so a slow or absent connection never holds the menu
 * up; each finished cover is handed to the window with WM_COVER_READY. */
#define WM_COVER_READY (WM_APP + 1)
HRESULT WINAPI URLDownloadToFileW(void* caller, const WCHAR* url, const WCHAR* file,
                                  DWORD reserved, void* callback);

/* Smuggler's Run 2: Hostile Territory icon by Haxy on SteamGridDB (https://www.steamgriddb.com/icon/31611), a square PNG. An empty URL
 * downloads nothing (a square covers\icon.png put there by hand is still used). */
#define ICON_URL L"https://cdn2.steamgriddb.com/icon/30de3848a3d427de6774066e0b9b09bc.png"
/* --- the launcher's icon -----------------------------------------------------
 * The icon (credited in README.md) is not ours to redistribute, so it is not built into this program: the first start
 * downloads it from SteamGridDB on the cover thread and turns it into covers\icon.ico (PNG images of 256, 48, 32 and 16
 * pixels). The launcher's window takes it when it is ready (WM_ICON_READY), the game window gets the same file
 * (PS2_WINDOW_ICON, prepare), and it is written into this program's file for the next start (embed_icon). A square
 * covers\icon.png put there by hand is used instead of the download. */
#define WM_ICON_READY (WM_APP + 2)
typedef struct { DWORD d1; WORD d2, d3; BYTE d4[8]; } GdipClsid;
int WINAPI GdipCreateBitmapFromScan0(INT w, INT h, INT stride, INT format, BYTE* scan0, void** bitmap);
int WINAPI GdipGetImageGraphicsContext(void* image, void** graphics);
int WINAPI GdipGraphicsClear(void* graphics, DWORD argb);
int WINAPI GdipSetPixelOffsetMode(void* graphics, int mode);
int WINAPI GdipSaveImageToFile(void* image, const WCHAR* file, const GdipClsid* encoder, const void* params);

static void icon_path(wchar_t* out, const wchar_t* ext) { swprintf(out, MAX_PATH, L"%ls\\covers\\icon.%ls", g_dir, ext); }

static int file_exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

/* covers\icon.png -> covers\icon.ico. Returns 1 when the .ico was written. */
static int make_icon(void) {
    static const GdipClsid png_encoder = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };
    static const int sizes[4] = { 256, 48, 32, 16 };
    wchar_t png[MAX_PATH], ico[MAX_PATH], part[MAX_PATH], tmp[MAX_PATH];
    icon_path(png, L"png");
    icon_path(ico, L"ico");
    swprintf(part, MAX_PATH, L"%ls.part", ico);
    void* src = NULL;
    if (GdipLoadImageFromFile(png, &src) != 0 || !src) return 0;
    BYTE* blob[4] = { 0 };
    DWORD len[4] = { 0 };
    int ok = 1;
    for (int i = 0; i < 4 && ok; i++) {
        void* bmp = NULL;
        void* g = NULL;
        ok = GdipCreateBitmapFromScan0(sizes[i], sizes[i], 0, 0x0026200A /* PixelFormat32bppARGB */, NULL, &bmp) == 0 && bmp;
        if (ok && GdipGetImageGraphicsContext(bmp, &g) == 0 && g) {
            GdipGraphicsClear(g, 0);
            GdipSetInterpolationMode(g, 7);  /* high-quality bicubic */
            GdipSetPixelOffsetMode(g, 4);    /* half: no dark seam at the edges */
            GdipDrawImageRectI(g, src, 0, 0, sizes[i], sizes[i]);
            GdipDeleteGraphics(g);
        }
        swprintf(tmp, MAX_PATH, L"%ls.%d.png", ico, sizes[i]);
        ok = ok && GdipSaveImageToFile(bmp, tmp, &png_encoder, NULL) == 0;
        if (bmp) GdipDisposeImage(bmp);
        if (ok) {
            FILE* f = _wfopen(tmp, L"rb");
            ok = f != NULL;
            if (ok) {
                fseek(f, 0, SEEK_END);
                len[i] = (DWORD)ftell(f);
                fseek(f, 0, SEEK_SET);
                blob[i] = (BYTE*)malloc(len[i] ? len[i] : 1);
                ok = blob[i] && fread(blob[i], 1, len[i], f) == len[i];
                fclose(f);
            }
        }
        DeleteFileW(tmp);
    }
    GdipDisposeImage(src);
    if (ok) {
        FILE* f = _wfopen(part, L"wb");
        ok = f != NULL;
        if (ok) {
            WORD head[3] = { 0, 1, 4 };  /* reserved, type 1 (icon), 4 images */
            fwrite(head, 2, 3, f);
            DWORD offset = 6 + 16 * 4;
            for (int i = 0; i < 4; i++) {
                BYTE dim = sizes[i] >= 256 ? 0 : (BYTE)sizes[i];  /* 0 means 256 */
                BYTE e[4] = { dim, dim, 0, 0 };
                WORD planes_bits[2] = { 1, 32 };
                fwrite(e, 1, 4, f);
                fwrite(planes_bits, 2, 2, f);
                fwrite(&len[i], 4, 1, f);
                fwrite(&offset, 4, 1, f);
                offset += len[i];
            }
            for (int i = 0; i < 4; i++) ok = ok && fwrite(blob[i], 1, len[i], f) == len[i];
            ok = fclose(f) == 0 && ok;
        }
        ok = ok && MoveFileExW(part, ico, MOVEFILE_REPLACE_EXISTING);
        if (!ok) DeleteFileW(part);
    }
    for (int i = 0; i < 4; i++) free(blob[i]);
    return ok;
}

/* Once the icon is here, it goes into this program's own file too, so that Explorer, shortcuts and the taskbar show it from
 * the next start on. A running program cannot change its file, but it can rename it: this one moves itself to <name>.old,
 * copies that back to <name> and writes the icon into the copy's resources (group 1, as a built-in icon would be). The next
 * start runs the copy and deletes the .old file. If a step fails (a read-only folder, say), the old file is put back and the
 * icon stays a window icon only. The icon is only written into the player's own copy, never into a release. */
void WINAPI SHChangeNotify(LONG event, UINT flags, const void* item1, const void* item2);

#pragma pack(push, 2)
typedef struct { BYTE w, h, colors, reserved; WORD planes, bits; DWORD bytes; WORD id; } GroupIconEntry;
#pragma pack(pop)

static void embed_icon(void) {
    wchar_t self[MAX_PATH], old[MAX_PATH], ico[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return;
    swprintf(old, MAX_PATH, L"%ls.old", self);
    DeleteFileW(old);  /* left by the start that wrote the icon */
    if (FindResourceW(NULL, MAKEINTRESOURCEW(1), (LPCWSTR)RT_GROUP_ICON)) return;  /* already in */
    icon_path(ico, L"ico");
    FILE* f = _wfopen(ico, L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    BYTE* data = size > 6 ? (BYTE*)malloc((size_t)size) : NULL;
    int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    WORD count = ok ? *(WORD*)(data + 4) : 0;
    ok = ok && *(WORD*)(data + 2) == 1 && count > 0 && count <= 16 && 6 + 16 * (long)count <= size;
    for (WORD i = 0; ok && i < count; i++) {
        const BYTE* e = data + 6 + 16 * i;
        DWORD bytes = *(const DWORD*)(e + 8), at = *(const DWORD*)(e + 12);
        ok = at <= (DWORD)size && bytes <= (DWORD)size - at;
    }
    if (ok && MoveFileExW(self, old, MOVEFILE_REPLACE_EXISTING)) {
        int copied = CopyFileW(old, self, FALSE);
        HANDLE h = copied ? BeginUpdateResourceW(self, FALSE) : NULL;
        int written = h != NULL;
        GroupIconEntry group_entries[16];
        BYTE group[6 + sizeof group_entries];
        for (WORD i = 0; written && i < count; i++) {
            const BYTE* e = data + 6 + 16 * i;
            DWORD bytes = *(const DWORD*)(e + 8), at = *(const DWORD*)(e + 12);
            GroupIconEntry* g = &group_entries[i];
            g->w = e[0], g->h = e[1], g->colors = e[2], g->reserved = 0;
            g->planes = *(const WORD*)(e + 4), g->bits = *(const WORD*)(e + 6), g->bytes = bytes, g->id = (WORD)(i + 1);
            written = UpdateResourceW(h, (LPCWSTR)RT_ICON, MAKEINTRESOURCEW(i + 1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                      data + at, bytes);
        }
        if (written) {
            WORD head[3] = { 0, 1, count };
            memcpy(group, head, 6);
            memcpy(group + 6, group_entries, sizeof(GroupIconEntry) * count);
            written = UpdateResourceW(h, (LPCWSTR)RT_GROUP_ICON, MAKEINTRESOURCEW(1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                                      group, (DWORD)(6 + sizeof(GroupIconEntry) * count));
        }
        if (h) written = EndUpdateResourceW(h, !written) && written;
        if (written) {
            SHChangeNotify(0x00002000L /* SHCNE_UPDATEITEM */, 0x0005 /* SHCNF_PATHW */, self, NULL);
        } else {  /* put the program back as it was */
            if (copied) DeleteFileW(self);
            MoveFileExW(old, self, MOVEFILE_REPLACE_EXISTING);
        }
    }
    free(data);
}

/* On the cover thread: the .ico, downloading the PNG first when there is none (tells the window when it is ready),
 * then into this program's file for the next start (embed_icon). */
static void fetch_icon(HWND hwnd) {
    wchar_t png[MAX_PATH], ico[MAX_PATH], part[MAX_PATH];
    icon_path(png, L"png");
    icon_path(ico, L"ico");
    if (!file_exists(ico)) {
        if (!file_exists(png)) {
            swprintf(part, MAX_PATH, L"%ls.part", png);
            if (!ICON_URL[0] || URLDownloadToFileW(NULL, ICON_URL, part, 0, NULL) != 0 || !MoveFileExW(part, png, MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(part);
                return;
            }
        }
        if (!make_icon()) return;
        PostMessageW(hwnd, WM_ICON_READY, 0, 0);
    }
    embed_icon();
}

/* The launcher's window takes covers\icon.ico (title bar, taskbar, Alt+Tab), when there is one. */
static void set_window_icon(HWND hwnd) {
    wchar_t ico[MAX_PATH];
    icon_path(ico, L"ico");
    HICON big = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    HICON small = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

static DWORD WINAPI fetch_covers(LPVOID param) {
    HWND hwnd = (HWND)param;
    wchar_t dir[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%ls\\covers", g_dir);
    CreateDirectoryW(dir, NULL);
    fetch_icon(hwnd);
    for (int i = 0; i < g_count; i++) {
        Game* g = &g_games[i];
        if (g->cover || !g->cover_name[0]) continue;
        wchar_t url[512] = L"https://thumbnails.libretro.com/Sony%20-%20PlayStation%202/Named_Boxarts/";
        size_t n = wcslen(url);
        for (const wchar_t* c = g->cover_name; *c && n < 500; c++) {
            if (*c == L' ') { wcscpy(url + n, L"%20"); n += 3; }
            else url[n++] = *c;
        }
        wcscpy(url + n, L".png");
        wchar_t file[MAX_PATH], part[MAX_PATH];
        swprintf(file, MAX_PATH, L"%ls\\%ls.png", dir, g->id);
        swprintf(part, MAX_PATH, L"%ls.part", file);
        if (URLDownloadToFileW(NULL, url, part, 0, NULL) == 0 &&
            MoveFileExW(part, file, MOVEFILE_REPLACE_EXISTING))
            PostMessageW(hwnd, WM_COVER_READY, (WPARAM)i, 0);
        else
            DeleteFileW(part);
    }
    return 0;
}

/* --- starting a game ---------------------------------------------------------
 * prepare() does everything mc2.cmd does before it starts ps2run: it writes
 * boot.txt, sets the PS2_* variables in this process (the game inherits them;
 * the ones that are off are removed, as mc2.cmd's empty "set" does), and
 * builds the command line. Returns 0 with a message in err on failure. */
typedef struct {
    wchar_t cmd[2048];
    wchar_t name[160];      /* " widescreen 60fps ..." or " original", as mc2.cmd */
    wchar_t title[256];
    wchar_t boot[MAX_PATH];
    char boot_text[1024];
} Launch;

/* --- the player's disc image -------------------------------------------------
 * ps2run reads plain ISO images (2048-byte sectors) and runs the program file on the disc that the recompiled code was made
 * from, byte for byte. Any other disc (another game, another region or release, a damaged copy) gets past the start and
 * then shows a black screen for good, with nothing to tell the player why. So the disc is checked before it is used, from
 * the ISO 9660 file system: the game's program file (games.txt: ELF name, e.g. SLUS_200.63) has to be in the root folder,
 * and when games.txt gives its CRC-32 too, the file has to match it. A CHD ("MComprHD"), a raw BIN with 2352-byte sectors
 * or a CUE sheet has no ISO 9660 volume descriptor at sector 16 and is turned away with the chdman step. */
static int read_at(HANDLE h, ULONGLONG at, void* buf, DWORD len) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)at;
    DWORD got = 0;
    return SetFilePointerEx(h, li, NULL, FILE_BEGIN) && ReadFile(h, buf, len, &got, NULL) && got == len;
}

static DWORD crc32_update(DWORD crc, const BYTE* p, size_t n) {
    static DWORD table[256];
    if (!table[1])
        for (DWORD i = 0; i < 256; i++) {
            DWORD c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* 1 when name (a directory record's "NAME.EXT;1") is want, ignoring case and the ";1" version. */
static int iso_name_is(const BYTE* name, int len, const char* want) {
    int i = 0;
    for (; i < len && name[i] != ';'; i++)
        if (!want[i] || towlower(name[i]) != towlower((unsigned char)want[i])) return 0;
    return want[i] == 0;
}

typedef struct { DWORD lba, size; int found; } IsoFile;

static const wchar_t k_not_iso[] =
    L"%ls is not a plain .iso disc image, so the game cannot read it.\n\n"
    L"The game is on a CD: a .chd can be unpacked with MAME's chdman:\n"
    L"    chdman extractcd -i \"game.chd\" -o \"game.cue\" -ob \"game.iso\"\n\n"
    L"Then choose the .iso.";

/* 1 when path is a disc image of this game that the build can run; else 0 with the reason for the player in msg. */
static int check_disc(const Game* g, const wchar_t* path, wchar_t* msg, size_t msgn) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* file = slash ? slash + 1 : path;
    const wchar_t* game = g->cover_name[0] ? g->cover_name : g->title;   /* "Smuggler's Run 2 - Hostile Territory (USA)" */
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        swprintf(msg, msgn, L"%ls could not be opened (error %lu).", file, GetLastError());
        return 0;
    }
    static BYTE sector[2048];
    if (!read_at(h, 16 * 2048, sector, 2048) || sector[0] != 1 || memcmp(sector + 1, "CD001", 5) != 0) {
        CloseHandle(h);
        swprintf(msg, msgn, k_not_iso, file);
        return 0;
    }
    /* the root folder's directory record sits at byte 156 of the primary volume descriptor */
    DWORD root_lba = *(const DWORD*)(sector + 156 + 2), root_size = *(const DWORD*)(sector + 156 + 10);
    if (root_size > 1024 * 1024) root_size = 1024 * 1024;
    char elf[64];
    WideCharToMultiByte(CP_ACP, 0, g->elf, -1, elf, sizeof elf, NULL, NULL);
    IsoFile program = { 0 }, cnf = { 0 };
    for (DWORD off = 0; off < root_size; off += 2048) {
        if (!read_at(h, ((ULONGLONG)root_lba << 11) + off, sector, 2048)) break;
        for (DWORD pos = 0; pos < 2048 && sector[pos] >= 34 && pos + sector[pos] <= 2048; pos += sector[pos]) {
            const BYTE* r = sector + pos;
            if (r[25] & 2) continue;  /* a folder */
            IsoFile* hit = iso_name_is(r + 33, r[32], elf) ? &program : iso_name_is(r + 33, r[32], "SYSTEM.CNF") ? &cnf : NULL;
            if (hit) hit->lba = *(const DWORD*)(r + 2), hit->size = *(const DWORD*)(r + 10), hit->found = 1;
        }
    }
    if (!program.found) {
        /* name what the disc is instead, from its SYSTEM.CNF ("BOOT2 = cdrom0:\SLES_123.45;1") */
        wchar_t other[80] = L"";
        char text[2049] = "";
        DWORD n = cnf.size < 2048 ? cnf.size : 2048;
        if (cnf.found && n && read_at(h, (ULONGLONG)cnf.lba << 11, text, n)) {
            text[n] = 0;
            char* b = strstr(text, "cdrom0:\\");
            if (!b) b = strstr(text, "cdrom:\\");
            if (b) {
                b = strchr(b, '\\') + 1;
                size_t len = strcspn(b, ";\r\n");
                if (len > 60) len = 60;
                b[len] = 0;
                swprintf(other, 80, L"%hs", b);
            }
        }
        CloseHandle(h);
        if (other[0])
            swprintf(msg, msgn, L"%ls is not %ls: it holds another game (%ls).\n\nThe game here is made from %ls's %ls and runs "
                     L"on that disc only. Choose your disc image of it.", file, game, other, game, g->elf);
        else
            swprintf(msg, msgn, L"%ls is not %ls: there is no PS2 game on it.\n\nThe game here is made from %ls's %ls and runs "
                     L"on that disc only. Choose your disc image of it.", file, game, game, g->elf);
        return 0;
    }
    if (g->has_crc) {
        static BYTE chunk[64 * 1024];
        DWORD crc = 0, left = program.size;
        ULONGLONG at = (ULONGLONG)program.lba << 11;
        int ok = 1;
        while (ok && left) {
            DWORD n = left < sizeof chunk ? left : (DWORD)sizeof chunk;
            ok = read_at(h, at, chunk, n);
            if (ok) crc = crc32_update(crc, chunk, n), at += n, left -= n;
        }
        if (!ok || crc != g->crc) {
            CloseHandle(h);
            swprintf(msg, msgn, L"%ls has %ls on it, but not the version the game here is made from, so it would stop at a "
                     L"black screen.\n\nIt may be another release of %ls, or a damaged or modified copy. Choose an untouched "
                     L"disc image of %ls.", file, g->elf, g->title, game);
            return 0;
        }
    }
    CloseHandle(h);
    return 1;
}

/* The player's disc image: a standard Open dialog, the choice kept in settings.ini. Returns 0 when cancelled. ps2run reads
 * plain ISO images (2048-byte sectors); a CHD or BIN/CUE has to be turned into one first (see README), so anything else is
 * refused here with that step and the dialog opens again. */
static int choose_disc(HWND hwnd, Game* g) {
    wchar_t file[MAX_PATH];
    wcsncpy(file, g->disc, MAX_PATH - 1);
    file[MAX_PATH - 1] = 0;
    wchar_t title[160];
    swprintf(title, 160, L"Choose your %ls disc image", g->title);
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PS2 disc image (*.iso)\0*.iso\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    for (;;) {
        if (!GetOpenFileNameW(&ofn)) return 0;
        wchar_t msg[1024];
        if (check_disc(g, file, msg, 1024)) break;
        MessageBoxW(hwnd, msg, g->title, MB_ICONERROR);
    }
    wcsncpy(g->disc, file, MAX_PATH - 1);
    g->disc[MAX_PATH - 1] = 0;
    WritePrivateProfileStringW(g->id, L"disc", g->disc, g_ini);
    return 1;
}

static int prepare(const Game* g, Launch* L, wchar_t* err, size_t errn) {
    const Options* o = &g->o;
    int wide = g->toggles && o->wide;
    int sixty = g->toggles && o->sixty;
    int hud43 = wide && g->hud && o->hud43;   /* the 4:3 HUD only does something with widescreen */
    int menus43 = wide && g->menus && o->menus43;
    int dither = o->dither && !o->truecolor;   /* true colour shows the 32-bit picture, which has no dither */

    wchar_t gd[96];
    swprintf(gd, 96, L"games\\%ls", g->id);
    wchar_t p[MAX_PATH];
    if (!g->disc[0] || !exists(g->disc)) {
        swprintf(err, errn, L"No disc image chosen (or it has moved): press Play to choose it.");
        return 0;
    }
    swprintf(p, MAX_PATH, L"%ls\\%ls\\patches.txt", g->tool, gd);
    if (!exists(p)) {
        swprintf(err, errn, L"%ls\\patches.txt is missing: rebuild the game with port.sh.", gd);
        return 0;
    }

    L->name[0] = 0;
    if (wide) wcscat(L->name, L" widescreen");
    if (hud43) wcscat(L->name, L" hud43");
    if (menus43) wcscat(L->name, L" menus43");
    if (sixty) wcscat(L->name, L" 60fps");
    if (o->scale != 1) { wchar_t s[8]; swprintf(s, 8, L" %dx", o->scale); wcscat(L->name, s); }
    if (o->look) { wcscat(L->name, L" "); wcscat(L->name, k_look_words[o->look]); }
    if (o->texfilter) { wcscat(L->name, L" "); wcscat(L->name, k_filter_words[o->texfilter]); }
    if (o->smooth) wcscat(L->name, L" smoothtextures");
    if (o->zfrac) wcscat(L->name, L" zfight");
    if (o->truecolor) wcscat(L->name, L" truecolor");
    if (!o->truecolor && !o->dither) wcscat(L->name, L" nodither");
    if (o->pacing) wcscat(L->name, L" framepacing");
    if (o->vsync) wcscat(L->name, L" vsync");
    if (o->skipintro) wcscat(L->name, L" skipintro");
    if (!L->name[0]) wcscpy(L->name, L" original");
    swprintf(L->title, 256, L"%ls (%ls)", g->title, L->name + 1);

    /* The patch lines of the options that are on (<id>-options/widescreen.txt and 60fps.txt, ps2run --patches lines),
     * copied into boot.txt; only lines that start with a digit (the runtime's reader stops at anything else, so the files'
     * # comments stay out). In Smuggler's Run 2 they set the game's own aspect factor (the PCSX2 community
     * patch) and frame interval, and the option word of the code built into the game program (sr2-options/toggles.txt). */
    L->boot[0] = 0;
    L->boot_text[0] = 0;
    if (g->toggles) {
        swprintf(L->boot, MAX_PATH, L"%ls\\boot.txt", g->opts);
        /* With 4:3 Menus the widescreen patch follows the picture (widescreen43.txt: the runtime's option word 000FFF00);
         * the Fixed Widescreen HUD's code (built in from hud43.pnach) is switched by the word at 000FFF04. */
        const wchar_t* files[2] = { menus43 ? L"widescreen43.txt" : L"widescreen.txt", L"60fps.txt" };
        int on[2] = { wide, sixty };
        snprintf(L->boot_text, sizeof L->boot_text, "2 000FFF04 2 %d\r\n", hud43);
        for (int f = 0; f < 2; f++) {
            if (!on[f]) continue;
            wchar_t fp[MAX_PATH];
            swprintf(fp, MAX_PATH, L"%ls\\%ls", g->opts, files[f]);
            FILE* pf = _wfopen(fp, L"rb");
            if (!pf) {
                swprintf(err, errn, L"%ls is missing.", fp);
                return 0;
            }
            char line[256];
            while (fgets(line, sizeof line, pf)) {
                if (line[0] < '0' || line[0] > '9') continue;
                size_t ln = strlen(line);
                while (ln && (line[ln - 1] == '\n' || line[ln - 1] == '\r')) line[--ln] = 0;
                if (strlen(L->boot_text) + ln + 3 < sizeof L->boot_text) {
                    strcat(L->boot_text, line);
                    strcat(L->boot_text, "\r\n");
                }
            }
            fclose(pf);
        }
        HANDLE h = CreateFileW(L->boot, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD wrote = 0;
        int ok = h != INVALID_HANDLE_VALUE &&
                 WriteFile(h, L->boot_text, (DWORD)strlen(L->boot_text), &wrote, NULL) &&
                 wrote == strlen(L->boot_text);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!ok) {
            swprintf(err, errn, L"Could not write %ls.", L->boot);
            return 0;
        }
    }

    wchar_t scale[8];
    swprintf(scale, 8, L"%d", o->scale);
    SetEnvironmentVariableW(L"PS2_SCALE", scale);
    SetEnvironmentVariableW(L"PS2_TRUE_COLOR", o->truecolor ? L"1" : NULL);
    SetEnvironmentVariableW(L"PS2_DITHER", dither ? L"1" : NULL);
    SetEnvironmentVariableW(L"PS2_LOOK", k_look_words[o->look == LOOK_FSR ? 0 : o->look]);
    SetEnvironmentVariableW(L"PS2_FSR", o->look == LOOK_FSR ? L"1" : NULL);
    SetEnvironmentVariableW(L"PS2_TEX_FILTER", o->texfilter ? k_filter_words[o->texfilter] : NULL);
    SetEnvironmentVariableW(L"PS2_TEX_SS", o->smooth ? L"1" : NULL);
    /* Fix Distant Z-Fighting: 16-bit depth keeps its fractions (runtime gs.wgsl, DEPTH_FRAC); not something the PS2 does */
    SetEnvironmentVariableW(L"PS2_DEPTH_FRAC", o->zfrac ? L"1" : NULL);
    SetEnvironmentVariableW(L"PS2_PACE", o->pacing ? L"clock" : NULL);
    SetEnvironmentVariableW(L"PS2_PRESENT_MODE", o->vsync ? L"fifo" : NULL);
    SetEnvironmentVariableW(L"PS2_WINDOW_TITLE", L->title);
    /* the controls (Settings > Controls): the game's keys.ini, which is where the game looks anyway (next to the memory card) */
    wchar_t keys[MAX_PATH];
    keys_path(g, keys);
    SetEnvironmentVariableW(L"PS2_KEYS", keys);
    /* the game window shows the launcher's icon, covers\icon.ico, once it has been downloaded (fetch_icon) */
    wchar_t icon[MAX_PATH];
    icon_path(icon, L"ico");
    SetEnvironmentVariableW(L"PS2_WINDOW_ICON", file_exists(icon) ? icon : NULL);
    /* 4:3 menus and videos: <id>-options/menus43.txt holds the runtime's NAME=VALUE lines
     * (PS2_WIDE_ONLY_IF, PS2_WIDE_WORD) that tell a race from a menu. */
    SetEnvironmentVariableW(L"PS2_WIDE_ONLY_IF", NULL);
    SetEnvironmentVariableW(L"PS2_WIDE_WORD", NULL);
    SetEnvironmentVariableW(L"PS2_WIDE_FLOAT", NULL);
    /* Smuggler's Run 2 draws a 640-pixel frame: where the Fixed Widescreen HUD's edges are (runtime gif.cpp, hud_mark) */
    SetEnvironmentVariableW(L"PS2_HUD_WIDTH", L"640");
    SetEnvironmentVariableW(L"PS2_SKIP_TEXTURE", NULL);
    SetEnvironmentVariableW(L"PS2_SKIP_SUBTRACT", NULL);  /* (Remove Car Outlines and its nooutline.txt were taken out 2026-11-08) */
    if (menus43) {
        wchar_t mp[MAX_PATH], line[256];
        swprintf(mp, MAX_PATH, L"%ls\\menus43.txt", g->opts);
        FILE* mf = _wfopen(mp, L"rt, ccs=UTF-8");
        while (mf && fgetws(line, 256, mf)) {
            size_t ln = wcslen(line);
            while (ln && (line[ln - 1] == L'\n' || line[ln - 1] == L'\r' || line[ln - 1] == L' ')) line[--ln] = 0;
            wchar_t* eq = wcschr(line, L'=');
            if (!ln || line[0] == L'#' || !eq) continue;
            *eq = 0;
            SetEnvironmentVariableW(line, eq + 1);
        }
        if (mf) fclose(mf);
    }

    int n = swprintf(L->cmd, 2048,
        L"\"%ls\" --disc \"%ls\" %ls --game-dir \"%ls\" --memcard \"%ls\\%ls.ps2\" --patches \"%ls\\patches.txt\"",
        g->exe_rel, g->disc, o->fullscreen ? L"--fullscreen" : L"--window", gd, gd, g->id, gd);
    if (g->toggles) n += swprintf(L->cmd + n, 2048 - n, L" --patches \"%ls\"", L->boot);
    if (wide) n += swprintf(L->cmd + n, 2048 - n, L" --widescreen");
    /* Skip Intro: the game asks for its developer switch "nofmv" to leave out the Rockstar and Angel Studios logos and
     * the intro movie (and then starts the menus' music itself); sr2-options/skipintro.pnach (built in) makes those two
     * questions ask for "skipintro" instead, so the menus keep their moving background, which -nofmv would take away too.
     * --arg hands it to the game as argv[1]. */
    if (o->skipintro) n += swprintf(L->cmd + n, 2048 - n, L" --arg -skipintro");
    return 1;
}

static void launch(HWND hwnd) {
    if (g_child || g_sel < 0 || g_sel >= g_count) return;
    Game* g = &g_games[g_sel];
    if (!g->available) { MessageBeep(MB_ICONWARNING); return; }
    /* a disc image that is missing, or one the game cannot run (a .chd or another game's ISO, kept from an older launcher
     * that took any file), is asked again */
    wchar_t msg[1024];
    if (g->disc[0] && exists(g->disc) && !check_disc(g, g->disc, msg, 1024)) {
        MessageBoxW(hwnd, msg, g->title, MB_ICONERROR);
        if (!choose_disc(hwnd, g)) return;
    } else if ((!g->disc[0] || !exists(g->disc)) && !choose_disc(hwnd, g)) {
        return;
    }
    Launch L;
    wchar_t err[512];
    if (!prepare(g, &L, err, 512)) {
        MessageBoxW(hwnd, err, g->title, MB_ICONERROR);
        return;
    }
    STARTUPINFOW si = { 0 };
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(g->exe, L.cmd, NULL, NULL, FALSE, 0, NULL, g->tool, &si, &pi)) {
        MessageBoxW(hwnd, L"Could not start the game.", g->title, MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    g_child = pi.hProcess;
    g_child_game = g_sel;
    g_child_start = GetTickCount();
    ShowWindow(hwnd, SW_HIDE);
}

/* "--check [id]": print what Play would start, like "mc2.cmd check", and
 * write boot.txt, but start nothing. Output goes to stdout when it is
 * redirected, else to the console the hub was started from. */
static void out(const wchar_t* fmt, ...) {
    static HANDLE h;
    if (!h) {
        h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!h || h == INVALID_HANDLE_VALUE) {
            if (AttachConsole(ATTACH_PARENT_PROCESS))
                h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        }
    }
    wchar_t w[4096];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(w, 4096, fmt, ap);
    va_end(ap);
    char u[8192];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
    DWORD wrote;
    if (h && h != INVALID_HANDLE_VALUE && n > 1) WriteFile(h, u, (DWORD)(n - 1), &wrote, NULL);
}

static int check(const wchar_t* id) {
    for (int i = 0; i < g_count; i++) {
        Game* g = &g_games[i];
        if (id && *id && _wcsicmp(id, g->id) != 0) continue;
        Launch L;
        wchar_t err[512];
        out(L"%ls:%ls\n", g->title, L"");
        if (!prepare(g, &L, err, 512)) { out(L"error: %ls\n", err); return 1; }
        out(L"name=%ls\n", L.name);
        out(L"cwd=%ls\nexe=%ls\ncmd=%ls\n", g->tool, g->exe, L.cmd);
        static const wchar_t* vars[] = { L"PS2_SCALE", L"PS2_TRUE_COLOR", L"PS2_PACE",
                                         L"PS2_PRESENT_MODE", L"PS2_WINDOW_TITLE", L"PS2_WIDE_ONLY_IF", L"PS2_WIDE_WORD", L"PS2_WIDE_FLOAT", L"PS2_DITHER", L"PS2_LOOK", L"PS2_TEX_SS", L"PS2_DEPTH_FRAC", L"PS2_SKIP_TEXTURE", L"PS2_SKIP_SUBTRACT", L"PS2_TEX_FILTER", L"PS2_FSR", L"PS2_WINDOW_ICON", L"PS2_KEYS", L"PS2_HUD_WIDTH" };
        for (int v = 0; v < (int)(sizeof vars / sizeof vars[0]); v++) {
            wchar_t val[512];
            if (GetEnvironmentVariableW(vars[v], val, 512)) out(L"%ls=%ls\n", vars[v], val);
            else out(L"%ls is not set\n", vars[v]);
        }
        if (L.boot[0]) {
            out(L"boot.txt=%ls\n", L.boot);
            wchar_t bt[128];
            MultiByteToWideChar(CP_UTF8, 0, L.boot_text, -1, bt, 128);
            out(L"%ls", bt);
        }
        return 0;
    }
    out(L"no game %ls in games.txt\n", id ? id : L"");
    return 1;
}

/* --- list page -------------------------------------------------------------- */

/* Highlight a row and start fading the background to its cover. */
static void select_row(HWND hwnd, int row) {
    if (row < 0 || row >= g_count) return;
    if (row != g_bg_to) {
        /* Whatever is showing now becomes the base of the new fade. */
        if (g_fade >= 128) g_bg_from = g_bg_to;
        g_bg_to = row;
        g_fade = 0;
    }
    g_sel = row;
    InvalidateRect(hwnd, NULL, FALSE);
}

static void move_sel(HWND hwnd, int d) {
    if (g_count) select_row(hwnd, (g_sel + d + g_count) % g_count);
}

static void set_col(HWND hwnd, int col) {
    g_col = col;
    InvalidateRect(hwnd, NULL, FALSE);
}

static int list_top(void) {
    return CLIENT_H - LIST_BOTTOM - g_count * (ROW_H + ROW_GAP);
}

static RECT row_rect(int row) {
    RECT r;
    r.left = MARGIN;
    r.right = CLIENT_W - MARGIN;
    r.top = list_top() + row * (ROW_H + ROW_GAP);
    r.bottom = r.top + ROW_H;
    return r;
}

static RECT button_rect(int row, int col) {
    RECT rr = row_rect(row), r;
    r.right = rr.right - 12 - (col == COL_PLAY ? BTN_W + BTN_GAP : 0);
    r.left = r.right - BTN_W;
    r.top = rr.top + 11;
    r.bottom = rr.bottom - 11;
    return r;
}

/* --- settings page ---------------------------------------------------------- */

static Game* sgame(void) { return &g_games[g_sel]; }

/* The settings page has three tabs, Graphics, P1 Controls and P2 Controls, chosen in its heading. g_srow -1 is the heading
 * itself. P2 Controls is player 2's controller only: the keyboard always works player 1. */
enum { TAB_GRAPHICS, TAB_P1, TAB_P2, TAB_COUNT };
static const wchar_t* k_tab_names[TAB_COUNT] = { L"GRAPHICS", L"P1 CONTROLS", L"P2 CONTROLS" };
static int g_tab = TAB_GRAPHICS;
static int g_dev = DEV_CONTROLLER;  /* the P1 Controls tab: the controller's buttons or the keyboard's keys */
static int is_controls(void) { return g_tab != TAB_GRAPHICS; }
static int cur_dev(void) { return g_tab == TAB_P2 ? DEV_CONTROLLER : g_dev; }
static int c_base(void) { return g_tab == TAB_P1; }   /* the first binding row: P1 Controls has the Device row above it */
static int g_capture = -1;          /* the Controls row waiting for a key or button, or -1 */
static DWORD g_capture_until;       /* it gives up then */
static int g_ignore_up;             /* a mouse button given to a row: its button-up is not a click */
static WORD g_capture_pad;          /* the controller buttons already down when the wait began */
static WORD g_capture_trig;         /* bit 0 / bit 1: a trigger already pulled when it began; bits 2-9 a stick direction */
static wchar_t g_cmsg[160];         /* a line about the last change ("Accelerate now has Space. Handbrake got E.") */

/* The Controls tabs' rows: (P1 only) 0 the Controller / Keyboard choice, then one per button, then Reset, then Back. */
static int cbind_count(void) {
    int n = 0;
    for (int i = 0; i < BIND_COUNT; i++) n += cur_dev() == DEV_KEYBOARD || k_binds[i].pad;
    return n;
}
static int cbind_index(int row) {    /* the k_binds index of a binding row, or -1 */
    int n = c_base();
    for (int i = 0; i < BIND_COUNT; i++) {
        if (cur_dev() != DEV_KEYBOARD && !k_binds[i].pad) continue;
        if (n++ == row) return i;
    }
    return -1;
}
#define C_DEVICE 0
static int c_reset(void) { return c_base() + cbind_count(); }
static int nrows(void) { return g_tab == TAB_GRAPHICS ? S_COUNT : c_base() + cbind_count() + 2; }
static int back_row(void) { return nrows() - 1; }

static int srow_enabled(int row) {
    const Game* g = sgame();
    if (row < 0 || is_controls()) return 1;
    if (row == S_WIDE || row == S_SIXTY) return g->toggles;
    if (row == S_HUD43) return g->hud && g->toggles && g->o.wide;
    if (row == S_DITHER) return !g->o.truecolor;
    if (row == S_MENUS43) return g->menus && g->toggles && g->o.wide;
    return 1;
}

static RECT srow_rect(int row) {
    RECT r;
    r.left = MARGIN;
    r.right = CLIENT_W - MARGIN;
    r.top = S_TOP + row * (S_ROW_H + S_ROW_GAP) - g_sscroll;
    r.bottom = r.top + S_ROW_H;
    if (row == back_row()) {    /* a button, not a full row */
        r.top += 10;
        r.left = (CLIENT_W - 160) / 2;
        r.right = r.left + 160;
    }
    return r;
}

/* The width of a row's value box. */
static int sval_w(int row) {
    if (is_controls()) return S_LOOK_W;
    return row == S_LOOK || row == S_TEXFILTER ? S_LOOK_W : row == S_DISC ? S_DISC_W : S_VAL_W;
}

static RECT sval_rect(int row) {
    RECT rr = srow_rect(row), r;
    r.right = rr.right - 12;
    r.left = r.right - sval_w(row);
    r.top = rr.top + 15;
    r.bottom = rr.bottom - 15;
    return r;
}

/* Rows with a row of boxes, one per value (like the resolution's 1x..4x): how many, their text, which one is on. */
static int sseg_count(int row) {
    if (is_controls()) return g_tab == TAB_P1 && row == C_DEVICE ? 2 : 0;
    return row == S_SCALE ? 4 : row == S_MENUS43 || row == S_TRUECOLOR ? 2 : 0;
}
static int sseg_w(int row) { return is_controls() ? S_SEG3_W : row == S_SCALE ? S_SEG_W : S_SEG2_W; }

static const wchar_t* sseg_text(int row, int k) {
    static const wchar_t* scale[4] = { L"1x", L"2x", L"3x", L"4x" };
    if (is_controls()) return k ? L"Keyboard" : L"Controller";
    if (row == S_SCALE) return scale[k];
    if (row == S_MENUS43) return k ? L"16:9" : L"4:3";
    return k ? L"32-bit" : L"16-bit";
}

static int sseg_value(const Options* o, int row) {
    if (is_controls()) return g_dev;
    if (row == S_SCALE) return o->scale - 1;
    if (row == S_MENUS43) return !o->menus43;   /* 4:3 menus and movies = the 4:3 Menus option on */
    return o->truecolor;
}

static void sseg_set(Options* o, int row, int k) {
    if (is_controls()) {    /* the Device choice is what plays as player 1 in the game, kept in keys.ini */
        g_dev = k;
        g_p1_keyboard = k == DEV_KEYBOARD;
        g_capture = -1;
        keys_set("p1_device", k == DEV_KEYBOARD ? "keyboard" : "controller");
        if (!keys_save(sgame())) {
            wchar_t p[MAX_PATH];
            keys_path(sgame(), p);
            swprintf(g_cmsg, 160, L"Could not write %ls", p);
        } else {
            swprintf(g_cmsg, 160, k == DEV_KEYBOARD ? L"Player 1 plays with the keyboard; the first controller is player 2"
                                                    : L"Player 1 plays with a controller; the keyboard does not work the game");
        }
        return;
    }
    if (row == S_SCALE) o->scale = k + 1;
    else if (row == S_MENUS43) o->menus43 = !k;
    else o->truecolor = k;
}

static RECT sseg_rect(int row, int k) {   /* k 0..sseg_count(row)-1, left to right */
    RECT rr = srow_rect(row), r;
    int n = sseg_count(row), w = sseg_w(row);
    int right = rr.right - 12;
    r.left = right - (n - k) * w - (n - 1 - k) * S_SEG_GAP;
    r.right = r.left + w;
    r.top = rr.top + 15;
    r.bottom = rr.bottom - 15;
    return r;
}

/* The three tabs in the heading, as drawn (their width comes from the heading font). */
static RECT tab_rect(int tab) {
    static int w[TAB_COUNT];
    if (!w[0]) {
        HDC dc = GetDC(NULL);
        HGDIOBJ old = SelectObject(dc, g_font_tab);
        SIZE sz;
        for (int t = 0; t < TAB_COUNT; t++) {
            GetTextExtentPoint32W(dc, k_tab_names[t], (int)wcslen(k_tab_names[t]), &sz);
            w[t] = sz.cx + 8;
        }
        SelectObject(dc, old);
        ReleaseDC(NULL, dc);
    }
    RECT r;
    r.left = MARGIN;
    for (int t = 0; t < tab; t++) r.left += w[t] + 20;
    r.right = r.left + w[tab];
    r.top = 28;
    r.bottom = 80;
    return r;
}

/* Scrolling: the most the rows can scroll, and keeping the highlighted row in view. */
static int smax_scroll(void) {
    int bottom = S_TOP + back_row() * (S_ROW_H + S_ROW_GAP) + S_ROW_H + 8;
    return bottom > S_VIEW_BOTTOM ? bottom - S_VIEW_BOTTOM : 0;
}

static void sscroll_to(int y) {
    g_sscroll = y < 0 ? 0 : y > smax_scroll() ? smax_scroll() : y;
}

static void sshow_row(void) {
    if (g_srow < 0) { sscroll_to(0); return; }
    RECT r = srow_rect(g_srow);
    if (r.top < S_VIEW_TOP + 6) sscroll_to(g_sscroll - (S_VIEW_TOP + 6 - r.top));
    else if (r.bottom > S_VIEW_BOTTOM - 6) sscroll_to(g_sscroll + r.bottom - (S_VIEW_BOTTOM - 6));
}

static void set_tab(HWND hwnd, int tab, int focus_heading) {
    g_tab = tab;
    g_player = tab == TAB_P2;
    g_capture = -1;
    g_cmsg[0] = 0;
    if (is_controls()) {
        keys_load(sgame());    /* the game's quick menu can change keys.ini too */
        g_dev = g_p1_keyboard ? DEV_KEYBOARD : DEV_CONTROLLER;
    }
    g_srow = focus_heading ? -1 : 0;
    while (g_srow >= 0 && g_srow < back_row() && !srow_enabled(g_srow)) g_srow++;
    g_sscroll = 0;
    sshow_row();
    InvalidateRect(hwnd, NULL, FALSE);
}

static void open_settings(HWND hwnd) {
    if (g_sel < 0 || g_sel >= g_count) return;
    g_page = PAGE_SETTINGS;
    set_tab(hwnd, TAB_GRAPHICS, 0);
}

static void close_settings(HWND hwnd) {
    g_page = PAGE_LIST;
    g_col = COL_SETTINGS;
    g_capture = -1;
    InvalidateRect(hwnd, NULL, FALSE);
}

static void smove(HWND hwnd, int d) {
    int n = nrows() + 1, r = g_srow;     /* -1 (the heading) .. nrows()-1, round */
    for (int i = 0; i < n; i++) {
        r = (r + 1 + d + n) % n - 1;
        if (srow_enabled(r)) break;
    }
    g_srow = r;
    sshow_row();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* --- the Controls tab ------------------------------------------------------- */

/* The triggers (bits 0, 1) and the stick directions pushed past about half way (bits 2-9: left stick left, right, up, down,
 * then the right stick; XInput's up is plus, the game's is minus). */
static WORD analog_bits(const PadState* p) {
    const int t = 16000;
    return (WORD)((p->lt > 100 ? 1 : 0) | (p->rt > 100 ? 2 : 0) | (p->lx < -t ? 4 : 0) | (p->lx > t ? 8 : 0) | (p->ly > t ? 16 : 0) |
                  (p->ly < -t ? 32 : 0) | (p->rx < -t ? 64 : 0) | (p->rx > t ? 128 : 0) | (p->ry > t ? 256 : 0) | (p->ry < -t ? 512 : 0));
}

static void capture_start(HWND hwnd, int row) {
    g_capture = row;
    g_capture_until = GetTickCount() + 5000;
    g_capture_pad = 0;
    g_capture_trig = 0;
    if (g_xget) {   /* what is already down (the A that chose the row) has to be let go first */
        for (DWORD i = 0; i < 4; i++) {
            XState st;
            if (g_xget(i, &st) != ERROR_SUCCESS) continue;
            g_capture_pad |= st.pad.buttons;
            g_capture_trig |= analog_bits(&st.pad);
        }
    }
    g_cmsg[0] = 0;
    InvalidateRect(hwnd, NULL, FALSE);
}

static void controls_saved(HWND hwnd, const wchar_t* what) {
    if (!keys_save(sgame())) {
        wchar_t p[MAX_PATH];
        keys_path(sgame(), p);
        swprintf(g_cmsg, 160, L"Could not write %ls", p);
    } else {
        swprintf(g_cmsg, 160, L"%ls", what);
    }
    g_capture = -1;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* The waiting row gets a key ("Space"), or a controller button ("a"). */
static void capture_key(HWND hwnd, const char* token) {
    int i = cbind_index(g_capture);
    if (i < 0) { g_capture = -1; return; }
    wchar_t shown[64], what[160];
    kb_display(token, shown, 64);
    bind_key(i, token);
    swprintf(what, 160, L"%ls: %ls", k_binds[i].label, shown);
    controls_saved(hwnd, what);
}

static void capture_pad(HWND hwnd, const char* word) {
    int i = cbind_index(g_capture);
    if (i < 0) { g_capture = -1; return; }
    wchar_t what[160];
    bind_pad(i, word);
    swprintf(what, 160, L"%ls: %ls", k_binds[i].label, pad_display(word));
    controls_saved(hwnd, what);
}

static void clear_binding(HWND hwnd) {
    int i = cbind_index(g_srow);
    if (!is_controls() || i < 0) return;
    wchar_t what[160];
    if (cur_dev() == DEV_KEYBOARD) {
        snprintf(g_kb[i], sizeof g_kb[i], "none");
        keys_set(k_binds[i].word, "none");
    } else {
        char name[48];
        snprintf(g_pad[g_player][i], sizeof g_pad[g_player][i], "none");
        snprintf(name, sizeof name, "%s%s", k_pad_prefix[g_player], k_binds[i].word);
        keys_set(name, "none");
    }
    swprintf(what, 160, L"%ls: None", k_binds[i].label);
    controls_saved(hwnd, what);
}

static void reset_controls(HWND hwnd) {
    for (int i = 0; i < BIND_COUNT; i++) {
        if (cur_dev() == DEV_KEYBOARD) {
            snprintf(g_kb[i], sizeof g_kb[i], "%s", k_binds[i].key_default);
            keys_set(k_binds[i].word, g_kb[i]);
        } else if (k_binds[i].pad) {
            char name[48];
            snprintf(g_pad[g_player][i], sizeof g_pad[g_player][i], "%s", k_binds[i].pad_default);
            snprintf(name, sizeof name, "%s%s", k_pad_prefix[g_player], k_binds[i].word);
            keys_set(name, g_pad[g_player][i]);
        }
    }
    controls_saved(hwnd, cur_dev() == DEV_KEYBOARD ? L"Keyboard controls set back to the defaults"
                         : g_player ? L"Player 2's controller buttons set back to the defaults" : L"Controller buttons set back to the defaults");
}

/* Change the highlighted setting: d = -1 / +1 for left / right, 0 to toggle
 * (cycles the resolution). Saved at once. */
static void schange(HWND hwnd, int d) {
    if (g_srow < 0) {   /* the heading: left / right or Enter changes the tab */
        int t = d == 0 ? (g_tab + 1) % TAB_COUNT : d > 0 ? (g_tab < TAB_COUNT - 1 ? g_tab + 1 : g_tab) : (g_tab ? g_tab - 1 : 0);
        if (t != g_tab) set_tab(hwnd, t, 1);
        return;
    }
    if (g_srow == back_row()) { if (d == 0) close_settings(hwnd); return; }
    if (is_controls()) {
        if (g_tab == TAB_P1 && g_srow == C_DEVICE) {
            int k = d == 0 ? !g_dev : d > 0;
            if (k != g_dev) { sseg_set(NULL, C_DEVICE, k); sscroll_to(0); InvalidateRect(hwnd, NULL, FALSE); }
        } else if (g_srow == c_reset()) {
            if (d == 0) reset_controls(hwnd);
        } else if (d == 0) {
            capture_start(hwnd, g_srow);
        }
        return;
    }
    if (g_srow == S_DISC) {
        if (d == 0 && choose_disc(hwnd, sgame())) InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (!srow_enabled(g_srow)) { MessageBeep(MB_ICONWARNING); return; }
    Game* g = sgame();
    Options* o = &g->o;
    int* t = NULL;
    switch (g_srow) {
    case S_WIDE: t = &o->wide; break;
    case S_HUD43: t = &o->hud43; break;
    case S_SIXTY: t = &o->sixty; break;
    case S_SMOOTH: t = &o->smooth; break;
    case S_ZFRAC: t = &o->zfrac; break;
    case S_DITHER: t = &o->dither; break;
    case S_PACING: t = &o->pacing; break;
    case S_VSYNC: t = &o->vsync; break;
    case S_FULLSCREEN: t = &o->fullscreen; break;
    case S_SKIPINTRO: t = &o->skipintro; break;
    case S_LOOK:
        o->look = (o->look + (d < 0 ? LOOK_FSR : 1)) % (LOOK_FSR + 1);
        break;
    case S_TEXFILTER:
        o->texfilter = (o->texfilter + (d < 0 ? 2 : 1)) % 3;
        break;
    case S_SCALE:
    case S_MENUS43:
    case S_TRUECOLOR: {    /* Enter: the next box (round again after the last); left / right: the box beside it */
        int n = sseg_count(g_srow), k = sseg_value(o, g_srow);
        if (d == 0) k = (k + 1) % n;
        else if (k + d >= 0 && k + d < n) k += d;
        sseg_set(o, g_srow, k);
        break;
    }
    }
    if (t) *t = d == 0 ? !*t : d > 0;
    save_options(g);
    InvalidateRect(hwnd, NULL, FALSE);
}

/* The row under a point, -1 for the heading's tabs (*tab says which), -2 for nothing. */
static int srow_at(int x, int y, int* tab) {
    POINT pt = { x, y };
    for (int t = 0; t < TAB_COUNT; t++) {
        RECT r = tab_rect(t);
        if (PtInRect(&r, pt)) { if (tab) *tab = t; return -1; }
    }
    if (y < S_VIEW_TOP || y >= S_VIEW_BOTTOM) return -2;
    for (int i = 0; i < nrows(); i++) {
        RECT r = srow_rect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -2;
}

/* --- drawing ---------------------------------------------------------------- */

/* Fill a rectangle with a colour at the given opacity (0..255). */
static void fill_alpha(HDC dc, RECT r, COLORREF c, int alpha) {
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 1, 1);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetPixel(mem, 0, 0, c);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
    AlphaBlend(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, 0, 0, 1, 1, bf);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void frame_rect(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

static void blit_cover(HDC dc, int index, int alpha) {
    if (index < 0 || index >= g_count || !g_games[index].cover) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_games[index].cover);
    if (alpha >= 255) {
        BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, mem, 0, 0, SRCCOPY);
    } else {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
        AlphaBlend(dc, 0, 0, CLIENT_W, CLIENT_H, mem, 0, 0, CLIENT_W, CLIENT_H, bf);
    }
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void draw_button(HDC dc, RECT r, const wchar_t* text, int focused, int enabled) {
    fill_alpha(dc, r, focused && enabled ? ACCENT : RGB(0, 0, 0), focused && enabled ? 255 : 100);
    frame_rect(dc, r, enabled ? ACCENT : ACCENT_DIM);
    SetTextColor(dc, enabled ? WHITE : GREY);
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

/* An ON/OFF or 1x..4x box: filled when it is the current value. */
static void draw_value(HDC dc, RECT r, const wchar_t* text, int on, int enabled) {
    fill_alpha(dc, r, on && enabled ? ACCENT : RGB(0, 0, 0), on && enabled ? 255 : 110);
    frame_rect(dc, r, enabled ? (on ? WHITE : ACCENT) : ACCENT_DIM);
    SetTextColor(dc, enabled ? WHITE : DARK_GREY);
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static void paint_list(HDC mem) {
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    /* Darken the cover: lightly overall, heavily behind the list. */
    fill_alpha(mem, rc, RGB(0, 0, 0), 70);
    RECT panel = { 0, list_top() - 22, CLIENT_W, CLIENT_H };
    fill_alpha(mem, panel, RGB(0, 0, 0), 75);
    RECT rule = { 0, panel.top, CLIENT_W, panel.top + 3 };
    fill_alpha(mem, rule, ACCENT, 255);

    for (int i = 0; i < g_count; i++) {
        RECT r = row_rect(i);
        fill_alpha(mem, r, i == g_sel ? ACCENT_DIM : RGB(0, 0, 0), i == g_sel ? 95 : 75);
        if (i == g_sel) {
            RECT bar = { r.left, r.top, r.left + 5, r.bottom };
            fill_alpha(mem, bar, ACCENT, 255);
        }

        /* Column 1: the label, not clickable. */
        int label_right = r.right - 12 - 2 * BTN_W - BTN_GAP - 12;
        RECT a = { r.left + 18, r.top + 4, label_right, r.top + 36 };
        SelectObject(mem, g_font_item);
        SetTextColor(mem, g_games[i].available ? WHITE : GREY);
        DrawTextW(mem, g_games[i].title, -1, &a, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT s = { r.left + 18, r.top + 36, label_right, r.bottom - 4 };
        SelectObject(mem, g_font_small);
        SetTextColor(mem, GREY);
        DrawTextW(mem, g_games[i].available ? g_games[i].subtitle : L"Not built",
                  -1, &s, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        /* Columns 2 and 3: Play and Settings. */
        SelectObject(mem, g_font_btn);
        draw_button(mem, button_rect(i, COL_PLAY), L"PLAY",
                    i == g_sel && g_col == COL_PLAY, g_games[i].available);
        draw_button(mem, button_rect(i, COL_SETTINGS), L"SETTINGS",
                    i == g_sel && g_col == COL_SETTINGS, 1);
    }
    if (!g_count) {
        RECT e = { MARGIN, CLIENT_H / 2 - 20, CLIENT_W - MARGIN, CLIENT_H / 2 + 20 };
        SelectObject(mem, g_font_item);
        SetTextColor(mem, WHITE);
        DrawTextW(mem, L"games.txt is missing or empty.", -1, &e, DT_CENTER | DT_SINGLELINE);
    }

    RECT h = { MARGIN, CLIENT_H - 44, CLIENT_W - MARGIN, CLIENT_H - 14 };
    RECT strip = { 0, CLIENT_H - 50, CLIENT_W, CLIENT_H };
    fill_alpha(mem, strip, RGB(0, 0, 0), 140);
    SelectObject(mem, g_font_small);
    SetTextColor(mem, GREY);
    DrawTextW(mem, L"Arrows to choose   Enter or A to select   Esc or B to quit",
              -1, &h, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

static void paint_settings(HDC mem) {
    const Game* g = sgame();
    const Options* o = &g->o;
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    fill_alpha(mem, rc, RGB(0, 0, 0), 185);

    /* The heading: the three tabs. The open one is white and underlined; with the heading highlighted it also gets the red. */
    SelectObject(mem, g_font_tab);    /* smaller than the heading font, so the three tabs fit across */
    for (int t = 0; t < TAB_COUNT; t++) {
        RECT r = tab_rect(t);
        if (t == g_tab && g_srow < 0) {
            RECT f = { r.left - 8, r.top + 2, r.right + 4, r.bottom - 2 };
            fill_alpha(mem, f, ACCENT_DIM, 110);
        }
        SetTextColor(mem, t == g_tab ? WHITE : DARK_GREY);
        DrawTextW(mem, k_tab_names[t], -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (t == g_tab) {
            RECT u = { r.left, r.bottom + 2, r.right - 8, r.bottom + 6 };
            fill_alpha(mem, u, ACCENT, 255);
        }
    }
    RECT rule = { 0, 96, CLIENT_W, 99 };
    fill_alpha(mem, rule, ACCENT, 255);

    static const wchar_t* labels[S_COUNT] = {
        [S_FULLSCREEN] = L"Fullscreen", [S_SCALE] = L"Resolution", [S_VSYNC] = L"Vsync", [S_PACING] = L"Frame Pacing Fix",
        [S_LOOK] = L"Picture Mode", [S_TEXFILTER] = L"Texture Filtering", [S_SIXTY] = L"60 FPS", [S_WIDE] = L"Widescreen", [S_HUD43] = L"Fixed Widescreen HUD",
        [S_MENUS43] = L"Menu/Movie Aspect Ratio",
        [S_TRUECOLOR] = L"Color Settings", [S_DITHER] = L"Dithering",
        [S_SMOOTH] = L"Smooth Distant Textures", [S_ZFRAC] = L"Fix Distant Z-Fighting", [S_SKIPINTRO] = L"Skip Intro", [S_DISC] = L"Disc Image", [S_BACK] = L"BACK",
    };
    /* Only the patch credits have a line under the name. */
    static const wchar_t* notes[S_COUNT] = { [S_WIDE] = L"Patch by Devina, sergx12 & Aced14", [S_SIXTY] = L"Patch by Souzooka" };
    int vals[S_COUNT] = {
        [S_FULLSCREEN] = o->fullscreen, [S_VSYNC] = o->vsync, [S_PACING] = o->pacing, [S_SIXTY] = o->sixty, [S_WIDE] = o->wide,
        [S_HUD43] = o->hud43, [S_DITHER] = o->dither, [S_SMOOTH] = o->smooth, [S_ZFRAC] = o->zfrac, [S_SKIPINTRO] = o->skipintro,
    };

    int saved = SaveDC(mem);
    IntersectClipRect(mem, 0, S_VIEW_TOP, CLIENT_W, S_VIEW_BOTTOM);
    for (int i = 0; i < nrows(); i++) {
        int en = srow_enabled(i);
        RECT r = srow_rect(i);
        if (r.bottom <= S_VIEW_TOP || r.top >= S_VIEW_BOTTOM) continue;
        if (i == back_row()) {
            SelectObject(mem, g_font_btn);
            draw_button(mem, r, L"BACK", g_srow == i, 1);
            continue;
        }
        fill_alpha(mem, r, i == g_srow ? ACCENT_DIM : RGB(0, 0, 0), i == g_srow ? 110 : 80);
        if (i == g_srow) {
            RECT bar = { r.left, r.top, r.left + 5, r.bottom };
            fill_alpha(mem, bar, ACCENT, 255);
        }
        int n = sseg_count(i);
        int text_right = r.right - 12 - (n ? n * sseg_w(i) + (n - 1) * S_SEG_GAP : sval_w(i)) - 10;
        const wchar_t* label;
        const wchar_t* note = NULL;
        int bi = is_controls() ? cbind_index(i) : -1;
        if (g_tab == TAB_GRAPHICS) label = labels[i], note = notes[i];
        else if (g_tab == TAB_P1 && i == C_DEVICE) label = L"Device";
        else if (i == c_reset()) label = L"Reset to Defaults";
        else label = k_binds[bi].label;
        /* the name in the middle of the row, or above the line under it */
        RECT a = { r.left + 18, note ? r.top + 4 : r.top, text_right, note ? r.top + 34 : r.bottom };
        SelectObject(mem, g_font_item);
        SetTextColor(mem, en ? WHITE : DARK_GREY);
        DrawTextW(mem, label, -1, &a, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (note) {
            RECT s = { r.left + 18, r.top + 35, text_right, r.bottom - 4 };
            SelectObject(mem, g_font_small);
            SetTextColor(mem, en ? GREY : DARK_GREY);
            DrawTextW(mem, note, -1, &s, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }

        SelectObject(mem, g_font_btn);
        if (n) {
            int v = sseg_value(o, i);
            for (int k = 0; k < n; k++) draw_value(mem, sseg_rect(i, k), sseg_text(i, k), v == k, en);
        } else if (is_controls()) {
            if (i == c_reset()) {
                draw_value(mem, sval_rect(i), L"RESET", 0, 1);
            } else if (i == g_capture) {
                draw_value(mem, sval_rect(i), cur_dev() == DEV_KEYBOARD ? L"Press a key..." : L"Press an input...", 1, 1);
            } else if (cur_dev() == DEV_KEYBOARD) {
                wchar_t keys[200];
                kb_display(g_kb[bi], keys, 200);
                SelectObject(mem, wcslen(keys) > 16 ? g_font_small : g_font_btn);
                RECT v = sval_rect(i);
                fill_alpha(mem, v, RGB(0, 0, 0), 110);
                frame_rect(mem, v, ACCENT);
                SetTextColor(mem, WHITE);
                RECT t = { v.left + 6, v.top, v.right - 6, v.bottom };
                DrawTextW(mem, keys, -1, &t, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            } else {
                draw_value(mem, sval_rect(i), pad_display(g_pad[g_player][bi]), 0, 1);
            }
        } else if (i == S_LOOK) {
            draw_value(mem, sval_rect(i), k_look_names[o->look], 0, 1);
        } else if (i == S_DISC) {
            const wchar_t* name = g->disc[0] ? wcsrchr(g->disc, L'\\') : NULL;
            draw_value(mem, sval_rect(i), name ? name + 1 : L"Choose...", 0, 1);
        } else if (i == S_TEXFILTER) {
            draw_value(mem, sval_rect(i), k_filter_names[o->texfilter], 0, 1);
        } else {
            draw_value(mem, sval_rect(i), vals[i] ? L"ON" : L"OFF", vals[i], en);
        }
    }
    RestoreDC(mem, saved);
    if (smax_scroll() > 0) {    /* scroll bar in the right margin */
        int track = S_VIEW_BOTTOM - S_VIEW_TOP - 8, content = track + smax_scroll();
        int thumb = track * track / content, at = S_VIEW_TOP + 4 + (track - thumb) * g_sscroll / smax_scroll();
        RECT bar = { CLIENT_W - MARGIN / 2 - 2, S_VIEW_TOP + 4, CLIENT_W - MARGIN / 2 + 2, S_VIEW_TOP + 4 + track };
        fill_alpha(mem, bar, RGB(0, 0, 0), 120);
        RECT th = { bar.left, at, bar.right, at + thumb };
        fill_alpha(mem, th, ACCENT, 255);
    }

    RECT h = { MARGIN, CLIENT_H - 44, CLIENT_W - MARGIN, CLIENT_H - 14 };
    RECT strip = { 0, CLIENT_H - 50, CLIENT_W, CLIENT_H };
    fill_alpha(mem, strip, RGB(0, 0, 0), 140);
    SelectObject(mem, g_font_small);
    SetTextColor(mem, g_cmsg[0] && g_capture < 0 ? WHITE : GREY);
    const wchar_t* hint = L"Arrows choose   Enter/A change   LB/RB tabs   Esc/B back";
    if (g_capture >= 0)
        hint = cur_dev() == DEV_KEYBOARD ? L"Press the key or mouse button for it (wait 5 seconds to cancel)"
                                     : L"Press a button, pull a trigger or push a stick (Esc or wait 5 s to cancel)";
    else if (g_cmsg[0]) hint = g_cmsg;
    else if (is_controls() && cbind_index(g_srow) >= 0)
        hint = L"Enter/A rebind   Del/Y clear   LB/RB tabs   Esc/B back";
    DrawTextW(mem, hint, -1, &h, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

/* "--render <tab> <device> <row> <file.bmp>": draws the settings page (tab 0 Graphics, 1 P1 Controls, 2 P2 Controls; device 0 controller,
 * 1 keyboard; row -1 the heading) of the first game into a picture without opening a window, to check the layout. */
static int render_to_file(int tab, int dev, int row, const wchar_t* file) {
    if (!g_count) return 1;
    g_sel = 0;
    g_games[0].cover = load_cover(g_games[0].id);
    g_bg_from = g_bg_to = 0;
    g_page = PAGE_SETTINGS;
    g_dev = dev;
    g_tab = tab;
    g_player = tab == TAB_P2;
    if (is_controls()) keys_load(&g_games[0]);
    g_srow = row;
    sshow_row();
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = CLIENT_W;
    bi.bmiHeader.biHeight = CLIENT_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(mem, &rc, black);
    DeleteObject(black);
    blit_cover(mem, 0, 255);
    SetBkMode(mem, TRANSPARENT);
    paint_settings(mem);
    GdiFlush();
    BITMAPFILEHEADER fh = { 0x4D42, 0, 0, 0, sizeof fh + sizeof bi.bmiHeader };
    DWORD size = CLIENT_W * CLIENT_H * 4;
    fh.bfSize = fh.bfOffBits + size;
    FILE* f = _wfopen(file, L"wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, size, 1, f);
        fclose(f);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return f ? 0 : 1;
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, CLIENT_W, CLIENT_H);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };

    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(mem, &rc, black);
    DeleteObject(black);
    blit_cover(mem, g_bg_from, 255);
    blit_cover(mem, g_bg_to, g_fade);
    SetBkMode(mem, TRANSPARENT);

    if (g_page == PAGE_SETTINGS && g_count) paint_settings(mem);
    else paint_list(mem);

    /* The layout is drawn at its design size and stretched to the window. */
    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right == CLIENT_W && client.bottom == CLIENT_H) {
        BitBlt(dc, 0, 0, CLIENT_W, CLIENT_H, mem, 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, CLIENT_W, CLIENT_H, SRCCOPY);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* --- input ------------------------------------------------------------------ */

/* Map a point in the (possibly resized) window back to design coordinates. */
static void to_design(HWND hwnd, int* x, int* y) {
    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right > 0 && client.bottom > 0) {
        *x = *x * CLIENT_W / client.right;
        *y = *y * CLIENT_H / client.bottom;
    }
}

/* Keep the client area cover-shaped while the user drags a window edge. */
static void keep_aspect(HWND hwnd, WPARAM edge, RECT* r) {
    RECT frame = { 0, 0, 0, 0 };
    AdjustWindowRect(&frame, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
    int extra_w = frame.right - frame.left, extra_h = frame.bottom - frame.top;
    int cw = r->right - r->left - extra_w, ch = r->bottom - r->top - extra_h;
    int side;   /* client height */
    if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT) side = cw * CLIENT_H / CLIENT_W;
    else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) side = ch;
    else side = cw * CLIENT_H / CLIENT_W > ch ? cw * CLIENT_H / CLIENT_W : ch;
    if (side < 400) side = 400;
    int w = side * CLIENT_W / CLIENT_H + extra_w, h = side + extra_h;
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) r->left = r->right - w;
    else r->right = r->left + w;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) r->top = r->bottom - h;
    else r->bottom = r->top + h;
}

static int row_at(int x, int y) {
    POINT pt = { x, y };
    for (int i = 0; i < g_count; i++) {
        RECT r = row_rect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

/* Which button, if any, is under the point. The label column is not clickable. */
static int button_at(int x, int y, int* row, int* col) {
    POINT pt = { x, y };
    for (int i = 0; i < g_count; i++) {
        for (int c = COL_PLAY; c <= COL_SETTINGS; c++) {
            RECT r = button_rect(i, c);
            if (PtInRect(&r, pt)) { *row = i; *col = c; return 1; }
        }
    }
    return 0;
}

/* Enter on the list: Play starts the game, Settings opens its page. */
static void activate(HWND hwnd) {
    if (g_col == COL_SETTINGS) open_settings(hwnd);
    else launch(hwnd);
}

static void key(HWND hwnd, WPARAM vk, LPARAM lp) {
    if (g_page == PAGE_SETTINGS) {
        if (g_capture >= 0) {   /* the waiting row takes the key, Esc included (a controller row: Esc cancels) */
            if (cur_dev() == DEV_CONTROLLER) {
                if (vk == VK_ESCAPE) { g_capture = -1; InvalidateRect(hwnd, NULL, FALSE); }
                return;
            }
            const char* token = vk_token(vk, lp);
            if (!token) {
                swprintf(g_cmsg, 160, L"That key belongs to the game window (F1, F2, F5, F8, F9, F11): press another");
                MessageBeep(MB_ICONWARNING);
                InvalidateRect(hwnd, NULL, FALSE);
                return;
            }
            capture_key(hwnd, token);
            return;
        }
        g_cmsg[0] = 0;
        if (vk == VK_UP) smove(hwnd, -1);
        else if (vk == VK_DOWN || vk == VK_TAB) smove(hwnd, 1);
        else if (vk == VK_LEFT) schange(hwnd, -1);
        else if (vk == VK_RIGHT) schange(hwnd, 1);
        else if (vk == VK_RETURN || vk == VK_SPACE) schange(hwnd, 0);
        else if (vk == VK_DELETE) clear_binding(hwnd);
        else if (vk == VK_PRIOR) set_tab(hwnd, g_tab ? g_tab - 1 : 0, g_srow < 0);
        else if (vk == VK_NEXT) set_tab(hwnd, g_tab < TAB_COUNT - 1 ? g_tab + 1 : g_tab, g_srow < 0);
        else if (vk == VK_ESCAPE || vk == VK_BACK) close_settings(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (vk == VK_UP) move_sel(hwnd, -1);
    else if (vk == VK_DOWN) move_sel(hwnd, 1);
    else if (vk == VK_LEFT) set_col(hwnd, COL_PLAY);
    else if (vk == VK_RIGHT) set_col(hwnd, COL_SETTINGS);
    else if (vk == VK_TAB) set_col(hwnd, !g_col);
    else if (vk == VK_RETURN || vk == VK_SPACE) activate(hwnd);
    else if (vk == VK_ESCAPE) DestroyWindow(hwnd);
}

/* While a controller row waits: the first button (or trigger) pressed after the wait began. */
static void capture_from_pad(HWND hwnd, WORD now, WORD trig) {
    static const struct { WORD bit; const char* word; } bits[] = {
        { 0x0001, "dpup" }, { 0x0002, "dpdown" }, { 0x0004, "dpleft" }, { 0x0008, "dpright" }, { 0x0010, "start" }, { 0x0020, "back" },
        { 0x0040, "leftstick" }, { 0x0080, "rightstick" }, { 0x0100, "leftshoulder" }, { 0x0200, "rightshoulder" },
        { 0x1000, "a" }, { 0x2000, "b" }, { 0x4000, "x" }, { 0x8000, "y" },
    };
    g_capture_pad &= now;       /* let go: from now on it counts */
    g_capture_trig &= trig;
    WORD fresh = now & ~g_capture_pad;
    WORD tfresh = trig & ~g_capture_trig;
    static const char* analog[10] = { "lefttrigger", "righttrigger", "leftstick_left", "leftstick_right", "leftstick_up", "leftstick_down",
                                      "rightstick_left", "rightstick_right", "rightstick_up", "rightstick_down" };
    for (int i = 0; i < 10; i++)
        if (tfresh & (1 << i)) { capture_pad(hwnd, analog[i]); return; }
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++)
        if (fresh & bits[i].bit) { capture_pad(hwnd, bits[i].word); return; }
}

/* The game stopped with an error soon after Play (a disc image it cannot read, a missing file): instead of the launcher just
 * coming back, say so with the last line of the game's newest log (<toolkit>\games\<id>\logs\ps2run_*.log). */
static void report_failed_start(HWND hwnd, Game* g, DWORD code) {
    wchar_t pattern[MAX_PATH], newest[MAX_PATH] = L"", path[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\games\\%ls\\logs\\ps2run_*.log", g->tool, g->id);
    WIN32_FIND_DATAW fd;
    FILETIME best = { 0 };
    HANDLE f = FindFirstFileW(pattern, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) { best = fd.ftLastWriteTime; wcsncpy(newest, fd.cFileName, MAX_PATH - 1); }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    wchar_t last[512] = L"";
    if (newest[0]) {
        swprintf(path, MAX_PATH, L"%ls\\games\\%ls\\logs\\%ls", g->tool, g->id, newest);
        FILE* in = _wfopen(path, L"rb");
        if (in) {
            char line[1024], keep[1024] = "";
            while (fgets(line, sizeof line, in)) {
                line[strcspn(line, "\r\n")] = 0;
                if (line[0]) strcpy(keep, line);
            }
            fclose(in);
            MultiByteToWideChar(CP_UTF8, 0, keep, -1, last, 512);
        }
    }
    /* The usual cause is the disc image (another game, a damaged copy), so the way out is offered right here: without it the
     * only other place to change it is Settings > Disc Image, and every Play would stop the same way. */
    wchar_t msg[2048];
    int n = last[0] ? swprintf(msg, 2048, L"The game stopped as it started (code %lu):\n\n%ls\n\nThe whole log is %ls", (unsigned long)code, last, path)
                    : swprintf(msg, 2048, L"The game stopped as it started (code %lu).", (unsigned long)code);
    if (n > 0)
        swprintf(msg + n, 2048 - n, L"\n\nThis is usually the disc image: is it %ls (USA)?\n\nChoose a different disc image?\n(%ls)", g->title, g->disc);
    if (MessageBoxW(hwnd, msg, g->title, MB_ICONERROR | MB_YESNO) == IDYES) choose_disc(hwnd, g);
}

static void tick(HWND hwnd) {
    if (g_child) {
        if (WaitForSingleObject(g_child, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(g_child, &code);
            CloseHandle(g_child);
            g_child = NULL;
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            if (code != 0 && GetTickCount() - g_child_start < 15000) report_failed_start(hwnd, &g_games[g_child_game], code);
        }
        return;
    }
    if (g_fade < 255) {
        g_fade += 255 * TICK_MS / FADE_MS;
        if (g_fade >= 255) { g_fade = 255; g_bg_from = g_bg_to; }
        InvalidateRect(hwnd, NULL, FALSE);
    }
    if (g_capture >= 0 && (LONG)(GetTickCount() - g_capture_until) > 0) {
        g_capture = -1;
        InvalidateRect(hwnd, NULL, FALSE);
    }
    if (!g_xget || GetForegroundWindow() != hwnd) return;
    WORD now = 0, trig = 0;
    for (DWORD i = 0; i < 4; i++) {
        XState st;
        if (g_xget(i, &st) != ERROR_SUCCESS) continue;
        now |= st.pad.buttons;
        trig |= analog_bits(&st.pad);
    }
    WORD pressed = now & ~g_pad_prev;
    g_pad_prev = now;
    if (g_capture >= 0) {
        if (cur_dev() == DEV_CONTROLLER) capture_from_pad(hwnd, now, trig);
        else if (pressed & 0x2000) { g_capture = -1; InvalidateRect(hwnd, NULL, FALSE); }   /* B cancels a keyboard row */
        return;
    }
    if (pressed & 0x0001) key(hwnd, VK_UP, 0);       /* D-pad up */
    if (pressed & 0x0002) key(hwnd, VK_DOWN, 0);     /* D-pad down */
    if (pressed & 0x0004) key(hwnd, VK_LEFT, 0);     /* D-pad left */
    if (pressed & 0x0008) key(hwnd, VK_RIGHT, 0);    /* D-pad right */
    if (pressed & 0x1000) key(hwnd, VK_RETURN, 0);   /* A */
    if (pressed & 0x2000) key(hwnd, VK_ESCAPE, 0);   /* B: back, or quit on the list */
    if (g_page == PAGE_SETTINGS) {
        if (pressed & 0x8000) key(hwnd, VK_DELETE, 0);   /* Y: clear a binding */
        if (pressed & 0x0100) key(hwnd, VK_PRIOR, 0);    /* LB: the tab to the left */
        if (pressed & 0x0200) key(hwnd, VK_NEXT, 0);     /* RB: the tab to the right */
    }
}

static void mouse_move(HWND hwnd, int x, int y) {
    if (g_page == PAGE_SETTINGS) {
        int tab = 0, r = srow_at(x, y, &tab);
        if (g_capture >= 0) return;
        if (r == -1) {
            if (tab == g_tab && g_srow != -1) { g_srow = -1; InvalidateRect(hwnd, NULL, FALSE); }
        } else if (r >= 0 && r != g_srow && srow_enabled(r)) {
            g_srow = r;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }
    int row = row_at(x, y), brow, col;
    if (row >= 0 && row != g_sel) select_row(hwnd, row);
    if (button_at(x, y, &brow, &col) && col != g_col) set_col(hwnd, col);
}

/* A mouse button pressed while a keyboard row waits: that row gets the mouse button. Returns 1 when it was taken. */
static int mouse_capture(HWND hwnd, const char* token) {
    if (g_page != PAGE_SETTINGS || g_capture < 0 || cur_dev() != DEV_KEYBOARD) return 0;
    capture_key(hwnd, token);
    g_ignore_up = 1;
    return 1;
}

static void mouse_click(HWND hwnd, int x, int y) {
    if (g_ignore_up) { g_ignore_up = 0; return; }
    if (g_page == PAGE_SETTINGS) {
        if (g_capture >= 0) return;
        int tab = 0, r = srow_at(x, y, &tab);
        g_cmsg[0] = 0;
        if (r == -1) { set_tab(hwnd, tab, 1); return; }
        if (r < 0) return;
        g_srow = r;
        if (sseg_count(r) && srow_enabled(r)) {
            POINT pt = { x, y };
            for (int k = 0; k < sseg_count(r); k++) {
                RECT s = sseg_rect(r, k);
                if (PtInRect(&s, pt)) {
                    sseg_set(&sgame()->o, r, k);
                    if (g_tab == TAB_GRAPHICS) save_options(sgame());
                    else sscroll_to(g_sscroll);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return;
                }
            }
        }
        schange(hwnd, 0);
        return;
    }
    int row, col;
    if (button_at(x, y, &row, &col)) {
        select_row(hwnd, row);
        g_col = col;
        activate(hwnd);
    }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: tick(hwnd); return 0;
    case WM_KEYDOWN:
        if (g_capture >= 0 && (lp & (1 << 30))) return 0;   /* a held key repeating (the Enter that chose the row) */
        key(hwnd, wp, lp);
        return 0;
    case WM_SYSKEYDOWN:     /* Alt and F10: only a waiting keyboard row takes them */
        if (g_page == PAGE_SETTINGS && g_capture >= 0 && cur_dev() == DEV_KEYBOARD) {
            if (!(lp & (1 << 30))) key(hwnd, wp, lp);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: mouse_capture(hwnd, "Left Mouse"); return 0;
    case WM_RBUTTONDOWN: mouse_capture(hwnd, "Right Mouse"); return 0;
    case WM_MBUTTONDOWN: mouse_capture(hwnd, "Middle Mouse"); return 0;
    case WM_XBUTTONDOWN: mouse_capture(hwnd, HIWORD(wp) == XBUTTON1 ? "Mouse X1" : "Mouse X2"); return TRUE;
    case WM_SIZING: keep_aspect(hwnd, wp, (RECT*)lp); return TRUE;
    case WM_SIZE: InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        to_design(hwnd, &x, &y);
        mouse_move(hwnd, x, y);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (g_page == PAGE_SETTINGS && mouse_capture(hwnd, (short)HIWORD(wp) > 0 ? "Wheel Up" : "Wheel Down")) {
            g_ignore_up = 0;
        } else if (g_page == PAGE_SETTINGS) {
            sscroll_to(g_sscroll - (short)HIWORD(wp) * S_WHEEL_STEP / WHEEL_DELTA);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        to_design(hwnd, &x, &y);
        mouse_click(hwnd, x, y);
        return 0;
    }
    case WM_ICON_READY:
        set_window_icon(hwnd);
        return 0;
    case WM_COVER_READY:
        if (wp < (WPARAM)g_count && !g_games[wp].cover) {
            g_games[wp].cover = load_cover(g_games[wp].id);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)prev;
    GetModuleFileNameW(NULL, g_dir, MAX_PATH);
    wchar_t* slash = wcsrchr(g_dir, L'\\');
    if (slash) *slash = 0;
    swprintf(g_ini, MAX_PATH, L"%ls\\settings.ini", g_dir);

    ULONG_PTR gdip_token = 0;
    GdipStartupInput gdip_in = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&gdip_token, &gdip_in, NULL);
    load_games();

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && _wcsicmp(argv[1], L"--check") == 0)
        return check(argc >= 3 ? argv[2] : NULL);
    (void)cmdline;

    static const wchar_t* xinput_dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !g_xget; i++) {
        HMODULE m = LoadLibraryW(xinput_dlls[i]);
        if (m) g_xget = (XGetState)(void*)GetProcAddress(m, "XInputGetState");
    }

    /* Impact, slanted, is close to the cover's heavy italic logo lettering. */
    g_font_head = CreateFontW(-38, 0, 0, 0, FW_NORMAL, TRUE, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Impact");
    g_font_tab = CreateFontW(-29, 0, 0, 0, FW_NORMAL, TRUE, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Impact");
    g_font_item = CreateFontW(-26, 0, 0, 0, FW_NORMAL, TRUE, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Impact");
    g_font_set = CreateFontW(-23, 0, 0, 0, FW_NORMAL, TRUE, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Impact");
    g_font_btn = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Impact");
    g_font_small = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    if (argv && argc >= 6 && _wcsicmp(argv[1], L"--render") == 0)
        return render_to_file(_wtoi(argv[2]), _wtoi(argv[3]), _wtoi(argv[4]), argv[5]);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"SR2RecompHub";
    RegisterClassW(&wc);

    /* Resizable, but WM_SIZING keeps the client area cover-shaped; there is no
     * maximize box because a maximized window could not keep that shape. */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    RECT r = { 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"SR2Recomp", style,
                              (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                              (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
                              w, h, NULL, NULL, inst, NULL);
    if (!hwnd) return 1;
    set_window_icon(hwnd);  /* downloaded on the first start (fetch_icon) */
    ShowWindow(hwnd, show);
    HANDLE covers = CreateThread(NULL, 0, fetch_covers, hwnd, 0, NULL);
    if (covers) CloseHandle(covers);
    SetTimer(hwnd, 1, TICK_MS, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
