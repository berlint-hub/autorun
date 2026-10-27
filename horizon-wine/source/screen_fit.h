/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Mapping a picture of src_w x src_h onto a panel of panel_w x panel_h.  The
 * Switch display compositor scales each layer's buffer to the screen ("full"),
 * and that is one of the ways a picture can be placed: stretched to fill, kept
 * in proportion with bars, or enlarged by whole steps only.  screen_fit_rect()
 * is the one place that decides, so the display, the input map and the Vulkan
 * blit agree on where the picture is; screen_fill_scales() is the stretched
 * case on its own.  Shared by the runtime and its host test
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

/* How a picture is placed on the panel -- the Scaling setting of the launcher:
 *
 *   FULL       stretched over the whole panel, proportions be damned (the look
 *              the Switch's own scaler gives every layer, and the old default);
 *   ASPECT     enlarged as far as it goes with the proportions kept, with black
 *              bars on the two sides left over (4:3 on a 16:9 panel: the sides);
 *   INTEGER    ASPECT, but only by whole steps, so every picture pixel is the
 *              same square block of panel pixels -- pixel art and low
 *              resolutions, and no shimmer as the picture moves;
 *   STRETCH_43 the central 4:3 of the picture stretched over the whole panel
 *              (the widescreen hack of a 4:3 game: screen_crop_4_3 first, then
 *              FULL).
 *
 * A picture larger than the panel is scaled down keeping the aspect ratio even
 * in INTEGER: a step below one would crop it, not enlarge it. */
enum screen_scaling
{
    SCREEN_SCALING_FULL = 0,
    SCREEN_SCALING_ASPECT = 1,
    SCREEN_SCALING_INTEGER = 2,
    SCREEN_SCALING_STRETCH_43 = 3,
};

/* 1 for the mode that shows the central 4:3: the caller crops the source with
 * screen_crop_4_3() (keeping the centre) and then asks for its rectangle. */
static inline int screen_scaling_crops_4_3( int mode )
{
    return mode == SCREEN_SCALING_STRETCH_43;
}

/* Where a src_w x src_h picture lands inside the panel for a mode: the
 * destination rectangle in panel pixels. The integer arithmetic is exact, so
 * the display, the input map and the Vulkan blit all land on the same pixels.
 * Pass the cropped source for STRETCH_43. */
static inline void screen_fit_rect( int mode, int src_w, int src_h, int panel_w, int panel_h,
                                    int *x, int *y, int *w, int *h )
{
    int width, height;

    if (src_w < 1) src_w = 1;
    if (src_h < 1) src_h = 1;
    if (panel_w < 1) panel_w = 1;
    if (panel_h < 1) panel_h = 1;

    if (mode == SCREEN_SCALING_FULL || mode == SCREEN_SCALING_STRETCH_43)
    {
        width = panel_w;
        height = panel_h;
    }
    else if (mode == SCREEN_SCALING_INTEGER && src_w <= panel_w && src_h <= panel_h)
    {
        int step = panel_w / src_w < panel_h / src_h ? panel_w / src_w : panel_h / src_h;

        if (step < 1) step = 1;
        width = src_w * step;
        height = src_h * step;
    }
    else
    {
        /* panel_w / src_w against panel_h / src_h, cross-multiplied so that no
         * rounding decides which side is the short one. */
        if ((long long)panel_w * src_h <= (long long)panel_h * src_w)
        {
            width = panel_w;
            height = (int)((long long)src_h * panel_w / src_w);
        }
        else
        {
            height = panel_h;
            width = (int)((long long)src_w * panel_h / src_h);
        }
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }

    if (width > panel_w) width = panel_w;
    if (height > panel_h) height = panel_h;
    if (w) *w = width;
    if (h) *h = height;
    if (x) *x = (panel_w - width) / 2;
    if (y) *y = (panel_h - height) / 2;
}

/* The workgroup grid the compute blit is dispatched with for one rectangle of
 * a picture whose destination is image_w x image_h. The shaders' local size is
 * 8, and vkCmdDispatchBase only starts a grid on a whole group: so the base is
 * the rectangle's start rounded down to a group, and the count is what it takes
 * from that base to the rectangle's end -- the few invocations before it fall
 * in the black bars, which they paint black again. Both are pulled back inside
 * the image, so no invocation ever lands outside it.
 *
 * Returns 0 for an image smaller than one group, which no dispatch can cover.
 * Used by dlls/win32u/vulkan.c (record_compute_cmd) and its host test. */
static inline int screen_dispatch_grid( int rect_x, int rect_y, int rect_w, int rect_h,
                                        int image_w, int image_h,
                                        unsigned int *base_x, unsigned int *base_y,
                                        unsigned int *groups_x, unsigned int *groups_y )
{
    int max_x = image_w / 8, max_y = image_h / 8;
    int bx, by, gx, gy;

    if (max_x < 1 || max_y < 1 || rect_w < 1 || rect_h < 1) return 0;
    bx = (rect_x > 0 ? rect_x : 0) / 8;
    by = (rect_y > 0 ? rect_y : 0) / 8;
    if (bx > max_x - 1) bx = max_x - 1;
    if (by > max_y - 1) by = max_y - 1;
    gx = (rect_x + rect_w - bx * 8 + 7) / 8;
    gy = (rect_y + rect_h - by * 8 + 7) / 8;
    if (bx + gx > max_x)
    {
        bx = max_x - gx;
        if (bx < 0) { bx = 0; gx = max_x; }
    }
    if (by + gy > max_y)
    {
        by = max_y - gy;
        if (by < 0) { by = 0; gy = max_y; }
    }
    if (base_x) *base_x = bx;
    if (base_y) *base_y = by;
    if (groups_x) *groups_x = gx;
    if (groups_y) *groups_y = gy;
    return 1;
}

/* The central 4:3 crop of a picture: narrow it (or shorten it) until it is
 * 4:3, keeping the centre. A 4:3 picture is returned unchanged. */
static inline void screen_crop_4_3( int *width, int *height )
{
    if (*width * 3 > *height * 4) *width = (*height * 4 + 2) / 3;
    else *height = (*width * 3 + 3) / 4;
}

#endif
