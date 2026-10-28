/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The arrow over a picture the compositor does not draw.
 *
 * Horizon has no hardware cursor. For a window the compositor paints the arrow
 * itself (compositor_gl.c); but while a program's own OpenGL or Vulkan surface
 * holds the screen the compositor has stepped aside, and the only thing that
 * can draw the arrow is the picture itself, just before it is shown
 * (dlls/win32u/winnx_opengl.c and vulkan.c). Neither a blit nor a
 * buffer-image copy blends: it writes whole pixels. The arrow's sprite is
 * clear around its shape, and a clear pixel copied whole is a black one on the
 * glass, so what goes in is the sprite's runs: the pixels of the arrow that
 * have a colour, as rectangles the picture can take in one copy each.
 *
 * The picture is the panel's own, scaled to the surface: here are the runs in
 * the surface's pixels, and the arrow at the surface's size, so that what a
 * copy puts in a pixel is the pixel the picture shows there and the arrow
 * lands where the compositor would have put it.
 */
#ifndef WINE_NX_POINTER_OVERLAY_H
#define WINE_NX_POINTER_OVERLAY_H

#include "pointer_cursor.h"

/* Enough for the arrow at four times the panel's size, where the pixels of a
 * run stop being contiguous: what does not fit is left out, never written past
 * the end. */
#define POINTER_OVERLAY_MAX_RUNS 192

/* One rectangle of the arrow, to be copied whole: the surface's pixels
 * [x0, x1) x [y0, y1), which show the sprite's pixels from (src_x0, src_y0) to
 * (src_x1, src_y0 + 1). A run's source is never wider than its pixels -- the
 * picture's pixel may show the sprite's column twice -- and never wider than
 * one sprite row. */
struct pointer_overlay_run
{
    int src_x0, src_x1, src_y;
    int x0, x1, y0, y1;
};

/* The panel pixel a pixel of the picture shows: the picture is the panel's own
 * scaled to it, and nearest is what a copy of the sprite scales with. */
static inline int pointer_overlay_column( int x, int panel, int picture )
{
    return (int)(((long long)x * 2 + 1) * panel / ((long long)picture * 2));
}

/* The arrow's opaque runs, its tip at the panel pixel (px, py), for the
 * picture the panel shows: the rectangle [picture_x, picture_x + picture_w) x
 * [picture_y, picture_y + picture_h) of the surface.  That is the whole surface
 * for a Vulkan swapchain image; for an OpenGL window with the widescreen hack
 * on, the display shows the central 4:3 of the buffer and the panel's pixels
 * are those of that rectangle, not of the whole surface -- the arrow belongs
 * where the panel shows it, at the scale the panel shows it with.
 *
 * Returns how many runs there are, at most POINTER_OVERLAY_MAX_RUNS; 0 when
 * the arrow is off the picture. */
static inline int pointer_overlay_runs( int px, int py, int panel_w, int panel_h,
                                        int picture_x, int picture_y, int picture_w, int picture_h,
                                        struct pointer_overlay_run *runs )
{
    int count = 0, x, y, x_begin, x_end, y_begin, y_end;

    if (panel_w < 1 || panel_h < 1 || picture_w < 1 || picture_h < 1) return 0;
    /* The arrow's own rectangle, a pixel either side: this runs on every
     * present of a whole screen, and a picture pixel on the edge of it may
     * show a panel pixel inside. */
    x_begin = picture_x + (int)((long long)px * picture_w / panel_w) - 1;
    x_end = picture_x + (int)((long long)(px + POINTER_CURSOR_W) * picture_w / panel_w) + 1;
    y_begin = picture_y + (int)((long long)py * picture_h / panel_h) - 1;
    y_end = picture_y + (int)((long long)(py + POINTER_CURSOR_H) * picture_h / panel_h) + 1;
    if (x_begin < picture_x) x_begin = picture_x;
    if (y_begin < picture_y) y_begin = picture_y;
    if (x_end > picture_x + picture_w) x_end = picture_x + picture_w;
    if (y_end > picture_y + picture_h) y_end = picture_y + picture_h;

    for (y = y_begin; y < y_end; y++)
    {
        int row = pointer_overlay_column( y - picture_y, panel_h, picture_h ) - py;
        int open = 0;

        if (row < 0 || row >= POINTER_CURSOR_H) continue;
        for (x = x_begin; x <= x_end; x++)
        {
            int column = pointer_overlay_column( x - picture_x, panel_w, picture_w ) - px;
            int inside = x < x_end && column >= 0 && column < POINTER_CURSOR_W &&
                         pointer_cursor_shape[row][column] != '.';

            if (inside && (!open || (column != runs[count - 1].src_x1 - 1 &&
                                     column != runs[count - 1].src_x1)))
            {
                /* A gap, or a column the surface's next pixel jumps over: the
                 * run ends and this pixel starts another. */
                if (count >= POINTER_OVERLAY_MAX_RUNS) return count;
                runs[count].src_x0 = column;
                runs[count].src_x1 = column + 1;
                runs[count].src_y = row;
                runs[count].x0 = x;
                runs[count].x1 = x + 1;
                runs[count].y0 = y;
                runs[count].y1 = y + 1;
                count++;
                open = 1;
            }
            else if (inside)
            {
                /* The same column once more, or the one after: the same
                 * rectangle, one pixel wider. */
                runs[count - 1].x1 = x + 1;
                if (column == runs[count - 1].src_x1) runs[count - 1].src_x1 = column + 1;
            }
            else open = 0;
        }
    }
    return count;
}

/* The sprite pixel a run carries for one of its own columns: the runs are
 * rectangles of the arrow and a copy writes whole pixels, so a copy needs the
 * colour of each of them.  column is counted from the run's left edge.  Opaque
 * white or opaque black: the same two bytes in either order. */
static inline uint32_t pointer_overlay_pixel( const struct pointer_overlay_run *run, int column )
{
    int sprite = run->src_x0 + (int)(((long long)column * 2 + 1) *
                                     (run->src_x1 - run->src_x0) / (2 * (run->x1 - run->x0)));

    if (sprite < run->src_x0) sprite = run->src_x0;
    if (sprite >= run->src_x1) sprite = run->src_x1 - 1;
    return pointer_cursor_shape[run->src_y][sprite] == 'W' ? 0xffffffffu : 0xff000000u;
}

#endif
