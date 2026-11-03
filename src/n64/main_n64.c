// N64 entry point of the gta2_re engine (GTA 2 decomp): initializes the hardware, reads the controller and runs the engine.
// The game (WinMain_5E53F0) runs in slices: gta2_game_frame() = Gta2Port_RunLoopSlice().
#include <libdragon.h>
#include <malloc.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int gta2_game_init(void);
int gta2_game_frame(void);
void gta2_game_key(int scancode, int vk, int down);
void gta2_port_flush_key_events(int with_latched);
int gta2_game_in_menu(void);
int gta2_game_pause_state(void);
void gta2_game_quit_to_menu(void);
int gta2_game_in_demo(void);
void gta2_n64_hold_until_menu(void); // render_n64.c
void gta2_n64_show_loading(void);    // intro_n64.c
void gta2_install_exception_fixups(void);
void gta2_n64_cheats_reset(void);    // Player.cpp
void gta2_n64_cheat_apply(int row);

// Monotonic clock in ms for the engine (timeGetTime, QueryPerformanceCounter...).
double gta2_n64_now_ms(void)
{
    return (double)timer_ticks() * 1000.0 / (double)TICKS_PER_SECOND;
}

#ifndef N64_GAME_320
#define N64_GAME_320 1 /* 1 = gameplay at 320x240 (half the pixels), 0 = gameplay at 640x240 */
#endif
#ifndef N64_DEBUG_OVERLAY
#define N64_DEBUG_OVERLAY 0
#endif
int gta2_n64_boot_map = -1; /* see read_boot_choice */
typedef struct { int sc, vk; } KeyDef;
enum { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_W, K_S, K_A, K_D, K_ENTER, K_ATTACK, K_JUMP, K_ESC, K_NEXT, K_RADIO, K_SPECIAL, K_PAUSE, K_COUNT };
static const KeyDef kKeys[K_COUNT] = {
    {0xC8, 0x26}, {0xD0, 0x28}, {0xCB, 0x25}, {0xCD, 0x27}, // arrows (menus)
    {0x11, 0x57}, {0x1F, 0x53}, {0x1E, 0x41}, {0x20, 0x44}, // W S A D (game)
    {0x1C, 0x0D}, {0x26, 0x4C}, {0x39, 0x20}, {0x01, 0x1B}, // Enter, attack L, jump/brake, Esc
    {0x25, 0x4B},                                           // next weapon K
    {0x3B, 0x70},                                           // F1: next radio station
    {0x17, 0x49},                                           // I: special (horn etc.; controls slot 9)
    {0x40, 0x75},                                           // F6: pause (engine TogglePause)
};
static uint8_t gKeyDown[K_COUNT];

static void set_key(int k, int down)
{
    if (gKeyDown[k] != (uint8_t)down)
    {
        gKeyDown[k] = (uint8_t)down;
        gta2_game_key(kKeys[k].sc, kKeys[k].vk, down);
    }
}

/* Rumble Pak: a short buzz when the player is hurt or crashes (Ped.cpp, Car_BC.cpp). A motor spinning for long feels sluggish, not punchy. */
enum { RUMBLE_MIN_MS = 60, RUMBLE_MAX_MS = 250 };
static int g_rumble_on = 1;
static uint32_t g_rumble_off; /* when the motor stops; 0 = stopped */
int gta2_n64_get_rumble(void) { return g_rumble_on; }
static void pad_rumble_off(void)
{
    if (g_rumble_off) { joypad_set_rumble_active(JOYPAD_PORT_1, false); g_rumble_off = 0; }
}
void gta2_n64_set_rumble(int on)
{
    g_rumble_on = on != 0;
    if (!g_rumble_on) pad_rumble_off();
}
void gta2_n64_rumble(int ms)
{
    if (!g_rumble_on || gta2_game_in_demo() || !joypad_get_rumble_supported(JOYPAD_PORT_1)) return;
    if (ms < RUMBLE_MIN_MS) ms = RUMBLE_MIN_MS;
    if (ms > RUMBLE_MAX_MS) ms = RUMBLE_MAX_MS;
    const uint32_t off = ((uint32_t)get_ticks_ms() + (uint32_t)ms) | 1u;
    if (!g_rumble_off || (int32_t)(off - g_rumble_off) > 0)
    {
        if (!g_rumble_off) joypad_set_rumble_active(JOYPAD_PORT_1, true);
        g_rumble_off = off;
    }
}

