/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Fitting a picture of src_w x src_h onto a panel of panel_w x panel_h: the
 * picture is scaled to fit inside ("contain"), centred, so both letterbox
 * bars match.  The Switch runtime presents the Wine desktop this way -- the
 * desktop may be any resolution (a widescreen-patched game asks for
 * 1920x1080) while the panel is 1280x720 -- and input maps through the same
 * transform.  Shared by the runtime and its host test
 * (horizon-wine/tests/screen_fit.c).
 */
#ifndef WINE_NX_SCREEN_FIT_H
#define WINE_NX_SCREEN_FIT_H

static inline void screen_fit_rect( int src_w, int src_h, int panel_w, int panel_h,
                                    int *x, int *y, int *w, int *h )
{
    double scale;
    int fw, fh, fx, fy;

    if (src_w < 1) src_w = 1;
    if (src_h < 1) src_h = 1;
    if (panel_w < 1) panel_w = 1;
    if (panel_h < 1) panel_h = 1;
    scale = (double)panel_w / src_w;
    if ((double)panel_h / src_h < scale) scale = (double)panel_h / src_h;
    fw = (int)(src_w * scale + 0.5);
    fh = (int)(src_h * scale + 0.5);
    if (fw > panel_w) fw = panel_w;
    if (fh > panel_h) fh = panel_h;
    fx = (panel_w - fw) / 2;
    fy = (panel_h - fh) / 2;
    if (x) *x = fx;
    if (y) *y = fy;
    if (w) *w = fw;
    if (h) *h = fh;
}

#endif
