/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Mapping a picture of src_w x src_h onto a panel of panel_w x panel_h: the
 * picture is stretched to fill the panel ("fill").  The Switch display
 * compositor scales each layer's buffer to the screen that way, so a game
 * window of any resolution shows full-screen -- a 4:3 window becomes the
 * "widescreen" look, a 16:9 window is scaled uniformly -- and input maps
 * through the same transform.  Shared by the runtime and its host test
 * (horizon-wine/tests/screen_fit.c).
 */
#ifndef WINE_NX_SCREEN_FIT_H
#define WINE_NX_SCREEN_FIT_H

static inline void screen_fill_scales( int src_w, int src_h, int panel_w, int panel_h,
                                       double *scale_x, double *scale_y )
{
    if (src_w < 1) src_w = 1;
    if (src_h < 1) src_h = 1;
    if (panel_w < 1) panel_w = 1;
    if (panel_h < 1) panel_h = 1;
    if (scale_x) *scale_x = (double)panel_w / src_w;
    if (scale_y) *scale_y = (double)panel_h / src_h;
}

#endif