/* Cheat menu of the pause screen, drawn by Hud.cpp: C-up, C-right and d-pad left held together while paused */
enum { CHEAT_ROWS = 8 };
static int g_cheat_sel = -1; /* selected row; -1 = closed */
int gta2_n64_cheat_menu_sel(void) { return g_cheat_sel; }

static void poll_input(void)
{
    joypad_poll();
    joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
    /* Controller. IN GAME:
     *   d-pad = move/turn   analog = turn only   A = accelerate   B = reverse   Z or L = attack   R = brake/jump
     *   C-left = next weapon   C-up = enter/exit car   C-right = next radio   C-down = special   Start = pause.
     *   B while paused = asks whether to quit the game: then A quits and B goes back to the pause.
     *   C-up + C-right + d-pad left together while paused = cheat menu: d-pad up/down selects, A applies, B or Start closes it.
     *   There is no "previous weapon" on the controller.
     * IN MENU: d-pad/analog navigates, A or Start confirms, B goes back. */
    const int menu = gta2_game_in_menu();
    /* "Ghost button" protection: when switching menu <-> game, Enter/Esc/Pause stay blocked until A, B, Start and C-up are released.
     * Without it, a button still held during the switch was read by the new screen as a new press and picked options by itself. Start also only counts after 2 consecutive reads (noise filter). */
    static int prev_menu = -1, guard, start_prev;
    if (menu != prev_menu) { if (prev_menu != -1) guard = 1; prev_menu = menu; }
    if (guard && !(in.btn.a || in.btn.b || in.btn.start || in.btn.c_up)) guard = 0;
    const int start_ok = in.btn.start && start_prev;
    start_prev = in.btn.start;
    /* MATCH-ENTRY SHIELD: on entering the game (or booting directly into a map) all game keys stay off until the
     * controller is 100% neutral (no button, analog near center) for 6 consecutive reads and 0.8 s have passed; 5 s at most. */
    const uint32_t now_ms = (uint32_t)get_ticks_ms();
    static int bp_menu = -1, neutral_n;
    static uint32_t blank_t0;
    const int pad_neutral = !(in.btn.a || in.btn.b || in.btn.z || in.btn.start || in.btn.l || in.btn.r || in.btn.c_up || in.btn.c_down ||
                              in.btn.c_left || in.btn.c_right || in.btn.d_up || in.btn.d_down || in.btn.d_left || in.btn.d_right) &&
                            in.stick_x > -15 && in.stick_x < 15 && in.stick_y > -15 && in.stick_y < 15;
    if (!menu && bp_menu != 0) { blank_t0 = now_ms | 1u; neutral_n = 0; }
    bp_menu = menu;
    neutral_n = pad_neutral ? (neutral_n < 100 ? neutral_n + 1 : neutral_n) : 0;
    int blank = 0;
    if (!menu && blank_t0)
    {
        const uint32_t el = now_ms - blank_t0;
        if (el >= 5000u || (el >= 800u && neutral_n >= 6)) blank_t0 = 0;
        else blank = 1;
    }
    const int dup = in.btn.d_up || (menu && in.stick_y > 40), ddown = in.btn.d_down || (menu && in.stick_y < -40);
    const int left = !blank && (in.btn.d_left || in.stick_x < -40), right = !blank && (in.btn.d_right || in.stick_x > 40);
    const int up = !blank && (dup || (!menu && in.btn.a)), down = !blank && (ddown || (!menu && in.btn.b));
    set_key(K_UP, up);   set_key(K_W, !menu && up);
    set_key(K_DOWN, down); set_key(K_S, !menu && down);
    set_key(K_LEFT, left); set_key(K_A, !menu && left);
    set_key(K_RIGHT, right); set_key(K_D, !menu && right);
    const int pause_state = menu ? 0 : gta2_game_pause_state();
    if (g_rumble_off && (menu || pause_state || (int32_t)(now_ms - g_rumble_off) >= 0)) pad_rumble_off();
    /* the button that closes the cheat menu must not also act on the pause screen once the menu is gone */
    static int cheat_combo_prev, cheat_guard;
    const int cheat_combo = in.btn.c_up && in.btn.c_right && in.btn.d_left;
    const joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (cheat_guard && !(in.btn.a || in.btn.b || in.btn.start)) cheat_guard = 0;
    if (pause_state != 1)
    {
        g_cheat_sel = -1;
    }
    else if (g_cheat_sel >= 0)
    {
        if (pressed.d_up) g_cheat_sel = (g_cheat_sel + CHEAT_ROWS - 1) % CHEAT_ROWS;
        if (pressed.d_down) g_cheat_sel = (g_cheat_sel + 1) % CHEAT_ROWS;
        if (pressed.a) gta2_n64_cheat_apply(g_cheat_sel);
        if (pressed.b || pressed.start) { g_cheat_sel = -1; cheat_guard = 1; }
    }
    else if (cheat_combo && !cheat_combo_prev)
    {
        g_cheat_sel = 0;
        gta2_n64_cheats_reset();
    }
    cheat_combo_prev = cheat_combo;
    const int cheat_block = g_cheat_sel >= 0 || cheat_guard;
    if (pause_state == 2 && pressed.a && !guard && !blank)
    {
        gta2_game_quit_to_menu();
        if (gta2_n64_boot_map < 0) /* booted straight into a map there is no menu to wait for: the game just ends */
        {
            gta2_n64_hold_until_menu();
            gta2_n64_show_loading();
        }
    }
    set_key(K_ENTER, !guard && !blank && !cheat_block && (menu ? (in.btn.a || in.btn.start) : !pause_state && in.btn.c_up));
    set_key(K_ESC, !guard && !blank && !cheat_block && (menu || pause_state) && in.btn.b); /* in game ESC only exists while paused */
    set_key(K_PAUSE, !menu && !blank && !guard && !cheat_block && pause_state != 2 && start_ok);
    set_key(K_ATTACK, !menu && !blank && (in.btn.z || in.btn.l));
    set_key(K_JUMP, !menu && !blank && in.btn.r);
    set_key(K_NEXT, !menu && !blank && in.btn.c_left);
    set_key(K_SPECIAL, !menu && !blank && in.btn.c_down);
    set_key(K_RADIO, !blank && !pause_state && in.btn.c_right); /* C-right changes the radio */
    /* Game keyboard event queue: in the menu and during the input block it is emptied every frame. Without this, keys pressed in the menu
     * (B = ESC, Enter, arrows) stayed queued and the game read them all at once when the match started: ESC opened "quit game", the next Enter
     * confirmed and the game went back to the menu; queued arrows made the character/car go "crazy". */
    if (menu || blank || g_cheat_sel >= 0) gta2_port_flush_key_events(!menu);
}

