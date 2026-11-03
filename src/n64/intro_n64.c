// The game's intro movie, played once at power-on before the engine starts, while the RAM is still free.
// tools/stage_data.py converts data/movie/intro.bik (Bink cannot be decoded here) to rom:/data/intro.m1v and intro.wav64;
// without those files (no ffmpeg at build time, or a game copy with no movie) the game simply starts without it.
#include <libdragon.h>
#include <stdio.h>

#define INTRO_VIDEO "rom:/data/intro.m1v"
#define INTRO_AUDIO_HZ 22050

static void intro_osd(void *ctx, int frame_idx, float time_sec, fmv_control_t *ctrl)
{
    (void)ctx; (void)frame_idx; (void)time_sec;
    joypad_poll();
    if (joypad_get_buttons_pressed(JOYPAD_PORT_1).start) ctrl->stop(ctrl);
}

void gta2_n64_play_intro(void)
{
    FILE *f = fopen(INTRO_VIDEO, "rb");
    if (!f) return;
    fclose(f);

    rdpq_init();
    /* its own audio rate: the game's mixer (audio_n64.c) has its own and is started later by the renderer */
    audio_init(INTRO_AUDIO_HZ, 4);
    mixer_init(2);
    wav64_init_compression(2); /* ULC, like the radios */
    video_register_codec(&mpeg1_codec);

    fmv_play(INTRO_VIDEO, &(fmv_parms_t){
        .disable_display_init = true, /* keeps the framebuffers already allocated */
        .osd_callback = intro_osd,
    });

    mixer_close();
    audio_close();

    /* the Start that skipped the movie must not also press a button in the menu */
    for (int i = 0; i < 50; ++i) {
        joypad_poll();
        if (!joypad_get_buttons_held(JOYPAD_PORT_1).start) break;
        wait_ms(20);
    }
}

/* The debug font, loaded only once: libdragon halts the game if a built-in font is loaded while it already is. Used here and by the overlay of render_n64.c */
int gta2_n64_debug_font(void)
{
    enum { FONT_ID = 14 };
    static int s_loaded;
    if (!s_loaded) {
        rdpq_text_register_font(FONT_ID, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));
        s_loaded = 1;
    }
    return FONT_ID;
}

/* The engine takes a while to start, and to go from a game back to the menu: this stays on screen until the next image. Not translated: at boot the language is not known yet */
void gta2_n64_show_loading(void)
{
    enum { GLYPH_W = 8, GLYPH_H = 8 };
    static const char text[] = "LOADING";

    rdpq_init();
    const int font_id = gta2_n64_debug_font();
    surface_t *disp = display_get();
    rdpq_attach_clear(disp, NULL);
    rdpq_set_mode_standard();
    rdpq_text_print(NULL, font_id, ((int)disp->width - (int)(sizeof(text) - 1) * GLYPH_W) / 2, ((int)disp->height + GLYPH_H) / 2, text);
    rdpq_detach_show();
    rspq_wait();
}
