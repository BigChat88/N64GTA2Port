// Dragon logo shown at power-on, before the intro movie. Adapted from lambertjamesd/n64brew2025's src/intro/logo.c
// (logo_libdragon), which in turn credits:
//
//   logo_libdragon and logo_n64brew sourced from the N64brew-GameJam2024
//   repository
//
//   Copyright (c) 2024 N64brew
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The animation is laid out in 640x480 coordinates and scaled to the current display, which stays open:
// the engine keeps using these framebuffers. Silent. The sprites come from assets/intro.
#include <libdragon.h>

void gta2_n64_play_logo(void)
{
    const color_t RED = RGBA32(221, 46, 26, 255);
    const color_t WHITE = RGBA32(255, 255, 255, 255);

    sprite_t *d1 = sprite_load("rom:/intro/dragon1.sprite");
    sprite_t *d2 = sprite_load("rom:/intro/dragon2.sprite");
    sprite_t *d3 = sprite_load("rom:/intro/dragon3.sprite");
    sprite_t *d4 = sprite_load("rom:/intro/dragon4.sprite");
    if (!d1 || !d2 || !d3 || !d4) {
        if (d1) sprite_free(d1);
        if (d2) sprite_free(d2);
        if (d3) sprite_free(d3);
        if (d4) sprite_free(d4);
        return;
    }

    rdpq_init();
    const float kx = (float)display_get_width() / 640.0f, ky = (float)display_get_height() / 480.0f;
#define SXF(v) ((float)(v) * kx)
#define SYF(v) ((float)(v) * ky)
#define SX(v) ((int)SXF(v))
#define SY(v) ((int)SYF(v))

    float angle1 = 3.2f, angle2 = 1.9f, angle3 = 0.9f;
    float scale1 = 0.0f, scale2 = 0.4f, scale3 = 0.8f, scroll4 = 400.0f;
    const uint32_t ms0 = get_ticks_ms();
    const int X0 = 10, Y0 = 30; /* translation of the whole animation */

    for (;;) {
        /* 0: rotate dragon head  1: rotate body and tail, scale up  2: scroll logo  3: fade out */
        const uint32_t tt = get_ticks_ms() - ms0;
        int part;
        if (tt < 1000) part = 0;
        else if (tt < 1500) part = 1;
        else if (tt < 4000) part = 2;
        else if (tt < 5000) part = 3;
        else break;

        joypad_poll();
        if (joypad_get_buttons_pressed(JOYPAD_PORT_1).start) break;

        angle1 -= angle1 * 0.04f; if (angle1 < 0.010f) angle1 = 0;
        if (part >= 1) {
            angle2 -= angle2 * 0.06f; if (angle2 < 0.01f) angle2 = 0;
            angle3 -= angle3 * 0.06f; if (angle3 < 0.01f) angle3 = 0;
            scale2 -= scale2 * 0.06f; if (scale2 < 0.01f) scale2 = 0;
            scale3 -= scale3 * 0.06f; if (scale3 < 0.01f) scale3 = 0;
        }
        if (part >= 2) scroll4 -= scroll4 * 0.08f;

        color_t red = RED, white = WHITE;
        if (part >= 3) {
            red.a = 255 - (tt - 4000) * 255 / 1000;
            white.a = 255 - (tt - 4000) * 255 / 1000;
        }

        rdpq_attach_clear(display_get(), NULL);

        /* the head is scissored so that it appears as it moves, as if the dragon jumped out */
        if (angle1 > 1.0f) rdpq_set_scissor(0, 0, SX(X0 + 300), SY(Y0 + 240));
        else rdpq_set_scissor(0, 0, SX(640), SY(Y0 + 240));

        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
        rdpq_set_prim_color(red);
        rdpq_sprite_blit(d1, SX(X0 + 216), SY(Y0 + 205), &(rdpq_blitparms_t){
            .theta = angle1, .scale_x = (scale1 + 1) * kx, .scale_y = (scale1 + 1) * ky, .cx = 176, .cy = 171 });

        rdpq_set_scissor(0, 0, SX(640), SY(480));

        /* black rectangle with an alpha gradient, covering the head's tail */
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_dithering(DITHER_NOISE_NOISE);
        float vtx[4][6] = {
            { SXF(X0 + 0),   SYF(Y0 + 180), 0,0,0,0 },
            { SXF(X0 + 200), SYF(Y0 + 180), 0,0,0,0 },
            { SXF(X0 + 200), SYF(Y0 + 240), 0,0,0,1 },
            { SXF(X0 + 0),   SYF(Y0 + 240), 0,0,0,1 },
        };
        rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
        rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);

        if (part >= 1) {
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

            color_t color = red;
            color.r *= 1 - scale3; color.g *= 1 - scale3; color.b *= 1 - scale3;
            rdpq_set_prim_color(color);

            rdpq_sprite_blit(d2, SX(X0 + 246), SY(Y0 + 230), &(rdpq_blitparms_t){
                .theta = angle2, .scale_x = (1 - scale2) * kx, .scale_y = (1 - scale2) * ky, .cx = 145, .cy = 113 });
            rdpq_sprite_blit(d3, SX(X0 + 266), SY(Y0 + 256), &(rdpq_blitparms_t){
                .theta = -angle3, .scale_x = (1 - scale3) * kx, .scale_y = (1 - scale3) * ky, .cx = 91, .cy = 24 });
        }

        if (part >= 2) {
            rdpq_set_prim_color(white);
            rdpq_sprite_blit(d4, SX(X0 + 161 + (int)scroll4), SY(Y0 + 182), &(rdpq_blitparms_t){ .scale_x = kx, .scale_y = ky });
        }

        rdpq_detach_show();
    }

    rspq_wait();
    sprite_free(d1);
    sprite_free(d2);
    sprite_free(d3);
    sprite_free(d4);
}