int gta2_n64_gfx_active(void);
/* Start choice, read by game_entry.cpp: -1 = original GTA 2 menu; 0 = bil (Industrial), 1 = ste (Residential), 2 = wil (Downtown)
 * straight into the map (no menu). Debug ROMs only (N64_DEBUG_OVERLAY): holding Z at power-on skips the menu: Z = bil, Z + C-down = ste, Z + C-left = wil. */
static void read_boot_choice(void)
{
    for (int i = 0; i < 4; ++i) { joypad_poll(); wait_ms(20); } /* the 1st poll may come back empty */
#if N64_DEBUG_OVERLAY
    joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
    if (in.btn.z) gta2_n64_boot_map = in.btn.c_left ? 2 : in.btn.c_down ? 1 : 0;
#endif
}

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    console_init();
    console_set_debug(true);
    console_set_render_mode(RENDER_MANUAL);
#if N64_GAME_320
    /* console_init() always selects 640x240: with both menus and gameplay at 320x240, smaller framebuffers leave about 307 KB more for the engine */
    display_close();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
#endif
    /* safe margin (overscan): without it, upscalers/TVs crop the edges (HUD, corners). Values in VI dots (640x480);
       adjust with -DN64_OVERSCAN_X=.. -DN64_OVERSCAN_Y=.. (0 = full image, as in the emulator). Must come AFTER console_init(). */
#ifndef N64_OVERSCAN_X
#define N64_OVERSCAN_X 0
#endif
#ifndef N64_OVERSCAN_Y
#define N64_OVERSCAN_Y 0
#endif
    vi_set_borders((vi_borders_t){ N64_OVERSCAN_X, N64_OVERSCAN_X, N64_OVERSCAN_Y, N64_OVERSCAN_Y });
    if (!is_memory_expanded())
    {
        printf("\n\n   GTA 2 needs the Expansion Pak.\n");
        for (;;) { console_render(); }
    }
    /* resolution: see N64_GAME_320 and gta2_n64_update_video_mode() */
    timer_init();
    gta2_install_exception_fixups();
    joypad_init();
    const int dfs = dfs_init(DFS_DEFAULT_LOCATION);
    if (dfs != 0)
    {
        printf("Failed to open the ROM file system (dfs_init=%d)\n", dfs);
        for (;;) { console_render(); }
    }
    console_render();

    read_boot_choice();
    console_render();
    int console_on_screen = 1; /* until the first frame of the engine; 0: the loading screen stays there instead */
    if (gta2_n64_boot_map < 0) { /* debug ROMs: holding Z at power-on goes straight into a map */
        extern void gta2_n64_play_logo(void);  /* logo_n64.c */
        extern void gta2_n64_play_intro(void); /* intro_n64.c */
        gta2_n64_play_logo();
        gta2_n64_play_intro();
        gta2_n64_show_loading();
        console_on_screen = 0;
    }

    const int rc = gta2_game_init();
    if (rc != 0)
    {
        printf("Engine exited during initialization: code %d\n", rc);
        for (;;) { console_render(); }
    }

    for (;;)
    {
        poll_input();
        const int r = gta2_game_frame();
#ifdef N64_MEMLOG
        { static unsigned fr; if ((++fr % 10) == 0) { struct mallinfo mi = mallinfo(); { extern unsigned g_smallN, g_smallB; { extern char *__heap_end, *__heap_top; extern uint32_t gta2_n64_frames(void); debugf("[mem] f=%u pres=%u arena=%d used=%d free=%d brk=%p sp=%p heap_end=%p heap_top=%p\n", fr, (unsigned)gta2_n64_frames(), mi.arena, mi.uordblks, mi.fordblks, sbrk(0), (void*)&mi, (void*)__heap_end, (void*)__heap_top); } (void)g_smallN; (void)g_smallB; } } }
#endif
        if (r != -1)
        {
            printf("Game ended: code %d\n", r);
            for (;;) { console_render(); }
        }
        if (console_on_screen && !gta2_n64_gfx_active()) console_render();
    }
}


/* Video mode per screen, called by the renderer before each frame. With N64_GAME_320=0: menus at 320x240, gameplay at 640x240.
 * display_change() reuses the 640x240 framebuffers from console_init(), so switching allocates no memory and cannot fail.
 * It also resets the VI borders when it takes effect (a few vblanks later), hence the safe margin is applied on every frame. */
#ifndef N64_MENU_320
#define N64_MENU_320 1
#endif
void gta2_frame_set_hud(int on); /* frame_n64.cpp */
/* Limit of the display, Options menu: on/off */
static int g_fps_limit = 1;
int gta2_n64_get_fps_limit(void) { return g_fps_limit; }
void gta2_n64_set_fps_limit(int on) { g_fps_limit = on ? 1 : 0; }
void gta2_n64_update_video_mode(void)
{
    static int s_fps_limit = -1;
    if (g_fps_limit != s_fps_limit)
    {
        s_fps_limit = g_fps_limit;
        display_set_fps_limit(g_fps_limit ? 30.0f : 0.0f);
    }
    static int s_small = N64_GAME_320;
    const int in_menu = gta2_game_in_menu();
    const int small = N64_GAME_320 || (N64_MENU_320 && in_menu);
    gta2_frame_set_hud(in_menu); /* the menu text is drawn like the HUD: full-resolution textures on whole pixels */
    if (small != s_small)
    {
        s_small = small;
        if (small) display_change(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
        else display_change(RESOLUTION_640x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    }
    vi_set_borders((vi_borders_t){ N64_OVERSCAN_X, N64_OVERSCAN_X, N64_OVERSCAN_Y, N64_OVERSCAN_Y });
}

/* ---- DragonFS folder listing with wildcard (used by _findfirst) ---- */
static int glob_match(const char *p, const char *s)
{
    for (; *p; ++p, ++s) {
        if (*p == '*') {
            while (p[1] == '*') ++p;
            if (!p[1]) return 1;
            for (; *s; ++s) if (glob_match(p + 1, s)) return 1;
            return glob_match(p + 1, s);
        }
        if (!*s) return 0;
        if (*p != '?' && *p != *s) return 0;
    }
    return !*s;
}

/* returns 1 and copies the 1st name matching 'glob' (lowercase) into 'out'; 0 if none. */
int gta2_n64_dir_find(const char *dirpath, const char *glob, char *out, int outsz)
{
    dir_t d;
    int r = dir_findfirst(dirpath, &d);
    while (r == 0) {
        if (d.d_type != DT_DIR && glob_match(glob, d.d_name)) {
            strncpy(out, d.d_name, outsz - 1);
            out[outsz - 1] = 0;
            return 1;
        }
        r = dir_findnext(dirpath, &d);
    }
    return 0;
}

/* CPU address (KSEG1, uncached) of a DragonFS file + offset; NULL if not found */
const void *gta2_n64_rom_ptr(const char *romPath, unsigned offset)
{
    uint32_t a = dfs_rom_addr(romPath);
    if (!a && strncmp(romPath, "rom:/", 5) == 0) a = dfs_rom_addr(romPath + 5);
    if (!a) return NULL;
    uint32_t phys = a & 0x1FFFFFFF;
    if (phys < 0x10000000u) phys += 0x10000000u;      /* came as a ROM offset */
    return (const void *)(uintptr_t)(0xA0000000u | (phys + offset));
}

#ifdef N64_MEMTRACE
void gta2_n64_dbg_mm(unsigned n, void *ra) { debugf("[mm] %u ra=%p\n", n, ra); }
/* debug: logs large allocations (>= 16 KB) with the caller's address (build with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc) */
#include <stdlib.h>
void *__real_malloc(size_t n); void *__real_calloc(size_t a, size_t b); void *__real_realloc(void *p, size_t n);
unsigned g_smallN, g_smallB;
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); if (n < 512) { g_smallN++; g_smallB += n; } if (n >= 512) debugf("[big] malloc %u -> %p ra=%p\n", (unsigned)n, p, __builtin_return_address(0)); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); if (a * b < 512) { g_smallN++; g_smallB += a * b; } if (a * b >= 512) debugf("[big] calloc %u -> %p ra=%p\n", (unsigned)(a * b), p, __builtin_return_address(0)); return p; }
void *__wrap_realloc(void *q, size_t n) { void *p = __real_realloc(q, n); if (n >= 512) debugf("[big] realloc %u -> %p ra=%p\n", (unsigned)n, p, __builtin_return_address(0)); return p; }
void *__real_memalign(size_t a, size_t n); void *__real_malloc_uncached(size_t n); void *__real_malloc_uncached_aligned(int a, size_t n);
void *__wrap_memalign(size_t a, size_t n) { void *p = __real_memalign(a, n); debugf("[big] memalign %u -> %p ra=%p\n", (unsigned)n, p, __builtin_return_address(0)); return p; }
void *__wrap_malloc_uncached(size_t n) { void *p = __real_malloc_uncached(n); debugf("[big] uncached %u -> %p ra=%p\n", (unsigned)n, p, __builtin_return_address(0)); return p; }
void *__wrap_malloc_uncached_aligned(int a, size_t n) { void *p = __real_malloc_uncached_aligned(a, n); debugf("[big] uncached_al %u -> %p ra=%p\n", (unsigned)n, p, __builtin_return_address(0)); return p; }
#endif
